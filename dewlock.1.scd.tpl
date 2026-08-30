dewlock(1) "##VERSION## (##SHA##)"

# NAME

dewlock - Screen locker for Wayland

# SYNOPSIS

dewlock [_options_]

# DESCRIPTION

dewlock is a minimal, beautiful screen locker for Wayland compositors. It is
a fork of swaylock.

dewlock uses the ext-session-lock-v1 protocol. Any compositor that
implements this protocol works with dewlock.

When dewlock locks the screen, it shows a password field. Enter your
password in the field to unlock the screen.

# OPTIONS

*-c, --config* <path>
	The config file to use. By default, dewlock checks these paths:
	_$XDG\_CONFIG\_HOME/dewlock/config_ (or _$HOME/.config/dewlock/config_
	if _$XDG\_CONFIG\_HOME_ is unset), and _SYSCONFDIR/dewlock/config_. See
	*CONFIGURATION* for details.

*-d, --debug*
	Enable debugging output.

*-f, --daemonize*
	Detach from the controlling terminal after dewlock locks the screen.

	Note: i3lock uses this behavior by default.

*-r, --ready-fd* <fd>
	The file descriptor for readiness notifications.

	When the session locks, dewlock writes a single newline to the FD. At
	this point, the compositor guarantees that no security-sensitive
	content is visible on screen.

*-h, --help*
	Show help message and quit.

*-v, --version*
	Show the version number and quit.

*-p, --pam*
	Create the PAM configuration and quit. Run this command with sudo. See
	*AUTHENTICATION* for details.

# CONFIGURATION

The config file consists of _namespace.key=value_ pairs, one per line.
dewlock treats lines that start with *#* as comments. See *-c* in
*OPTIONS* for the config file lookup paths.

*background.path* <path>
	The path to the PNG image to display as the background.

*background.mode* <mode>
	The background scaling mode: _stretch_, _fill_, _fit_, _center_,
	_tile_, or _solid\_color_. _solid\_color_ displays only
	*color.background* and hides the image. Defaults to _fill_.

*font.family* <font>
	Sets the font of the text. Defaults to _sans-serif_.

*font.size* <size>
	Sets the font size. dewlock also uses this value to set the spacing
	of every other element on the screen. Defaults to _16_.

*color.background* <rrggbb[aa]>
	Sets the color painted behind the background image. Defaults to
	_A3A3A3FF_.

*color.overlay* <rrggbb[aa]>
	Sets the color of the overlay drawn over the screen and behind the
	password field. Defaults to _00000055_.

*color.text* <rrggbb[aa]>
	Sets the color of the text and the password field border. Defaults to
	_FFFFFFFF_.

*color.warning* <rrggbb[aa]>
	Sets the color of the Caps Lock warning text. Defaults to _FFDD00FF_.

*color.error* <rrggbb[aa]>
	Sets the color of the text and password field border when
	authentication fails. Defaults to _CC6566FF_.

## DEFAULTS

Running without a config file, or with one that leaves these keys unset, is
equivalent to the following configuration:

```
background.mode=fill
font.family=sans-serif
font.size=16
color.background=A3A3A3FF
color.overlay=00000055
color.text=FFFFFFFF
color.warning=FFDD00FF
color.error=CC6566FF
```

_background.path_ has no default. dewlock draws no background image unless
you set one.

# AUTHENTICATION

## WITH PAM

On most systems, dewlock does not need a dedicated PAM configuration. It
uses the PAM fallback service.

On some systems, dewlock needs a dedicated PAM configuration. Without it,
dewlock can lock the screen and fail to unlock it.

To configure PAM, run *dewlock -p*. Alternatively, copy _pam/dewlock_ from
the dewlock source folder to _/etc/pam.d/dewlock_.

## WITHOUT PAM

On systems without PAM, dewlock uses _shadow.h_.

Systems with a tcb-like setup need no further action. This includes systems
that use musl's native support or glibc with tcb.

On other systems, _/etc/shadow_ stores the passwords for all users. dewlock
needs the setuid bit:

```
sudo chmod a+s /usr/local/bin/dewlock
```

If _/etc/shadow_ belongs to the _shadow_ group, use the setgid bit instead:

```
sudo chgrp shadow /usr/local/bin/dewlock
sudo chmod g+s /usr/local/bin/dewlock
```

dewlock drops root permissions shortly after it starts.

# SIGNALS

*SIGUSR1*
	Unlock the screen and exit.

# AUTHOR
	Manuel Spagnolo <_shikaan@disroot.org_>

# SEE ALSO
	Project homepage: _https://github.com/shikaan/dewlock_

# LICENSE
	dewlock is MIT licensed. It includes code from swaylock and i3lock,
	written by Drew DeVault. Full text:
	_https://github.com/shikaan/dewlock/blob/##VERSION##/LICENSE_
