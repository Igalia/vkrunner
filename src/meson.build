vkrunner_exe = executable('vkrunner',
                          sources : 'main.rs',
                          link_with : vkrunner_rust_lib,
                          install : true)
if rust.found()
    rust.test('exe-rust-unit-tests', vkrunner_exe)
endif
