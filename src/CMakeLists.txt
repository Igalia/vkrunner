# cmake file for VkRunner

include_directories("${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}")

set(VKRUNNERBIN_SOURCE_FILES
        main.c
        )

set(VKRUNNERGUI_SOURCE_FILES
        gui.c
        gui-worker.c
        )

add_executable(vkrunnerbin ${VKRUNNERBIN_SOURCE_FILES})
set_target_properties(vkrunnerbin PROPERTIES OUTPUT_NAME "vkrunner")

target_link_libraries(vkrunnerbin vkrunner)

install(TARGETS vkrunnerbin DESTINATION bin)

if(GTK_FOUND)
  add_executable(vkrunner-gui ${VKRUNNERGUI_SOURCE_FILES})
  target_link_libraries(vkrunner-gui vkrunner)
  install(TARGETS vkrunner-gui DESTINATION bin)
  add_definitions(${GTK_CFLAGS})
  target_link_libraries(vkrunner-gui ${GTK_LIBRARIES})
endif()
