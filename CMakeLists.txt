cmake_minimum_required(VERSION 3.18)

if(CMAKE_BUILD_TYPE EQUAL "Release")
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

project(
  Shijima-Wii
  VERSION 1.0.0
  LANGUAGES CXX
)

# libshijima
if(NOT DEFINED SHIJIMA_USE_PUGIXML)
  set(SHIJIMA_USE_PUGIXML NO)
endif()
set(SHIJIMA_BUILD_EXAMPLES NO)
set(SHIMEJIFINDER_BUILD_EXAMPLES NO)
add_subdirectory(libshijima)
include_directories(libshijima)

# qutex
add_subdirectory(qutex/loader)
include_directories(qutex)

# GRRLIB and its dependencies
find_library(GRRLIB grrlib REQUIRED)
find_library(PNGU pngu REQUIRED)
find_library(FAT fat
  PATHS "${OGC_ROOT}/lib/${OGC_SUBDIR}"
  REQUIRED
)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JPEG REQUIRED libjpeg)
pkg_check_modules(PNG REQUIRED libpng)
pkg_check_modules(FREETYPE REQUIRED freetype2)

add_executable(${PROJECT_NAME})

if(SHIJIMA_USE_PUGIXML)
  target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers
  )
else()
  target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers
    -DSHIJIMA_NO_PUGIXML
  )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
  ${DEVKITPRO}/portlibs/ppc/include
  ${DEVKITPRO}/portlibs/wii/include
)

target_sources(${PROJECT_NAME} PRIVATE
  source/main.cc
)

add_dependencies(${PROJECT_NAME} Shijima-Wii shijima qutex-loader)

target_link_libraries(${PROJECT_NAME} PRIVATE
  ${GRRLIB}
  ${PNGU}
  ${PNG_LIBRARIES}
  ${JPEG_LIBRARIES}
  ${FREETYPE_LIBRARIES}
  ${FAT}
  shijima
  qutex-loader
)

# Here we go, builds executable
ogc_create_dol(${PROJECT_NAME})
