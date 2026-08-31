COMMON_CFLAGS := -std=c11 \
	-D_POSIX_C_SOURCE=200809L \
	-Wall \
	-Wextra \
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

# Keep debug symbols because they are needed in the packagin processes
RELEASE_CFLAGS := -O2 -DNDEBUG -g

# Allow packagers to introduce their own flags
DISTRO_CFLAGS := $(CFLAGS)

# ------------------

##P ERR_ON_WARN  - 0 to keep warnings non-fatal (default: 1)
ERR_ON_WARN ?= 1
ifeq ($(ERR_ON_WARN),1)
    COMMON_CFLAGS += -Werror
endif

##P BUILD_TYPE   - 'debug' for sanitizers (default: 'release')
BUILD_TYPE ?= release
ifeq ($(BUILD_TYPE),debug)
    CFLAGS := $(COMMON_CFLAGS) $(DEBUG_CFLAGS) $(DISTRO_CFLAGS)
    LDFLAGS += $(SANITIZERS)
else
    CFLAGS := $(COMMON_CFLAGS) $(RELEASE_CFLAGS) $(DISTRO_CFLAGS)
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

##P PREFIX       - install prefix (default: '/usr/local')
PREFIX ?= /usr/local

##P DESTDIR      - staging directory prepended to every install path
DESTDIR ?=

##P SYSCONFDIR   - system configuration directory (default: '/etc')
SYSCONFDIR ?= /etc

# ------------------

BINNAME = "main-${BUILD_TYPE}-${AUTH_BACKEND}"
BINDIR := $(DESTDIR)$(PREFIX)/bin
MANDIR := $(DESTDIR)$(PREFIX)/share/man/man1
BASHDIR := $(DESTDIR)$(PREFIX)/share/bash-completion/completions
ZSHDIR := $(DESTDIR)$(PREFIX)/share/zsh/site-functions
FISHDIR := $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d
PAMDIR := $(DESTDIR)$(SYSCONFDIR)/pam.d

.PHONY: all install clean help

##T all     - build the binary (default)
all: main docs

##T docs    - generate the manpage from the scdoc template
docs: dewlock.1.scd.tpl
	@if command -v scdoc > /dev/null 2>&1; then \
		echo "Generating manpage dewlock.1.roff..."; \
		sed "s/##VERSION##/${VERSION}/g; s/##SHA##/${SHA}/g" dewlock.1.scd.tpl > dewlock.1.scd; \
		scdoc < dewlock.1.scd > dewlock.1.roff; \
		echo "Generating manpage dewlock.1.roff... DONE"; \
	else \
		echo "WARN: Unable to find scdoc. Skipping manpage generation."; \
	fi

##T install - install the executable, manpage, completions and configuration
install:
	@if [ ! -f ${BINNAME} ]; then \
		echo "ERROR: missing or mismatching binary. You can:";\
		echo " - run 'make BUILD_TYPE=${BUILD_TYPE} AUTH_BACKEND=${AUTH_BACKEND} all'";\
		echo " - run 'make install' with the correct parameters";\
		exit 1; \
	fi
	@echo "Installing dewlock..."
	@install -Dm755 ${BINNAME} ${BINDIR}/dewlock
	@echo "  Executable : ${BINDIR}/dewlock"
	@if [ -f dewlock.1.roff ]; then \
		install -Dm644 dewlock.1.roff ${MANDIR}/dewlock.1; \
		echo "  Man        : ${MANDIR}/dewlock.1"; \
	fi
	@install -Dm644 completions/dewlock.bash ${BASHDIR}/dewlock
	@install -Dm644 completions/dewlock.zsh ${ZSHDIR}/_dewlock
	@install -Dm644 completions/dewlock.fish ${FISHDIR}/dewlock.fish
	@echo "  Completions: ${BASHDIR}, ${ZSHDIR}, ${FISHDIR}"
ifeq ($(AUTH_BACKEND),pam)
	@if install -Dm644 pam/dewlock ${PAMDIR}/dewlock 2>/dev/null; then \
		echo "  PAM config : ${PAMDIR}/dewlock"; \
	else \
		echo "  PAM config : SKIPPED, ${PAMDIR} is not writable"; \
		echo "               run 'sudo dewlock --pam' to install it"; \
	fi
endif
	@echo "Installing dewlock... DONE"
ifneq ($(AUTH_BACKEND),pam)
	@echo
	@echo "The shadow backend reads /etc/shadow. Grant access with either:"
	@echo "  sudo chmod a+s ${PREFIX}/bin/dewlock"
	@echo "  sudo chgrp shadow ${PREFIX}/bin/dewlock && sudo chmod g+s ${PREFIX}/bin/dewlock"
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

protocols/ext-session-lock-v1-protocol.o: CFLAGS := -O2 $(DISTRO_CFLAGS)
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
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o ${BINNAME}
