project('vkrunner', ['c', 'rust'],
        version : '0.1',
        default_options : 'default_library=static')

cc = meson.get_compiler('c')

m_dep = cc.find_library('m', required : false)

rust = import('rust', required : false)

subdir('vkrunner')
subdir('src')
