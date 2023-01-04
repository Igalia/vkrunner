project('vkrunner', ['c', 'rust'],
        version : '0.1',
        default_options : 'default_library=static')

cc = meson.get_compiler('c')

foreach a : ['-Wall', '-Wuninitialized', '-Wempty-body', '-Wformat',
      '-Wformat-security', '-Winit-self', '-Wundef',
      '-Wvla', '-Wpointer-arith', '-Wmissing-declarations']
  if cc.has_argument(a)
    add_project_arguments(a, language : ['c'])
  endif
endforeach

configinc = include_directories('.')

cdata = configuration_data()

if build_machine.endian() == 'little'
   cdata.set('HAVE_LITTLE_ENDIAN', true)
elif build_machine.endian() == 'big'
   cdata.set('HAVE_BIG_ENDIAN', true)
endif

if cc.has_function('ffs')
   cdata.set('HAVE_FFS', true)
endif

if cc.has_function('ffsl')
   cdata.set('HAVE_FFSL', true)
endif

configure_file(output : 'config.h', configuration : cdata)

vulkan_header_cdata = configuration_data()

vulkan_deps = []
vulkan_header = get_option('vulkan_header')

if vulkan_header == ''
   vulkan_dep = dependency('vulkan', required : false)
   vulkan_deps += vulkan_dep

   if not vulkan_dep.found()
      cc.has_header('vulkan/vulkan.h', required : true)
   endif

   vulkan_header = '<vulkan/vulkan.h>'
endif

vulkan_header_cdata.set('VULKAN_HEADER', vulkan_header)
      
configure_file(output : 'vk-vulkan-header.h',
               configuration : vulkan_header_cdata,
               input : 'vk-vulkan-header.h.in')

m_dep = cc.find_library('m', required : false)

subdir('vkrunner')
subdir('src')
