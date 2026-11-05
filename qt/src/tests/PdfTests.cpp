// Chapters added to a PDF Qt wrote (qt/src/PdfOutline): what a reader sees.
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>

#include "../PdfOutline.h"
#include "SelfTest.h"

#ifdef INKLEAF_HAVE_QTPDF
#include <QPdfBookmarkModel>
#include <QPdfDocument>
#endif

TEST("pdf", "chapters are added to an exported PDF, nested and in any language") {
  QTemporaryDir dir;
  CHECK(dir.isValid());
  const QString file = dir.filePath("t.pdf");
  {
    QPdfWriter w(file);
    w.setResolution(96);
    QPainter p(&w);
    for (int i = 0; i < 3; ++i) {
      if (i) w.newPage();
      p.drawText(100, 100, QString("Page %1").arg(i + 1));
    }
  }
  CHECK(pdfoutline::add(file, {{"Forces", 0, 0}, {"Friction", 1, 1}, {"الطاقة", 0, 2}, {"Not a page", 0, 9}}));
#ifdef INKLEAF_HAVE_QTPDF
  QPdfDocument doc;
  CHECK(doc.load(file) == QPdfDocument::Error::None);
  CHECK_EQ(doc.pageCount(), 3);
  QPdfBookmarkModel m;
  m.setDocument(&doc);
  CHECK_EQ(m.rowCount(), 2);
  const QModelIndex first = m.index(0, 0), second = m.index(1, 0);
  CHECK_EQ(m.data(first, int(QPdfBookmarkModel::Role::Title)).toString(), QString("Forces"));
  CHECK_EQ(m.rowCount(first), 1);
  CHECK_EQ(m.data(m.index(0, 0, first), int(QPdfBookmarkModel::Role::Page)).toInt(), 1);
  CHECK_EQ(m.data(second, int(QPdfBookmarkModel::Role::Title)).toString(), QString("الطاقة"));
  CHECK_EQ(m.data(second, int(QPdfBookmarkModel::Role::Page)).toInt(), 2);
#endif
}
