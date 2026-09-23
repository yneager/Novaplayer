cmake_minimum_required(VERSION 3.21)
project(NovaPlayer VERSION 0.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets)

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
  src/homepage.cpp
  src/homepage.h
  src/mainwindow.cpp
  src/mainwindow.h
  src/interpolationcontroller.cpp
  src/interpolationcontroller.h
)

target_include_directories(NovaPlayer PRIVATE "${MPV_INCLUDE_DIR}")
target_link_libraries(NovaPlayer PRIVATE Qt6::Widgets Qt6::WebEngineWidgets "${MPV_LIBRARY}")

set(VUI_RESOURCE_FILES
  resources/vui/index.html
  resources/vui/home.css
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/vui/InterVariable.woff2")
  list(APPEND VUI_RESOURCE_FILES resources/vui/InterVariable.woff2)
endif()

qt_add_resources(NovaPlayer "vui_home"
  PREFIX "/vui"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/vui"
  FILES ${VUI_RESOURCE_FILES}
)

if(WIN32)
  target_compile_definitions(NovaPlayer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MSVC)
  # Sources are UTF-8.
  target_compile_options(NovaPlayer PRIVATE /utf-8)
endif()
