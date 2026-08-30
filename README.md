<h1 align="center">dewlock</h1>

<p align="center">
A minimal, beautiful screen locker for Wayland.
</p>

dewlock is a fork of [swaywm/swaylock](https://github.com/swaywm/swaylock)
focused on providing a minimalist, beautiful experience.

Like its predecessor is compatible with any Wayland compositor implementing 
the ext-session-lock-v1 Wayland protocol.

<p align="center">
  <img width="640" alt="preview" src="https://github.com/user-attachments/assets/c1cf52f0-9bc1-419e-bf82-252d1a16bb75" />
</p>

## Quick Start

### Installation

There are no packaged builds yet. See [CONTRIBUTING.md](CONTRIBUTING.md) to
build from source.

### Usage

```sh
dewlock
```

Configuration lives in a single `namespace.key=value` file (by default
`$XDG_CONFIG_HOME/dewlock/config`). See [dewlock(1)](dewlock.1.scd) for
every option and configuration key.

##### With PAM

On most systems, dewlock does not need a dedicated PAM configuration. It
uses the PAM fallback service.

On some systems, dewlock needs a dedicated PAM configuration. Without it,
dewlock can lock the screen and not unlock again.

To configure PAM, run this command:

```
dewlock --pam
```

You can also configure PAM manually:

```
# from dewlock folder
cp pam/dewlock /etc/pam.d/dewlock
```

##### Without PAM

On systems without PAM, dewlock uses `shadow.h`.

Systems which rely on a tcb-like setup (either via musl's native support or via
glibc+[tcb](https://www.openwall.com/tcb/)), require no further action.

For most other systems, where passwords for all users are stored in `/etc/shadow`,
dewlock needs to be installed suid:

```sh
sudo chmod a+s /usr/local/bin/dewlock
```

Optionally, on systems where the file `/etc/shadow` is owned by the `shadow`
group, the binary can be made sgid instead:

```sh
sudo chgrp shadow /usr/local/bin/dewlock
sudo chmod g+s /usr/local/bin/dewlock
```

Dewlock will drop root permissions shortly after startup.

## Contributing

If you'd like to request a feature or report a bug, please create a 
[GitHub Issue](https://github.com/shikaan/dewlock/issues).

## License

[MIT](./LICENSE)
