#include "BackKey.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

BackKey::BackKey(QObject* parent) : QObject(parent) { qApp->installEventFilter(this); }

bool BackKey::eventFilter(QObject* watched, QEvent* event) {
  const QEvent::Type t = event->type();
  if (t != QEvent::ShortcutOverride && t != QEvent::KeyPress && t != QEvent::KeyRelease) return QObject::eventFilter(watched, event);
  auto* k = static_cast<QKeyEvent*>(event);
  if (k->key() != Qt::Key_Back) return QObject::eventFilter(watched, event);
  // Every part of it is ours: no popup's own shortcut, and no unanswered release closing the window.
  event->accept();
  // (The same event reaches the filter once per object it's sent to: act on the window's copy only.)
  if (t == QEvent::KeyRelease && !k->isAutoRepeat() && watched->isWindowType()) {
    if (!closeTopPopup()) emit pressed();
  }
  return true;
}

bool BackKey::closeTopPopup() {
  QObject* best = nullptr;
  qreal bestZ = 0;
  int bestIndex = -1;
  for (QWindow* w : QGuiApplication::topLevelWindows()) {
    auto* qw = qobject_cast<QQuickWindow*>(w);
    if (!qw || !qw->isVisible()) continue;
    for (QObject* o : qw->findChildren<QObject*>()) {
      if (!o->inherits("QQuickPopup") || o->inherits("QQuickToolTip") || !o->property("opened").toBool()) continue;
      // Where it is in the stack of open popups: its z, then which opened later.
      auto* content = o->property("contentItem").value<QQuickItem*>();
      QQuickItem* item = content ? content->parentItem() : nullptr;
      QQuickItem* overlay = item ? item->parentItem() : nullptr;
      const qreal z = o->property("z").toReal();
      const int index = overlay ? int(overlay->childItems().indexOf(item)) : 0;
      if (!best || z > bestZ || (z == bestZ && index > bestIndex)) { best = o; bestZ = z; bestIndex = index; }
    }
  }
  if (!best) return false;
  // One that must be answered (no Esc) stays open, and Back does nothing else.
  const int closeOnEscape = 0x10;
  if (best->property("closePolicy").toInt() & closeOnEscape) QMetaObject::invokeMethod(best, "close");
  return true;
}

void BackKey::leaveApp() {
#ifdef Q_OS_ANDROID
  QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) activity.callMethod<jboolean>("moveTaskToBack", "(Z)Z", jboolean(true));
  });
#endif
}
