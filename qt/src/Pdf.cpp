#include "Pdf.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#ifdef INKLEAF_HAVE_QTPDF
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPdfBookmarkModel>
#include <QPdfSearchModel>
#include <QPdfLink>
#include <functional>
#elif defined(Q_OS_ANDROID)
#include <QJniEnvironment>
#include <QJniObject>
#endif
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

// --- PDF files ------------------------------------------------------------------------

#ifdef INKLEAF_HAVE_QTPDF
namespace {
class QtPdfFile : public PdfFile {
public:
  QPdfDocument doc;
  int pageCount() const override { return doc.pageCount(); }
  QSizeF pageSize(int page) const override { return doc.pagePointSize(page); }
  QImage render(int page, QSize full, QRect clip) override {
    QPdfDocumentRenderOptions opt;
    opt.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
    QImage img;
    if (clip.isEmpty()) {
      img = doc.render(page, full, opt);
    } else {
      opt.setScaledSize(full);
      opt.setScaledClipRect(clip);
      img = doc.render(page, clip.size(), opt);
    }
    return img;
  }
  std::vector<std::vector<QRectF>> find(int page, const QString& what) override {
    std::vector<std::vector<QRectF>> out;
    if (page < 0 || page >= doc.pageCount() || what.trimmed().isEmpty()) return out;
    if (!search) {
      search = std::make_unique<QPdfSearchModel>();
      search->setDocument(&doc);
    }
    if (search->searchString() != what) search->setSearchString(what);
    for (const QPdfLink& l : search->resultsOnPage(page)) {
      std::vector<QRectF> rs;
      for (const QRectF& r : l.rectangles()) rs.push_back(r);
      if (!rs.empty()) out.push_back(std::move(rs));
    }
    return out;
  }
  std::unique_ptr<QPdfSearchModel> search;
  QString text(int page) override { return page >= 0 && page < doc.pageCount() ? doc.getAllText(page).text() : QString(); }
  std::vector<Entry> outline() override {
    std::vector<Entry> out;
    QPdfBookmarkModel model;
    model.setDocument(&doc);
    const std::function<void(const QModelIndex&)> walk = [&](const QModelIndex& parent) {
      for (int r = 0; r < model.rowCount(parent); ++r) {
        const QModelIndex i = model.index(r, 0, parent);
        const QString title = model.data(i, int(QPdfBookmarkModel::Role::Title)).toString().simplified();
        if (!title.isEmpty())
          out.push_back({title, model.data(i, int(QPdfBookmarkModel::Role::Level)).toInt(), model.data(i, int(QPdfBookmarkModel::Role::Page)).toInt()});
        if (out.size() < 2000) walk(i);
      }
    };
    walk({});
    return out;
  }
};
}  // namespace

std::unique_ptr<PdfFile> PdfFile::open(const QString& path, Error* error) {
  auto f = std::make_unique<QtPdfFile>();
  const auto err = f->doc.load(path);
  if (error) *error = err == QPdfDocument::Error::None ? Error::None : err == QPdfDocument::Error::IncorrectPassword ? Error::Password : Error::Invalid;
  if (err != QPdfDocument::Error::None) return nullptr;
  return f;
}
#elif defined(Q_OS_ANDROID)
namespace {
// Android's PdfRenderer, through qt/android/src/app/inkleaf/qt/PdfPages.java.
class AndroidPdfFile : public PdfFile {
public:
  QString path;
  std::vector<QSizeF> sizes;
  int pageCount() const override { return int(sizes.size()); }
  QSizeF pageSize(int page) const override { return page >= 0 && page < int(sizes.size()) ? sizes[size_t(page)] : QSizeF(); }
  QImage render(int page, QSize full, QRect clip) override {
    if (clip.isEmpty()) clip = QRect(QPoint(0, 0), full);
    QJniObject arr = QJniObject::callStaticObjectMethod("app/inkleaf/qt/PdfPages", "render", "(Ljava/lang/String;IIIIIII)[I",
        QJniObject::fromString(path).object<jstring>(), jint(page), jint(full.width()), jint(full.height()), jint(clip.x()), jint(clip.y()), jint(clip.width()), jint(clip.height()));
    if (!arr.isValid()) return {};
    QJniEnvironment env;
    auto jarr = arr.object<jintArray>();
    const jsize n = env->GetArrayLength(jarr);
    if (n != clip.width() * clip.height()) return {};
    QImage img(clip.size(), QImage::Format_ARGB32);
    env->GetIntArrayRegion(jarr, 0, n, reinterpret_cast<jint*>(img.bits()));
    return img;
  }
};
}  // namespace

std::unique_ptr<PdfFile> PdfFile::open(const QString& path, Error* error) {
  QJniObject arr = QJniObject::callStaticObjectMethod("app/inkleaf/qt/PdfPages", "sizes", "(Ljava/lang/String;)[F", QJniObject::fromString(path).object<jstring>());
  if (!arr.isValid()) {
    if (error) *error = Error::Invalid;
    return nullptr;
  }
  QJniEnvironment env;
  auto jarr = arr.object<jfloatArray>();
  const jsize n = env->GetArrayLength(jarr);
  std::vector<jfloat> v(static_cast<size_t>(n));
  env->GetFloatArrayRegion(jarr, 0, n, v.data());
  auto f = std::make_unique<AndroidPdfFile>();
  f->path = path;
  const int pages = n > 0 ? int(v[0]) : 0;
  for (int i = 0; i < pages && 2 + 2 * i < n; ++i) f->sizes.emplace_back(v[size_t(1 + 2 * i)], v[size_t(2 + 2 * i)]);
  if (error) *error = Error::None;
  return f;
}
#else
std::unique_ptr<PdfFile> PdfFile::open(const QString&, Error* error) {
  if (error) *error = Error::Invalid;
  return nullptr;
}
#endif

PdfFile* PdfDocs::doc(const QString& asset) {
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
  std::shared_ptr<PdfFile> d = PdfFile::open(file);
  if (!d) return nullptr;
  m_docs.insert(asset, d);
  return d.get();
}

QSizeF PdfDocs::pageSize(const QString& asset, int page) {
  PdfFile* d = doc(asset);
  if (!d || page < 0 || page >= d->pageCount()) return {};
  return d->pageSize(page) * kUnitsPerPoint;
}

std::vector<PdfFile::Entry> PdfDocs::outline(const QString& asset) {
  PdfFile* d = doc(asset);
  return d ? d->outline() : std::vector<PdfFile::Entry>{};
}

std::vector<std::vector<QRectF>> PdfDocs::find(const QString& asset, int page, const QString& what) {
  PdfFile* d = doc(asset);
  if (!d) return {};
  auto out = d->find(page, what);
  for (auto& m : out)
    for (QRectF& r : m) r = QRectF(r.x() * kUnitsPerPoint, r.y() * kUnitsPerPoint, r.width() * kUnitsPerPoint, r.height() * kUnitsPerPoint);
  return out;
}

QString PdfDocs::text(const QString& asset, int page) {
  PdfFile* d = doc(asset);
  return d ? d->text(page) : QString();
}

QImage PdfDocs::render(const QString& asset, int page, double scale, QRectF region) {
  PdfFile* d = doc(asset);
  if (!d || page < 0 || page >= d->pageCount()) return {};
  const QSizeF units = d->pageSize(page) * kUnitsPerPoint;
  const QSize full(int(std::ceil(units.width() * scale)), int(std::ceil(units.height() * scale)));
  if (region.isEmpty()) return d->render(page, full);
  // Only the part asked for, at full sharpness.
  const QRect clip(int(region.x() * scale), int(region.y() * scale), int(std::ceil(region.width() * scale)), int(std::ceil(region.height() * scale)));
  return d->render(page, full, clip);
}

// --- importing ------------------------------------------------------------------------

void PdfImporter::add(const QVariantList& files, const QString& folderId) {
  for (const QVariant& v : files) {
    const QUrl url(v.toString());
    Job j;
    j.path = url.isLocalFile() ? url.toLocalFile() : v.toString();
    j.name = QFileInfo(j.path).fileName();
    j.folderId = folderId;
    if (!j.path.startsWith("content:") && !j.path.endsWith(".pdf", Qt::CaseInsensitive)) j.state = "failed", j.label = "Not a PDF file";
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
  // Opened from a copy in the cache (the file may be an Android content: URL).
  const QString tmp = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/pdf/import.pdf";
  QDir().mkpath(QFileInfo(tmp).path());
  {
    QFile t(tmp);
    if (t.open(QIODevice::WriteOnly)) t.write(bytes);
  }
  PdfFile::Error err = PdfFile::Error::None;
  std::unique_ptr<PdfFile> pdfFile = PdfFile::open(tmp, &err);
  if (!pdfFile) {
    set(j, "failed", err == PdfFile::Error::Password ? "Password-protected: can't be imported" : "Not a valid PDF", 1);
    QTimer::singleShot(0, this, &PdfImporter::runNext);
    return;
  }
  PdfFile& pdf = *pdfFile;
  const int n = pdf.pageCount();
  QJsonArray pages;
  for (int i = 0; i < n; ++i) {
    const QSizeF s = pdf.pageSize(i) * kUnitsPerPoint;
    pages.append(QJsonObject{{"width", std::round(s.width())}, {"height", std::round(s.height())}});
    if (i % 20 == 0 || i == n - 1) set(j, "pages", QString("Reading pages… %1 of %2").arg(i + 1).arg(n), 0.3 + 0.25 * (i + 1) / std::max(1, n));
  }
  // The first page's picture, so the library shows the PDF before it's opened.
  QString thumb;
  if (n > 0) {
    const QSizeF s = pdf.pageSize(0) * kUnitsPerPoint;
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
