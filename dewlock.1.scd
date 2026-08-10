dewlock(1)

# NAME

dewlock - Screen locker for Wayland

# SYNOPSIS

_dewlock_ [options...]

Locks your Wayland session.

# OPTIONS

*-c, --config* <path>
	The config file to use. By default, the following paths are checked:
	_$HOME/.dewlock/config_, _$XDG\_CONFIG\_HOME/dewlock/config_, and
	_SYSCONFDIR/dewlock/config_. See *CONFIGURATION* for details.

*-d, --debug*
	Enable debugging output.

*-f, --daemonize*
	Detach from the controlling terminal after locking.

	Note: this is the default behavior of i3lock.

*-r, --ready-fd* <fd>
	File descriptor to send readiness notifications to.

	When the session has been locked, a single newline is written to the FD.
	At this point, the compositor guarantees that no security sensitive content
	is visible on-screen.

*-h, --help*
	Show help message and quit.

*-v, --version*
	Show the version number and quit.

# CONFIGURATION

The config file consists of _namespace.key=value_ pairs, one per line. Lines
starting with *#* are treated as comments. See *-c* in *OPTIONS* for the
config file lookup paths.

*background.path* <path>
	Path to the PNG image to display as the background.

*background.mode* <mode>
	Background scaling mode: _stretch_, _fill_, _fit_, _center_, _tile_, or
	_solid\_color_. Use _solid\_color_ to display only *color.background*,
	ignoring the image. Defaults to _fill_.

*font.family* <font>
	Sets the font of the text. Defaults to _sans-serif_.

*font.size* <size>
	Sets the font size, which is also used to derive the spacing of every
	other element on the screen. Defaults to _16_.

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

## Defaults

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

_background.path_ has no default; no background image is drawn unless one is
set.

# SIGNALS

*SIGUSR1*
	Unlock the screen and exit.

# AUTHORS

Maintained by Manuel Spagnolo <shikaan@disroot.org>. For more information
about dewlock development, see https://github.com/shikaan/dewlock.
