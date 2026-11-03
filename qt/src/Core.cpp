#include "Core.h"

#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <QDir>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QStandardPaths>

extern "C" {
struct InkleafResult {
  bool ok;
  bool is_json;
  uint8_t* data;
  size_t len;
};
::Core* inkleaf_open(const char* data_dir, InkleafResult* error);
InkleafResult inkleaf_call(const ::Core* core, const char* cmd, const char* args, const uint8_t* bytes, size_t len);
void inkleaf_free(InkleafResult r);
void inkleaf_close(::Core* core);
}

namespace {
InkleafCore* g_instance = nullptr;

/// The library folder: the same one the earlier (Tauri) app used, so its
/// notebooks open here (Linux ~/.local/share/app.inkleaf.desktop,
/// Windows %APPDATA%\app.inkleaf.desktop).
QString libraryDir() {
  if (const QByteArray env = qgetenv("INKLEAF_DATA_DIR"); !env.isEmpty()) return QString::fromUtf8(env);
#ifdef Q_OS_WIN
  const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);  // %APPDATA% (roaming)
  const QString roaming = qEnvironmentVariable("APPDATA", base);
  return QDir(roaming).filePath("app.inkleaf.desktop");
#else
  return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("app.inkleaf.desktop");
#endif
}

/// Commands that change what the library shows.
bool changesLibrary(const QString& cmd) {
  static const QStringList reads = {"library", "trash", "search", "notebook", "pages", "elements", "assetBytes", "buildDocument", "backupStatus", "syncStatus", "dataDir", "simplifyStroke", "recognizeHandwriting", "inspectBackup"};
  return !reads.contains(cmd) && cmd != "saveElements" && cmd != "setPageThumbnail";
}
}  // namespace

InkleafCore::InkleafCore(QObject* parent) : QObject(parent) {
  g_instance = this;
  m_dir = libraryDir();
  QDir().mkpath(m_dir);
  InkleafResult err{false, true, nullptr, 0};
  m_core = inkleaf_open(QDir::toNativeSeparators(m_dir).toUtf8().constData(), &err);
  if (!m_core) {
    m_lastError = QString::fromUtf8(reinterpret_cast<const char*>(err.data), int(err.len));
    inkleaf_free(err);
    qCritical("Inkleaf: can't open the library in %s: %s", qPrintable(m_dir), qPrintable(m_lastError));
  }
}

InkleafCore::~InkleafCore() {
  if (m_core) inkleaf_close(m_core);
  if (g_instance == this) g_instance = nullptr;
}

InkleafCore* InkleafCore::instance() {
  if (!g_instance) new InkleafCore();
  return g_instance;
}

InkleafCore* InkleafCore::create(QQmlEngine*, QJSEngine*) {
  // One core for the whole app; QML must not delete it.
  if (!g_instance) new InkleafCore();
  QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
  return g_instance;
}

QJsonValue InkleafCore::callJson(const QString& cmd, const QJsonObject& args, QString* error, const QByteArray& bytes) {
  if (!m_core) {
    if (error) *error = m_lastError;
    return {};
  }
  const QByteArray a = QJsonDocument(args).toJson(QJsonDocument::Compact);
  InkleafResult r = inkleaf_call(m_core, cmd.toUtf8().constData(), a.constData(), reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size()));
  const QByteArray out(reinterpret_cast<const char*>(r.data), int(r.len));
  inkleaf_free(r);
  // Answers are wrapped so plain values (strings, null) parse as JSON too.
  const QJsonDocument doc = QJsonDocument::fromJson("[" + out + "]");
  const QJsonValue v = doc.array().isEmpty() ? QJsonValue() : doc.array().first();
  if (!r.ok) {
    m_lastError = v.toObject().value("error").toString(QString::fromUtf8(out));
    if (error) *error = m_lastError;
    return {};
  }
  if (changesLibrary(cmd)) emit libraryChanged();
  return v;
}

QByteArray InkleafCore::callRaw(const QString& cmd, const QByteArray& argsJson, QString* error) {
  if (!m_core) {
    if (error) *error = m_lastError;
    return {};
  }
  InkleafResult r = inkleaf_call(m_core, cmd.toUtf8().constData(), argsJson.constData(), nullptr, 0);
  QByteArray out(reinterpret_cast<const char*>(r.data), int(r.len));
  const bool ok = r.ok;
  inkleaf_free(r);
  if (!ok) {
    m_lastError = QJsonDocument::fromJson(out).object().value("error").toString(QString::fromUtf8(out));
    if (error) *error = m_lastError;
    return {};
  }
  if (changesLibrary(cmd)) emit libraryChanged();
  return out;
}

QByteArray InkleafCore::callBytes(const QString& cmd, const QJsonObject& args, QString* error) {
  if (!m_core) {
    if (error) *error = m_lastError;
    return {};
  }
  const QByteArray a = QJsonDocument(args).toJson(QJsonDocument::Compact);
  InkleafResult r = inkleaf_call(m_core, cmd.toUtf8().constData(), a.constData(), nullptr, 0);
  QByteArray out(reinterpret_cast<const char*>(r.data), int(r.len));
  const bool ok = r.ok;
  inkleaf_free(r);
  if (!ok) {
    m_lastError = QJsonDocument::fromJson(out).object().value("error").toString();
    if (error) *error = m_lastError;
    return {};
  }
  return out;
}

QVariant InkleafCore::call(const QString& cmd, const QVariantMap& args) {
  QString err;
  const QJsonValue v = callJson(cmd, QJsonObject::fromVariantMap(args), &err);
  if (!err.isEmpty()) {
    if (QJSEngine* js = qjsEngine(this)) js->throwError(err);
    return {};
  }
  return v.toVariant();
}

QString InkleafCore::saveBytes(const QString& path, const QString& cmd, const QVariantMap& args) {
  const QString file = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
  QString err;
  const QByteArray bytes = callBytes(cmd, QJsonObject::fromVariantMap(args), &err);
  if (err.isEmpty()) {
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) err = "Couldn't write " + file + ": " + f.errorString();
  }
  if (!err.isEmpty()) {
    if (QJSEngine* js = qjsEngine(this)) js->throwError(err);
    return {};
  }
  return file;
}

QString InkleafCore::documentsFolder() const { return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString(); }
