// The Rust core for QML: every command of the old web front end, with the
// same names and arguments (see qt/native/src/lib.rs).
#pragma once

#include <QJsonValue>
#include <QObject>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

struct Core;  // opaque, from the Rust side

class InkleafCore : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(InkleafCore)
  QML_SINGLETON
public:
  explicit InkleafCore(QObject* parent = nullptr);
  ~InkleafCore() override;

  static InkleafCore* instance();
  static InkleafCore* create(class QQmlEngine*, class QJSEngine*);

  /// Runs a command; returns its JSON answer (QVariant) or throws a QML error.
  Q_INVOKABLE QVariant call(const QString& cmd, const QVariantMap& args = {});
  /// The same from C++: the answer as JSON, `error` set on failure.
  QJsonValue callJson(const QString& cmd, const QJsonObject& args, QString* error = nullptr, const QByteArray& bytes = {});
  /// The answer's JSON text as it comes from the core (no parsing): for the scripts.
  QByteArray callRaw(const QString& cmd, const QByteArray& argsJson, QString* error = nullptr);
  /// For commands that return a file (assetBytes, buildDocument, ...).
  QByteArray callBytes(const QString& cmd, const QJsonObject& args, QString* error = nullptr);

  /// Runs a command that makes a file (buildDocument…) and writes it to `path`
  /// (a path or file: URL); returns the path, or throws.
  Q_INVOKABLE QString saveBytes(const QString& path, const QString& cmd, const QVariantMap& args = {});
  /// The folder documents are usually saved in (for save dialogs).
  Q_INVOKABLE QString documentsFolder() const;

  QString dataDir() const { return m_dir; }
  Q_INVOKABLE QString lastError() const { return m_lastError; }

signals:
  /// A command changed the library (the library screen refreshes).
  void libraryChanged();

private:
  ::Core* m_core = nullptr;
  QString m_dir;
  QString m_lastError;
};
