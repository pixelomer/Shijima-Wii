cmake_minimum_required(VERSION 3.18)

project(
  Shijima-Wii
  VERSION 1.0.0
  LANGUAGES CXX
)

# libshijima
set(SHIJIMA_BUILD_EXAMPLES NO)
set(SHIMEJIFINDER_BUILD_EXAMPLES NO)
add_subdirectory(libshijima)
include_directories(libshijima)

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

target_compile_options(${PROJECT_NAME} PRIVATE
  -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${DEVKITPRO}/portlibs/ppc/include
  ${DEVKITPRO}/portlibs/wii/include
)

target_sources(${PROJECT_NAME} PRIVATE
  source/main.cc
)

add_dependencies(${PROJECT_NAME} Shijima-Wii shijima)

target_link_libraries(${PROJECT_NAME} PRIVATE
  ${GRRLIB}
  ${PNGU}
  ${PNG_LIBRARIES}
  ${JPEG_LIBRARIES}
  ${FREETYPE_LIBRARIES}
  ${FAT}
  shijima
)

# Here we go, builds executable
ogc_create_dol(${PROJECT_NAME})
