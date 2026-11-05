// The assistant: you ask in your own words, the AI (Settings → AI) works out
// what to do with Inkleaf's own actions. It reads your library straight away
// (to answer, or to plan), but anything that changes it is only planned: you
// see the plan and say Do it, then it's carried out, and one Undo takes all of
// it back. It never deletes anything, and never reads locked notebooks.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class Assistant : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  /// The conversation as shown: [{ role: "user" | "assistant" | "plan" | "error", text, steps?, state? }].
  Q_PROPERTY(QVariantList messages READ messages NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  /// What it's doing ("Reading “Biology”…"), while busy.
  Q_PROPERTY(QString status READ status NOTIFY changed)
  /// A plan is waiting for Do it or Cancel.
  Q_PROPERTY(bool planWaiting READ planWaiting NOTIFY changed)
  /// The last plan can be taken back.
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)

public:
  explicit Assistant(QObject* parent = nullptr);
  QVariantList messages() const { return m_ui; }
  bool busy() const { return m_busy; }
  QString status() const { return m_status; }
  bool planWaiting() const { return !m_plan.isEmpty() && m_planState == "pending"; }
  bool canUndo() const { return !m_journal.isEmpty(); }

  /// Where the user is: a notebook (its id and title, and the page they're on, from 0) or the library ("").
  Q_INVOKABLE void setContext(const QString& notebookId, const QString& title, int page);
  /// Asks (a question, or something to do).
  Q_INVOKABLE void send(const QString& text);
  /// Carries out the plan; or drops it.
  Q_INVOKABLE void approve();
  Q_INVOKABLE void cancelPlan();
  /// Takes back everything the last plan did.
  Q_INVOKABLE void undo();
  /// Stops waiting for the AI.
  Q_INVOKABLE void stop();
  /// A new conversation.
  Q_INVOKABLE void reset();

  // For the self-tests: run a tool call as the model would ({ name, input }); the result's text.
  QString runToolForTest(const QString& name, const QJsonObject& input, bool* queued = nullptr);
  bool approveForTest();

signals:
  void changed();
  /// Before the plan changes notebooks: an open one saves what's on screen.
  void aboutToChange();
  /// These notebooks changed (an open one reloads); the library may have too.
  void notebooksChanged(const QStringList& ids);

private:
  void addUi(const QVariantMap& m);
  void setStatus(const QString& s);
  void runStep();
  void onStep(int generation, const QJsonValue& answer, const QString& error);
  QJsonArray tools() const;
  QString systemPrompt() const;
  /// Runs a tool now (reading) or adds it to the plan (changing): the text the model gets back.
  QString handle(const QString& name, const QJsonObject& input, bool* error, bool* queued);
  QString read(const QString& name, const QJsonObject& input);
  QString plan(const QString& name, const QJsonObject& input);
  /// Carries out one planned step; what it did, for the journal.
  void execute(const QJsonObject& step);
  QJsonValue resolve(const QJsonValue& v) const;
  QString idOf(const QJsonObject& input, const char* key) const;
  void journal(const QJsonObject& entry) { m_journal.append(entry); }
  void touch(const QString& notebookId) { if (!notebookId.isEmpty() && !m_touched.contains(notebookId)) m_touched << notebookId; }

  QVariantList m_ui;
  QJsonArray m_api;
  QJsonArray m_plan;  ///< [{ tool, input, summary, icon, ref? }]
  QString m_planState;
  QJsonArray m_journal;  ///< what the last plan did, to take back
  QStringList m_touched;
  QHash<QString, QString> m_refs;  ///< "new:3" → the real id, while carrying out
  int m_nextRef = 1;
  bool m_busy = false;
  QString m_status;
  int m_generation = 0;
  int m_rounds = 0;
  QString m_note;  ///< told to the model with the next message (what the user did with the plan)
  QString m_sentNote;  ///< the note sent with the question being answered
  QString m_ctxNotebook, m_ctxTitle;
  int m_ctxPage = -1;
};
