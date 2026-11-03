// An HTML canvas 2D context over QPainter, for the web app's drawing code
// (papers, covers, daftar pages, text, shapes, sticky notes) run unchanged in
// a QJSEngine (see Js.h). Covers what that code uses: paths in the current
// transform, fills and strokes with colours, gradients and patterns, text in
// CSS fonts (with right-to-left and Arabic shaping from Qt), images, clipping,
// dashes, alpha, composite modes and blurred shadows.
#pragma once

#include <QBrush>
#include <QFont>
#include <QImage>
#include <QJSValue>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QVariant>
#include <cmath>
#include <memory>
#include <vector>

/// A gradient made by createLinearGradient / createRadialGradient.
class JsGradient : public QObject {
  Q_OBJECT
public:
  explicit JsGradient(QGradient* g) : m_g(g) {}
  Q_INVOKABLE void addColorStop(double offset, const QString& color);
  const QGradient& gradient() const { return *m_g; }

private:
  std::unique_ptr<QGradient> m_g;
};

/// A pattern made by createPattern.
class JsPattern : public QObject {
  Q_OBJECT
public:
  explicit JsPattern(QImage img) : m_img(std::move(img)) {}
  const QImage& image() const { return m_img; }
  Q_INVOKABLE void setTransform(const QJSValue&) {}

private:
  QImage m_img;
};

/// A decoded picture (new Image() in the shims).
class JsImage : public QObject {
  Q_OBJECT
  Q_PROPERTY(int naturalWidth READ width CONSTANT)
  Q_PROPERTY(int naturalHeight READ height CONSTANT)
  Q_PROPERTY(int width READ width CONSTANT)
  Q_PROPERTY(int height READ height CONSTANT)
  Q_PROPERTY(bool complete READ complete CONSTANT)
public:
  bool complete() const { return true; }
  explicit JsImage(QImage img) : m_img(std::move(img)) {}
  const QImage& image() const { return m_img; }
  int width() const { return m_img.width(); }
  int height() const { return m_img.height(); }

private:
  QImage m_img;
};

/// A canvas and its 2D context in one object.
class JsCanvas : public QObject {
  Q_OBJECT
  Q_PROPERTY(int width READ width WRITE setWidth)
  Q_PROPERTY(int height READ height WRITE setHeight)
  Q_PROPERTY(QJSValue fillStyle READ fillStyle WRITE setFillStyle)
  Q_PROPERTY(QJSValue strokeStyle READ strokeStyle WRITE setStrokeStyle)
  Q_PROPERTY(double lineWidth READ lineWidth WRITE setLineWidth)
  Q_PROPERTY(QString lineCap READ lineCap WRITE setLineCap)
  Q_PROPERTY(QString lineJoin READ lineJoin WRITE setLineJoin)
  Q_PROPERTY(double miterLimit READ miterLimit WRITE setMiterLimit)
  Q_PROPERTY(double lineDashOffset READ lineDashOffset WRITE setLineDashOffset)
  Q_PROPERTY(QString font READ font WRITE setFont)
  Q_PROPERTY(QString textAlign READ textAlign WRITE setTextAlign)
  Q_PROPERTY(QString textBaseline READ textBaseline WRITE setTextBaseline)
  Q_PROPERTY(QString direction READ direction WRITE setDirection)
  Q_PROPERTY(double globalAlpha READ globalAlpha WRITE setGlobalAlpha)
  Q_PROPERTY(QString globalCompositeOperation READ composite WRITE setComposite)
  Q_PROPERTY(QString shadowColor READ shadowColor WRITE setShadowColor)
  Q_PROPERTY(double shadowBlur READ shadowBlur WRITE setShadowBlur)
  Q_PROPERTY(double shadowOffsetX READ shadowOffsetX WRITE setShadowOffsetX)
  Q_PROPERTY(double shadowOffsetY READ shadowOffsetY WRITE setShadowOffsetY)
  Q_PROPERTY(bool imageSmoothingEnabled READ smoothing WRITE setSmoothing)
  Q_PROPERTY(QString filter READ filter WRITE setFilter)
  Q_PROPERTY(QString letterSpacing READ letterSpacing WRITE setLetterSpacing)
  // Accepts canvas.style.* and className (the web code sizes canvases it shows).
  Q_PROPERTY(QVariantMap style MEMBER m_style)
  Q_PROPERTY(QString className MEMBER m_className)
public:
  JsCanvas(int w, int h, QObject* parent = nullptr);
  /// Draws through `external` (e.g. on a PDF page) instead of into a picture;
  /// w and h are the page's size. The painter must outlive the canvas.
  JsCanvas(QPainter* external, int w, int h, QObject* parent = nullptr);
  ~JsCanvas() override;

  /// The picture so far (ends painting first).
  QImage image();
  /// Clears and resizes; keeps nothing.
  void reset(int w, int h);

  int width() const { return m_external ? m_size.width() : m_img.width(); }
  int height() const { return m_external ? m_size.height() : m_img.height(); }
  void setWidth(int w) { if (!m_external) reset(w, height()); }
  void setHeight(int h) { if (!m_external) reset(width(), h); }

  // The context is the canvas itself (canvas.getContext("2d")).
  Q_INVOKABLE QObject* getContext(const QString&) { return this; }
  Q_INVOKABLE QObject* canvas() { return this; }

  // State
  Q_INVOKABLE void save();
  Q_INVOKABLE void restore();
  Q_INVOKABLE void reset2() { reset(width(), height()); }

  // Transforms
  Q_INVOKABLE void translate(double x, double y);
  Q_INVOKABLE void rotate(double a);
  Q_INVOKABLE void scale(double x, double y);
  Q_INVOKABLE void transform(double a, double b, double c, double d, double e, double f);
  Q_INVOKABLE void setTransform(double a, double b, double c, double d, double e, double f);
  Q_INVOKABLE void resetTransform() { st().ctm = QTransform(); }
  Q_INVOKABLE QVariantList getTransformList() const;

  // Paths (kept in device space, as the spec says: points use the
  // transform current when they're added).
  Q_INVOKABLE void beginPath() { m_path = QPainterPath(); m_path.setFillRule(Qt::WindingFill); m_hasPoint = false; }
  Q_INVOKABLE void moveTo(double x, double y);
  Q_INVOKABLE void lineTo(double x, double y);
  Q_INVOKABLE void closePath();
  Q_INVOKABLE void quadraticCurveTo(double cx, double cy, double x, double y);
  Q_INVOKABLE void bezierCurveTo(double c1x, double c1y, double c2x, double c2y, double x, double y);
  Q_INVOKABLE void arc(double x, double y, double r, double a0, double a1, bool ccw = false);
  Q_INVOKABLE void ellipse(double x, double y, double rx, double ry, double rot, double a0, double a1, bool ccw = false);
  Q_INVOKABLE void arcTo(double x1, double y1, double x2, double y2, double r);
  Q_INVOKABLE void rect(double x, double y, double w, double h);
  Q_INVOKABLE void roundRect(double x, double y, double w, double h, const QJSValue& radii = QJSValue());

  Q_INVOKABLE void fill(const QString& rule = QString());
  Q_INVOKABLE void stroke();
  Q_INVOKABLE void clip(const QString& rule = QString());
  Q_INVOKABLE bool isPointInPath(double x, double y) const { return m_path.contains(QPointF(x, y)); }

  Q_INVOKABLE void fillRect(double x, double y, double w, double h);
  Q_INVOKABLE void strokeRect(double x, double y, double w, double h);
  Q_INVOKABLE void clearRect(double x, double y, double w, double h);

  // Text
  Q_INVOKABLE void fillText(const QString& text, double x, double y, const QJSValue& maxWidth = QJSValue());
  Q_INVOKABLE void strokeText(const QString& text, double x, double y, const QJSValue& maxWidth = QJSValue());
  Q_INVOKABLE QVariantMap measureText(const QString& text);

  // Images
  Q_INVOKABLE void drawImage(QObject* source, double a, double b, const QJSValue& c = QJSValue(), const QJSValue& d = QJSValue(), const QJSValue& e = QJSValue(),
                             const QJSValue& f = QJSValue(), const QJSValue& g = QJSValue(), const QJSValue& h = QJSValue());
  Q_INVOKABLE QJSValue getImageData(int x, int y, int w, int h);
  Q_INVOKABLE void putImageData(const QJSValue& data, int x, int y);
  Q_INVOKABLE QByteArray pixels(int x, int y, int w, int h);
  Q_INVOKABLE QString toDataURL(const QString& type = QStringLiteral("image/png"), double quality = 0.92);
  Q_INVOKABLE void putPixels(const QByteArray& rgba, int x, int y, int w, int h);

  // Styles
  Q_INVOKABLE QObject* createLinearGradient(double x0, double y0, double x1, double y1);
  Q_INVOKABLE QObject* createRadialGradient(double x0, double y0, double r0, double x1, double y1, double r1);
  Q_INVOKABLE QObject* createPattern(QObject* source, const QString& repeat);
  Q_INVOKABLE void setLineDash(const QVariantList& dashes);
  Q_INVOKABLE QVariantList getLineDash() const;

  // Properties
  QJSValue fillStyle() const { return st().fillJs; }
  void setFillStyle(const QJSValue& v);
  QJSValue strokeStyle() const { return st().strokeJs; }
  void setStrokeStyle(const QJSValue& v);
  double lineWidth() const { return st().lineWidth; }
  void setLineWidth(double w) { if (w > 0 && std::isfinite(w)) st().lineWidth = w; }
  QString lineCap() const;
  void setLineCap(const QString& c);
  QString lineJoin() const;
  void setLineJoin(const QString& j);
  double miterLimit() const { return st().miter; }
  void setMiterLimit(double m) { if (m > 0) st().miter = m; }
  double lineDashOffset() const { return st().dashOffset; }
  void setLineDashOffset(double o) { st().dashOffset = o; }
  QString font() const { return st().fontCss; }
  void setFont(const QString& css);
  QString textAlign() const { return st().align; }
  void setTextAlign(const QString& a) { st().align = a; }
  QString textBaseline() const { return st().baseline; }
  void setTextBaseline(const QString& b) { st().baseline = b; }
  QString direction() const { return st().direction; }
  void setDirection(const QString& d) { st().direction = d; }
  double globalAlpha() const { return st().alpha; }
  void setGlobalAlpha(double a) { if (a >= 0 && a <= 1) st().alpha = a; }
  QString composite() const { return st().compositeName; }
  void setComposite(const QString& c);
  QString shadowColor() const { return st().shadowColor.name(QColor::HexArgb); }
  void setShadowColor(const QString& c);
  double shadowBlur() const { return st().shadowBlur; }
  void setShadowBlur(double b) { if (b >= 0) st().shadowBlur = b; }
  double shadowOffsetX() const { return st().shadowOffset.x(); }
  void setShadowOffsetX(double x) { st().shadowOffset.setX(x); }
  double shadowOffsetY() const { return st().shadowOffset.y(); }
  void setShadowOffsetY(double y) { st().shadowOffset.setY(y); }
  bool smoothing() const { return st().smooth; }
  void setSmoothing(bool s) { st().smooth = s; }
  QString filter() const { return st().filter; }
  void setFilter(const QString& f) { st().filter = f; }
  QString letterSpacing() const { return st().letterSpacingCss; }
  void setLetterSpacing(const QString& s);

  /// A CSS font shorthand ("italic 700 16px Inter, sans-serif") as a QFont.
  static QFont parseFont(const QString& css);
  /// A CSS colour ("#rgb", "#rrggbbaa", "rgba(…)", "hsl(…)", names) as a QColor.
  static QColor parseColor(const QString& css);

private:
  struct State {
    QTransform ctm;
    QJSValue fillJs = QJSValue(QStringLiteral("#000000"));
    QJSValue strokeJs = QJSValue(QStringLiteral("#000000"));
    QBrush fill = QBrush(Qt::black);
    QBrush strokeBrush = QBrush(Qt::black);
    bool fillUser = false, strokeUser = false;  // gradient/pattern in user space
    double lineWidth = 1, miter = 10, dashOffset = 0;
    Qt::PenCapStyle cap = Qt::FlatCap;
    Qt::PenJoinStyle join = Qt::MiterJoin;
    QVector<qreal> dash;
    QString fontCss = QStringLiteral("10px sans-serif");
    QFont font;
    QString align = QStringLiteral("start"), baseline = QStringLiteral("alphabetic"), direction = QStringLiteral("inherit");
    double alpha = 1;
    QString compositeName = QStringLiteral("source-over");
    QPainter::CompositionMode composite = QPainter::CompositionMode_SourceOver;
    QColor shadowColor = Qt::transparent;
    double shadowBlur = 0;
    QPointF shadowOffset;
    bool smooth = true;
    QString filter = QStringLiteral("none");
    QString letterSpacingCss = QStringLiteral("0px");
    double letterSpacing = 0;
    int clipDepth = 0;
  };
  State& st() { return m_states.back(); }
  const State& st() const { return m_states.back(); }
  QPainter& painter();
  QBrush brushFor(const QJSValue& v, bool* userSpace);
  QPen strokePen() const;
  /// Fills `devicePath` (device space) with `brush`, with the shadow if any.
  void fillDevice(const QPainterPath& devicePath, const QBrush& brush, bool userSpace);
  void drawShadow(const QPainterPath& devicePath);
  void ensurePoint(double x, double y);
  void drawText(const QString& text, double x, double y, bool stroke, const QJSValue& maxWidth);

  QImage m_img;
  QVariantMap m_style;
  QString m_className;
  std::unique_ptr<QPainter> m_painter;
  QPainter* m_external = nullptr;
  QSize m_size;
  std::vector<State> m_states;
  QPainterPath m_path;
  bool m_hasPoint = false;
};
