#include "CompareRender.h"

#include <QDir>
#include <QImage>
#include <QJSValue>
#include <QtDebug>
#include <functional>
#include <vector>

#include "Js.h"
#include "draw/Covers.h"
#include "draw/DaftarPapers.h"

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
  QStringList ids;
  for (int k = 0, n = list.property("length").toInt(); k < n; ++k) ids << list.property(k).property("id").toString();
  // Covers aren't in the paper catalogue.
  for (const draw::CoverDesign& d : draw::coverDesigns()) ids << d.tmpl;
  for (const QString& id : ids) {
    const bool special = false;
    if (id.startsWith("cover")) {
      // Every design in every cover colour; all sizes and orientations in two.
      const char* covers[] = {"#2563eb", "#0d9488", "#16a34a", "#ca8a04", "#ea580c", "#dc2626", "#db2777", "#7c3aed", "#475569", "#111827", "#f5f5f4"};
      for (const Size& s : sizes)
        for (int turn = 0; turn < 2; ++turn)
          for (const char* c : covers) {
            if ((std::string(s.id) != "a4" || turn) && std::string(c) != "#2563eb" && std::string(c) != "#ca8a04") continue;
            const double w = turn ? s.h : s.w, h = turn ? s.w : s.h;
            const QString color = c;
            out.push_back({QString("%1-%2%3-%4").arg(id, s.id, turn ? "-landscape" : "", color.mid(1)), [=, &js] { return js.paperScript(id, color, w, h, 1.0); },
                           [=, &js] { return js.paper(id, color, w, h, 1.0); }});
          }
      out.push_back({id + "-x2", [=, &js] { return js.paperScript(id, "#7c3aed", 794, 1123, 2.0); }, [=, &js] { return js.paper(id, "#7c3aed", 794, 1123, 2.0); }});
      continue;
    }
    for (const Size& s : sizes) {
      for (int turn = 0; turn < 2; ++turn) {
        const double w = turn ? s.h : s.w, h = turn ? s.w : s.h;
        for (const char* c : colors) {
          // Daftar pages: one size and colour (drawn by the scripts both
          // ways until their step).
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
  // Where daftar pages' filled-in values go.
  const char* pages[] = {"daftar", "daftar-blue", "daftar-title", "daftar-profile", "daftar-profile-2", "daftar-timetable", "daftar-holidays", "daftar-diagnostic", "daftar-students", "daftar-memos", "daftar-absence"};
  for (const char* t : pages) {
    for (const auto& [w, h] : {std::pair<double, double>{794, 1123}, {1123, 794}, {559, 794}}) {
      ++total;
      const QJSValue js = Js::instance().call("daftarPapers.daftarSlots", {QString(t), w, h});
      const QHash<QString, draw::Slot> native = draw::daftarSlots(t, w, h);
      QStringList bad;
      const QStringList keys = js.toVariant().toMap().keys();
      if (keys.size() != native.size()) bad << QString("%1 vs %2 slots").arg(keys.size()).arg(native.size());
      for (const QString& k : keys) {
        const QJSValue v = js.property(k);
        const draw::Slot n = native.value(k, {-1, -1, -1});
        if (std::abs(v.property("x").toNumber() - n.x) > 1e-6 || std::abs(v.property("y").toNumber() - n.y) > 1e-6 || std::abs(v.property("w").toNumber() - n.w) > 1e-6) bad << k;
      }
      if (!bad.isEmpty()) {
        ++failed;
        qWarning().noquote() << "DIFFERS slots" << t << w << h << bad.join(", ");
      }
    }
  }
  qInfo().noquote() << QString("compare-render: %1 of %2 cases differ").arg(failed).arg(total);
  return failed ? 1 : 0;
}
