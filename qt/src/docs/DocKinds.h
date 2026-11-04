// Documents made ready to fill in: a lesson plan (جذاذة), a test or
// worksheet, a class register (attendance and marks, with averages). Each
// is a notebook whose pages start with tables and text; the daftar the
// teacher picks fills in the school, the teacher, the subject, the year and,
// for a register, the class's students.
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class DocKinds : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(DocKinds)
  QML_SINGLETON
public:
  explicit DocKinds(QObject* parent = nullptr) : QObject(parent) {}

  /// The daftars and their classes: [{ notebookId, title, classes, school, teacher, subject, year }].
  Q_INVOKABLE QVariantList daftars() const;
  /// A class's students as listed in a daftar (names, in order).
  Q_INVOKABLE QStringList students(const QString& notebookId, const QString& cls) const;

  /// A lesson plan: { lang ("ar" or "fr"), title, subject, level, duration,
  /// teacher, school, cls, daftarId }. Returns the notebook ({} on failure).
  Q_INVOKABLE QVariantMap lessonPlan(const QVariantMap& o, const QString& folderId);
  /// A test or worksheet: { lang, number, subject, level, duration, school,
  /// year, exercises (count), points ([…]), daftarId }.
  Q_INVOKABLE QVariantMap test(const QVariantMap& o, const QString& folderId);
  /// A class register: { lang, cls, subject, year, students ([names]),
  /// tests (count), daftarId }: attendance pages and a marks page with averages.
  Q_INVOKABLE QVariantMap classRegister(const QVariantMap& o, const QString& folderId);

signals:
  void failed(const QString& message);
};
