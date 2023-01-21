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
   'vr-allocate-store.c',
   'vr-allocate-store.h',
   'vr-box.h',
   'vr-buffer.c',
   'vr-buffer.h',
   'vr-char.c',
   'vr-char.h',
   'vr-config.c',
   'vr-config-private.h',
   'vr-context.h',
   'vr-error-message.c',
   'vr-error-message.h',
   'vr-executor.c',
   'vr-flush-memory.c',
   'vr-flush-memory.h',
   'vr-format-private.h',
   'vr-half-float.h',
   'vr-list.c',
   'vr-list.h',
   'vr-pipeline-key.h',
   'vr-requirements.h',
   'vr-script-private.h',
   'vr-source-private.h',
   'vr-temp-file.c',
   'vr-temp-file.h',
   'vr-util.c',
   'vr-util.h',
   'vr-vk.h',
   'vr-vk-device-funcs.h',
   'vr-vk-instance-funcs.h',
   'vr-pipeline.c',
   'vr-pipeline.h',
   'vr-subprocess.c',
   'vr-subprocess.h',
   'vr-test.c',
   'vr-test.h',
   'vr-tolerance.h',
   'vr-vbo.h',
   'vr-window.c',
   'vr-window.h',
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
    rust.test('rust-unit-tests', rust_parts_lib)
endif
