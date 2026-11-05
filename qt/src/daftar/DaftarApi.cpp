#include "DaftarApi.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QtDebug>

#include "../Core.h"
#include "../draw/Covers.h"
#include "Cover.h"
#include "Daftar.h"
#include "Store.h"

namespace {

/// The library shows the finished result (its pictures are stored last, and
/// storing a picture alone doesn't refresh it).
struct Refresh {
  ~Refresh() { emit InkleafCore::instance()->libraryChanged(); }
};

std::optional<daftar::Meta> metaOf(const QVariant& v) {
  if (!v.isValid() || v.isNull()) return std::nullopt;
  const QJsonObject o = QJsonObject::fromVariantMap(v.toMap());
  if (!o["classes"].isArray()) return std::nullopt;
  return daftar::Meta::fromJson(o);
}

QJsonArray parseArray(const QVariant& v) {
  const QJsonDocument d = QJsonDocument::fromJson(v.toString().isEmpty() ? QByteArray("[]") : v.toString().toUtf8());
  return d.array();
}

QString trimmed(const QVariant& v) { return v.toString().trimmed(); }

/// The details from the form's values.
daftar::Meta metaFrom(const DaftarApi& api, const QVariantMap& r) {
  daftar::Meta m;
  m.classes = api.parseClasses(r.value("classes").toString());
  m.year = trimmed(r.value("year"));
  m.region = trimmed(r.value("region"));
  m.directorate = trimmed(r.value("directorate"));
  m.school = trimmed(r.value("school"));
  m.teacher = trimmed(r.value("teacher"));
  m.payroll = trimmed(r.value("payroll"));
  m.subject = trimmed(r.value("subject"));
  for (const QJsonValue& v : parseArray(r.value("timetable"))) {
    const QJsonObject l = v.toObject();
    if (m.classes.contains(l["cls"].toString())) m.timetable.push_back({l["day"].toInt(), l["start"].toString(), l["end"].toString(), l["cls"].toString()});
  }
  for (const QJsonValue& v : parseArray(r.value("holidays"))) {
    const daftar::Holiday h = daftar::Holiday::fromJson(v.toObject());
    m.holidays.push_back({h.name.trimmed(), h.from, h.to >= h.from ? h.to : h.from});
  }
  return m;
}

}  // namespace

QVariantList DaftarApi::days() const {
  QVariantList out;
  for (const auto& d : daftar::days()) out << QVariantMap{{"ar", d.ar}, {"fr", d.fr}};
  return out;
}

QVariantList DaftarApi::outlines() const {
  QVariantList out;
  for (const auto& o : daftar::outlines()) out << QVariantMap{{"name", o.name}, {"lines", o.lines}};
  return out;
}

QVariant DaftarApi::info(const QString& notebookId) const {
  try {
    const auto m = daftar::parseDaftar(store::notebook(notebookId)["meta"]);
    if (!m) return QVariant::fromValue(nullptr);
    return m->toJson().toVariantMap();
  } catch (const store::StoreError& e) {
    qWarning().noquote() << "daftar:" << e.message();
    return QVariant::fromValue(nullptr);
  }
}

QString DaftarApi::holidayToday(const QVariant& meta) const {
  const auto m = metaOf(meta);
  if (!m) return {};
  const auto h = daftar::holidayOn(*m, daftar::today());
  return h ? h->name : QString();
}

QVariantMap DaftarApi::formValues(const QVariant& meta, const QString& title) const {
  const auto m = metaOf(meta);
  QJsonArray tt, hs;
  if (m) {
    const QJsonObject j = m->toJson();
    tt = j["timetable"].toArray();
    hs = j["holidays"].toArray();
  }
  auto compact = [](const QJsonArray& a) { return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)); };
  return {{"teacher", m ? m->teacher : QString()},
          {"subject", m ? m->subject : QString()},
          {"school", m ? m->school : QString()},
          {"year", m ? m->year : daftar::schoolYear()},
          {"region", m ? m->region : QString()},
          {"directorate", m ? m->directorate : QString()},
          {"payroll", m ? m->payroll : QString()},
          {"title", title},
          {"classes", m ? m->classes.join(", ") : QString()},
          {"timetable", compact(tt)},
          {"holidays", compact(hs)}};
}

QVariant DaftarApi::validate(const QVariantMap& v) const {
  if (parseClasses(v.value("classes").toString()).isEmpty()) return QStringLiteral("Add at least one class.");
  for (const QJsonValue& x : parseArray(v.value("timetable"))) {
    const QJsonObject l = x.toObject();
    if (!(l["start"].toString() < l["end"].toString())) {
      const int day = l["day"].toInt();
      return QString("A %1 lesson ends before it starts.").arg(day >= 0 && day < 7 ? daftar::days()[size_t(day)].fr : QString());
    }
  }
  for (const QJsonValue& x : parseArray(v.value("holidays"))) {
    const QJsonObject h = x.toObject();
    if (h["from"].toString().isEmpty() || h["name"].toString().trimmed().isEmpty()) {
      if (!h["from"].toString().isEmpty()) return QStringLiteral("Give each holiday a name.");
      const QString name = h["name"].toString().trimmed();
      return QStringLiteral("Choose the dates of “%1”.").arg(name.isEmpty() ? QStringLiteral("the new holiday") : name);
    }
  }
  return QVariant::fromValue(nullptr);
}

QStringList DaftarApi::parseClasses(const QString& s) const {
  static const QRegularExpression sep(QStringLiteral("[,;،\\n]+"));
  QStringList out;
  for (const QString& c : s.split(sep)) {
    const QString t = c.trimmed();
    if (!t.isEmpty() && !out.contains(t)) out << t;
  }
  return out.mid(0, 30);
}

QVariantMap DaftarApi::create(const QVariantMap& values, const QString& folderId) {
  const Refresh refresh;
  try {
    const daftar::Meta meta = metaFrom(*this, values);
    const QString title = values.value("title").toString().trimmed();
    const QJsonObject nb = daftar::createDaftar(title.isEmpty() ? QStringLiteral("دفتر النصوص") : title, folderId, values.value("cover").toString().isEmpty() ? QStringLiteral("#5b2a5e") : values.value("cover").toString(), meta);
    try {
      cover::setUpCover(nb);
    } catch (const store::StoreError& e) {
      qWarning().noquote() << "daftar cover:" << e.message();
    }
    daftar::finishDaftarCover(nb, meta);
    const QJsonArray pages = store::pages(nb["id"].toString());
    if (!pages.isEmpty()) {
      try {
        cover::refreshCoverThumbnail(pages.at(0).toObject());
      } catch (const store::StoreError& e) {
        qWarning().noquote() << "daftar cover:" << e.message();
      }
    }
    return nb.toVariantMap();
  } catch (const store::StoreError& e) {
    qWarning().noquote() << "daftar:" << e.message();
    emit failed(e.message());
    return {};
  }
}

QVariantMap DaftarApi::update(const QString& notebookId, const QVariantMap& values) {
  const Refresh refresh;
  try {
    const QJsonObject nb = store::notebook(notebookId);
    auto before = daftar::parseDaftar(nb["meta"]);
    if (!before) return {{"removed", QStringList()}};
    const daftar::Meta next = metaFrom(*this, values);
    // Renamed classes keep their pages: the pages move to the new name first.
    const QJsonObject renames = QJsonDocument::fromJson(values.value("renames").toString().toUtf8()).object();
    for (auto it = renames.begin(); it != renames.end(); ++it) {
      const QString from = it.key(), to = it.value().toString().trimmed();
      if (to.isEmpty() || from == to || !before->classes.contains(from) || !next.classes.contains(to) || before->classes.contains(to)) continue;
      for (const QJsonValue& v : store::pages(notebookId))
        if (v.toObject()["section"].toString() == from) store::setPageSection(v.toObject()["id"].toString(), to);
      // (The old timetable stays as it was: its notes on the timetable page are matched by their old text.)
      before->classes[before->classes.indexOf(from)] = to;
    }
    daftar::updateDaftar(nb, *before, next);
    QStringList removed;
    for (const QString& c : before->classes)
      if (!next.classes.contains(c)) removed << c;
    return {{"removed", removed}};
  } catch (const store::StoreError& e) {
    qWarning().noquote() << "daftar:" << e.message();
    emit failed(e.message());
    return {{"removed", QStringList()}};
  }
}

double DaftarApi::holidayDays(const QVariantMap& h) const { return daftar::holidayDays(daftar::Holiday::fromJson(QJsonObject::fromVariantMap(h))); }

QVariantList DaftarApi::nationalHolidaysFor(const QString& year) const {
  QVariantList out;
  for (const auto& h : daftar::nationalHolidays(year.isEmpty() ? daftar::schoolYear() : year)) out << h.toJson().toVariantMap();
  return out;
}

QVariantList DaftarApi::parseStudents(const QString& text) const {
  QVariantList out;
  for (const auto& s : daftar::parseStudents(text)) out << QVariantMap{{"code", s.code}, {"name", s.name}};
  return out;
}

QString DaftarApi::studentsSummary(const QString& text) const {
  const auto n = daftar::parseStudents(text);
  if (n.empty()) return QStringLiteral("No names found yet.");
  const int coded = int(std::count_if(n.begin(), n.end(), [](const daftar::Student& s) { return !s.code.isEmpty(); }));
  return QString("%1 student%2 found · %3 with a Massar code. First: %4").arg(n.size()).arg(n.size() > 1 ? "s" : "").arg(coded).arg(n[0].name);
}

QVariantList DaftarApi::coverDesigns() const {
  QVariantList out;
  for (const auto& d : draw::coverDesigns()) out << QVariantMap{{"id", d.id}, {"label", d.label}, {"template", d.tmpl}};
  return out;
}

void DaftarApi::setUpCover(const QVariantMap& notebook) {
  const Refresh refresh;
  try {
    cover::setUpCover(QJsonObject::fromVariantMap(notebook));
  } catch (const store::StoreError& e) {
    qWarning().noquote() << "cover:" << e.message();
    emit failed(e.message());
  }
}

void DaftarApi::applyCover(const QVariantMap& notebook, const QString& design, const QString& color) {
  const Refresh refresh;
  try {
    cover::applyCover(QJsonObject::fromVariantMap(notebook), design, color);
  } catch (const store::StoreError& e) {
    qWarning().noquote() << "cover:" << e.message();
    emit failed(e.message());
  }
}
