// Inkleaf — native Qt app.
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QTimer>
#include <QQmlComponent>
#include <QNetworkProxyFactory>
#include <QIcon>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>

#include "Js.h"
#include "Pdf.h"
#include "PaperImage.h"
#include "ShadowImage.h"

int main(int argc, char* argv[]) {
  // Antialiased ink: the GPU smooths stroke edges (4× multisampling).
  QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
  fmt.setSamples(4);
  QSurfaceFormat::setDefaultFormat(fmt);

  // Set before the application exists, so the desktop portal is told the
  // app's id once, at start-up.
  QGuiApplication::setApplicationName("Inkleaf");
  QGuiApplication::setOrganizationName("Inkleaf");
  QGuiApplication::setApplicationVersion(QStringLiteral(INKLEAF_VERSION));
  QGuiApplication::setDesktopFileName("inkleaf");
  QGuiApplication app(argc, argv);
  // Update checks (and anything else online) go through the system's proxy settings.
  QNetworkProxyFactory::setUseSystemConfiguration(true);
  // The bundled web code trips the QML compiler's style hints; they're noise here.
  QLoggingCategory::setFilterRules(QStringLiteral("qt.qml.compiler=false"));
  QGuiApplication::setWindowIcon(QIcon(":/qt/qml/Inkleaf/assets/icon.svg"));

  // The UI font and every handwriting face for text boxes and notes.
  // Loaded from files: Qt drops kerning for fonts loaded from memory, so the
  // bundled ones are copied to the cache folder first (once per version).
  {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/fonts-" + QStringLiteral(INKLEAF_VERSION);
    QDir().mkpath(dir);
    for (const QString& f : QDir(QStringLiteral(":/qt/qml/Inkleaf/assets/fonts")).entryList({"*.ttf"})) {
      const QString src = QStringLiteral(":/qt/qml/Inkleaf/assets/fonts/") + f, dst = dir + "/" + f;
      if (QFileInfo(dst).size() != QFileInfo(src).size()) {
        QFile::remove(dst);
        QFile::copy(src, dst);
      }
      if (QFontDatabase::addApplicationFont(dst) < 0) QFontDatabase::addApplicationFont(src);
    }
  }

  // Developer check: evaluates a script with the bridge loaded and prints
  // the result (inkleaf --js "<expression>").
  if (const int i = QCoreApplication::arguments().indexOf("--js"); i > 0 && i + 1 < argc) {
    QJSValue r = Js::instance().engine().evaluate(QCoreApplication::arguments().at(i + 1));
    // Promises settle after the script returns; __after() reads their results.
    QCoreApplication::processEvents();
    const QJSValue after = Js::instance().engine().globalObject().property("__after");
    if (after.isCallable()) r = after.call();
    qInfo().noquote() << (r.isError() ? "error: " + r.toString() : Js::instance().engine().evaluate("JSON.stringify").call({r}).toString());
    return 0;
  }

  // Developer check: imports PDFs, then quits (inkleaf --import-pdf <file>…).
  if (const int i = QCoreApplication::arguments().indexOf("--import-pdf"); i > 0 && i + 1 < argc) {
    PdfImporter importer;
    QVariantList files;
    for (const QString& f : QCoreApplication::arguments().mid(i + 1)) files << f;
    importer.add(files, QString());
    while (importer.busy() || importer.jobs().isEmpty()) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    for (const QVariant& j : importer.jobs()) qInfo().noquote() << j.toMap()["name"].toString() << j.toMap()["state"].toString() << j.toMap()["label"].toString();
    return 0;
  }

  // Developer check: draws every paper with the web app's code, then quits
  // (inkleaf --render-papers <folder>).
  if (const int i = QCoreApplication::arguments().indexOf("--render-papers"); i > 0 && i + 1 < argc) {
    const QString dir = QCoreApplication::arguments().at(i + 1);
    QDir().mkpath(dir);
    QJSValue list = Js::instance().fn("TEMPLATES");
    const int n = list.property("length").toInt();
    for (int k = 0; k < n; ++k) {
      const QString id = list.property(k).property("id").toString();
      Js::instance().paper(id, "#fffdf7", 794, 1123, 1.0).save(dir + "/" + id + ".png");
    }
    for (const char* c : {"cover", "cover-geometric", "cover-zellige", "cover-marble", "cover-waves", "cover-leather", "cover-kraft", "cover-dots", "cover-minimal"})
      Js::instance().paper(c, "#2563eb", 794, 1123, 0.5).save(dir + "/" + c + ".png");
    return 0;
  }
  QFont ui("Inter");
  ui.setPixelSize(14);
  ui.setHintingPreference(QFont::PreferNoHinting);
  QGuiApplication::setFont(ui);

  // Our tooltips and scroll bars; the rest from the Basic style.
  QQuickStyle::setStyle("InkleafStyle");
  QQmlApplicationEngine engine;
  engine.addImageProvider("shadow", new ShadowImage);
  engine.addImageProvider("paper", new PaperImage);
  engine.addImageProvider("ink", new InkSampleImage);
  engine.addImageProvider("cover", new CoverImage);
  engine.addImportPath(QStringLiteral("qrc:/qt/qml"));  // the default from Qt 6.5 on
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  // Packaging check (inkleaf --smoke): the window loads without QML errors and
  // the modules Qt's own file dialogs need are there; exits 0 when all is well.
  static bool smoke = QCoreApplication::arguments().contains("--smoke");
  static int qmlErrors = 0;
  if (smoke) {
    static QtMessageHandler previous = qInstallMessageHandler([](QtMsgType t, const QMessageLogContext& c, const QString& m) {
      if (t != QtDebugMsg && t != QtInfoMsg && (m.contains("qrc:/") || m.contains("module") || m.contains("unavailable"))) ++qmlErrors;
      previous(t, c, m);
    });
  }
  engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Inkleaf/qml/Main.qml")));
  if (smoke) {
    QQmlComponent extra(&engine);
    extra.setData("import QtQuick\nimport QtQuick.Dialogs\nimport Qt.labs.folderlistmodel\nItem { FolderDialog {} FileDialog {} FolderListModel {} }", QUrl("smoke.qml"));
    std::unique_ptr<QObject> made(extra.create());
    if (!made) {
      qWarning().noquote() << "smoke: dialogs module missing:" << extra.errorString();
      return 1;
    }
    QTimer::singleShot(4000, &app, [&engine] {
      const bool ok = !engine.rootObjects().isEmpty() && qmlErrors == 0;
      qInfo("smoke: %s (%d QML problems)", ok ? "ok" : "FAILED", qmlErrors);
      QCoreApplication::exit(ok ? 0 : 1);
    });
  }
  return app.exec();
}
