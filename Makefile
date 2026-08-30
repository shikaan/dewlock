BUILD_TYPE ?= debug

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

ifeq ($(BUILD_TYPE),release)
    CFLAGS := $(COMMON_CFLAGS) $(RELEASE_CFLAGS)
    LDFLAGS += -s
else
    CFLAGS := $(COMMON_CFLAGS) $(DEBUG_CFLAGS)
    LDFLAGS += $(SANITIZERS)
endif

DEPS := wayland-client xkbcommon cairo
DEPS_CFLAGS := $(shell pkg-config --cflags $(DEPS))
DEPS_LIBS := $(shell pkg-config --libs $(DEPS))

# PAM=1 authenticates via libpam (src/auth-pam.c); PAM=0 authenticates via
# /etc/shadow (src/auth-shadow.c) and requires the binary to run setuid root.
PAM ?= 1

ifeq ($(PAM),1)
    AUTH_BACKEND := src/auth-pam.o
    AUTH_BACKEND_LIBS := -lpam
else
    AUTH_BACKEND := src/auth-shadow.o
    AUTH_BACKEND_LIBS := -lcrypt
endif

# ------------------

# Vendored so the build doesn't depend on wayland-protocols being installed
# on the target machine.
EXT_SESSION_LOCK := protocols/ext-session-lock-v1.xml

VERSION ?= v0.0.0
SHA ?= $(shell git rev-parse --short HEAD 2>/dev/null || echo dev)

.PHONY: all install clean help

### all - build the binary (default)
all: main

### install - build in release mode and install the executable, manpage and PAM config
install: MAN_FOLDER := ~/.local/share/man/man1
install: BIN_FOLDER := ~/.local/bin
install:
	@echo "Installing dewlock..."
	@make -s clean
	@make -s BUILD_TYPE=release PAM=$(PAM) VERSION=$(VERSION) SHA=$(SHA) all
	@mkdir -p ${BIN_FOLDER}
	@cp ./main ${BIN_FOLDER}/dewlock
	@chmod +x ${BIN_FOLDER}/dewlock
	@echo "Installing dewlock... DONE"
	@echo "  Executable: ${BIN_FOLDER}/dewlock"
	@if [ -f dewlock.1.roff ]; then \
		mkdir -p ${MAN_FOLDER}; \
		cp dewlock.1.roff ${MAN_FOLDER}/dewlock.1; \
		echo "  Man       : ${MAN_FOLDER}/dewlock.1"; \
	fi
ifeq ($(PAM),1)
	@if [ "$$(id -u)" = "0" ]; then \
		cp pam/dewlock /etc/pam.d/dewlock; \
		echo "  PAM       : /etc/pam.d/dewlock"; \
	else \
		echo "WARN: not root, skipping PAM config install."; \
		echo "      Run as root, or manually install pam/dewlock to /etc/pam.d/dewlock"; \
	fi
endif

### help - list available targets
help:
	@echo "Usage: make [target]"
	@echo
	@echo "Targets:"
	@grep -E '^### ' $(MAKEFILE_LIST) | sed 's/^### /  /'

### clean - remove build artifacts
clean:
	rm -f main *.o src/*.o protocols/*.c protocols/*.h

# ---------------------

protocols/ext-session-lock-v1-client-protocol.h:
	wayland-scanner client-header $(EXT_SESSION_LOCK) $@

protocols/ext-session-lock-v1-protocol.c: protocols/ext-session-lock-v1-client-protocol.h
	wayland-scanner private-code $(EXT_SESSION_LOCK) $@

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

main: CFLAGS += -Isrc $(DEPS_CFLAGS) \
	-isystem protocols -DVERSION='"$(VERSION)"' -DSHA='"$(SHA)"'
main: LDLIBS += $(DEPS_LIBS) $(AUTH_BACKEND_LIBS) -lm -lrt
main: main.o protocols/ext-session-lock-v1-protocol.o src/auth.o \
	src/background.o src/cli.o src/clock.o src/config.o src/ctx.o \
	src/log.o src/loop.o src/password.o \
	src/render.o src/safebuf.o src/seat.o src/unicode.o $(AUTH_BACKEND)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
