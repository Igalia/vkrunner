# cmake file for VkRunner

include_directories("${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}")

set(VKRUNNERBIN_SOURCE_FILES
        main.c
        )

add_executable(vkrunnerbin ${VKRUNNERBIN_SOURCE_FILES})
set_target_properties(vkrunnerbin PROPERTIES OUTPUT_NAME "vkrunner")

target_link_libraries(vkrunnerbin vkrunner)

install(TARGETS vkrunnerbin DESTINATION bin)
