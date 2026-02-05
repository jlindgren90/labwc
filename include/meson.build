# generate C headers from Rust modules
gen_headers = []
foreach module : ['rect', 'view_api']
  gen_headers += custom_target(
    input: '../src/' + module + '.rs',
    output: module + '.h',
    command: [
      'cbindgen',
      '@INPUT@',
      '-q', '-l', 'c',
      '-c', '../cbindgen.toml',
      '-o', '@OUTPUT@'
    ],
    build_by_default: true
  )
endforeach

# generate Rust bindings from C headers (combined into one
# to work around duplication of forward-declared types)
bindings = [
  custom_target(
    input: [
      'common/edge.h',
      'common/node-type.h',
      'foreign-toplevel.h',
      'rs-bindings.h',
      'rs-types.h',
      'ssd.h',
      'view-c.h',
    ],
    output: 'bindings.rs',
    command: [
      'bindgen',
      '--no-prepend-enum-name',
      '--with-derive-default',
      '../include/rs-bindings.h',
      '-o', '@OUTPUT@'
    ],
    build_by_default: true
  )
]
