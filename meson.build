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
gdk = dependency('gdk-3.0')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')
wayland_client = dependency('wayland-client')
wayland_protocols = dependency('wayland-protocols', version: '>=1.14')

subdir('protocols')

executable(
	'oguri',
	files([
		'oguri.c',
	]),
	dependencies: [
		cairo,
		gdk,
		gdk_pixbuf,
		wayland_client,
		client_protos,
	],
)
