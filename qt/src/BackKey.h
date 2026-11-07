// Android's back button (and back gesture), handled the way phones expect:
// it closes the popup, menu or drawer on top; with none open it asks the
// screens to go back (BackKey.pressed), and only then does Inkleaf leave.
// Qt left alone would sometimes close a popup *and* the whole app, as the
// key's release, which no one took, closed the window.
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class BackKey : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(BackKey)
  QML_SINGLETON
public:
  explicit BackKey(QObject* parent = nullptr);
  /// Closes the popup on top (one that Esc would close); false when none is open.
  Q_INVOKABLE bool closeTopPopup();
  /// Back on the first screen: Inkleaf goes to the background, as Android apps do
  /// (it keeps running, so a sync under way finishes). Does nothing on a computer.
  Q_INVOKABLE void leaveApp();
signals:
  /// Back, with no popup open: the screen goes back a step (or Inkleaf leaves).
  void pressed();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
};
