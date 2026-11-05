#include "Core.h"
#include "daftar/Cover.h"
#include "daftar/Store.h"

#include <QPointer>
#include <QFile>
#include <QSaveFile>
#include <QThreadPool>
#include <QStandardPaths>
#include <QUrl>

#include <QDir>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QCoreApplication>
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
InkleafResult inkleaf_ai_stream(const ::Core* core, const char* args, bool (*on_text)(void*, const uint8_t*, size_t), void* user);
}

namespace {
InkleafCore* g_instance = nullptr;

/// The library folder: the same one the earlier (Tauri) app used, so its
/// notebooks open here (Linux ~/.local/share/app.inkleaf.desktop,
/// Windows %APPDATA%\app.inkleaf.desktop).
QString libraryDir() {
  if (const QByteArray env = qgetenv("INKLEAF_DATA_DIR"); !env.isEmpty()) return QString::fromUtf8(env);
#if defined(Q_OS_ANDROID)
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);  // the app's private storage
#elif defined(Q_OS_WIN)
  const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);  // %APPDATA% (roaming)
  const QString roaming = qEnvironmentVariable("APPDATA", base);
  return QDir(roaming).filePath("app.inkleaf.desktop");
#else
  return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("app.inkleaf.desktop");
#endif
}

/// Commands that change what the library shows.
bool changesLibrary(const QString& cmd) {
  static const QStringList reads = {"library", "trash", "search", "notebook", "pages", "elements", "assetBytes", "buildDocument", "backupStatus", "syncStatus", "dataDir", "anyUnlocked", "snippets", "saveSnippet", "renameSnippet", "deleteSnippet", "useSnippet", "aiStatus", "aiTest", "aiAsk", "aiStep", "pageTemplates", "notesForAi", "pagesToRead", "readingProgress", "setPageInkText", "simplifyStroke", "recognizeHandwriting", "inspectBackup", "renameNotebookOnly"};
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

InkleafCore* InkleafCore::create(QQmlEngine*, QJSEngine* engine) {
  // One core for the whole app; QML must not delete it.
  if (!g_instance) new InkleafCore();
  g_instance->m_engine = engine;
  QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
  return g_instance;
}

QJsonValue InkleafCore::callJson(const QString& cmd, const QJsonObject& args, QString* error, const QByteArray& bytes) {
  if (!m_core) {
    if (error) *error = m_lastError;
    return {};
  }
  // Renaming a notebook: the title on its cover follows (from every place that renames).
  if (cmd == QLatin1String("renameNotebook")) {
    const QString id = args["id"].toString(), before = store::notebook(id)["title"].toString();
    QString err;
    const QJsonValue v = callJson(QStringLiteral("renameNotebookOnly"), args, &err, bytes);
    if (!err.isEmpty()) {
      if (error) *error = err;
      return v;
    }
    if (before != args["title"].toString().trimmed()) cover::retitle(id, before, args["title"].toString().trimmed());
    emit libraryChanged();
    return v;
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

void InkleafCore::callAsync(const QString& cmd, const QVariantMap& args, const QJSValue& callback) {
  if (!m_core) {
    QJSValue cb = callback;
    if (cb.isCallable()) cb.call({QJSValue(), QJSValue(m_lastError)});
    return;
  }
  // Script values belong to the GUI thread: the callback waits here under an
  // id, and the worker only ever sees plain bytes (copying a QJSValue on
  // another thread corrupts the script engine).
  const quint64 id = ++m_nextCall;
  m_callbacks.insert(id, callback);
  const QByteArray a = QJsonDocument(QJsonObject::fromVariantMap(args)).toJson(QJsonDocument::Compact);
  const QByteArray name = cmd.toUtf8();
  const ::Core* core = m_core;
  QPointer<InkleafCore> self(this);
  QThreadPool::globalInstance()->start([core, a, name, id, self]() {
    InkleafResult r = inkleaf_call(core, name.constData(), a.constData(), nullptr, 0);
    const QByteArray out(reinterpret_cast<const char*>(r.data), int(r.len));
    const bool ok = r.ok;
    inkleaf_free(r);
    QMetaObject::invokeMethod(qApp, [self, id, name, out, ok]() {
      if (self) self->finishAsync(id, QString::fromUtf8(name), out, ok);
    }, Qt::QueuedConnection);
  });
}

namespace {
/// What the worker passes to the core's streaming call: where pieces go.
struct StreamSink {
  QPointer<InkleafCore> self;
  int id;
  std::shared_ptr<std::atomic_bool> stop;
};
extern "C" bool streamPiece(void* user, const uint8_t* text, size_t len) {
  auto* sink = static_cast<StreamSink*>(user);
  if (sink->stop->load()) return false;
  const QString piece = QString::fromUtf8(reinterpret_cast<const char*>(text), qsizetype(len));
  QPointer<InkleafCore> self = sink->self;
  const int id = sink->id;
  QMetaObject::invokeMethod(qApp, [self, id, piece]() {
    if (self) self->streamText(id, piece);
  }, Qt::QueuedConnection);
  return true;
}
}  // namespace

int InkleafCore::askStream(const QVariantMap& request, const QJSValue& onText, const QJSValue& onDone) {
  static int next = 0;
  const int id = ++next;
  if (!m_core) {
    QJSValue cb = onDone;
    if (cb.isCallable()) cb.call({QJSValue(QString()), QJSValue(m_lastError)});
    return id;
  }
  auto stop = std::make_shared<std::atomic_bool>(false);
  m_streams.insert(id, {onText, onDone, stop});
  const QByteArray a = QJsonDocument(QJsonObject{{"request", QJsonObject::fromVariantMap(request)}}).toJson(QJsonDocument::Compact);
  const ::Core* core = m_core;
  QPointer<InkleafCore> self(this);
  QThreadPool::globalInstance()->start([core, a, id, self, stop]() {
    StreamSink sink{self, id, stop};
    InkleafResult r = inkleaf_ai_stream(core, a.constData(), &streamPiece, &sink);
    const QByteArray out(reinterpret_cast<const char*>(r.data), int(r.len));
    const bool ok = r.ok;
    inkleaf_free(r);
    QMetaObject::invokeMethod(qApp, [self, id, out, ok]() {
      if (self) self->streamDone(id, out, ok);
    }, Qt::QueuedConnection);
  });
  return id;
}

void InkleafCore::cancelAsk(int id) {
  auto it = m_streams.find(id);
  if (it != m_streams.end()) it->stop->store(true);
}

void InkleafCore::streamText(int id, const QString& piece) {
  auto it = m_streams.find(id);
  if (it == m_streams.end() || it->stop->load()) return;
  QJSValue cb = it->onText;
  if (cb.isCallable()) cb.call({QJSValue(piece)});
}

void InkleafCore::streamDone(int id, const QByteArray& out, bool ok) {
  const Stream st = m_streams.take(id);
  QJSValue cb = st.onDone;
  if (!cb.isCallable()) return;
  const QJsonDocument doc = QJsonDocument::fromJson("[" + out + "]");
  const QJsonValue v = doc.array().isEmpty() ? QJsonValue() : doc.array().first();
  if (!ok) cb.call({QJSValue(QString()), QJSValue(v.toObject().value("error").toString(QString::fromUtf8(out)))});
  else cb.call({QJSValue(v.toString()), QJSValue(QString())});
}

void InkleafCore::finishAsync(quint64 id, const QString& cmd, const QByteArray& out, bool ok) {
  QJSValue cb = m_callbacks.take(id);
  const QJsonDocument doc = QJsonDocument::fromJson("[" + out + "]");
  const QJsonValue v = doc.array().isEmpty() ? QJsonValue() : doc.array().first();
  QJSEngine* js = m_engine ? m_engine.data() : qjsEngine(this);
  if (!ok) {
    const QString err = v.toObject().value("error").toString(QString::fromUtf8(out));
    if (cb.isCallable()) cb.call({QJSValue(), QJSValue(err)});
    return;
  }
  if (changesLibrary(cmd)) emit libraryChanged();
  if (cb.isCallable()) cb.call({js ? js->toScriptValue(v.toVariant()) : QJSValue(), QJSValue(QString())});
}

QString InkleafCore::saveBytes(const QString& path, const QString& cmd, const QVariantMap& args) {
  const QString file = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
  QString err;
  const QByteArray bytes = callBytes(cmd, QJsonObject::fromVariantMap(args), &err);
  if (err.isEmpty()) {
    if (file.startsWith("content:")) {
      // Android's document URLs can't be written by renaming a temporary file.
      QFile f(file);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(bytes) != bytes.size()) err = "Couldn't write the file: " + f.errorString();
    } else {
      QSaveFile f(file);
      if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) err = "Couldn't write " + file + ": " + f.errorString();
    }
  }
  if (!err.isEmpty()) {
    if (QJSEngine* js = qjsEngine(this)) js->throwError(err);
    return {};
  }
  return file;
}

QString InkleafCore::documentsFolder() const { return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString(); }
