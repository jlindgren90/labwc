wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')
wayland_scanner = find_program('wayland-scanner')

wayland_scanner_code = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_server = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.h',
	arguments: ['server-header', '@INPUT@', '@OUTPUT@'],
)

server_protocols = [
	wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
	wl_protocol_dir / 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml',
	wl_protocol_dir / 'stable/tablet/tablet-v2.xml',
	wl_protocol_dir / 'staging/cursor-shape/cursor-shape-v1.xml',
	wl_protocol_dir / 'staging/drm-lease/drm-lease-v1.xml',
	wl_protocol_dir / 'staging/xwayland-shell/xwayland-shell-v1.xml',
	wl_protocol_dir / 'staging/tearing-control/tearing-control-v1.xml',
	wl_protocol_dir / 'staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml',
	wl_protocol_dir / 'staging/ext-workspace/ext-workspace-v1.xml',
	wl_protocol_dir / 'staging/ext-image-capture-source/ext-image-capture-source-v1.xml',
	wl_protocol_dir / 'staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml',
	'cosmic-workspace-unstable-v1.xml',
	'wlr-layer-shell-unstable-v1.xml',
	'wlr-input-inhibitor-unstable-v1.xml',
	'wlr-output-power-management-unstable-v1.xml',
]

server_protos_src = []
server_protos_headers = []

foreach xml : server_protocols
	server_protos_src += wayland_scanner_code.process(xml)
	server_protos_headers += wayland_scanner_server.process(xml)
endforeach

lib_server_protos = static_library(
	'server_protos',
	server_protos_src + server_protos_headers,
	dependencies: [wayland_server]
)

server_protos = declare_dependency(
	link_with: lib_server_protos,
	sources: server_protos_headers,
)
