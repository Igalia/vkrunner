project('vkrunner', ['c', 'rust'],
        version : '0.1',
        default_options : 'default_library=static')

cc = meson.get_compiler('c')

m_dep = cc.find_library('m', required : false)

rust = import('rust', required : true)

vulkan_header_path = get_option('vulkan_header_path')

if vulkan_header_path == ''
    vulkan_dep = dependency('vulkan', required : true)

    vulkan_header_path = vulkan_dep.get_variable(pkgconfig : 'includedir')
endif

vulkan_header_include_dirs = include_directories(vulkan_header_path)

cc.has_header('vulkan/vulkan.h',
              include_directories : vulkan_header_include_dirs,
              required : true)

vulkan_header_file = vulkan_header_path / 'vulkan' / 'vulkan.h'

subdir('vkrunner')
subdir('src')
