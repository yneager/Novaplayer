cmake_minimum_required(VERSION 3.21)
project(LambdaPlayer VERSION 0.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets WebChannel)

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

qt_add_executable(LambdaPlayer WIN32
  src/main.cpp
  src/homepage.cpp
  src/homepage.h
  src/playerchrome.cpp
  src/playerchrome.h
  src/mainwindow.cpp
  src/mainwindow.h
  src/interpolationcontroller.cpp
  src/interpolationcontroller.h
)

target_include_directories(LambdaPlayer PRIVATE "${MPV_INCLUDE_DIR}")
target_link_libraries(LambdaPlayer PRIVATE Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebChannel "${MPV_LIBRARY}")

set(VUI_RESOURCE_FILES
  resources/vui/index.html
  resources/vui/home.css
  resources/vui/player.html
  resources/vui/styles.css
  resources/vui/player.js
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/vui/InterVariable.woff2")
  list(APPEND VUI_RESOURCE_FILES resources/vui/InterVariable.woff2)
endif()

qt_add_resources(LambdaPlayer "vui_home"
  PREFIX "/vui"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/vui"
  FILES ${VUI_RESOURCE_FILES}
)

if(WIN32)
  target_compile_definitions(LambdaPlayer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MSVC)
  # Sources are UTF-8.
  target_compile_options(LambdaPlayer PRIVATE /utf-8)
endif()

set_target_properties(LambdaPlayer PROPERTIES OUTPUT_NAME "LambdaPlayer")
