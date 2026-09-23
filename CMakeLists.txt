cmake_minimum_required(VERSION 3.21)
project(NovaPlayer VERSION 0.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(MPV_ROOT "" CACHE PATH "Path to libmpv development package")
if(NOT MPV_INCLUDE_DIR)
  find_path(MPV_INCLUDE_DIR NAMES mpv/client.h HINTS "${MPV_ROOT}/include")
endif()
if(NOT MPV_LIBRARY)
  find_library(MPV_LIBRARY NAMES mpv mpv-2 mpv-1 HINTS "${MPV_ROOT}/lib")
endif()
if(NOT MPV_INCLUDE_DIR OR NOT MPV_LIBRARY)
  message(FATAL_ERROR "libmpv development files not found.")
endif()

qt_add_executable(NovaPlayer WIN32
  src/main.cpp
  src/mainwindow.cpp
  src/mainwindow.h
  src/interpolationcontroller.cpp
  src/interpolationcontroller.h
)

target_include_directories(NovaPlayer PRIVATE "${MPV_INCLUDE_DIR}")
target_link_libraries(NovaPlayer PRIVATE Qt6::Widgets "${MPV_LIBRARY}")

if(WIN32)
  target_compile_definitions(NovaPlayer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MSVC)
  # Sources are UTF-8.
  target_compile_options(NovaPlayer PRIVATE /utf-8)
endif()
