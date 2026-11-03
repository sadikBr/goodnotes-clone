# Inkleaf, the native Qt app (Linux, Windows; Android later).
# Build:  cmake -S qt -B build/qt -DCMAKE_BUILD_TYPE=Release && cmake --build build/qt -j
# The Rust core (../core, via qt/native) is built with cargo and linked in.
cmake_minimum_required(VERSION 3.21)
project(Inkleaf VERSION 0.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2)
qt_standard_project_setup()

# --- the Rust core -------------------------------------------------------------
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CARGO_PROFILE debug)
  set(CARGO_FLAGS "")
else()
  set(CARGO_PROFILE release)
  set(CARGO_FLAGS --release)
endif()
set(CARGO_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/cargo)
if(WIN32)
  set(NATIVE_LIB ${CARGO_TARGET_DIR}/${CARGO_PROFILE}/inkleaf_native.lib)
else()
  set(NATIVE_LIB ${CARGO_TARGET_DIR}/${CARGO_PROFILE}/libinkleaf_native.a)
endif()
add_custom_target(inkleaf_native_build
  COMMAND cargo build -p inkleaf-native ${CARGO_FLAGS} --target-dir ${CARGO_TARGET_DIR}
  WORKING_DIRECTORY ${REPO_ROOT}
  BYPRODUCTS ${NATIVE_LIB}
  COMMENT "Building the Rust core"
  USES_TERMINAL)
add_library(inkleaf_native STATIC IMPORTED)
set_target_properties(inkleaf_native PROPERTIES IMPORTED_LOCATION ${NATIVE_LIB})
add_dependencies(inkleaf_native inkleaf_native_build)

# --- the app -------------------------------------------------------------------
qt_add_executable(inkleaf
  src/main.cpp
  src/Core.h src/Core.cpp
  src/Ink.h src/Ink.cpp
  src/Paper.h src/Paper.cpp
  src/InkCanvas.h src/InkCanvas.cpp
)

# Theme is a singleton (must be set before the module is declared).
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(inkleaf
  URI Inkleaf
  VERSION 1.0
  RESOURCE_PREFIX /qt/qml
  QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/Library.qml
    qml/Editor.qml
    qml/IconButton.qml
    qml/Icon.qml
    qml/IconData.js
  RESOURCES
    assets/icon.svg
    assets/fonts/inter-latin-400-normal.woff
    assets/fonts/inter-latin-600-normal.woff
    assets/fonts/inter-latin-700-normal.woff
    assets/fonts/inter-latin-ext-400-normal.woff
)

target_compile_definitions(inkleaf PRIVATE INKLEAF_VERSION="${PROJECT_VERSION}")
target_link_libraries(inkleaf PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 inkleaf_native)
if(UNIX AND NOT APPLE)
  target_link_libraries(inkleaf PRIVATE dl pthread m)
endif()
if(WIN32)
  target_link_libraries(inkleaf PRIVATE ws2_32 userenv bcrypt ntdll advapi32 ole32 oleaut32 runtimeobject propsys shell32 crypt32 secur32 ncrypt)
  set_target_properties(inkleaf PROPERTIES WIN32_EXECUTABLE ON)
endif()

install(TARGETS inkleaf RUNTIME DESTINATION bin)
