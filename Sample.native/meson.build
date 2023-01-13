project('computesample', 'c', version : '1.0', default_options : 'warning_level=3')

src = ['main.c']
cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

vulkan = dependency('vulkan')

deps = [ vulkan, m_dep ]

configure_file(input: 'smaPass1.spv', output: 'smaPass1.spv', copy: true)
configure_file(input: 'smaPass2.spv', output: 'smaPass2.spv', copy: true)
configure_file(input: 'sample.dat', output: 'sample.dat', copy: true)

exe = library('computesample', src, dependencies: deps)