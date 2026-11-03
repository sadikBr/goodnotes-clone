#include "Pdf.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QBuffer>

#include "Core.h"

namespace {
constexpr double kUnitsPerPoint = 96.0 / 72.0;
}

PdfDocs& PdfDocs::instance() {
  static PdfDocs docs;
  return docs;
}

QPdfDocument* PdfDocs::doc(const QString& asset) {
  if (auto it = m_docs.constFind(asset); it != m_docs.constEnd()) return it->get();
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/pdf";
  QDir().mkpath(dir);
  const QString file = dir + "/" + QString(asset).replace('/', '_') + ".pdf";
  if (!QFileInfo::exists(file)) {
    QString err;
    const QByteArray bytes = InkleafCore::instance()->callBytes("assetBytes", {{"id", asset}}, &err);
    if (bytes.isEmpty()) return nullptr;
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return nullptr;
    f.write(bytes);
  }
  auto d = std::make_shared<QPdfDocument>();
  if (d->load(file) != QPdfDocument::Error::None) return nullptr;
  m_docs.insert(asset, d);
  return d.get();
}

QSizeF PdfDocs::pageSize(const QString& asset, int page) {
  QPdfDocument* d = doc(asset);
  if (!d || page < 0 || page >= d->pageCount()) return {};
  return d->pagePointSize(page) * kUnitsPerPoint;
}

QImage PdfDocs::render(const QString& asset, int page, double scale, QRectF region) {
  QPdfDocument* d = doc(asset);
  if (!d || page < 0 || page >= d->pageCount()) return {};
  const QSizeF units = d->pagePointSize(page) * kUnitsPerPoint;
  const QSize full(int(std::ceil(units.width() * scale)), int(std::ceil(units.height() * scale)));
  QPdfDocumentRenderOptions opt;
  opt.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
  if (region.isEmpty()) return d->render(page, full, opt);
  // Only the part asked for, at full sharpness.
  const QRect clip(int(region.x() * scale), int(region.y() * scale), int(std::ceil(region.width() * scale)), int(std::ceil(region.height() * scale)));
  opt.setScaledSize(full);
  opt.setScaledClipRect(clip);
  return d->render(page, clip.size(), opt);
}

// --- importing ------------------------------------------------------------------------

void PdfImporter::add(const QVariantList& files, const QString& folderId) {
  for (const QVariant& v : files) {
    const QUrl url(v.toString());
    Job j;
    j.path = url.isLocalFile() ? url.toLocalFile() : v.toString();
    j.name = QFileInfo(j.path).fileName();
    j.folderId = folderId;
    if (!j.path.endsWith(".pdf", Qt::CaseInsensitive)) j.state = "failed", j.label = "Not a PDF file";
    else if (QFileInfo(j.path).size() > 300LL * 1024 * 1024) j.state = "failed", j.label = "Larger than 300 MB";
    m_jobs.append(j);
  }
  emit changed();
  if (!m_running) QTimer::singleShot(0, this, &PdfImporter::runNext);
}

void PdfImporter::set(Job& j, const QString& state, const QString& label, double progress) {
  j.state = state;
  j.label = label;
  j.progress = progress;
  emit changed();
  // Show progress as it goes (each step runs on the GUI thread).
  QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void PdfImporter::runNext() {
  int idx = -1;
  for (int i = 0; i < m_jobs.size(); ++i)
    if (m_jobs[i].state == "waiting") {
      idx = i;
      break;
    }
  if (idx < 0) {
    m_running = false;
    emit changed();
    return;
  }
  m_running = true;
  Job& j = m_jobs[idx];
  // Reading.
  set(j, "reading", "Reading the file… 0%", 0);
  QFile f(j.path);
  QByteArray bytes;
  if (f.open(QIODevice::ReadOnly)) {
    const qint64 size = f.size();
    bytes.reserve(size);
    while (!f.atEnd()) {
      bytes += f.read(4 * 1024 * 1024);
      const double fr = size ? double(bytes.size()) / size : 1;
      set(j, "reading", QString("Reading the file… %1%").arg(int(fr * 100)), 0.3 * fr);
    }
  }
  if (bytes.isEmpty()) {
    set(j, "failed", "Couldn't read the file", 1);
    QTimer::singleShot(0, this, &PdfImporter::runNext);
    return;
  }
  // Pages.
  set(j, "pages", "Opening the PDF…", 0.3);
  QPdfDocument pdf;
  const auto err = pdf.load(j.path);
  if (err != QPdfDocument::Error::None) {
    set(j, "failed", err == QPdfDocument::Error::IncorrectPassword ? "Password-protected: can't be imported" : "Not a valid PDF", 1);
    QTimer::singleShot(0, this, &PdfImporter::runNext);
    return;
  }
  const int n = pdf.pageCount();
  QJsonArray pages;
  for (int i = 0; i < n; ++i) {
    const QSizeF s = pdf.pagePointSize(i) * kUnitsPerPoint;
    pages.append(QJsonObject{{"width", std::round(s.width())}, {"height", std::round(s.height())}});
    if (i % 20 == 0 || i == n - 1) set(j, "pages", QString("Reading pages… %1 of %2").arg(i + 1).arg(n), 0.3 + 0.25 * (i + 1) / std::max(1, n));
  }
  // The first page's picture, so the library shows the PDF before it's opened.
  QString thumb;
  if (n > 0) {
    const QSizeF s = pdf.pagePointSize(0) * kUnitsPerPoint;
    const double scale = 200.0 / std::max(1.0, s.width());
    QImage img = pdf.render(0, QSize(int(s.width() * scale), int(s.height() * scale)));
    QImage flat(img.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    QPainter(&flat).drawImage(0, 0, img);
    QByteArray jpg;
    QBuffer b(&jpg);
    b.open(QIODevice::WriteOnly);
    flat.save(&b, "JPEG", 82);
    thumb = "data:image/jpeg;base64," + QString::fromLatin1(jpg.toBase64());
  }
  // Saving.
  set(j, "saving", "Saving into Inkleaf…", 0.75);
  QString e;
  const QJsonValue asset = InkleafCore::instance()->callJson("storePdfAsset", {}, &e, bytes);
  if (!e.isEmpty()) {
    set(j, "failed", e, 1);
    QTimer::singleShot(0, this, &PdfImporter::runNext);
    return;
  }
  QString title = QFileInfo(j.name).completeBaseName().trimmed();
  if (title.isEmpty()) title = "Imported PDF";
  const QJsonObject nb = InkleafCore::instance()
                             ->callJson("createPdfNotebook",
                                        {{"input", QJsonObject{{"title", title.left(200)}, {"folderId", j.folderId.isEmpty() ? QJsonValue() : QJsonValue(j.folderId)}, {"assetId", asset}, {"pages", pages}}}}, &e)
                             .toObject();
  if (!e.isEmpty()) {
    set(j, "failed", e, 1);
    QTimer::singleShot(0, this, &PdfImporter::runNext);
    return;
  }
  if (!thumb.isEmpty()) {
    const QJsonArray np = InkleafCore::instance()->callJson("pages", {{"notebookId", nb["id"]}}).toArray();
    if (!np.isEmpty()) InkleafCore::instance()->callJson("setPageThumbnail", {{"pageId", np[0].toObject()["id"]}, {"thumbnail", thumb}});
  }
  j.notebookId = nb["id"].toString();
  set(j, "done", QString("%1 page%2").arg(n).arg(n == 1 ? "" : "s"), 1);
  emit added(j.notebookId);
  QTimer::singleShot(0, this, &PdfImporter::runNext);
}

void PdfImporter::cancelWaiting() {
  for (Job& j : m_jobs)
    if (j.state == "waiting") j.state = "cancelled", j.label = "Not imported";
  emit changed();
}

void PdfImporter::clearFinished() {
  m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(), [](const Job& j) { return j.state == "done" || j.state == "failed" || j.state == "cancelled"; }), m_jobs.end());
  emit changed();
}

QVariantList PdfImporter::jobs() const {
  QVariantList out;
  for (const Job& j : m_jobs) out << QVariantMap{{"name", j.name}, {"state", j.state}, {"label", j.label}, {"progress", j.progress}, {"notebookId", j.notebookId}};
  return out;
}

bool PdfImporter::busy() const {
  return std::any_of(m_jobs.begin(), m_jobs.end(), [](const Job& j) { return j.state == "waiting" || j.state == "reading" || j.state == "pages" || j.state == "saving"; });
}

double PdfImporter::progress() const {
  double sum = 0;
  int n = 0;
  for (const Job& j : m_jobs) {
    if (j.state == "cancelled") continue;
    sum += j.state == "done" || j.state == "failed" ? 1 : j.progress;
    ++n;
  }
  return n ? sum / n : 1;
}
