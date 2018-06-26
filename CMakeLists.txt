cmake_minimum_required(VERSION 2.8.2)

include(TestBigEndian)
include(CheckLibraryExists)
include(CheckFunctionExists)
include(CheckCCompilerFlag)

project(VkRunner)
set(VKRUNNER_VERSION 0.1)

TEST_BIG_ENDIAN(IS_BIG_ENDIAN)
if(IS_BIG_ENDIAN)
  set(PLATFORM_ENDIANNESS 1)
else()
  set(PLATFORM_ENDIANNESS 0)
endif()

foreach(FLAG -Wall -Wuninitialized -Wempty-body -Wformat
        -Wformat-security -Winit-self -Wundef
        -Wvla -Wpointer-arith -Wmissing-declarations)
      CHECK_C_COMPILER_FLAG("${FLAG}" HAVE_FLAG-${FLAG})
      if(${HAVE_FLAG-${FLAG}})
        add_definitions("${FLAG}")
      endif()
endforeach()

CHECK_LIBRARY_EXISTS(m sinf "" NEED_LIBM)

CHECK_FUNCTION_EXISTS(ffs HAVE_FFS)
if(HAVE_FFS)
  add_definitions(-DHAVE_FFS)
endif()

CHECK_FUNCTION_EXISTS(ffsl HAVE_FFSL)
if(HAVE_FFSL)
  add_definitions(-DHAVE_FFSL)
endif()

configure_file(
  "${PROJECT_SOURCE_DIR}/config.h.in"
  "${PROJECT_BINARY_DIR}/config.h"
  )

add_subdirectory(vkrunner)
add_subdirectory(src)
