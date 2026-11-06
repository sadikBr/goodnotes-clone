// Write from your phone: Inkleaf serves a writing page on your own Wi-Fi
// (a small web server, only while it's on), the phone opens it from a QR code,
// and what you write there lands on the page you're on, through the same path
// as the zoom box. The phone shows a part of the page (two fingers move and
// zoom it), and gets a fresh picture of it whenever the page changes.
//
// Only a phone with the code (a random token in the QR code's address) gets
// the page or a connection; one phone at a time.
#pragma once

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class QTcpServer;
class QTcpSocket;
#include "InkCanvas.h"

class PhonePad : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(InkCanvas* canvas READ canvas WRITE setCanvas NOTIFY canvasChanged)
  Q_PROPERTY(bool running READ running NOTIFY stateChanged)
  /// A phone is connected.
  Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
  /// What the phone opens (http://192.168.1.20:47321/?k=…), and the same as a QR code (a data: URL).
  Q_PROPERTY(QString url READ url NOTIFY stateChanged)
  Q_PROPERTY(QString qrImage READ qrImage NOTIFY stateChanged)
  Q_PROPERTY(int port READ port NOTIFY stateChanged)
  /// Why it couldn't start ("" when it did).
  Q_PROPERTY(QString error READ error NOTIFY stateChanged)
  /// The part of the page the phone shows (page units), and its page.
  Q_PROPERTY(int page READ page NOTIFY regionChanged)
  Q_PROPERTY(QRectF region READ region NOTIFY regionChanged)
  /// The phone has told us its screen (the region is known).
  Q_PROPERTY(bool hasRegion READ hasRegion NOTIFY regionChanged)
  /// The firewall that may keep the phone out: "ufw", "firewalld", "windows", "macos" or "" (none found);
  /// the command that lets it in (Linux), or "".
  Q_PROPERTY(QString firewall READ firewall NOTIFY stateChanged)
  Q_PROPERTY(QString firewallCommand READ firewallCommand NOTIFY stateChanged)

public:
  explicit PhonePad(QObject* parent = nullptr);
  ~PhonePad() override;

  /// The port it listens on (the first free from here). Fixed, so a firewall rule for it keeps working.
  static constexpr quint16 kPort = 47321;

  InkCanvas* canvas() const { return m_canvas; }
  void setCanvas(InkCanvas* c);
  bool running() const { return m_server != nullptr; }
  bool connected() const { return m_ws != nullptr; }
  QString url() const { return m_url; }
  QString qrImage() const { return m_qr; }
  int port() const { return m_port; }
  QString error() const { return m_error; }
  int page() const { return m_page; }
  QRectF region() const { return m_region; }
  bool hasRegion() const { return m_aspect > 0; }
  QString firewall() const { return m_firewall; }
  QString firewallCommand() const;

  Q_INVOKABLE void start();
  Q_INVOKABLE void stop();

  // For the self-tests.
  static QByteArray acceptKey(const QByteArray& key);
  static QString lanAddress();
  /// Which firewall this computer has on (looked up once per start).
  static QString detectFirewall();

signals:
  void canvasChanged();
  void stateChanged();
  void regionChanged();
  /// The phone picked a tool ("pen", "highlighter", "eraser"): the editor switches to it.
  void toolRequested(const QString& tool);

private:
  void onNewConnection();
  void onHttpData(QTcpSocket* s);
  void serve(QTcpSocket* s, const QByteArray& request);
  void upgrade(QTcpSocket* s, const QByteArray& key);
  void onWsData();
  void onWsGone();
  void onMessage(const QJsonObject& m);
  void sendText(const QJsonObject& o);
  void sendFrame(quint8 opcode, const QByteArray& payload);
  void sendState();
  void scheduleImage(int ms);
  void sendImage();
  void setRegion(int page, QRectF r);
  QRectF clamped(int page, QRectF r) const;
  void showOnDesktop();
  void endStroke();

  QPointer<InkCanvas> m_canvas;
  QTcpServer* m_server = nullptr;
  QHash<QTcpSocket*, QByteArray> m_pending;  ///< HTTP requests coming in
  QTcpSocket* m_ws = nullptr;                 ///< the phone's live connection
  QByteArray m_wsBuf, m_fragments;
  QString m_token, m_url, m_qr, m_error, m_firewall;
  int m_port = 0;

  int m_page = 0;
  QRectF m_region;
  double m_aspect = 0;   ///< the phone's view, height ÷ width
  double m_viewW = 400;  ///< its width in pixels (the picture's size)
  bool m_stroke = false;
  int m_ackSeq = 0;  ///< the last stroke the phone ended (a picture says it includes it)
  quint64 m_sentVersion = 0, m_sentGesture = 0;
  QRectF m_sentRegion;
  int m_sentPage = -1;
  double m_imageAt = 0;  ///< when the last picture went
  double m_selfMoveAt = 0;  ///< when we last scrolled the desktop (its view change is ours, not the user's)
  QTimer m_imageTimer;
};
