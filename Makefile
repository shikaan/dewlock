COMMON_CFLAGS := -std=c11 \
	-D_POSIX_C_SOURCE=200809L \
	-Wall \
	-Wextra \
	-Werror \
	-fdiagnostics-color=always \
	-fno-common \
	-Winit-self \
	-Wfloat-equal \
	-Wundef \
	-Wpointer-arith \
	-Wcast-align \
	-Wcast-qual \
	-Wstrict-prototypes \
	-Wswitch-default \
	-Wswitch-enum \
	-Waggregate-return \
	-Wstrict-overflow=5 \
	-Wwrite-strings \
	-Wshadow \
	-pedantic \
	-Wconversion \
	-Wsign-conversion \
	-Wno-ignored-qualifiers

SANITIZERS := -fsanitize=address,undefined

DEBUG_CFLAGS := -g -O0 $(SANITIZERS) -DDEBUG

RELEASE_CFLAGS := -O2 -DNDEBUG

# ------------------

##P BUILD_TYPE   - use 'release' to skip debug symbols (default: 'debug')
BUILD_TYPE ?= debug
ifeq ($(BUILD_TYPE),release)
    CFLAGS := $(COMMON_CFLAGS) $(RELEASE_CFLAGS)
    LDFLAGS += -s
else
    CFLAGS := $(COMMON_CFLAGS) $(DEBUG_CFLAGS)
    LDFLAGS += $(SANITIZERS)
endif

##P AUTH_BACKEND - 'pam' for libpam, 'shadow' for shadow (default: 'pam')
AUTH_BACKEND ?= pam
ifeq ($(AUTH_BACKEND),pam)
    AUTH := src/auth-pam.o
    AUTH_LIBS := -lpam
else
    AUTH := src/auth-shadow.o
    AUTH_LIBS := -lcrypt
endif

##P VERSION      - version number in help and manpages (default: 'v0.0.0')
VERSION ?= v0.0.0

##P SHA          - SHA hash in help and manpages (default: 'dev')
SHA ?= $(shell git rev-parse --short HEAD 2>/dev/null || echo dev)

# ------------------

BIN_NAME = "main-${BUILD_TYPE}-${AUTH_BACKEND}"

.PHONY: all install clean help

##T all     - build the binary (default)
all: main docs

##T docs - generate the manpage from the scdoc template
docs: dewlock.1.scd.tpl
	@if command -v scdoc > /dev/null 2>&1; then \
		echo "Generating manpage dewlock.1.roff..."; \
		sed "s/##VERSION##/${VERSION}/g; s/##SHA##/${SHA}/g" dewlock.1.scd.tpl > dewlock.1.scd; \
		scdoc < dewlock.1.scd > dewlock.1.roff; \
		echo "Generating manpage dewlock.1.roff... DONE"; \
	else \
		echo "WARN: Unable to find scdoc. Skipping manpage generation."; \
	fi

##T install - install the executable, manpage and configuration
install: MAN_FOLDER := ~/.local/share/man/man1
install: BIN_FOLDER := ~/.local/bin
install:
	@if [ "$$(id -u)" != "0" ]; then \
		echo "ERROR: 'make install' must be run as admin. Retry with sudo.";\
		exit 1; \
	fi
	@if [ ! -f ${BIN_NAME} ]; then \
		echo "ERROR: missing or mismatching binary. You can:";\
		echo " - run 'make BUILD_TYPE=${BUILD_TYPE} AUTH_BACKEND=${AUTH_BACKEND} all'";\
		echo " - run 'make install' with the correct parameters";\
		exit 1; \
	fi
	@echo "Installing dewlock..."
	@mkdir -p ${BIN_FOLDER}
	@cp ${BIN_NAME} ${BIN_FOLDER}/dewlock
	@chmod +x ${BIN_FOLDER}/dewlock
	@echo "Installing dewlock... DONE"
	@if [ -f dewlock.1.roff ]; then \
		echo "Installing manpages..."; \
		mkdir -p ${MAN_FOLDER}; \
		cp dewlock.1.roff ${MAN_FOLDER}/dewlock.1; \
		echo "Installing manpages... DONE"; \
	fi
ifeq ($(AUTH_BACKEND),pam)
	@echo "Installing PAM configuration..."
	@cp pam/dewlock /etc/pam.d/dewlock
	@echo "Installing PAM configuration... DONE"
else
	@echo "Setting permissions..."
	@chgrp shadow ${BIN_FOLDER}/dewlock
	@chmod g+s ${BIN_FOLDER}/dewlock
	@echo "Setting permissions... DONE"
endif

##T help    - list available targets
help:
	@echo "Usage: make [parameters] [target]"
	@echo
	@echo "Parameters:"
	@grep -e '^##P ' $(MAKEFILE_LIST) | sed 's/^##P /  /'
	@echo
	@echo "Targets:"
	@grep -e '^##T ' $(MAKEFILE_LIST) | sed 's/^##T /  /'

##T clean   - remove build artifacts
clean:
	rm -f main-* *.o src/*.o protocols/*.c protocols/*.h

# ---------------------

protocols/ext-session-lock-v1-client-protocol.h:
	wayland-scanner client-header ./protocols/ext-session-lock-v1.xml $@

protocols/ext-session-lock-v1-protocol.c: protocols/ext-session-lock-v1-client-protocol.h
	wayland-scanner private-code ./protocols/ext-session-lock-v1.xml $@

protocols/ext-session-lock-v1-protocol.o: CFLAGS := -O2
protocols/ext-session-lock-v1-protocol.o: protocols/ext-session-lock-v1-client-protocol.h \
	protocols/ext-session-lock-v1-protocol.c

main.o: protocols/ext-session-lock-v1-client-protocol.h
src/auth.o: src/log.o src/safebuf.o
src/background.o: src/log.o
src/clock.o: src/loop.o
src/cli.o:
src/config.o: src/background.o src/color.h src/log.o src/strcmp.h
src/loop.o: src/log.o
src/safebuf.o: src/log.o
src/password.o: src/auth.o src/loop.o src/safebuf.o src/seat.o src/unicode.o
src/render.o: src/background.o src/log.o
src/seat.o: src/log.o src/loop.o
src/auth-pam.o: src/auth.o src/log.o src/safebuf.o
src/auth-shadow.o: src/auth.o src/log.o src/safebuf.o

main: CFLAGS += -Isrc $(shell pkg-config --cflags wayland-client xkbcommon cairo) \
	-isystem protocols -DVERSION='"$(VERSION)"' -DSHA='"$(SHA)"'
main: LDLIBS += $(shell pkg-config --libs wayland-client xkbcommon cairo) \
	$(AUTH_LIBS) -lm -lrt
main: main.o protocols/ext-session-lock-v1-protocol.o src/auth.o \
	src/background.o src/cli.o src/clock.o src/config.o src/ctx.o \
	src/log.o src/loop.o src/password.o \
	src/render.o src/safebuf.o src/seat.o src/unicode.o $(AUTH)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o ${BIN_NAME}
