<h1 align="center">dewlock</h1>

<p align="center">
A minimal, beautiful screen locker for Wayland.
</p>

<p align="center">
  <img width="640" alt="preview" src="https://github.com/user-attachments/assets/c1cf52f0-9bc1-419e-bf82-252d1a16bb75" />
</p>

dewlock is a fork of [swaywm/swaylock](https://github.com/swaywm/swaylock),
built for a minimal, beautiful experience.

When dewlock locks the screen, it shows a password field. Enter your
password in the field to unlock the screen.

## Installation

There are no packaged builds yet. First, install the dependencies listed in
[CONTRIBUTING.md](CONTRIBUTING.md). Then build and install dewlock from
source:

```sh
make all
sudo make install
```

This procedure installs the executable, the man page, the shell completions,
and the PAM configuration file. All files go under `/usr/local`. To install
dewlock in a different directory, set the `PREFIX` variable:

```sh
make install PREFIX=~/.local
```

If you install dewlock without root permissions, this procedure does not
install the PAM configuration file. See [Authentication](#authentication)
for more information.

## Usage

```sh
dewlock
```

Configuration lives in a single `namespace.key=value` file (by default
`$XDG_CONFIG_HOME/dewlock/config`). See [dewlock(1)](dewlock.1.scd) for
every option and configuration key.

## Authentication

### With PAM

On most systems, dewlock does not need a dedicated PAM configuration. It
uses the PAM fallback service.

On some systems, dewlock needs a dedicated PAM configuration. Without it,
dewlock can lock the screen and fail to unlock it.

The `sudo make install` command installs this configuration for you. If you
installed dewlock without root permissions, run this command:

```sh
sudo dewlock --pam
```

Alternatively, copy the PAM config manually:

```sh
# from dewlock folder
sudo cp pam/dewlock /etc/pam.d/dewlock
```

### Without PAM

On systems without PAM, dewlock uses `shadow.h`. To select this backend, set
`AUTH_BACKEND` at build time:

```sh
make AUTH_BACKEND=shadow all
sudo make AUTH_BACKEND=shadow install
```

Systems with a tcb-like setup need no further action. This includes systems
that use musl's native support or glibc with
[tcb](https://www.openwall.com/tcb/).

On other systems, `/etc/shadow` stores the passwords for all users. dewlock
needs the setuid bit:

```sh
sudo chmod a+s /usr/local/bin/dewlock
```

If `/etc/shadow` belongs to the `shadow` group, use the setgid bit instead:

```sh
sudo chgrp shadow /usr/local/bin/dewlock
sudo chmod g+s /usr/local/bin/dewlock
```

dewlock drops root permissions shortly after it starts.

## Compatibility

dewlock runs on any Wayland compositor that implements the
ext-session-lock-v1 protocol.

## Contributing

To request a feature or report a bug, open an
[issue](https://github.com/shikaan/dewlock/issues).

## License

[MIT](./LICENSE)
