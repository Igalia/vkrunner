# cmake file for VkRunner

include_directories(${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR})

set(VKRUNNER_PUBLIC_HEADERS
        vr-config.h
        vr-execute.h
        vr-result.h
        vkrunner.h
        )

set(VKRUNNER_SOURCE_FILES
        vr-allocate-store.c
        vr-allocate-store.h
        vr-box.c
        vr-box.h
        vr-buffer.c
        vr-buffer.h
        vr-config.c
        vr-enum-table.h
        vr-error-message.c
        vr-error-message.h
        vr-execute.c
        vr-feature-offsets.c
        vr-feature-offsets.h
        vr-flush-memory.c
        vr-flush-memory.h
        vr-format-table.h
        vr-format.h
        vr-format.c
        vr-half-float.c
        vr-half-float.h
        vr-hex.c
        vr-hex.h
        vr-list.c
        vr-list.h
        vr-pipeline-key.c
        vr-pipeline-key.h
        vr-result.c
        vr-script.c
        vr-script.h
        vr-util.c
        vr-util.h
        vr-vk.c
        vr-vk.h
        vr-vk-core-funcs.h
        vr-vk-device-funcs.h
        vr-vk-instance-funcs.h
        vr-pipeline.c
        vr-pipeline.h
        vr-pipeline-properties.h
        vr-subprocess.c
        vr-subprocess.h
        vr-test.c
        vr-test.h
        vr-vbo.c
        vr-vbo.h
        vr-window.c
        vr-window.h
        ${VKRUNNER_PUBLIC_HEADERS}
        )

add_library(vkrunner STATIC ${VKRUNNER_SOURCE_FILES})

macro(vkrunner_add_lib libname)
  set(VKRUNNER_PRIVATE_LIBS "${VKRUNNER_PRIVATE_LIBS} -l${libname}")
  target_link_libraries(vkrunner ${libname})
endmacro()

if(NEED_LIBM)
  vkrunner_add_lib(m)
endif()

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  vkrunner_add_lib(dl)
endif()

include_directories(${VULKAN_INCLUDE_DIRS})
add_definitions(${VULKAN_CFLAGS_OTHER})

install(TARGETS vkrunner DESTINATION lib)
install(FILES ${VKRUNNER_PUBLIC_HEADERS} DESTINATION include/vkrunner)

configure_file(
  "${PROJECT_SOURCE_DIR}/vkrunner/vkrunner.pc.in"
  "${PROJECT_BINARY_DIR}/vkrunner/vkrunner.pc"
  @ONLY
  )
install(FILES "${PROJECT_BINARY_DIR}/vkrunner/vkrunner.pc"
  DESTINATION lib/pkgconfig)
