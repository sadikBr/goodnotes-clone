// Not part of the app: it only tells the packaging tools (linuxdeploy's Qt
// plugin, windeployqt, androiddeployqt) to bundle QML modules that the app
// imports from generated files they never see (gen/Prefs.qml: QtCore).
import QtCore

QtObject {}
