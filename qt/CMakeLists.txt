# Inkleaf, the native Qt app (Linux, Windows; Android later).
# Build:  cmake -S qt -B build/qt -DCMAKE_BUILD_TYPE=Release && cmake --build build/qt -j
# The Rust core (../core, via qt/native) is built with cargo and linked in.
cmake_minimum_required(VERSION 3.21)
project(Inkleaf VERSION 0.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Pdf)
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
  src/InkCanvas.h src/InkCanvas.cpp
  src/RoundImage.h src/RoundImage.cpp
  src/ShadowImage.h src/ShadowImage.cpp
  src/JsCanvas.h src/JsCanvas.cpp
  src/Js.h src/Js.cpp
  src/PaperImage.h src/PaperImage.cpp
  src/Pdf.h src/Pdf.cpp
)

# Theme is a singleton (must be set before the module is declared).
set_source_files_properties(qml/Theme.qml qml/AppSettings.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Fonts: the UI's Inter and the handwriting faces for text (as in src/fonts.ts).
file(GLOB INKLEAF_FONTS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf)

# Prefs.qml wraps Settings from QtCore (Qt 6.5+) or Qt.labs.settings (6.4).
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
  set(INKLEAF_SETTINGS_IMPORT QtCore)
else()
  set(INKLEAF_SETTINGS_IMPORT Qt.labs.settings)
endif()
configure_file(qml/Prefs.qml.in ${CMAKE_CURRENT_BINARY_DIR}/gen/Prefs.qml @ONLY)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/gen/Prefs.qml PROPERTIES QT_RESOURCE_ALIAS qml/Prefs.qml)
qt_add_qml_module(inkleaf
  URI Inkleaf
  VERSION 1.0
  RESOURCE_PREFIX /qt/qml
  QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/AppSettings.qml
    qml/SettingsDialog.qml
    qml/Library.qml
    qml/Editor.qml
    qml/IconButton.qml
    qml/Icon.qml
    qml/IconData.js
    qml/ui/Shadow.qml
    qml/ui/UiButton.qml
    qml/ui/Segmented.qml
    qml/ui/UiSwitch.qml
    qml/ui/UiSlider.qml
    qml/ui/UiField.qml
    qml/ui/UiSelect.qml
    qml/ui/Swatches.qml
    qml/ui/PopupMenu.qml
    qml/ui/FormDialog.qml
    qml/ui/Toast.qml
    qml/ui/Book.qml
    qml/ui/FolderArt.qml
    qml/ui/Card.qml
    qml/ui/NewTile.qml
    qml/ui/CreateMenu.qml
    qml/ui/CreateItem.qml
    qml/ui/DashedBox.qml
    qml/ui/ImportPanel.qml
    qml/editor/ToolPrefs.qml
    qml/editor/ToolBtn.qml
    qml/editor/Pill.qml
    qml/editor/Chip.qml
    qml/editor/SmallTool.qml
    qml/editor/FormatButtons.qml
    qml/editor/Fonts.js
    qml/editor/ColorPopover.qml
    qml/editor/ToolDrawer.qml
    qml/editor/SelectionBar.qml
    qml/editor/PageSidebar.qml
    qml/editor/PaperPicker.qml
    qml/editor/CoverPicker.qml
    qml/editor/Instrument.qml
    qml/editor/TextEditor.qml
    qml/editor/Exporter.qml
    qml/editor/ShortcutsSheet.qml
    ${CMAKE_CURRENT_BINARY_DIR}/gen/Prefs.qml
  RESOURCES
    assets/icon.svg
    js/bridge.js
    ${INKLEAF_FONTS}
)

# The controls style (tooltips, scroll bars; everything else from Basic): a
# plain QML module in the app's resources.
qt_add_resources(inkleaf "inkleaf_style"
  PREFIX /qt/qml/InkleafStyle
  BASE style
  FILES style/qmldir style/ToolTip.qml style/ScrollBar.qml style/ScrollIndicator.qml
)

target_compile_definitions(inkleaf PRIVATE INKLEAF_VERSION="${PROJECT_VERSION}")
target_link_libraries(inkleaf PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Pdf inkleaf_native)
if(UNIX AND NOT APPLE)
  target_link_libraries(inkleaf PRIVATE dl pthread m)
endif()
if(WIN32)
  target_link_libraries(inkleaf PRIVATE ws2_32 userenv bcrypt ntdll advapi32 ole32 oleaut32 runtimeobject propsys shell32 crypt32 secur32 ncrypt)
  set_target_properties(inkleaf PROPERTIES WIN32_EXECUTABLE ON)
endif()

install(TARGETS inkleaf RUNTIME DESTINATION bin)
