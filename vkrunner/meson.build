pkg = import('pkgconfig')

rust = import('rust', required : false)

vkrunner_public_headers = [
   'vr-callback.h',
   'vr-config.h',
   'vr-executor.h',
   'vr-format.h',
   'vr-inspect.h',
   'vr-result.h',
   'vr-script.h',
   'vr-shader-stage.h',
   'vr-source.h',
   'vkrunner.h',
]

vkrunner_source_files = [
   'vr-util.c',
   'vr-util.h',
   'vr-window-format.h',
] + vkrunner_public_headers

vkrunner_deps = vulkan_deps + [m_dep]

if target_machine.system() != 'windows'
   vkrunner_deps += dependency('dl')
endif

rust_parts_lib = static_library('rust_parts',
                                rust_crate_type : 'staticlib',
                                sources : 'lib.rs')

vkrunner_lib = library('vkrunner',
                       install : true,
                       dependencies : vkrunner_deps,
                       sources : vkrunner_source_files,
                       link_with: rust_parts_lib,
                       include_directories : include_directories('..'))

install_headers(vkrunner_public_headers, subdir : 'vkrunner')

pkg.generate(vkrunner_lib,
             description : 'Vulkan shader testing script library')

if rust.found()
    # Fake compiler program to help test compiler.rs
    fake_compiler = executable('fake_compiler', sources: 'fake_compiler.rs')
    rust.test('rust-unit-tests',
              rust_parts_lib,
              depends: [fake_compiler],
              env: {'PIGLIT_GLSLANG_VALIDATOR_BINARY':
                    fake_compiler.full_path(),
                    'PIGLIT_SPIRV_AS_BINARY': fake_compiler.full_path(),
                    'PIGLIT_SPIRV_DIS_BINARY': fake_compiler.full_path(),
                   })
endif
