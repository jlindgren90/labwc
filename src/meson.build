labwc_sources = files(
  'action.c',
  'buffer.c',
  'debug.c',
  'desktop.c',
  'dnd.c',
  'idle.c',
  'interactive.c',
  'layers.c',
  'magnifier.c',
  'main.c',
  'node.c',
  'output.c',
  'output-state.c',
  'output-virtual.c',
  'overlay.c',
  'resistance.c',
  'resize-outlines.c',
  'seat.c',
  'server.c',
  'session-lock.c',
  'tearing.c',
  'theme.c',
  'view.c',
  'view-impl-common.c',
  'xdg.c',
  'xdg-popup.c',
)

if have_xwayland
  labwc_sources += files(
    'xwayland.c',
    'xwayland-unmanaged.c',
  )
endif

if have_libsfdo
  labwc_sources += files(
    'desktop-entry.c',
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
subdir('scaled-buffer')
subdir('ssd')
