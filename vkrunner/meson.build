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

vulkan_bindings = rust.bindgen(
    input : vulkan_header_file,
    output : 'vulkan_bindings.rs',
    include_directories : vulkan_header_include_dirs,
    args : ['--generate', 'types,vars',
            '--no-prepend-enum-name',
            '--allowlist-type', '^(PFN|Vk).*',
            '--allowlist-var', '^VK_.*',
            '--with-derive-default']
)

vkrunner_deps = [m_dep]

sources = structured_sources([
    'lib.rs',
    'small_float.rs',
    'half_float.rs',
    'hex.rs',
    'format.rs',
    'tolerance.rs',
    'util.rs',
    'parse_num.rs',
    'vbo.rs',
    'vk.rs',
    'vulkan_funcs.rs',
    'requirements.rs',
    'slot.rs',
    'result.rs',
    'shader_stage.rs',
    'pipeline_key.rs',
    'window_format.rs',
    'source.rs',
    'config.rs',
    'stream.rs',
    'script.rs',
    'context.rs',
    'buffer.rs',
    'window.rs',
    'allocate_store.rs',
    'executor.rs',
    'temp_file.rs',
    'logger.rs',
    'compiler.rs',
    'pipeline_set.rs',
    'flush_memory.rs',
    'inspect.rs',
    'tester.rs',
    'fake_vulkan.rs',
    'env_var_test.rs',
    'format_table.rs',
    'vulkan_funcs_data.rs',
    'features.rs',
    'pipeline_key_data.rs',
    'enum_table.rs',
    vulkan_bindings
])

vkrunner_static_lib = library('vkrunner-c',
                              rust_crate_type : 'staticlib',
                              install : true,
                              dependencies : vkrunner_deps,
                              sources : sources)
vkrunner_rust_lib = library('vkrunner',
                            rust_crate_type : 'lib',
                            install : true,
                            sources : sources)

install_headers(vkrunner_public_headers, subdir : 'vkrunner')

pkg.generate(vkrunner_static_lib,
             name : 'vkrunner',
             description : 'Vulkan shader testing script library')

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
