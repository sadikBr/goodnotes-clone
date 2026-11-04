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

#include "Catalog.h"
#include "daftar/Cover.h"
#include <QElapsedTimer>
#include "draw/Covers.h"
#include "draw/Render.h"
#include "tests/Golden.h"
#include "tests/SelfTest.h"
#include "Pdf.h"
#include "PaperImage.h"
#include "ShadowImage.h"

int main(int argc, char* argv[]) {
  // No multisampling for the window: it turns off Qt Quick's own smooth
  // edges (rounded corners, circles) and isn't always honoured by the
  // driver, which left them jagged. The page canvas gets its own
  // multisampled layer for smooth ink (Editor.qml).

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
  QLoggingCategory::setFilterRules(QStringLiteral("qt.qml.compiler=false\nqt.qml.usedbeforedeclared=false"));
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

  // The C++ code's own tests.
  if (QCoreApplication::arguments().contains("--self-test")) return selftest::runAll();
  // The reference results the code is checked against (docs/MIGRATION.md):
  // inkleaf --golden record|check <folder> [<diffs folder>].
  if (const int i = QCoreApplication::arguments().indexOf("--golden"); i > 0 && i + 2 < argc)
    return golden(QCoreApplication::arguments().at(i + 1), QCoreApplication::arguments().at(i + 2), QCoreApplication::arguments().value(i + 3));
  // Developer check: times the cover previews (inkleaf --time-covers).
  if (QCoreApplication::arguments().contains("--time-covers")) {
    for (int w : {128, 240, 500})
      for (const draw::CoverDesign& d : draw::coverDesigns()) {
        QElapsedTimer t;
        t.start();
        for (int k = 0; k < 3; ++k) cover::coverPreview(d.id, "#2563eb", QString("Notebook %1").arg(k), w, false);
        qInfo("%4d px %-10s %6.1f ms", w, qPrintable(d.id), t.nsecsElapsed() / 3e6);
      }
    return 0;
  }
  // Developer check: draws every paper and cover, then quits (inkleaf --render-papers <folder>).
  if (const int i = QCoreApplication::arguments().indexOf("--render-papers"); i > 0 && i + 1 < argc) {
    const QString dir = QCoreApplication::arguments().at(i + 1);
    QDir().mkpath(dir);
    for (const PaperTemplate& t : paperTemplates()) draw::paperImage(t.id, "#fffdf7", 794, 1123, 1.0).save(dir + "/" + t.id + ".png");
    for (const draw::CoverDesign& d : draw::coverDesigns()) draw::paperImage(d.tmpl, "#2563eb", 794, 1123, 0.5).save(dir + "/" + d.tmpl + ".png");
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
    // The dialogs' modules, and the editor (it loads only when a notebook opens).
    extra.setData("import QtQuick\nimport QtQuick.Dialogs\nimport Qt.labs.folderlistmodel\nimport Inkleaf\nItem { FolderDialog {} FileDialog {} FolderListModel {} Editor { notebookId: \"\" } }", QUrl("smoke.qml"));
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
