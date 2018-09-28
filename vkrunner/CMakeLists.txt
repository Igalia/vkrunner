# cmake file for VkRunner

include_directories(${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR})

set(VKRUNNER_PUBLIC_HEADERS
        vr-callback.h
        vr-executor.h
        vr-format.h
        vr-inspect.h
        vr-result.h
        vr-source.h
        vkrunner.h
        )

set(VKRUNNER_SOURCE_FILES
        vr-allocate-store.c
        vr-allocate-store.h
        vr-box.c
        vr-box.h
        vr-buffer.c
        vr-buffer.h
        vr-config.h
        vr-context.c
        vr-context.h
        vr-enum-table.h
        vr-error-message.c
        vr-error-message.h
        vr-executor.c
        vr-executor-private.h
        vr-feature-offsets.c
        vr-feature-offsets.h
        vr-flush-memory.c
        vr-flush-memory.h
        vr-format-table.h
        vr-format-private.h
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
        vr-shader-stage.h
        vr-source-private.h
        vr-source.c
        vr-stream.c
        vr-stream.h
        vr-strtof.c
        vr-strtof.h
        vr-temp-file.c
        vr-temp-file.h
        vr-util.c
        vr-util.h
        vr-vk.c
        vr-vk.h
        vr-vk-device-funcs.h
        vr-vk-instance-funcs.h
        vr-pipeline.c
        vr-pipeline.h
        vr-pipeline-properties.h
        vr-subprocess.c
        vr-subprocess.h
        vr-test.c
        vr-test.h
        vr-tolerance.c
        vr-tolerance.h
        vr-vbo.c
        vr-vbo.h
        vr-window.c
        vr-window.h
        ${VKRUNNER_PUBLIC_HEADERS}
        )

set(VKRUNNER_TEST_SOURCES
        test-script.c
        test-stream.c
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

option(BUILD_VKRUNNER_TESTS "Whether to buid VkRunner unit tests" OFF)

if(BUILD_VKRUNNER_TESTS)
  add_executable(unit-tester ${VKRUNNER_TEST_SOURCES})
  target_link_libraries(unit-tester vkrunner)

  pkg_check_modules(CRITERION criterion)
  if(NOT CRITERION_FOUND)
    message(FATAL_ERROR
      "Criterion is required to build the unit tests")
  endif()

  target_link_libraries(unit-tester ${CRITERION_LDFLAGS} ${CRITERION_LIBRARIES})
  include_directories(${CRITERION_INCLUDE_DIRS})
  add_definitions(${CRITERION_CFLAGS_OTHER})

  add_definitions("-DBUILD_VKRUNNER_TESTS")
endif()

configure_file(
  "${PROJECT_SOURCE_DIR}/vkrunner/vkrunner.pc.in"
  "${PROJECT_BINARY_DIR}/vkrunner/vkrunner.pc"
  @ONLY
  )
install(FILES "${PROJECT_BINARY_DIR}/vkrunner/vkrunner.pc"
  DESTINATION lib/pkgconfig)
