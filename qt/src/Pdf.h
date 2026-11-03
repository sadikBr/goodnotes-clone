// Imported PDFs: page backgrounds drawn by Qt PDF (the web app uses pdf.js),
// and importing PDF files into new notebooks with progress (src/pdfImport.ts).
#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QRectF>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <memory>

class QPdfDocument;

/// Open PDFs by asset id (copied to the cache folder once, then loaded from there).
class PdfDocs {
public:
  static PdfDocs& instance();
  /// Page `page` of asset `asset` drawn `scale` pixels per page unit (1/96 inch);
  /// only `region` (page units) when it isn't empty.
  QImage render(const QString& asset, int page, double scale, QRectF region = {});
  /// The page's size in page units.
  QSizeF pageSize(const QString& asset, int page);

private:
  QPdfDocument* doc(const QString& asset);
  QHash<QString, std::shared_ptr<QPdfDocument>> m_docs;
};

/// The PDF import queue with its progress (for the library's import panel).
class PdfImporter : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  /// [{ name, state ("waiting"|"reading"|"pages"|"saving"|"done"|"failed"|"cancelled"), label, progress (0–1), notebookId }]
  Q_PROPERTY(QVariantList jobs READ jobs NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(double progress READ progress NOTIFY changed)
public:
  using QObject::QObject;
  /// Queues files (file: URLs or paths) for import into `folderId` ("" for the top level).
  Q_INVOKABLE void add(const QVariantList& files, const QString& folderId);
  Q_INVOKABLE void cancelWaiting();
  /// Forgets finished, failed and cancelled imports.
  Q_INVOKABLE void clearFinished();
  QVariantList jobs() const;
  bool busy() const;
  double progress() const;

signals:
  void changed();
  /// A notebook was added (the library refreshes).
  void added(const QString& notebookId);

private:
  struct Job {
    QString path, name, folderId, state = "waiting", label = "Waiting", notebookId;
    double progress = 0;
  };
  void runNext();
  void set(Job& j, const QString& state, const QString& label, double progress);
  QList<Job> m_jobs;
  bool m_running = false;
};
