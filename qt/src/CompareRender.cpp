#include "CompareRender.h"

#include <QDir>
#include <QImage>
#include <QJSValue>
#include <QtDebug>
#include <functional>
#include <vector>

#include "Js.h"

namespace {

struct Case {
  QString name;
  std::function<QImage()> script, native;
};

struct Diff {
  int pixels = 0;  ///< pixels where a channel differs by more than the tolerance
  int max = 0;     ///< the largest channel difference
};

Diff diff(const QImage& a0, const QImage& b0, QImage* map) {
  const QImage a = a0.convertToFormat(QImage::Format_ARGB32), b = b0.convertToFormat(QImage::Format_ARGB32);
  Diff d;
  if (a.size() != b.size()) {
    d.pixels = -1;
    return d;
  }
  if (map) {
    *map = QImage(a.size(), QImage::Format_ARGB32);
    map->fill(Qt::white);
  }
  for (int y = 0; y < a.height(); ++y) {
    const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
    const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
    for (int x = 0; x < a.width(); ++x) {
      const int m = std::max({std::abs(qRed(pa[x]) - qRed(pb[x])), std::abs(qGreen(pa[x]) - qGreen(pb[x])), std::abs(qBlue(pa[x]) - qBlue(pb[x])), std::abs(qAlpha(pa[x]) - qAlpha(pb[x]))});
      d.max = std::max(d.max, m);
      if (m > 2) {
        ++d.pixels;
        if (map) map->setPixel(x, y, qRgb(255, 0, 0));
      }
    }
  }
  return d;
}

std::vector<Case> cases() {
  std::vector<Case> out;
  Js& js = Js::instance();
  struct Size {
    const char* id;
    double w, h;
  };
  const Size sizes[] = {{"a4", 794, 1123}, {"a5", 559, 794}, {"letter", 816, 1056}, {"legal", 816, 1344}, {"square", 900, 900}};
  const char* colors[] = {"#ffffff", "#fffdf7", "#fdf3c8", "#eef1f4", "#22262d"};
  QJSValue list = js.fn("TEMPLATES");
  const int n = list.property("length").toInt();
  for (int k = 0; k < n; ++k) {
    const QString id = list.property(k).property("id").toString();
    const bool special = id.startsWith("daftar") || id.startsWith("cover");
    for (const Size& s : sizes) {
      for (int turn = 0; turn < 2; ++turn) {
        const double w = turn ? s.h : s.w, h = turn ? s.w : s.h;
        for (const char* c : colors) {
          // Covers and daftar pages: one size and colour each (they're drawn
          // by the scripts both ways until their step).
          if (special && (std::string(s.id) != "a4" || turn || std::string(c) != "#ffffff")) continue;
          const QString color = c;
          const QString name = QString("paper-%1-%2%3-%4").arg(id, s.id, turn ? "-landscape" : "", color.mid(1));
          out.push_back({name, [=, &js] { return js.paperScript(id, color, w, h, 1.0); }, [=, &js] { return js.paper(id, color, w, h, 1.0); }});
        }
      }
    }
    // Sharper (HiDPI) and a zoomed-in part.
    out.push_back({"paper-" + id + "-x2", [=, &js] { return js.paperScript(id, "#ffffff", 794, 1123, 2.0); }, [=, &js] { return js.paper(id, "#ffffff", 794, 1123, 2.0); }});
    out.push_back({"paper-" + id + "-part", [=, &js] { return js.paperScript(id, "#fffdf7", 794, 1123, 3.0, QRectF(100, 200, 250, 180)); },
                   [=, &js] { return js.paper(id, "#fffdf7", 794, 1123, 3.0, QRectF(100, 200, 250, 180)); }});
  }
  return out;
}

}  // namespace

int compareRender(const QString& outDir) {
  QDir().mkpath(outDir);
  int failed = 0, total = 0;
  for (const Case& c : cases()) {
    ++total;
    const QImage a = c.script(), b = c.native();
    QImage map;
    const Diff d = diff(a, b, &map);
    if (d.pixels != 0) {
      ++failed;
      qWarning().noquote() << "DIFFERS" << c.name << (d.pixels < 0 ? QString("(size %1x%2 vs %3x%4)").arg(a.width()).arg(a.height()).arg(b.width()).arg(b.height()) : QString("%1 pixels, max %2").arg(d.pixels).arg(d.max));
      a.save(outDir + "/" + c.name + "-script.png");
      b.save(outDir + "/" + c.name + "-native.png");
      if (d.pixels > 0) map.save(outDir + "/" + c.name + "-diff.png");
    }
  }
  qInfo().noquote() << QString("compare-render: %1 of %2 cases differ").arg(failed).arg(total);
  return failed ? 1 : 0;
}
