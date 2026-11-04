#include "Model.h"

#include <QUuid>
#include <algorithm>

namespace ed {

namespace {
QJsonArray p2s(const std::vector<P2>& pts) {
  QJsonArray a;
  for (const P2& p : pts) a.append(QJsonArray{p[0], p[1]});
  return a;
}
std::vector<P2> p2sOf(const QJsonValue& v) {
  std::vector<P2> out;
  for (const QJsonValue& p : v.toArray()) out.push_back({p.toArray().at(0).toDouble(), p.toArray().at(1).toDouble()});
  return out;
}
std::optional<QString> optString(const QJsonObject& o, const char* k) {
  const QJsonValue v = o.value(k);
  if (v.isString()) return v.toString();
  return std::nullopt;
}
std::optional<double> optNumber(const QJsonObject& o, const char* k) {
  const QJsonValue v = o.value(k);
  if (v.isDouble()) return v.toDouble();
  return std::nullopt;
}
std::optional<bool> optBool(const QJsonObject& o, const char* k) {
  const QJsonValue v = o.value(k);
  if (v.isBool()) return v.toBool();
  return std::nullopt;
}
}  // namespace

Shape Shape::fromJson(const QJsonObject& o) {
  Shape s;
  s.kind = o["kind"].toString();
  if (s.kind == "ellipse") {
    s.cx = o["cx"].toDouble();
    s.cy = o["cy"].toDouble();
    s.rx = o["rx"].toDouble();
    s.ry = o["ry"].toDouble();
  } else {
    s.points = p2sOf(o["points"]);
  }
  s.closed = o["closed"].toBool();
  for (const QJsonValue& b : o["breaks"].toArray()) s.breaks.push_back(b.toInt());
  return s;
}

QJsonObject Shape::toJson() const {
  if (kind == "ellipse") return {{"kind", kind}, {"cx", cx}, {"cy", cy}, {"rx", rx}, {"ry", ry}};
  QJsonObject o{{"kind", kind}, {"points", p2s(points)}};
  if (kind == "path") {
    o["closed"] = closed;
    if (!breaks.empty()) {
      QJsonArray b;
      for (int i : breaks) b.append(i);
      o["breaks"] = b;
    }
  }
  return o;
}

QString kindName(Kind k) {
  switch (k) {
    case Kind::Stroke: return "stroke";
    case Kind::Shape: return "shape";
    case Kind::Text: return "text";
    case Kind::Sticky: return "sticky";
    case Kind::Image: return "image";
    case Kind::Graph: return "graph";
    case Kind::Table: return "table";
  }
  return "stroke";
}

std::optional<Kind> kindOf(const QString& n) {
  if (n == "stroke") return Kind::Stroke;
  if (n == "shape") return Kind::Shape;
  if (n == "text") return Kind::Text;
  if (n == "sticky") return Kind::Sticky;
  if (n == "image") return Kind::Image;
  if (n == "graph") return Kind::Graph;
  if (n == "table") return Kind::Table;
  return std::nullopt;
}

El El::fromJson(const QJsonObject& o) {
  El e;
  e.id = o["id"].toString();
  e.z = o["z"].toDouble();
  e.kind = kindOf(o["kind"].toString()).value_or(Kind::Stroke);
  static const QStringList known = {"id", "z", "kind", "color", "width", "tool", "style", "points", "smooth", "shape", "fill", "x", "y", "w", "h", "text", "src", "fontSize", "font", "align", "role", "session", "bold", "italic", "rotation"};
  for (auto it = o.begin(); it != o.end(); ++it)
    if (!known.contains(it.key())) e.extra.insert(it.key(), it.value());
  e.color = o["color"].toString();
  e.width = o["width"].toDouble();
  switch (e.kind) {
    case Kind::Stroke:
      e.tool = o["tool"].toString();
      e.style = optString(o, "style");
      for (const QJsonValue& p : o["points"].toArray()) {
        const QJsonArray a = p.toArray();
        e.points.push_back({a.at(0).toDouble(), a.at(1).toDouble(), a.size() > 2 ? a.at(2).toDouble() : 0.5});
      }
      e.smooth = optNumber(o, "smooth");
      break;
    case Kind::Shape:
      e.shape = Shape::fromJson(o["shape"].toObject());
      e.fill = optString(o, "fill");
      break;
    default:
      e.x = o["x"].toDouble();
      e.y = o["y"].toDouble();
      e.w = o["w"].toDouble();
      e.h = o["h"].toDouble();
      e.text = o["text"].toString();
      e.src = o["src"].toString();
      e.fontSize = optNumber(o, "fontSize");
      e.font = optString(o, "font");
      e.align = optString(o, "align");
      e.role = optString(o, "role");
      e.session = optString(o, "session");
      e.bold = optBool(o, "bold");
      e.italic = optBool(o, "italic");
      e.rotation = optNumber(o, "rotation");
      break;
  }
  return e;
}

QJsonObject El::toJson(bool withPoints) const {
  QJsonObject o = extra;
  o["id"] = id;
  o["z"] = z;
  o["kind"] = kindName(kind);
  switch (kind) {
    case Kind::Stroke: {
      o["tool"] = tool;
      if (style) o["style"] = *style;
      o["color"] = color;
      o["width"] = width;
      if (withPoints) {
        QJsonArray pts;
        for (const Pt& p : points) pts.append(QJsonArray{p[0], p[1], p[2]});
        o["points"] = pts;
      }
      if (smooth) o["smooth"] = *smooth;
      break;
    }
    case Kind::Shape:
      o["shape"] = shape.toJson();
      o["color"] = color;
      o["width"] = width;
      o["fill"] = fill ? QJsonValue(*fill) : QJsonValue(QJsonValue::Null);
      break;
    default:
      o["x"] = x;
      o["y"] = y;
      o["w"] = w;
      if (kind != Kind::Text) o["h"] = h;
      if (kind == Kind::Image) {
        o["src"] = src;
      } else {
        o["text"] = text;
        o["color"] = color;
        if (fontSize) o["fontSize"] = *fontSize;
        if (font) o["font"] = *font;
        if (bold) o["bold"] = *bold;
        if (italic) o["italic"] = *italic;
        if (align) o["align"] = *align;
        if (role) o["role"] = *role;
        if (session) o["session"] = *session;
      }
      if (kind == Kind::Table && width > 0) o["width"] = width;
      if (rotation) o["rotation"] = *rotation;
      break;
  }
  return o;
}

ElPtr makeEl(El e) {
  static quint64 counter = 0;
  e.rev = ++counter;
  e.cachedBounds.reset();
  e.cachedFrame.reset();
  return std::make_shared<El>(std::move(e));
}

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

double nextZ(const std::vector<El>& els) {
  double m = -1;
  for (const El& e : els) m = std::max(m, e.z);
  return m + 1;
}

std::vector<El> fromStored(const QJsonArray& rows) {
  std::vector<El> out;
  for (const QJsonValue& v : rows) {
    const QJsonObject row = v.toObject();
    const QJsonValue data = row["data"];
    if (!data.isObject() || !kindOf(row["kind"].toString())) continue;
    QJsonObject d = data.toObject();
    d["id"] = row["id"];
    d["z"] = row["z"];
    d["kind"] = row["kind"];
    out.push_back(El::fromJson(d));
  }
  std::stable_sort(out.begin(), out.end(), [](const El& a, const El& b) { return a.z < b.z; });
  return out;
}

QJsonObject toStored(const El& el) { return {{"id", el.id}, {"kind", kindName(el.kind)}, {"z", el.z}, {"data", el.toJson()}}; }

}  // namespace ed
