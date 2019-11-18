project(
	'oguri',
	'c',
	version: '1.0.0',
	license: 'MIT',
	meson_version: '>=0.46.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

cairo = dependency('cairo')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')
wayland_client = dependency('wayland-client')
wayland_protocols = dependency('wayland-protocols', version: '>=1.14')

subdir('protocols')

c = meson.get_compiler('c')

executable(
	'oguri',
	files([
		'animation.c',
		'buffers.c',
		'cairo-pixbuf.c',
		'config.c',
		'oguri.c',
		'output.c',
	]),
	dependencies: [
		cairo,
		gdk_pixbuf,
		wayland_client,
		client_protos,
		c.find_library('rt'),  # For shm_open
	],
	install: true,
)

executable(
	'ogurictl',
	files([
		'ogurictl.c',
	]),
	install: true,
)
