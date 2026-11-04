// Installs a new version from inside the app: downloads the release file
// for this kind of computer, checks it against the checksum GitHub publishes,
// then installs it and restarts:
//   Windows (installed with the setup): runs the new setup silently; it reopens Inkleaf.
//   Linux (AppImage): puts the new AppImage in place of this one and restarts it.
//   Android: downloads with the system's download manager and opens the installer.
// Elsewhere (a portable copy, a build from source) it can't: the banner then
// offers the download page instead.
#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

class Updater : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(Updater)
  QML_SINGLETON
  /// "" (nothing yet), "downloading", "ready", "failed".
  Q_PROPERTY(QString state READ state NOTIFY changed)
  /// 0..1 while downloading (-1 when the size isn't known).
  Q_PROPERTY(double progress READ progress NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  /// This copy of Inkleaf can update itself.
  Q_PROPERTY(bool canInstall READ canInstall CONSTANT)
  /// Which release file it needs: "setup" (Windows), "appimage", "apk", or "".
  Q_PROPERTY(QString kind READ kind CONSTANT)
public:
  explicit Updater(QObject* parent = nullptr);
  ~Updater() override;

  QString state() const { return m_state; }
  double progress() const { return m_progress; }
  QString error() const { return m_error; }
  bool canInstall() const;
  QString kind() const;

  /// Downloads `url` (the release file); `sha256` (hex, may be "") and `size`
  /// (bytes, 0 if unknown) are checked when it's done.
  Q_INVOKABLE void download(const QString& url, const QString& sha256, qint64 size, const QString& version);
  Q_INVOKABLE void cancel();
  /// Installs what was downloaded. Desktop: starts the new version and quits
  /// this one (save first). Android: opens the system's installer.
  Q_INVOKABLE bool install();
  /// Where the latest release is described (INKLEAF_RELEASES_URL overrides it, for testing).
  Q_INVOKABLE QString releasesUrl(const QString& repo) const;
  /// What's new in `version` (its section of the CHANGELOG.md built into the
  /// app, as Markdown), "" if it has none.
  Q_INVOKABLE QString notesFor(const QString& version) const;

signals:
  void changed();

private:
  void fail(const QString& why);
  void set(const QString& state, double progress = -1);
  void finished();
  QString m_state, m_error;
  double m_progress = -1;
  QString m_expectedSha, m_version, m_file;
  qint64 m_expectedSize = 0;
  QNetworkAccessManager* m_net = nullptr;
  QPointer<QNetworkReply> m_reply;
  std::unique_ptr<QFile> m_out;
  QCryptographicHash m_hash{QCryptographicHash::Sha256};
#ifdef Q_OS_ANDROID
  qint64 m_androidId = -1;
  QTimer m_poll;
  void pollAndroid();
#endif
};
