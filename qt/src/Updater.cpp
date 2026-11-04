#include "Updater.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <cerrno>
#include <cstdio>
#include <cstring>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qnativeinterface.h>
#endif

namespace {
/// The AppImage this is running from ("" when it isn't one).
QString appImage() { return qEnvironmentVariable("APPIMAGE"); }
}  // namespace

Updater::Updater(QObject* parent) : QObject(parent) {
#ifdef Q_OS_ANDROID
  m_poll.setInterval(500);
  connect(&m_poll, &QTimer::timeout, this, &Updater::pollAndroid);
#endif
}

Updater::~Updater() {
  if (m_reply) m_reply->abort();
}

QString Updater::kind() const {
#if defined(Q_OS_WIN)
  return "setup";
#elif defined(Q_OS_ANDROID)
  return "apk";
#elif defined(Q_OS_LINUX)
  return appImage().isEmpty() ? QString() : QStringLiteral("appimage");
#else
  return {};
#endif
}

QString Updater::releasesUrl(const QString& repo) const {
  const QString over = qEnvironmentVariable("INKLEAF_RELEASES_URL");
  return over.isEmpty() ? "https://api.github.com/repos/" + repo + "/releases/latest" : over;
}

bool Updater::canInstall() const {
#if defined(Q_OS_WIN)
  // Installed with the setup (a portable copy has no uninstaller next to it).
  return QFile::exists(QCoreApplication::applicationDirPath() + "/uninstall.exe");
#elif defined(Q_OS_ANDROID)
  return true;
#elif defined(Q_OS_LINUX)
  // An AppImage in a folder this user can write to (to put the new one there).
  const QString a = appImage();
  return !a.isEmpty() && QFileInfo(QFileInfo(a).absolutePath()).isWritable();
#else
  return false;
#endif
}

void Updater::set(const QString& state, double progress) {
  m_state = state;
  m_progress = progress;
  emit changed();
}

void Updater::fail(const QString& why) {
  m_error = why;
  if (m_out) {
    m_out->close();
    m_out->remove();
    m_out.reset();
  }
  set("failed");
}

void Updater::download(const QString& url, const QString& sha256, qint64 size, const QString& version) {
  if (m_state == "downloading") return;
  if (!canInstall()) return fail("This copy of Inkleaf can't update itself: download the new version instead.");
  m_error.clear();
  m_expectedSha = sha256.trimmed().toLower();
  m_expectedSize = size;
  m_version = version;
  set("downloading", 0);
#ifdef Q_OS_ANDROID
  const QJniObject ctx = QJniObject(QNativeInterface::QAndroidApplication::context());
  m_androidId = QJniObject::callStaticMethod<jlong>("app/inkleaf/qt/Updates", "download", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)J",
                                                    ctx.object(), QJniObject::fromString(url).object<jstring>(),
                                                    QJniObject::fromString("Inkleaf " + version).object<jstring>());
  if (m_androidId < 0) return fail("Couldn't start the download.");
  m_poll.start();
#else
  // Next to the AppImage it will replace (a rename then swaps them), or in the temporary folder.
  const QString dir = kind() == "appimage" ? QFileInfo(appImage()).absolutePath() : QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  m_file = dir + "/" + (kind() == "appimage" ? QString(".Inkleaf_%1.AppImage.part").arg(version) : QString("Inkleaf_%1_setup.exe").arg(version));
  m_out = std::make_unique<QFile>(m_file);
  if (!m_out->open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail("Couldn't save the download (" + m_out->errorString() + ").");
  m_hash.reset();
  if (!m_net) m_net = new QNetworkAccessManager(this);
  QNetworkRequest req{QUrl(url)};
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setHeader(QNetworkRequest::UserAgentHeader, "Inkleaf/" + QCoreApplication::applicationVersion());
  req.setRawHeader("Accept", "application/octet-stream");
  m_reply = m_net->get(req);
  connect(m_reply, &QNetworkReply::readyRead, this, [this] {
    const QByteArray chunk = m_reply->readAll();
    m_hash.addData(chunk);
    if (m_out && m_out->write(chunk) != chunk.size()) {
      m_reply->abort();
      fail("Couldn't save the download (is the disk full?).");
    }
  });
  connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
    if (total <= 0) total = m_expectedSize;
    if (m_state == "downloading") set("downloading", total > 0 ? double(got) / double(total) : -1);
  });
  connect(m_reply, &QNetworkReply::finished, this, &Updater::finished);
#endif
}

void Updater::finished() {
  QNetworkReply* r = m_reply;
  if (!r) return;
  r->deleteLater();
  if (m_state != "downloading") return;  // cancelled or failed already
  if (r->error() != QNetworkReply::NoError) return fail("The download failed: " + r->errorString());
  if (!m_out) return fail("The download failed.");
  const QByteArray rest = r->readAll();
  m_hash.addData(rest);
  m_out->write(rest);
  m_out->close();
  const qint64 got = QFileInfo(m_file).size();
  if (m_expectedSize > 0 && got != m_expectedSize) return fail(QString("The download is incomplete (%1 of %2 bytes).").arg(got).arg(m_expectedSize));
  if (!m_expectedSha.isEmpty() && QString::fromLatin1(m_hash.result().toHex()) != m_expectedSha) return fail("The download doesn't match the release's checksum: it wasn't installed.");
  m_out.reset();
  set("ready", 1);
}

void Updater::cancel() {
#ifdef Q_OS_ANDROID
  if (m_androidId >= 0) {
    const QJniObject ctx = QJniObject(QNativeInterface::QAndroidApplication::context());
    QJniObject::callStaticMethod<void>("app/inkleaf/qt/Updates", "cancel", "(Landroid/content/Context;J)V", ctx.object(), jlong(m_androidId));
    m_androidId = -1;
  }
  m_poll.stop();
#endif
  if (m_state != "downloading") return;
  m_error.clear();
  set("");
  if (m_reply) m_reply->abort();
  if (m_out) {
    m_out->close();
    m_out->remove();
    m_out.reset();
  }
}

bool Updater::install() {
  if (m_state != "ready") return false;
#if defined(Q_OS_ANDROID)
  const QJniObject ctx = QJniObject(QNativeInterface::QAndroidApplication::context());
  const bool ok = QJniObject::callStaticMethod<jboolean>("app/inkleaf/qt/Updates", "install", "(Landroid/content/Context;J)Z", ctx.object(), jlong(m_androidId));
  if (!ok) fail("Couldn't open Android's installer.");
  return ok;
#elif defined(Q_OS_WIN)
  // Silently, then it opens Inkleaf again (/RELAUNCH, in the setup script).
  if (!QProcess::startDetached(m_file, {"/S", "/RELAUNCH"})) {
    fail("Couldn't start the installer.");
    return false;
  }
  QTimer::singleShot(0, qApp, &QCoreApplication::quit);
  return true;
#else
  // The new AppImage takes this one's place (this one keeps running from its
  // open file until it quits), then starts.
  const QString target = appImage();
  QFile::setPermissions(m_file, QFile::permissions(target) | QFile::ExeOwner | QFile::ReadOwner | QFile::ExeGroup | QFile::ExeOther);
  if (::rename(QFile::encodeName(m_file).constData(), QFile::encodeName(target).constData()) != 0) {
    fail("Couldn't replace the app file (" + QString::fromLocal8Bit(strerror(errno)) + ").");
    return false;
  }
  QProcess::startDetached(target, {});
  QTimer::singleShot(0, qApp, &QCoreApplication::quit);
  return true;
#endif
}

#ifdef Q_OS_ANDROID
void Updater::pollAndroid() {
  if (m_androidId < 0) return m_poll.stop();
  const QJniObject ctx = QJniObject(QNativeInterface::QAndroidApplication::context());
  const QJniObject arr = QJniObject::callStaticObjectMethod("app/inkleaf/qt/Updates", "progress", "(Landroid/content/Context;J)[J", ctx.object(), jlong(m_androidId));
  if (!arr.isValid()) return;
  QJniEnvironment env;
  jlong v[3] = {0, 0, 0};
  env->GetLongArrayRegion(arr.object<jlongArray>(), 0, 3, v);
  // [done, total, status]: status 8 = successful, 16 = failed (DownloadManager).
  if (v[2] == 16) {
    m_poll.stop();
    m_androidId = -1;
    return fail("The download failed.");
  }
  if (v[2] != 8) {
    const qint64 total = v[1] > 0 ? v[1] : m_expectedSize;
    set("downloading", total > 0 ? double(v[0]) / double(total) : -1);
    return;
  }
  m_poll.stop();
  if (!m_expectedSha.isEmpty()) {
    const QString sha = QJniObject::callStaticObjectMethod("app/inkleaf/qt/Updates", "sha256", "(Landroid/content/Context;J)Ljava/lang/String;", ctx.object(), jlong(m_androidId)).toString();
    if (sha.toLower() != m_expectedSha) {
      cancel();
      return fail("The download doesn't match the release's checksum: it wasn't installed.");
    }
  }
  set("ready", 1);
}
#endif
