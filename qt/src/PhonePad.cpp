#include "PhonePad.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QProcess>
#include <QStandardPaths>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QtEndian>
#include <algorithm>
#include <cmath>

#include "Ink.h"
#include "InkCanvas.h"
#include "third_party/qrcodegen/qrcodegen.hpp"

namespace {

double nowMs() { return double(QDateTime::currentMSecsSinceEpoch()); }

// A request bigger than this, or a message from the phone, is dropped (nothing it sends is large).
constexpr int kMaxRequest = 16 * 1024;
constexpr qint64 kMaxMessage = 512 * 1024;

QString qrDataUrl(const QString& text) {
  using qrcodegen::QrCode;
  const QrCode qr = QrCode::encodeText(text.toUtf8().constData(), QrCode::Ecc::MEDIUM);
  const int n = qr.getSize(), quiet = 4, cell = 8;
  QImage img((n + 2 * quiet) * cell, (n + 2 * quiet) * cell, QImage::Format_Grayscale8);
  img.fill(255);
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x)
      if (qr.getModule(x, y))
        for (int dy = 0; dy < cell; ++dy) {
          uchar* row = img.scanLine((y + quiet) * cell + dy);
          std::fill(row + (x + quiet) * cell, row + (x + quiet + 1) * cell, uchar(0));
        }
  QByteArray png;
  QBuffer buf(&png);
  buf.open(QIODevice::WriteOnly);
  img.save(&buf, "PNG");
  return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

QByteArray httpResponse(const QByteArray& status, const QByteArray& type, const QByteArray& body) {
  return "HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) +
         "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + body;
}

}  // namespace

PhonePad::PhonePad(QObject* parent) : QObject(parent) {
  m_imageTimer.setSingleShot(true);
  connect(&m_imageTimer, &QTimer::timeout, this, &PhonePad::sendImage);
}

PhonePad::~PhonePad() { stop(); }

void PhonePad::setCanvas(InkCanvas* c) {
  if (c == m_canvas) return;
  if (m_canvas) disconnect(m_canvas, nullptr, this, nullptr);
  m_canvas = c;
  if (c) {
    connect(c, &InkCanvas::historyChanged, this, [this] { scheduleImage(60); });
    // A held stroke turning into a shape, a shape being drawn, a selection moving: shown as it goes.
    connect(c, &InkCanvas::gestureShown, this, [this] { if (m_stroke) scheduleImage(0); });
    connect(c, &InkCanvas::pagesChanged, this, [this] {
      if (!m_canvas) return;
      m_page = std::clamp(m_page, 0, std::max(0, m_canvas->pageCount() - 1));
      setRegion(m_page, m_region);
      sendState();
      scheduleImage(30);
    });
    connect(c, &InkCanvas::toolChanged, this, &PhonePad::sendState);
    connect(c, &InkCanvas::toolStateChanged, this, &PhonePad::sendState);
    // You went to another page on the desktop: the phone follows.
    connect(c, &InkCanvas::viewChanged, this, [this] {
      if (!m_canvas || !m_ws || m_stroke || nowMs() - m_selfMoveAt < 500) return;
      const int cur = m_canvas->currentPage();
      if (cur >= 0 && cur != m_page) {
        setRegion(cur, QRectF(m_region.x(), 0, m_region.width(), m_region.height()));
        sendState();
        scheduleImage(0);
      }
    });
    connect(c, &InkCanvas::notebookIdChanged, this, [this] { stop(); });
  }
  emit canvasChanged();
}

// --- the server -------------------------------------------------------------------------

QString PhonePad::lanAddress() {
  // The address on your own network: 192.168.x first, then 10.x, then 172.16–31.x;
  // not containers', VMs' or VPNs'.
  QString best;
  int bestRank = 99;
  for (const QNetworkInterface& i : QNetworkInterface::allInterfaces()) {
    const auto f = i.flags();
    if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning) || (f & QNetworkInterface::IsLoopBack)) continue;
    const QString name = i.name().toLower();
    if (name.startsWith("docker") || name.startsWith("br-") || name.startsWith("veth") || name.startsWith("virbr") || name.startsWith("vmnet") ||
        name.startsWith("tun") || name.startsWith("wg") || name.startsWith("tailscale") || name.startsWith("zt"))
      continue;
    for (const QNetworkAddressEntry& e : i.addressEntries()) {
      const QHostAddress a = e.ip();
      if (a.protocol() != QAbstractSocket::IPv4Protocol) continue;
      const quint32 v = a.toIPv4Address();
      int rank = 9;
      if ((v >> 16) == 0xC0A8) rank = 0;                       // 192.168/16
      else if ((v >> 24) == 10) rank = 1;                       // 10/8
      else if ((v >> 20) == 0xAC1) rank = 2;                    // 172.16/12
      else if ((v >> 16) == 0xA9FE) continue;                   // 169.254: no network
      if (rank < bestRank) bestRank = rank, best = a.toString();
    }
  }
  return best;
}

QString PhonePad::detectFirewall() {
#if defined(Q_OS_WIN)
  return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
  return QStringLiteral("macos");
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
  // ufw (Omarchy, Ubuntu, Mint…): on when its config says so (its status needs root).
  QFile ufw(QStringLiteral("/etc/ufw/ufw.conf"));
  if (ufw.open(QIODevice::ReadOnly)) {
    for (const QByteArray& line : ufw.readAll().split('\n'))
      if (line.trimmed().replace(' ', "").toLower() == "enabled=yes") return QStringLiteral("ufw");
  }
  // firewalld (Fedora, openSUSE, RHEL…): asks it whether it's running.
  const QString fwcmd = QStandardPaths::findExecutable(QStringLiteral("firewall-cmd"), {"/usr/bin", "/usr/sbin", "/bin", "/sbin"});
  if (!fwcmd.isEmpty()) {
    QProcess p;
    p.start(fwcmd, {QStringLiteral("--state")});
    if (p.waitForFinished(1500) && p.exitCode() == 0 && p.readAllStandardOutput().trimmed() == "running") return QStringLiteral("firewalld");
  }
  return {};
#else
  return {};
#endif
}

QString PhonePad::firewallCommand() const {
  if (m_firewall == "ufw") return QStringLiteral("sudo ufw allow %1/tcp").arg(m_port);
  if (m_firewall == "firewalld") return QStringLiteral("sudo firewall-cmd --permanent --add-port=%1/tcp && sudo firewall-cmd --reload").arg(m_port);
  return {};
}

void PhonePad::start() {
  if (m_server) return;
  m_error.clear();
  const QString ip = lanAddress();
  if (ip.isEmpty()) {
    m_error = QStringLiteral("This computer isn't on a network. Connect it to the same Wi-Fi as your phone.");
    emit stateChanged();
    return;
  }
  m_server = new QTcpServer(this);
  m_port = 0;
  for (quint16 p = kPort; p < kPort + 10; ++p)
    if (m_server->listen(QHostAddress::AnyIPv4, p)) {
      m_port = p;
      break;
    }
  if (!m_port) {
    delete m_server;
    m_server = nullptr;
    m_error = QStringLiteral("Couldn't open a port for the phone (%1 to %2 are all in use).").arg(kPort).arg(kPort + 9);
    emit stateChanged();
    return;
  }
  connect(m_server, &QTcpServer::newConnection, this, &PhonePad::onNewConnection);
  m_token.clear();
  for (int i = 0; i < 4; ++i) m_token += QString::number(QRandomGenerator::system()->generate(), 36);
  m_url = QStringLiteral("http://%1:%2/?k=%3").arg(ip).arg(m_port).arg(m_token);
  m_qr = qrDataUrl(m_url);
  m_firewall = detectFirewall();
  if (qEnvironmentVariableIsSet("INKLEAF_TRACE")) qInfo("phone pad: %s", qPrintable(m_url));
  emit stateChanged();
}

void PhonePad::stop() {
  endStroke();
  if (m_ws) {
    sendFrame(0x8, {});
    m_ws->disconnect(this);
    m_ws->disconnectFromHost();
    m_ws->deleteLater();
    m_ws = nullptr;
  }
  for (auto it = m_pending.begin(); it != m_pending.end(); ++it) it.key()->deleteLater();
  m_pending.clear();
  if (m_server) {
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
  }
  m_aspect = 0;
  m_url.clear();
  m_qr.clear();
  m_token.clear();
  emit regionChanged();
  emit stateChanged();
}

void PhonePad::onNewConnection() {
  while (m_server && m_server->hasPendingConnections()) {
    QTcpSocket* s = m_server->nextPendingConnection();
    m_pending.insert(s, {});
    connect(s, &QTcpSocket::readyRead, this, [this, s] { onHttpData(s); });
    connect(s, &QTcpSocket::disconnected, this, [this, s] {
      m_pending.remove(s);
      if (s != m_ws) s->deleteLater();
    });
    // A request that never finishes doesn't stay open.
    QTimer::singleShot(10000, s, [this, s] {
      if (m_pending.contains(s)) {
        m_pending.remove(s);
        s->abort();
        s->deleteLater();
      }
    });
  }
}

void PhonePad::onHttpData(QTcpSocket* s) {
  if (!m_pending.contains(s)) return;
  QByteArray& buf = m_pending[s];
  buf += s->readAll();
  if (buf.size() > kMaxRequest) {
    m_pending.remove(s);
    s->abort();
    return;
  }
  const int end = buf.indexOf("\r\n\r\n");
  if (end < 0) return;
  const QByteArray request = buf.left(end);
  m_pending.remove(s);
  serve(s, request);
}

void PhonePad::serve(QTcpSocket* s, const QByteArray& request) {
  const QList<QByteArray> lines = request.split('\n');
  const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
  if (first.size() < 2 || first[0] != "GET") {
    s->write(httpResponse("405 Method Not Allowed", "text/plain", "Only GET."));
    s->disconnectFromHost();
    return;
  }
  QHash<QByteArray, QByteArray> headers;
  for (int i = 1; i < lines.size(); ++i) {
    const QByteArray l = lines[i].trimmed();
    const int c = l.indexOf(':');
    if (c > 0) headers.insert(l.left(c).trimmed().toLower(), l.mid(c + 1).trimmed());
  }
  const QUrl url(QString::fromLatin1("http://phone") + QString::fromLatin1(first[1]));
  const bool ok = !m_token.isEmpty() && QUrlQuery(url).queryItemValue("k") == m_token;
  const QString path = url.path();
  if (!ok) {
    s->write(httpResponse("403 Forbidden", "text/html; charset=utf-8",
                          "<!doctype html><meta name=viewport content='width=device-width'><body style='font:16px system-ui;padding:24px;background:#111;color:#eee'>"
                          "<h3>Scan the code again</h3><p>Open <b>Write from your phone</b> in Inkleaf on your computer and scan the code it shows.</p>"));
    s->disconnectFromHost();
    return;
  }
  if (path == "/ws" && headers.value("upgrade").toLower() == "websocket" && headers.contains("sec-websocket-key")) {
    upgrade(s, headers.value("sec-websocket-key"));
    return;
  }
  if (path == "/") {
    QFile f(QStringLiteral(":/phone/pad.html"));
    if (f.open(QIODevice::ReadOnly)) s->write(httpResponse("200 OK", "text/html; charset=utf-8", f.readAll()));
    else s->write(httpResponse("500 Internal Server Error", "text/plain", "The writing page is missing from this build of Inkleaf."));
  } else {
    s->write(httpResponse("404 Not Found", "text/plain", "Not here."));
  }
  s->disconnectFromHost();
}

QByteArray PhonePad::acceptKey(const QByteArray& key) {
  return QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
}

void PhonePad::upgrade(QTcpSocket* s, const QByteArray& key) {
  // One phone at a time: a new one takes over.
  if (m_ws) {
    endStroke();
    QTcpSocket* old = m_ws;
    m_ws = nullptr;
    old->disconnect(this);
    old->abort();
    old->deleteLater();
  }
  s->disconnect(this);
  s->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + acceptKey(key) + "\r\n\r\n");
  s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
  m_ws = s;
  m_wsBuf.clear();
  m_fragments.clear();
  m_sentVersion = 0;
  m_sentPage = -1;
  connect(s, &QTcpSocket::readyRead, this, &PhonePad::onWsData);
  connect(s, &QTcpSocket::disconnected, this, &PhonePad::onWsGone);
  emit stateChanged();
}

void PhonePad::onWsGone() {
  if (!m_ws) return;
  endStroke();
  m_ws->deleteLater();
  m_ws = nullptr;
  m_aspect = 0;
  emit regionChanged();
  emit stateChanged();
}

// A WebSocket's frames (RFC 6455): the phone's are masked; ours aren't.
void PhonePad::onWsData() {
  if (!m_ws) return;
  m_wsBuf += m_ws->readAll();
  while (true) {
    if (m_wsBuf.size() < 2) return;
    const quint8 b0 = quint8(m_wsBuf[0]), b1 = quint8(m_wsBuf[1]);
    const bool fin = b0 & 0x80, masked = b1 & 0x80;
    const quint8 op = b0 & 0x0F;
    qint64 len = b1 & 0x7F;
    int at = 2;
    if (len == 126) {
      if (m_wsBuf.size() < 4) return;
      len = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(m_wsBuf.constData() + 2));
      at = 4;
    } else if (len == 127) {
      if (m_wsBuf.size() < 10) return;
      len = qint64(qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(m_wsBuf.constData() + 2)));
      at = 10;
    }
    if (len < 0 || len > kMaxMessage || !masked) return onWsGone();
    if (m_wsBuf.size() < at + 4 + len) return;
    const QByteArray mask = m_wsBuf.mid(at, 4);
    QByteArray payload = m_wsBuf.mid(at + 4, int(len));
    for (int i = 0; i < payload.size(); ++i) payload[i] = char(payload[i] ^ mask[i % 4]);
    m_wsBuf.remove(0, at + 4 + int(len));
    if (op == 0x8) {  // close
      sendFrame(0x8, {});
      m_ws->disconnectFromHost();
      return;
    }
    if (op == 0x9) {  // ping
      sendFrame(0xA, payload);
      continue;
    }
    if (op == 0x1 || op == 0x0) {
      m_fragments += payload;
      if (m_fragments.size() > kMaxMessage) return onWsGone();
      if (!fin) continue;
      const QJsonDocument doc = QJsonDocument::fromJson(m_fragments);
      m_fragments.clear();
      if (doc.isObject()) onMessage(doc.object());
      if (!m_ws) return;
    }
  }
}

void PhonePad::sendFrame(quint8 opcode, const QByteArray& payload) {
  if (!m_ws) return;
  QByteArray h;
  h.append(char(0x80 | opcode));
  const qint64 n = payload.size();
  if (n < 126) {
    h.append(char(n));
  } else if (n < 65536) {
    h.append(char(126));
    h.append(char((n >> 8) & 0xFF));
    h.append(char(n & 0xFF));
  } else {
    h.append(char(127));
    for (int i = 7; i >= 0; --i) h.append(char((quint64(n) >> (8 * i)) & 0xFF));
  }
  m_ws->write(h);
  m_ws->write(payload);
}

void PhonePad::sendText(const QJsonObject& o) { sendFrame(0x1, QJsonDocument(o).toJson(QJsonDocument::Compact)); }

// --- what the phone says ----------------------------------------------------------------

void PhonePad::onMessage(const QJsonObject& m) {
  if (!m_canvas) return;
  const QString t = m["t"].toString();
  if (t == "hello") {
    // Its view's size: the region takes its shape.
    const double w = m["w"].toDouble(), h = m["h"].toDouble();
    if (w < 50 || h < 50) return;
    m_viewW = std::min(1600.0, w * std::clamp(m["dpr"].toDouble(1), 1.0, 3.0));
    const bool first = m_aspect <= 0;
    m_aspect = h / w;
    if (first) {
      // About half the page's width (writing a comfortable size), from the top of what's on the desktop.
      const int page = std::max(0, m_canvas->currentPage());
      const QSizeF ps = m_canvas->pageSize(page);
      const double rw = ps.width() * (m_aspect > 1 ? 0.5 : 0.9);
      const QRectF onScreen = m_canvas->pageRectToItem(page, QRectF(0, 0, ps.width(), ps.height()));
      const double top = onScreen.y() < 0 ? -onScreen.y() / std::max(0.01, double(m_canvas->zoom())) : 0;
      setRegion(page, QRectF(ps.width() * 0.04, top, rw, rw * m_aspect));
    } else {
      setRegion(m_page, QRectF(m_region.x(), m_region.y(), m_region.width(), m_region.width() * m_aspect));
    }
    m_sentVersion = 0;
    sendState();
    scheduleImage(0);
    showOnDesktop();
  } else if (t == "view") {
    setRegion(m_page, QRectF(m["x"].toDouble(), m["y"].toDouble(), m["w"].toDouble(), m["w"].toDouble() * m_aspect));
    scheduleImage(m["end"].toBool() ? 0 : 90);
    if (m["end"].toBool()) {
      sendState();
      showOnDesktop();
    }
  } else if (t == "d") {
    endStroke();
    if (m_aspect <= 0) return;
    const double mag = std::max(1.0, (m_viewW / std::max(1.0, m_region.width())) / std::max(0.05, double(m_canvas->zoom())));
    m_stroke = true;
    m_canvas->remotePress(m_page, m["x"].toDouble(), m["y"].toDouble(), 0, mag);
  } else if (t == "m") {
    if (!m_stroke) return;
    // [x, y, t, x, y, t, …]: page units, and ms since the stroke began (when it was written).
    const QJsonArray pts = m["p"].toArray();
    for (int i = 0; i + 2 < pts.size(); i += 3) m_canvas->remoteDrag(pts[i].toDouble(), pts[i + 1].toDouble(), pts[i + 2].toDouble());
  } else if (t == "u") {
    m_ackSeq = m["s"].toInt();
    endStroke();
    scheduleImage(0);
  } else if (t == "undo") {
    endStroke();
    m_canvas->undo();
  } else if (t == "redo") {
    endStroke();
    m_canvas->redo();
  } else if (t == "tool") {
    emit toolRequested(m["id"].toString());
  } else if (t == "page") {
    const int to = std::clamp(m_page + m["d"].toInt(), 0, std::max(0, m_canvas->pageCount() - 1));
    if (to == m_page) return;
    setRegion(to, QRectF(m_region.x(), 0, m_region.width(), m_region.height()));
    m_selfMoveAt = nowMs();
    m_canvas->goToPage(to);
    sendState();
    scheduleImage(0);
  }
}

void PhonePad::endStroke() {
  if (!m_stroke) return;
  m_stroke = false;
  if (m_canvas) m_canvas->remoteRelease();
}

// --- the region and what the phone is sent ----------------------------------------------

QRectF PhonePad::clamped(int page, QRectF r) const {
  if (!m_canvas || m_aspect <= 0) return r;
  const QSizeF ps = m_canvas->pageSize(page);
  if (ps.isEmpty()) return r;
  // Always on the page: at most as wide as the page, and as tall.
  const double maxW = std::min(ps.width(), ps.height() / m_aspect);
  const double w = std::clamp(r.width(), std::min(60.0, maxW), maxW), h = w * m_aspect;
  const double x = std::clamp(r.x(), 0.0, ps.width() - w), y = std::clamp(r.y(), 0.0, std::max(0.0, ps.height() - h));
  return {x, y, w, h};
}

void PhonePad::setRegion(int page, QRectF r) {
  const QRectF c = clamped(page, r);
  if (page == m_page && c == m_region) return;
  m_page = page;
  m_region = c;
  emit regionChanged();
}

void PhonePad::showOnDesktop() {
  if (!m_canvas || m_aspect <= 0) return;
  m_selfMoveAt = nowMs();
  m_canvas->ensureVisible(m_page, m_region);
}

void PhonePad::sendState() {
  if (!m_ws || !m_canvas || m_aspect <= 0) return;
  const QString tool = m_canvas->tool();
  const QSizeF ps = m_canvas->pageSize(m_page);
  // The line as the desktop will draw it (the phone shows it so while it's written).
  const bool hl = tool == "highlighter";
  const ink::Pen pen = hl ? ink::Pen::Highlighter : tool == "shape" ? ink::Pen::Ballpoint : ink::penFromName(m_canvas->penStyle(), false);
  const double width = m_canvas->widths().value(tool, 3).toDouble();
  const QColor live = ink::liveColor(QColor(m_canvas->colors().value(tool, "#1d1f23").toString()), pen);
  sendText({{"t", "state"},
            {"page", m_page},
            {"pages", m_canvas->pageCount()},
            {"pw", ps.width()},
            {"ph", ps.height()},
            {"x", m_region.x()},
            {"y", m_region.y()},
            {"w", m_region.width()},
            {"tool", tool},
            {"color", live.name(QColor::HexRgb)},
            {"alpha", live.alphaF()},
            {"width", width},
            {"line", tool == "pen" || hl || tool == "shape" ? double(ink::lineWidth(pen, float(width))) : width}});
}

void PhonePad::scheduleImage(int ms) {
  if (!m_ws || m_aspect <= 0) return;
  if (!m_imageTimer.isActive() || m_imageTimer.remainingTime() > ms) m_imageTimer.start(ms);
}

void PhonePad::sendImage() {
  if (!m_ws || !m_canvas || m_aspect <= 0) return;
  const quint64 v = m_canvas->pageVersion(m_page);
  const quint64 g = m_canvas->gestureRev();
  if (v == m_sentVersion && g == m_sentGesture && m_region == m_sentRegion && m_page == m_sentPage) return;
  // While a stroke is under way the page only changes when it's erasing (or
  // moving a selection): shown as it goes, about ten times a second (a pen's
  // stroke is drawn by the phone itself until it ends).
  if (m_stroke && nowMs() - m_imageAt < 100) return scheduleImage(int(100 - (nowMs() - m_imageAt)) + 1);
  m_imageAt = nowMs();
  const double scale = m_viewW / std::max(1.0, m_region.width());
  const QImage img = m_canvas->regionImage(m_page, m_region, scale, true);
  if (img.isNull()) return;
  QByteArray jpg;
  QBuffer buf(&jpg);
  buf.open(QIODevice::WriteOnly);
  img.convertToFormat(QImage::Format_RGB32).save(&buf, "JPEG", 82);
  m_sentVersion = v;
  m_sentGesture = g;
  m_sentRegion = m_region;
  m_sentPage = m_page;
  // Which part it is, and the strokes it includes; then the picture.
  // "shaped": the stroke being written has become a shape (held still) and is
  // in the picture; the phone stops drawing its own line.
  const bool shaped = m_stroke && m_canvas->liveStroke(m_page).isEmpty() && m_canvas->tool() != "eraser";
  sendText({{"t", "img"}, {"page", m_page}, {"x", m_region.x()}, {"y", m_region.y()}, {"w", m_region.width()}, {"h", m_region.height()}, {"s", m_ackSeq}, {"shaped", shaped}});
  sendFrame(0x2, jpg);
}
