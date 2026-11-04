# Inkleaf, the native Qt app (Linux, Windows; Android later).
# Build:  cmake -S qt -B build/qt -DCMAKE_BUILD_TYPE=Release && cmake --build build/qt -j
# The Rust core (../core, via qt/native) is built with cargo and linked in.
cmake_minimum_required(VERSION 3.21)
# The version is the app's (../VERSION, set by scripts/set-version.py).
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION INKLEAF_VERSION_FILE)
string(STRIP "${INKLEAF_VERSION_FILE}" INKLEAF_VERSION_FILE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION)
project(Inkleaf VERSION ${INKLEAF_VERSION_FILE} LANGUAGES CXX)
# Android's version number: 0.8.1 → 801 (major*10000 + minor*100 + patch).
math(EXPR INKLEAF_VERSION_CODE "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/daftar ${CMAKE_CURRENT_SOURCE_DIR}/src/math)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Quick QuickControls2 Svg)
# Qt PDF draws imported PDFs; where it isn't shipped (Android) the system's
# PDF renderer is used instead.
find_package(Qt6 6.4 QUIET OPTIONAL_COMPONENTS Pdf)
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
set(CARGO_ENV "")
set(CARGO_OUT ${CARGO_TARGET_DIR}/${CARGO_PROFILE})
if(ANDROID)
  # Cross-compiled with the NDK for the ABI being built.
  set(RUST_TARGET_arm64-v8a aarch64-linux-android)
  set(RUST_TARGET_armeabi-v7a armv7-linux-androideabi)
  set(RUST_TARGET_x86_64 x86_64-linux-android)
  set(RUST_TARGET ${RUST_TARGET_${ANDROID_ABI}})
  string(TOUPPER ${RUST_TARGET} RUST_TARGET_ENV)
  string(REPLACE "-" "_" RUST_TARGET_ENV ${RUST_TARGET_ENV})
  set(CLANG_TRIPLE ${RUST_TARGET})
  if(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(CLANG_TRIPLE armv7a-linux-androideabi)
  endif()
  set(NDK_BIN ${ANDROID_NDK}/toolchains/llvm/prebuilt/${ANDROID_HOST_TAG}/bin)
  set(NDK_CC ${NDK_BIN}/${CLANG_TRIPLE}${ANDROID_PLATFORM_LEVEL}-clang)
  set(CARGO_ENV ${CMAKE_COMMAND} -E env
    CARGO_TARGET_${RUST_TARGET_ENV}_LINKER=${NDK_CC}
    CC_${RUST_TARGET}=${NDK_CC}
    AR_${RUST_TARGET}=${NDK_BIN}/llvm-ar)
  set(CARGO_FLAGS ${CARGO_FLAGS} --target ${RUST_TARGET})
  set(CARGO_OUT ${CARGO_TARGET_DIR}/${RUST_TARGET}/${CARGO_PROFILE})
endif()
if(WIN32)
  set(NATIVE_LIB ${CARGO_OUT}/inkleaf_native.lib)
else()
  set(NATIVE_LIB ${CARGO_OUT}/libinkleaf_native.a)
endif()
add_custom_target(inkleaf_native_build
  COMMAND ${CARGO_ENV} cargo build -p inkleaf-native ${CARGO_FLAGS} --target-dir ${CARGO_TARGET_DIR}
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
  src/Canvas.h src/Canvas.cpp
  src/draw/Fonts.h src/draw/Fonts.cpp
  src/draw/Papers.h src/draw/Papers.cpp
  src/draw/Covers.h src/draw/Covers.cpp
  src/draw/DaftarPapers.h src/draw/DaftarPapers.cpp
  src/draw/Freehand.h src/draw/Freehand.cpp
  src/draw/Strokes.h src/draw/Strokes.cpp
  src/draw/Elements.h src/draw/Elements.cpp
  src/editor/Model.h src/editor/Model.cpp
  src/editor/Geometry.h src/editor/Geometry.cpp
  src/editor/TextFormat.h src/editor/TextFormat.cpp
  src/editor/Snap.h src/editor/Snap.cpp
  src/editor/Table.h src/editor/Table.cpp
  src/editor/Gestures.h src/editor/Gestures.cpp
  src/editor/Instrument.h src/editor/Instrument.cpp
  src/editor/EditorCore.h src/editor/EditorCore.cpp
  src/draw/Render.h src/draw/Render.cpp
  src/Catalog.h src/Catalog.cpp
  src/daftar/Store.h src/daftar/Store.cpp
  src/daftar/Daftar.h src/daftar/Daftar.cpp
  src/daftar/Cover.h src/daftar/Cover.cpp
  src/daftar/DaftarApi.h src/daftar/DaftarApi.cpp
  src/daftar/DaftarEditor.h src/daftar/DaftarEditor.cpp
  src/tests/SelfTest.h src/tests/SelfTest.cpp
  src/tests/GeometryTests.cpp
  src/tests/TextAndSnapTests.cpp src/tests/MathTests.cpp src/tests/TableTests.cpp src/tests/GestureTests.cpp src/tests/DaftarTests.cpp src/tests/Samples.h src/tests/Samples.cpp src/tests/Golden.h src/tests/Golden.cpp src/tests/EditorSessions.h src/tests/EditorSessions.cpp src/tests/JsonCompare.h
  src/PaperImage.h src/PaperImage.cpp
  src/Pdf.h src/Pdf.cpp
  src/Updater.h src/Updater.cpp
  src/math/MathExpr.h src/math/MathExpr.cpp
  src/math/MathKit.h src/math/MathKit.cpp
  src/LaserTrail.h src/LaserTrail.cpp
  src/VectorSvg.h src/VectorSvg.cpp
)

# Theme is a singleton (must be set before the module is declared).
set_source_files_properties(qml/Theme.qml qml/AppSettings.qml qml/Ai.qml qml/Notice.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Fonts: the UI's Inter and the handwriting faces for text (as in src/fonts.ts).
file(GLOB INKLEAF_FONTS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf)

# Prefs.qml wraps Settings from QtCore (Qt 6.5+) or Qt.labs.settings (6.4).
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
  set(INKLEAF_SETTINGS_IMPORT QtCore)
else()
  set(INKLEAF_SETTINGS_IMPORT Qt.labs.settings)
endif()
configure_file(qml/Prefs.qml.in ${CMAKE_CURRENT_BINARY_DIR}/gen/Prefs.qml @ONLY)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/gen/Prefs.qml PROPERTIES QT_RESOURCE_ALIAS qml/Prefs.qml
  # (generated outside the source folder: qmlcachegen's path for it breaks on Windows)
  QT_QML_SKIP_CACHEGEN TRUE)
# QML files in sub-folders (qml/editor, qml/ui) get qmldir files of their
# own (Qt 6.8+), so the folders' files find each other.
if(QT_KNOWN_POLICY_QTP0004)
  qt_policy(SET QTP0004 NEW)
endif()
qt_add_qml_module(inkleaf
  URI Inkleaf
  VERSION 1.0
  RESOURCE_PREFIX /qt/qml
  QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/AppSettings.qml
    qml/Ai.qml
    qml/Notice.qml
    qml/SettingsDialog.qml
    qml/Library.qml
    qml/Editor.qml
    qml/Daftar.qml
    qml/Services.qml
    qml/ui/SignInDialog.qml
    qml/ui/UpdateBanner.qml
    qml/ui/NewNotebookDialog.qml
    qml/IconButton.qml
    qml/Icon.qml
    qml/IconData.js
    qml/ui/Shadow.qml
    qml/ui/LiveImage.qml
    qml/ui/UiButton.qml
    qml/ui/Segmented.qml
    qml/ui/UiSwitch.qml
    qml/ui/UiSlider.qml
    qml/ui/UiField.qml
    qml/ui/UiSelect.qml
    qml/ui/Swatches.qml
    qml/ui/PopupMenu.qml
    qml/ui/FormDialog.qml
    qml/ui/Kbd.qml
    qml/ui/Toast.qml
    qml/ui/Book.qml
    qml/ui/FolderArt.qml
    qml/ui/Card.qml
    qml/ui/NewTile.qml
    qml/ui/CreateMenu.qml
    qml/ui/CreateItem.qml
    qml/ui/DashedBox.qml
    qml/ui/ImportPanel.qml
    qml/ui/AiPanel.qml
    qml/editor/ToolPrefs.qml
    qml/editor/ToolBtn.qml
    qml/editor/Pill.qml
    qml/editor/Chip.qml
    qml/editor/SmallTool.qml
    qml/editor/FormatButtons.qml
    qml/editor/ToolGroup.qml
    qml/editor/ModeGroup.qml
    qml/editor/InkToText.qml
    qml/editor/EraserCursor.qml
    qml/editor/MathCard.qml
    qml/editor/GraphDialog.qml
    qml/editor/TableCellEditor.qml
    qml/editor/TableHandles.qml
    qml/editor/TablePicker.qml
    qml/editor/ScratchNotice.qml
    qml/editor/PiecesPanel.qml
    qml/editor/SidePane.qml
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
    ${INKLEAF_FONTS}
)

# The controls style (tooltips, scroll bars; everything else from Basic): a
# plain QML module in the app's resources.
qt_add_resources(inkleaf "inkleaf_style"
  PREFIX /qt/qml/InkleafStyle
  BASE style
  FILES style/qmldir style/ToolTip.qml style/ScrollBar.qml style/ScrollIndicator.qml
)

# What's new in each version (shown after an update, and in Settings).
qt_add_resources(inkleaf "inkleaf_changelog"
  PREFIX /
  BASE ${CMAKE_CURRENT_SOURCE_DIR}/..
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/../CHANGELOG.md
)

target_compile_definitions(inkleaf PRIVATE INKLEAF_VERSION="${PROJECT_VERSION}")
target_link_libraries(inkleaf PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Svg Qt6::Quick Qt6::QuickControls2 inkleaf_native)
if(TARGET Qt6::Pdf)
  target_link_libraries(inkleaf PRIVATE Qt6::Pdf)
  target_compile_definitions(inkleaf PRIVATE INKLEAF_HAVE_QTPDF)
elseif(NOT ANDROID)
  message(FATAL_ERROR "Qt PDF is needed (qt6-pdf / qt6-webengine)")
endif()
if(ANDROID)
  target_link_libraries(inkleaf PRIVATE dl m log)
  # A separate app for now (the current Android app keeps its own library).
  set_target_properties(inkleaf PROPERTIES
    QT_ANDROID_PACKAGE_NAME app.inkleaf.qt
    QT_ANDROID_APP_NAME Inkleaf
    QT_ANDROID_VERSION_NAME ${PROJECT_VERSION}
    QT_ANDROID_VERSION_CODE ${INKLEAF_VERSION_CODE}
    QT_ANDROID_MIN_SDK_VERSION 28
    QT_ANDROID_TARGET_SDK_VERSION 35
    QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android)
elseif(UNIX AND NOT APPLE)
  target_link_libraries(inkleaf PRIVATE dl pthread m)
endif()
if(WIN32)
  target_compile_definitions(inkleaf PRIVATE _USE_MATH_DEFINES)  # M_PI
  target_sources(inkleaf PRIVATE packaging/inkleaf.rc)  # the app icon
  target_link_libraries(inkleaf PRIVATE ws2_32 userenv bcrypt ntdll advapi32 ole32 oleaut32 runtimeobject propsys shell32 crypt32 secur32 ncrypt)
  set_target_properties(inkleaf PROPERTIES WIN32_EXECUTABLE ON)
endif()

if(NOT ANDROID)  # (the APK is made by Qt's apk target)
  install(TARGETS inkleaf RUNTIME DESTINATION bin)
endif()
if(UNIX AND NOT APPLE AND NOT ANDROID)
  install(FILES packaging/inkleaf.desktop DESTINATION share/applications)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/inkleaf.png DESTINATION share/icons/hicolor/256x256/apps RENAME inkleaf.png)
endif()
