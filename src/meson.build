labwc_sources = files(
  'action.cpp',
  'buffer.cpp',
  'debug.cpp',
  'desktop.cpp',
  'dnd.cpp',
  'edges.cpp',
  'idle.cpp',
  'interactive.cpp',
  'layers.cpp',
  'magnifier.cpp',
  'main.cpp',
  'node.cpp',
  'output.cpp',
  'output-state.cpp',
  'output-virtual.cpp',
  'overlay.cpp',
  'placement.cpp',
  'regions.cpp',
  'resistance.cpp',
  'resize-outlines.cpp',
  'seat.cpp',
  'server.cpp',
  'session-lock.cpp',
  'snap-constraints.cpp',
  'snap.cpp',
  'tearing.cpp',
  'theme.cpp',
  'view.cpp',
  'view-impl-common.cpp',
  'window-rules.cpp',
  'workspaces.cpp',
  'xdg.cpp',
  'xdg-popup.cpp',
)

if have_xwayland
  labwc_sources += files(
    'xwayland.cpp',
    'xwayland-unmanaged.cpp',
  )
endif

if have_libsfdo
  labwc_sources += files(
    'desktop-entry.cpp',
  )
endif

subdir('common')
subdir('config')
subdir('cycle')
subdir('decorations')
subdir('foreign-toplevel')
subdir('img')
subdir('input')
subdir('menu')
subdir('protocols')
subdir('scaled-buffer')
subdir('ssd')
