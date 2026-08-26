project(
	'dewlock',
	'c',
	version: '1.8.6',
	license: 'MIT',
	meson_version: '>=0.59.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

cc = meson.get_compiler('c')

add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')

add_project_arguments(cc.get_supported_arguments([
	'-Wno-unused-parameter',
	'-Wno-unused-result',
	'-Wundef',
	'-Wvla',
]), language: 'c')

is_freebsd = host_machine.system().startswith('freebsd')
if is_freebsd
	add_project_arguments('-D_C11_SOURCE', language: 'c')
endif

wayland_client = dependency('wayland-client', version: '>=1.20.0')
wayland_protos = dependency('wayland-protocols', version: '>=1.25', fallback: 'wayland-protocols')
wayland_scanner = dependency('wayland-scanner', version: '>=1.15.0', native: true)
xkbcommon = dependency('xkbcommon')
cairo = dependency('cairo')
libpam = cc.find_library('pam', required: get_option('pam'))
crypt = cc.find_library('crypt', required: not libpam.found())
math = cc.find_library('m')
rt = cc.find_library('rt')

git = find_program('git', required: false)
scdoc = find_program('scdoc', required: get_option('man-pages'))
wayland_scanner_prog = find_program(wayland_scanner.get_variable('wayland_scanner'), native: true)

version = meson.project_version()
sha = 'dev'
if git.found()
	git_describe = run_command([git, 'describe', '--always', '--tags'], check: false)
	git_sha = run_command([git, 'rev-parse', '--short', 'HEAD'], check: false)
	if git_describe.returncode() == 0
		version = git_describe.stdout().strip()
	endif
	if git_sha.returncode() == 0
		sha = git_sha.stdout().strip()
	endif
endif

wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

wayland_scanner_code = generator(
	wayland_scanner_prog,
	output: '@BASENAME@-protocol.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client = generator(
	wayland_scanner_prog,
	output: '@BASENAME@-client-protocol.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)

client_protocols = [
	wl_protocol_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml',
]

protos_src = []
foreach xml : client_protocols
	protos_src += wayland_scanner_code.process(xml)
	protos_src += wayland_scanner_client.process(xml)
endforeach

add_project_arguments([
	'-DVERSION="@0@"'.format(version),
	'-DSHA="@0@"'.format(sha),
	'-DSYSCONFDIR="@0@"'.format(get_option('prefix') / get_option('sysconfdir')),
], language: 'c')

dependencies = [
	cairo,
	math,
	rt,
	xkbcommon,
	wayland_client,
]

sources = [
	'main.c',
	'src/background-image.c',
	'src/cairo.c',
	'src/cli.c',
	'src/clock.c',
	'src/comm.c',
	'src/config.c',
	'src/log.c',
	'src/loop.c',
	'src/password-buffer.c',
	'src/password.c',
	'src/pool-buffer.c',
	'src/render.c',
	'src/seat.c',
	'src/unicode.c',
]

if libpam.found()
	sources += ['src/pam.c']
	dependencies += [libpam]
else
	warning('The dewlock binary often needs to be setuid when compiled without libpam')
	warning('You must do this manually post-install: chmod a+s /path/to/dewlock')
	warning('See the "Without PAM" section of the README for details.')
	sources += ['src/shadow.c']
	dependencies += [crypt]
endif

dewlock_inc = include_directories('src')

executable('dewlock',
	sources + protos_src,
	include_directories: [dewlock_inc],
	dependencies: dependencies,
	install: true
)

if libpam.found()
	install_data(
		'pam/dewlock',
		install_dir: get_option('sysconfdir') / 'pam.d'
	)
endif

if scdoc.found()
	mandir = get_option('mandir')
	man_files = [
		'dewlock.1.scd',
	]
	foreach filename : man_files
		topic = filename.split('.')[-3].split('/')[-1]
		section = filename.split('.')[-2]
		output = '@0@.@1@'.format(topic, section)

		custom_target(
			output,
			input: filename,
			output: output,
			command: scdoc,
			feed: true,
			capture: true,
			install: true,
			install_dir: '@0@/man@1@'.format(mandir, section)
		)
	endforeach
endif

subdir('completions')
