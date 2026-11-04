// JSON comparison for the side-by-side checks (docs/MIGRATION.md): ids
// replaced by their order of appearance, numbers compared within a hair.
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace {

/// Element and other ids replaced by their order of first appearance.
void normalise(QJsonValue& v, QHash<QString, QString>& ids) {
  auto map = [&](const QString& id) {
    if (!ids.contains(id)) ids.insert(id, QString("#%1").arg(ids.size() + 1));
    return ids.value(id);
  };
  if (v.isObject()) {
    QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it) {
      QJsonValue c = it.value();
      if (it.key() == "id" && c.isString()) c = map(c.toString());
      else if (it.key() == "pageId" && c.isString()) c = map(c.toString());
      else if ((it.key() == "ids" || it.key() == "hidden") && c.isArray()) {
        QJsonArray a = c.toArray();
        for (int k = 0; k < a.size(); ++k) {
          if (a[k].isString()) a[k] = map(a[k].toString());
          else if (a[k].isArray()) {
            QJsonArray b = a[k].toArray();
            for (int q = 0; q < b.size(); ++q) b[q] = map(b[q].toString());
            a[k] = b;
          }
        }
        c = a;
      } else normalise(c, ids);
      it.value() = c;
    }
    v = o;
  } else if (v.isArray()) {
    QJsonArray a = v.toArray();
    for (int k = 0; k < a.size(); ++k) {
      QJsonValue c = a[k];
      normalise(c, ids);
      a[k] = c;
    }
    v = a;
  }
}

/// Differences between two JSON values (numbers within a hair).
void compare(const QJsonValue& a, const QJsonValue& b, const QString& path, QStringList& out) {
  if (out.size() > 12) return;
  if (a.isDouble() && b.isDouble()) {
    const double x = a.toDouble(), y = b.toDouble();
    if (std::abs(x - y) > 1e-6 * std::max(1.0, std::abs(x))) out << QString("%1: %2 vs %3").arg(path).arg(x, 0, 'g', 12).arg(y, 0, 'g', 12);
    return;
  }
  if (a.type() != b.type()) {
    // A key missing on one side counts as null (JSON.stringify drops undefined).
    if ((a.isNull() || a.isUndefined()) && (b.isNull() || b.isUndefined())) return;
    out << QString("%1: %2 vs %3").arg(path, QString::fromUtf8(QJsonDocument(QJsonArray{a}).toJson(QJsonDocument::Compact)).left(160), QString::fromUtf8(QJsonDocument(QJsonArray{b}).toJson(QJsonDocument::Compact)).left(160));
    return;
  }
  if (a.isObject()) {
    const QJsonObject x = a.toObject(), y = b.toObject();
    QStringList keys = x.keys();
    for (const QString& k : y.keys())
      if (!keys.contains(k)) keys << k;
    for (const QString& k : keys) compare(x.value(k), y.value(k), path + "." + k, out);
    return;
  }
  if (a.isArray()) {
    const QJsonArray x = a.toArray(), y = b.toArray();
    if (x.size() != y.size()) out << QString("%1: %2 vs %3 items").arg(path).arg(x.size()).arg(y.size());
    for (int k = 0; k < std::min(x.size(), y.size()); ++k) compare(x[k], y[k], QString("%1[%2]").arg(path).arg(k), out);
    return;
  }
  // The instrument's picture: the same SVG, spacing aside.
  static const QRegularExpression between(QStringLiteral(">\\s+<"));
  // (-0.00 and 0.00 draw the same.)
  auto tidy = [](QString t) { return t.replace(between, "><").replace("-0.00", "0.00").simplified(); };
  if (a.isString() && b.isString() && tidy(a.toString()) == tidy(b.toString())) return;
  if (a.isString() && b.isString() && a != b) {
    // Where they part.
    const QString x = tidy(a.toString()), y = tidy(b.toString());
    int k = 0;
    while (k < x.size() && k < y.size() && x[k] == y[k]) ++k;
    const int from = std::max(0, k - 40);
    out << QString("%1 (at %2): …%3… vs …%4…").arg(path).arg(k).arg(x.mid(from, 120), y.mid(from, 120));
    return;
  }
  if (a != b) out << QString("%1: %2 vs %3").arg(path, a.toVariant().toString().left(120), b.toVariant().toString().left(120));
}


/// The same, with room for another Qt version's text measuring: numbers
/// within 0.05 (or 0.1%), whitespace between SVG tags ignored.
inline void compareLoose(const QJsonValue& a, const QJsonValue& b, const QString& path, QStringList& out) {
  if (out.size() > 12) return;
  if (a.isDouble() && b.isDouble()) {
    const double x = a.toDouble(), y = b.toDouble();
    if (std::abs(x - y) > std::max(0.05, 1e-3 * std::abs(x))) out << QString("%1: %2 vs %3").arg(path).arg(x, 0, 'g', 10).arg(y, 0, 'g', 10);
    return;
  }
  if (a.isObject() && b.isObject()) {
    const QJsonObject x = a.toObject(), y = b.toObject();
    QStringList keys = x.keys();
    for (const QString& k : y.keys())
      if (!keys.contains(k)) keys << k;
    for (const QString& k : keys) compareLoose(x.value(k), y.value(k), path + "." + k, out);
    return;
  }
  if (a.isArray() && b.isArray()) {
    const QJsonArray x = a.toArray(), y = b.toArray();
    if (x.size() != y.size()) out << QString("%1: %2 vs %3 items").arg(path).arg(x.size()).arg(y.size());
    for (int k = 0; k < std::min(x.size(), y.size()); ++k) compareLoose(x[k], y[k], QString("%1[%2]").arg(path).arg(k), out);
    return;
  }
  compare(a, b, path, out);
}

}  // namespace
