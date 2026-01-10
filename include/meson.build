configure_file(output: 'config.h',  configuration: conf_data)

# generate C headers from Rust modules
gen_headers = []
foreach module : ['util', 'view']
  gen_headers += custom_target(
    input: '../src/' + module + '.rs',
    output: module + '-rs.h',
    command: [
      'cbindgen',
      '@INPUT@',
      '-l', 'c',
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
      'foreign-toplevel.h',
      'rs-bindings.h',
      'rs-types.h',
      'view-c.h',
    ],
    output: 'bindings.rs',
    command: [
      'bindgen',
      '--with-derive-default',
      '../include/rs-bindings.h',
      '-o', '@OUTPUT@'
    ],
    build_by_default: true
  )
]
