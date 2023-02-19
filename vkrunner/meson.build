pkg = import('pkgconfig')

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

vkrunner_deps = [m_dep]

vkrunner_static_lib = library('vkrunner-c',
                              rust_crate_type : 'staticlib',
                              install : true,
                              dependencies : vkrunner_deps,
                              sources : 'lib.rs')
vkrunner_rust_lib = library('vkrunner',
                            rust_crate_type : 'lib',
                            install : true,
                            sources : 'lib.rs')

install_headers(vkrunner_public_headers, subdir : 'vkrunner')

pkg.generate(vkrunner_static_lib,
             name : 'vkrunner',
             description : 'Vulkan shader testing script library')

if rust.found()
    # Fake compiler program to help test compiler.rs
    fake_compiler = executable('fake_compiler', sources: 'fake_compiler.rs')
    rust.test('lib-rust-unit-tests',
              vkrunner_rust_lib,
              depends: [fake_compiler],
              env: {'PIGLIT_GLSLANG_VALIDATOR_BINARY':
                    fake_compiler.full_path(),
                    'PIGLIT_SPIRV_AS_BINARY': fake_compiler.full_path(),
                    'PIGLIT_SPIRV_DIS_BINARY': fake_compiler.full_path(),
                   })
endif
