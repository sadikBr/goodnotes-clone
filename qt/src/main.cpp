// Inkleaf — native Qt app.
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
  // Antialiased ink: the GPU smooths stroke edges (4× multisampling).
  QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
  fmt.setSamples(4);
  QSurfaceFormat::setDefaultFormat(fmt);

  QGuiApplication app(argc, argv);
  QGuiApplication::setApplicationName("Inkleaf");
  QGuiApplication::setOrganizationName("Inkleaf");
  QGuiApplication::setApplicationVersion(QStringLiteral(INKLEAF_VERSION));
  QGuiApplication::setDesktopFileName("inkleaf");
  QGuiApplication::setWindowIcon(QIcon(":/qt/qml/Inkleaf/assets/icon.svg"));

  for (const char* f : {"inter-latin-400-normal.woff", "inter-latin-600-normal.woff", "inter-latin-700-normal.woff", "inter-latin-ext-400-normal.woff"})
    QFontDatabase::addApplicationFont(QStringLiteral(":/qt/qml/Inkleaf/assets/fonts/") + f);
  QFont ui("Inter");
  ui.setPixelSize(14);
  ui.setHintingPreference(QFont::PreferNoHinting);
  QGuiApplication::setFont(ui);

  QQuickStyle::setStyle("Basic");
  QQmlApplicationEngine engine;
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Inkleaf/qml/Main.qml")));
  return app.exec();
}
