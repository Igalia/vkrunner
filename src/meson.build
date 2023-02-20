vkrunner_bin_source_files = [
   'main.c',
   'getopt.c',
]

executable('vkrunner',
           vkrunner_bin_source_files,
           link_with : vkrunner_static_lib,
           install : true,
           dependencies : m_dep,
           include_directories : include_directories('..'))
