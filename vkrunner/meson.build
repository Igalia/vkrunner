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

vkrunner_deps = []

vkrunner_lib = library('vkrunner',
                       rust_crate_type : 'staticlib',
                       install : true,
                       dependencies : vkrunner_deps,
                       sources : 'lib.rs')

install_headers(vkrunner_public_headers, subdir : 'vkrunner')

pkg.generate(vkrunner_lib,
             description : 'Vulkan shader testing script library')

if rust.found()
    # Fake compiler program to help test compiler.rs
    fake_compiler = executable('fake_compiler', sources: 'fake_compiler.rs')
    rust.test('rust-unit-tests',
              vkrunner_lib,
              depends: [fake_compiler],
              env: {'PIGLIT_GLSLANG_VALIDATOR_BINARY':
                    fake_compiler.full_path(),
                    'PIGLIT_SPIRV_AS_BINARY': fake_compiler.full_path(),
                    'PIGLIT_SPIRV_DIS_BINARY': fake_compiler.full_path(),
                   })
endif
