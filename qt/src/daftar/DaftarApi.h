// The daftar and cover setup for QML (was qt/js/daftarQt.ts and the cover
// calls): the settings form's values and checks, creating and updating a
// daftar, holidays, student lists, outlines, and applying covers.
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class DaftarApi : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(DaftarApi)
  QML_SINGLETON
public:
  explicit DaftarApi(QObject* parent = nullptr) : QObject(parent) {}

  /// The day names, Sunday first: [{ar, fr}].
  Q_INVOKABLE QVariantList days() const;
  /// The built-in lesson outlines: [{name, lines}].
  Q_INVOKABLE QVariantList outlines() const;
  /// A notebook's daftar details, or null when it isn't a daftar.
  Q_INVOKABLE QVariant info(const QString& notebookId) const;
  /// The holiday today falls in ("" when none).
  Q_INVOKABLE QString holidayToday(const QVariant& meta) const;
  /// The settings form's values for a daftar (empty ones for a new daftar).
  Q_INVOKABLE QVariantMap formValues(const QVariant& meta, const QString& title) const;
  /// What's wrong with the form's values, or null.
  Q_INVOKABLE QVariant validate(const QVariantMap& values) const;
  /// "1AC-2, 1AC-3 ; TC-1" → ["1AC-2", "1AC-3", "TC-1"] (no duplicates).
  Q_INVOKABLE QStringList parseClasses(const QString& s) const;
  /// Creates a daftar from the form's values: the notebook (empty on failure).
  Q_INVOKABLE QVariantMap create(const QVariantMap& values, const QString& folderId);
  /// Applies changed details: {removed: classes whose pages are kept}.
  Q_INVOKABLE QVariantMap update(const QString& notebookId, const QVariantMap& values);
  Q_INVOKABLE double holidayDays(const QVariantMap& holiday) const;
  Q_INVOKABLE QVariantList nationalHolidaysFor(const QString& year) const;
  Q_INVOKABLE QVariantList parseStudents(const QString& text) const;
  /// "12 students found · …" for the paste dialog.
  Q_INVOKABLE QString studentsSummary(const QString& text) const;

  // Covers
  /// The cover designs: [{id, label, template}].
  Q_INVOKABLE QVariantList coverDesigns() const;
  /// Puts the title and date on a new notebook's cover.
  Q_INVOKABLE void setUpCover(const QVariantMap& notebook);
  /// Switches a notebook's cover design and colour (adding a cover page if it has none).
  Q_INVOKABLE void applyCover(const QVariantMap& notebook, const QString& design, const QString& color);

signals:
  /// A failure to show (the form's own messages come from validate).
  void failed(const QString& message);
};
