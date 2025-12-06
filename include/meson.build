configure_file(output: 'config.h',  configuration: conf_data)

# generate C headers from Rust modules
gen_headers = []
foreach module : ['view']
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

# generate Rust bindings from C modules
bindings = []
foreach module : ['view-c']
  bindings += custom_target(
    input: module + '.h',
    output: module + '.rs',
    command: [
      'bindgen',
      '@INPUT@',
      '-o', '@OUTPUT@'
    ],
    build_by_default: true
  )
endforeach
