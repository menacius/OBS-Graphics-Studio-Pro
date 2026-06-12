/*
 * title-editor.cpp
 *
 * After Effects-style title editor.
 * Implemented with plain Qt widgets for maximum OBS compatibility.
 */

#include "title-editor.h"
#include "title-data.h"
#include "title-source.h"
#include "title-assets.h"
#include "timecode-spinbox.h"
#include "title-localization.h"
#include "plugin-main.h"
#include "title-preferences.h"

#include <obs-module.h>

#include <QApplication>

#include <QBuffer>
#include <QIODevice>
#include <QPainter>
#include <QBrush>
#include <QPainterPath>
#include <QPolygonF>
#include <QLineF>
#include <QCursor>
#include <QImage>
#include <QImageReader>
#include <QSize>
#include <QSvgRenderer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QIcon>
#include <QPixmap>
#include <QStringList>
#include <QVariant>
#include <QLocale>
#include <QStyle>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QColorDialog>
#include <QFileDialog>
#include <QInputDialog>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QScrollArea>
#include <QScrollBar>
#include <QListWidget>
#include <QStackedWidget>
#include <QSizePolicy>
#include <QFrame>
#include <QSignalBlocker>
#include <QKeyEvent>
#include <QEvent>
#include <QKeySequence>
#include <QAbstractSpinBox>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextOption>
#include <QAbstractTextDocumentLayout>
#include <QDateTime>
#include <QTransform>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QConicalGradient>
#include <QToolButton>
#include <QMenu>
#include <QMenuBar>
#include <QTabWidget>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QMainWindow>
#include <QScreen>
#include <QSettings>
#include <QWindow>
#include <QContextMenuEvent>
#include <cmath>
#include <algorithm>
#include <vector>
#include <initializer_list>
#include <set>
#include <map>
#include <limits>
#include <tuple>
#include <functional>

namespace {

constexpr const char *kEditorLayoutSettingsGroup = "TitleEditorLayout";
constexpr const char *kEditorGeometryKey = "geometry";
constexpr const char *kEditorWindowStateKey = "windowState";
constexpr const char *kEditorPanelsLockedKey = "panelsLocked";
constexpr const char *kEditorCanvasTransparencyKey = "canvasTransparency";
constexpr const char *kEditorSafeGuidesVisibleKey = "safeGuidesVisible";
constexpr const char *kGraphicPropertiesDockObjectName = "OBSGraphicsStudioProGraphicPropertiesDock";
constexpr const char *kLayerPropertiesDockObjectName = "OBSGraphicsStudioProLayerPropertiesDock";
constexpr const char *kEffectsDockObjectName = "OBSGraphicsStudioProEffectsDock";
constexpr const char *kStylesDockObjectName = "OBSGraphicsStudioProStylesDock";
constexpr const char *kColorSwatchesDockObjectName = "OBSGraphicsStudioProColorSwatchesDock";
constexpr const char *kTimelineDockObjectName = "OBSGraphicsStudioProTimelineDock";

static QPoint clamp_popup_position_to_screen(const QPoint &desired, const QSize &popup_size, QWidget *anchor)
{
    QScreen *screen = QApplication::screenAt(desired);
    if (!screen && anchor && anchor->window() && anchor->window()->windowHandle())
        screen = anchor->window()->windowHandle()->screen();
    if (!screen)
        screen = QApplication::primaryScreen();
    if (!screen)
        return desired;

    const QRect available = screen->availableGeometry().adjusted(6, 6, -6, -6);
    QPoint pos = desired;
    pos.setX(std::clamp(pos.x(), available.left(), std::max(available.left(), available.right() - popup_size.width() + 1)));
    pos.setY(std::clamp(pos.y(), available.top(), std::max(available.top(), available.bottom() - popup_size.height() + 1)));
    return pos;
}

class NumericDragLabel : public QLabel {
public:
    NumericDragLabel(const QString &text, QWidget *field, QWidget *parent = nullptr,
                     std::function<void()> drag_started = {},
                     std::function<void()> drag_finished = {})
        : QLabel(text, parent), spin_box_(find_spin_box(field)),
          drag_started_(std::move(drag_started)), drag_finished_(std::move(drag_finished))
    {
        if (!spin_box_) return;
        setToolTip(obsgs_tr("OBSTitles.DragNumericLabelTooltip"));
        setCursor(Qt::SizeHorCursor);
    }

    ~NumericDragLabel() override
    {
        if (dragging_)
            QApplication::restoreOverrideCursor();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !can_drag()) {
            QLabel::mousePressEvent(event);
            return;
        }

        dragging_ = true;
        drag_start_x_ = event->globalPosition().x();
        drag_start_value_ = spin_value();
        grabMouse(Qt::SizeHorCursor);
        QApplication::setOverrideCursor(Qt::SizeHorCursor);
        if (drag_started_)
            drag_started_();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!dragging_) {
            QLabel::mouseMoveEvent(event);
            return;
        }
        if (!can_drag()) {
            finish_drag();
            event->accept();
            return;
        }

        const double delta = event->globalPosition().x() - drag_start_x_;
        set_spin_value(drag_start_value_ + delta * spin_step());
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!dragging_ || event->button() != Qt::LeftButton) {
            QLabel::mouseReleaseEvent(event);
            return;
        }

        finish_drag();
        event->accept();
    }

    void leaveEvent(QEvent *event) override
    {
        if (!dragging_)
            QLabel::leaveEvent(event);
    }

private:
    static QAbstractSpinBox *find_spin_box(QWidget *field)
    {
        if (!field) return nullptr;
        if (auto *spin = qobject_cast<QAbstractSpinBox *>(field)) return spin;
        return field->findChild<QAbstractSpinBox *>();
    }

    bool can_drag() const
    {
        return spin_box_ && spin_box_->isEnabled() && spin_box_->isVisible() && isEnabled();
    }

    double spin_value() const
    {
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(spin_box_)) return spin->value();
        if (auto *spin = qobject_cast<QSpinBox *>(spin_box_)) return spin->value();
        return 0.0;
    }

    double spin_step() const
    {
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(spin_box_)) return spin->singleStep();
        if (auto *spin = qobject_cast<QSpinBox *>(spin_box_)) return spin->singleStep();
        return 1.0;
    }

    void set_spin_value(double value)
    {
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(spin_box_)) {
            spin->setValue(std::clamp(value, spin->minimum(), spin->maximum()));
        } else if (auto *spin = qobject_cast<QSpinBox *>(spin_box_)) {
            spin->setValue(std::clamp((int)std::round(value), spin->minimum(), spin->maximum()));
        }
    }

    void finish_drag()
    {
        dragging_ = false;
        releaseMouse();
        QApplication::restoreOverrideCursor();
        if (drag_finished_)
            drag_finished_();
    }

    QAbstractSpinBox *spin_box_ = nullptr;
    std::function<void()> drag_started_;
    std::function<void()> drag_finished_;
    bool dragging_ = false;
    double drag_start_x_ = 0.0;
    double drag_start_value_ = 0.0;
};

class StrokeOptionsLabel : public QLabel {
public:
    StrokeOptionsLabel(const QString &text, QDoubleSpinBox *spin, QWidget *parent = nullptr,
                       std::function<void()> clicked = {},
                       std::function<void()> drag_started = {},
                       std::function<void()> drag_finished = {})
        : QLabel(text, parent), spin_(spin), clicked_(std::move(clicked)),
          drag_started_(std::move(drag_started)), drag_finished_(std::move(drag_finished))
    {
        setCursor(Qt::PointingHandCursor);
        setToolTip(obsgs_tr("OBSTitles.DragNumericLabelTooltip"));
    }

    ~StrokeOptionsLabel() override
    {
        if (dragging_)
            QApplication::restoreOverrideCursor();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !spin_ || !spin_->isEnabled()) {
            QLabel::mousePressEvent(event);
            return;
        }
        dragging_ = true;
        moved_ = false;
        drag_start_x_ = event->globalPosition().x();
        drag_start_value_ = spin_->value();
        grabMouse(Qt::SizeHorCursor);
        QApplication::setOverrideCursor(Qt::SizeHorCursor);
        if (drag_started_)
            drag_started_();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!dragging_ || !spin_) {
            QLabel::mouseMoveEvent(event);
            return;
        }
        const double delta = event->globalPosition().x() - drag_start_x_;
        if (std::abs(delta) >= 2.0)
            moved_ = true;
        spin_->setValue(std::clamp(drag_start_value_ + delta * spin_->singleStep(),
                                   spin_->minimum(), spin_->maximum()));
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!dragging_ || event->button() != Qt::LeftButton) {
            QLabel::mouseReleaseEvent(event);
            return;
        }
        dragging_ = false;
        releaseMouse();
        QApplication::restoreOverrideCursor();
        if (drag_finished_)
            drag_finished_();
        if (!moved_ && clicked_)
            clicked_();
        event->accept();
    }

private:
    QDoubleSpinBox *spin_ = nullptr;
    std::function<void()> clicked_;
    std::function<void()> drag_started_;
    std::function<void()> drag_finished_;
    bool dragging_ = false;
    bool moved_ = false;
    double drag_start_x_ = 0.0;
    double drag_start_value_ = 0.0;
};

class TransformLockCheckBox : public QCheckBox {
public:
    explicit TransformLockCheckBox(QWidget *parent = nullptr) : QCheckBox(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect().adjusted(1, 1, -1, -1);
        const QColor bg = underMouse() ? QColor(48, 48, 48) : QColor(36, 36, 36);
        const QColor border = isChecked() ? QColor(111, 143, 196) : QColor(58, 58, 58);
        const QColor fg = isChecked() ? QColor(215, 226, 248) : QColor(170, 170, 170);
        p.setPen(QPen(border, 1));
        p.setBrush(bg);
        p.drawRoundedRect(r, 2, 2);

        p.setPen(QPen(fg, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        if (isChecked())
            p.drawArc(QRectF(width() / 2.0 - 5.0, height() / 2.0 - 8.0, 10.0, 12.0), 0, 180 * 16);
        else
            p.drawArc(QRectF(width() / 2.0 - 1.0, height() / 2.0 - 8.0, 10.0, 12.0), 30 * 16, 150 * 16);

        const QRectF body(width() / 2.0 - 5.0, height() / 2.0 - 1.0, 10.0, 8.0);
        p.setBrush(fg);
        p.drawRoundedRect(body, 1.5, 1.5);
        p.setPen(bg);
        p.drawPoint(QPointF(width() / 2.0, height() / 2.0 + 3.0));
    }
};

class AnchorGridButton : public QPushButton {
public:
    explicit AnchorGridButton(QWidget *parent = nullptr) : QPushButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setProperty("active_index", 4);
    }

    std::function<void(int)> anchor_selected;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF outer = rect().adjusted(1, 1, -1, -1);
        p.setPen(QPen(underMouse() ? QColor(74, 74, 74) : QColor(55, 55, 55), 1));
        p.setBrush(underMouse() ? QColor(48, 48, 48) : QColor(36, 36, 36));
        p.drawRoundedRect(outer, 2, 2);

        const int active = property("active_index").toInt();
        constexpr int cell = 7;
        constexpr int gap = 3;
        const int grid = cell * 3 + gap * 2;
        const int origin_x = (width() - grid) / 2;
        const int origin_y = (height() - grid) / 2;
        for (int i = 0; i < 9; ++i) {
            QRectF cell_rect(origin_x + (i % 3) * (cell + gap),
                             origin_y + (i / 3) * (cell + gap),
                             cell, cell);
            p.setPen(QPen(i == active ? QColor(111, 143, 196) : QColor(160, 160, 160), 1.2));
            p.setBrush(i == active ? QColor(75, 110, 168) : Qt::NoBrush);
            p.drawRect(cell_rect);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            QPushButton::mouseReleaseEvent(event);
            return;
        }

        constexpr int cell = 7;
        constexpr int gap = 3;
        const int grid = cell * 3 + gap * 2;
        const int origin_x = (width() - grid) / 2;
        const int origin_y = (height() - grid) / 2;
        const QPoint pos = event->pos();
        for (int i = 0; i < 9; ++i) {
            QRect cell_rect(origin_x + (i % 3) * (cell + gap),
                            origin_y + (i / 3) * (cell + gap),
                            cell + gap, cell + gap);
            if (cell_rect.contains(pos)) {
                setProperty("active_index", i);
                update();
                if (anchor_selected)
                    anchor_selected(i);
                break;
            }
        }
        event->accept();
    }
};

class HsvColorPicker : public QWidget {
public:
    explicit HsvColorPicker(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(276, 330);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void set_color(const QColor &color)
    {
        current_ = color.isValid() ? color : QColor(Qt::white);
        int h = current_.hsvHue();
        hue_ = h < 0 ? 0 : h;
        saturation_ = std::clamp((double)current_.hsvSaturationF(), 0.0, 1.0);
        value_ = std::clamp((double)current_.valueF(), 0.0, 1.0);
        alpha_ = std::clamp((double)current_.alphaF(), 0.0, 1.0);
        update();
    }

    QColor color() const
    {
        QColor color;
        color.setHsvF(hue_ / 359.0, saturation_, value_, alpha_);
        return color;
    }

    std::function<void(const QColor &)> color_changed;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        const QRect sv = sv_rect();
        QImage image(sv.size(), QImage::Format_RGB32);
        for (int y = 0; y < image.height(); ++y) {
            const double v = 1.0 - (double)y / std::max(1, image.height() - 1);
            for (int x = 0; x < image.width(); ++x) {
                const double s = (double)x / std::max(1, image.width() - 1);
                QColor c;
                c.setHsvF(hue_ / 359.0, s, v, 1.0);
                image.setPixelColor(x, y, c);
            }
        }
        p.drawImage(sv.topLeft(), image);
        p.setPen(QColor(18, 18, 18));
        p.drawRect(sv.adjusted(0, 0, -1, -1));
        const QPoint sv_handle(sv.left() + (int)std::round(saturation_ * (sv.width() - 1)),
                               sv.top() + (int)std::round((1.0 - value_) * (sv.height() - 1)));
        p.setPen(QPen(Qt::white, 1));
        p.drawEllipse(sv_handle, 5, 5);
        p.setPen(QPen(Qt::black, 1));
        p.drawEllipse(sv_handle, 6, 6);

        const QRect hue = hue_rect();
        QLinearGradient hue_gradient(hue.topLeft(), hue.topRight());
        for (int i = 0; i <= 6; ++i) {
            QColor c;
            c.setHsvF(i / 6.0, 1.0, 1.0, 1.0);
            hue_gradient.setColorAt(i / 6.0, c);
        }
        p.fillRect(hue, hue_gradient);
        p.setPen(QColor(18, 18, 18));
        p.drawRect(hue.adjusted(0, 0, -1, -1));
        draw_slider_handle(p, hue, hue_ / 359.0);

        const QRect alpha = alpha_rect();
        draw_checkerboard(p, alpha);
        QColor opaque = color();
        opaque.setAlphaF(1.0);
        QColor transparent = opaque;
        transparent.setAlphaF(0.0);
        QLinearGradient alpha_gradient(alpha.topLeft(), alpha.topRight());
        alpha_gradient.setColorAt(0.0, transparent);
        alpha_gradient.setColorAt(1.0, opaque);
        p.fillRect(alpha, alpha_gradient);
        p.setPen(QColor(18, 18, 18));
        p.drawRect(alpha.adjusted(0, 0, -1, -1));
        draw_slider_handle(p, alpha, alpha_);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        update_from_pos(event->pos());
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton) {
            update_from_pos(event->pos());
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

private:
    QRect sv_rect() const { return QRect(8, 8, 260, 260); }
    QRect hue_rect() const { return QRect(8, 282, 260, 20); }
    QRect alpha_rect() const { return QRect(8, 316, 260, 20); }

    void update_from_pos(const QPoint &pos)
    {
        if (sv_rect().contains(pos)) {
            const QRect r = sv_rect();
            saturation_ = std::clamp((double)(pos.x() - r.left()) / std::max(1, r.width() - 1), 0.0, 1.0);
            value_ = std::clamp(1.0 - (double)(pos.y() - r.top()) / std::max(1, r.height() - 1), 0.0, 1.0);
        } else if (hue_rect().contains(pos)) {
            const QRect r = hue_rect();
            hue_ = std::clamp((double)(pos.x() - r.left()) / std::max(1, r.width() - 1), 0.0, 1.0) * 359.0;
        } else if (alpha_rect().contains(pos)) {
            const QRect r = alpha_rect();
            alpha_ = std::clamp((double)(pos.x() - r.left()) / std::max(1, r.width() - 1), 0.0, 1.0);
        } else {
            return;
        }

        current_ = color();
        update();
        if (color_changed)
            color_changed(current_);
    }

    static void draw_checkerboard(QPainter &p, const QRect &rect)
    {
        const int cell = 8;
        for (int y = rect.top(); y < rect.bottom(); y += cell) {
            for (int x = rect.left(); x < rect.right(); x += cell) {
                const bool dark = ((x / cell) + (y / cell)) % 2;
                p.fillRect(QRect(x, y, cell, cell).intersected(rect),
                           dark ? QColor(155, 155, 155) : QColor(220, 220, 220));
            }
        }
    }

    static void draw_slider_handle(QPainter &p, const QRect &rect, double value)
    {
        const int x = rect.left() + (int)std::round(std::clamp(value, 0.0, 1.0) * (rect.width() - 1));
        const QRect handle(x - 4, rect.top() - 2, 8, rect.height() + 4);
        p.setPen(QPen(QColor(70, 220, 110), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(handle, 2, 2);
    }

    QColor current_ = Qt::white;
    double hue_ = 120.0;
    double saturation_ = 1.0;
    double value_ = 1.0;
    double alpha_ = 1.0;
};

static QString gradient_editor_hex(const QColor &color)
{
    return color.alpha() < 255
        ? QStringLiteral("#%1%2%3%4")
              .arg(color.red(), 2, 16, QLatin1Char('0'))
              .arg(color.green(), 2, 16, QLatin1Char('0'))
              .arg(color.blue(), 2, 16, QLatin1Char('0'))
              .arg(color.alpha(), 2, 16, QLatin1Char('0'))
              .toUpper()
        : QStringLiteral("#%1%2%3")
              .arg(color.red(), 2, 16, QLatin1Char('0'))
              .arg(color.green(), 2, 16, QLatin1Char('0'))
              .arg(color.blue(), 2, 16, QLatin1Char('0'))
              .toUpper();
}

static bool gradient_editor_parse_hex(QString text, QColor &color)
{
    text = text.trimmed();
    if (text.startsWith(QStringLiteral("#"))) text.remove(0, 1);
    if (text.size() != 6 && text.size() != 8) return false;
    bool ok = false;
    const uint value = text.toUInt(&ok, 16);
    if (!ok) return false;
    color = text.size() == 6
        ? QColor((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF, 255)
        : QColor((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
    return color.isValid();
}

static QColor gradient_editor_color_from_argb(uint32_t argb)
{
    return QColor((argb >> 16) & 0xFF,
                  (argb >> 8) & 0xFF,
                  argb & 0xFF,
                  (argb >> 24) & 0xFF);
}

static uint32_t gradient_editor_argb_from_color(const QColor &color)
{
    return ((uint32_t)color.alpha() << 24) |
           ((uint32_t)color.red() << 16) |
           ((uint32_t)color.green() << 8) |
           (uint32_t)color.blue();
}

class GradientEditorPreview : public QWidget {
public:
    explicit GradientEditorPreview(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(440, 300);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    void set_gradient(int type, uint32_t start_color, uint32_t end_color,
                      double start_pos, double end_pos, double start_opacity, double end_opacity,
                      double opacity, double angle, double center_x, double center_y, double scale)
    {
        type_ = std::clamp(type, 0, 4);
        colors_[0] = gradient_editor_color_from_argb(start_color);
        colors_[1] = gradient_editor_color_from_argb(end_color);
        colors_[0].setAlpha(255);
        colors_[1].setAlpha(255);
        positions_[0] = std::clamp(start_pos, 0.0, 1.0);
        positions_[1] = std::clamp(end_pos, 0.0, 1.0);
        stop_opacities_[0] = std::clamp(start_opacity, 0.0, 1.0);
        stop_opacities_[1] = std::clamp(end_opacity, 0.0, 1.0);
        opacity_ = std::clamp(opacity, 0.0, 1.0);
        angle_ = angle;
        center_x_ = std::clamp(center_x, 0.0, 1.0);
        center_y_ = std::clamp(center_y, 0.0, 1.0);
        scale_ = std::clamp(scale, 0.01, 10.0);
        update();
    }

    int gradient_type() const { return type_; }
    int selected_stop() const { return selected_stop_; }
    uint32_t stop_color_argb(int index) const { return gradient_editor_argb_from_color(colors_[std::clamp(index, 0, 1)]); }
    QColor stop_color(int index) const
    {
        QColor color = colors_[std::clamp(index, 0, 1)];
        color.setAlphaF(std::clamp(color.alphaF() * stop_opacities_[std::clamp(index, 0, 1)] * opacity_, 0.0, 1.0));
        return color;
    }
    double stop_position(int index) const { return positions_[std::clamp(index, 0, 1)]; }
    double stop_opacity(int index) const { return stop_opacities_[std::clamp(index, 0, 1)]; }
    double gradient_opacity() const { return opacity_; }
    double angle() const { return angle_; }
    double center_x() const { return center_x_; }
    double center_y() const { return center_y_; }
    double scale() const { return scale_; }

    void set_gradient_type(int type) { type_ = std::clamp(type, 0, 4); changed(); }
    void set_angle(double value) { angle_ = value; changed(); }
    void set_center_x(double value) { center_x_ = std::clamp(value, 0.0, 1.0); changed(); }
    void set_center_y(double value) { center_y_ = std::clamp(value, 0.0, 1.0); changed(); }
    void set_scale(double value) { scale_ = std::clamp(value, 0.01, 10.0); changed(); }
    void set_stop_position(int index, double value) { positions_[std::clamp(index, 0, 1)] = std::clamp(value, 0.0, 1.0); changed(); }
    void set_stop_opacity(int index, double value) { stop_opacities_[std::clamp(index, 0, 1)] = std::clamp(value, 0.0, 1.0); changed(); }
    void set_stop_color(int index, const QColor &color)
    {
        if (!color.isValid()) return;
        const int i = std::clamp(index, 0, 1);
        colors_[i] = color;
        colors_[i].setAlpha(255);
        stop_opacities_[i] = color.alphaF();
        changed();
    }
    void set_selected_stop(int index)
    {
        selected_stop_ = std::clamp(index, 0, 1);
        update();
        if (selection_changed) selection_changed(selected_stop_);
    }

    QPoint stop_global_anchor(int index) const
    {
        const QPointF p = stop_point(std::clamp(index, 0, 1));
        return mapToGlobal(QPoint((int)std::round(p.x()), (int)std::round(p.y() + 14.0)));
    }

    std::function<void()> gradient_changed;
    std::function<void(int)> selection_changed;
    std::function<void(int, const QPoint &)> color_popup_requested;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor(43, 43, 43));

        QRectF canvas = preview_rect();
        draw_checkerboard(p, canvas.toAlignedRect());
        p.fillRect(canvas, preview_brush(canvas));
        p.setPen(QPen(QColor(24, 24, 24), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(canvas, 3, 3);

        QRectF line = gradient_line_rect();
        p.setPen(QPen(QColor(220, 220, 220), 1));
        p.drawLine(QPointF(line.left(), line.center().y()), QPointF(line.right(), line.center().y()));
        p.setPen(QPen(QColor(120, 120, 120), 1));
        for (int i = 0; i < 2; ++i) {
            double other = positions_[1 - i];
            double mid = (positions_[i] + other) * 0.5;
            QPointF m(line.left() + mid * line.width(), line.center().y());
            QPolygonF diamond;
            diamond << QPointF(m.x(), m.y() - 5) << QPointF(m.x() + 5, m.y())
                    << QPointF(m.x(), m.y() + 5) << QPointF(m.x() - 5, m.y());
            p.setBrush(QColor(75, 75, 75));
            p.drawPolygon(diamond);
        }

        for (int i = 0; i < 2; ++i) {
            QRectF handle = stop_handle_rect(i);
            p.setPen(QPen(i == selected_stop_ ? QColor(120, 170, 230) : QColor(18, 18, 18), i == selected_stop_ ? 2 : 1));
            p.setBrush(QColor(36, 36, 36));
            p.drawRoundedRect(handle, 2, 2);
            QRectF swatch = handle.adjusted(4, 4, -4, -4);
            draw_checkerboard(p, swatch.toAlignedRect());
            p.fillRect(swatch, stop_color(i));
            p.setPen(QColor(18, 18, 18));
            p.drawRect(swatch.adjusted(0, 0, -1, -1));
        }

        p.setPen(QColor(210, 210, 210));
        p.drawText(QRectF(canvas.left() + 10, canvas.top() + 8, 220, 20),
                   QStringLiteral("%1  %2%")
                       .arg(type_name(type_))
                       .arg((int)std::round(opacity_ * 100.0)));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        int hit = hit_stop(event->pos());
        if (hit >= 0) {
            set_selected_stop(hit);
            dragging_stop_ = true;
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (dragging_stop_ && (event->buttons() & Qt::LeftButton)) {
            const QRectF line = gradient_line_rect();
            positions_[selected_stop_] = std::clamp((event->position().x() - line.left()) / line.width(), 0.0, 1.0);
            changed();
            event->accept();
            return;
        }
        setCursor(hit_stop(event->pos()) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        dragging_stop_ = false;
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        int hit = hit_stop(event->pos());
        if (hit >= 0) {
            set_selected_stop(hit);
            if (color_popup_requested) color_popup_requested(hit, stop_global_anchor(hit));
            event->accept();
            return;
        }
        if (gradient_line_rect().adjusted(-6, -12, 6, 12).contains(event->position())) {
            const QRectF line = gradient_line_rect();
            const double pos = std::clamp((event->position().x() - line.left()) / line.width(), 0.0, 1.0);
            const int target = std::abs(pos - positions_[0]) <= std::abs(pos - positions_[1]) ? 0 : 1;
            set_selected_stop(target);
            positions_[target] = pos;
            changed();
            if (color_popup_requested) color_popup_requested(target, stop_global_anchor(target));
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

private:
    QRectF preview_rect() const { return QRectF(12, 12, width() - 24, height() - 62); }
    QRectF gradient_line_rect() const
    {
        QRectF canvas = preview_rect();
        return QRectF(canvas.left() + 28, canvas.bottom() - 62, canvas.width() - 56, 20);
    }
    QPointF stop_point(int index) const
    {
        const QRectF line = gradient_line_rect();
        return QPointF(line.left() + positions_[index] * line.width(), line.center().y());
    }
    QRectF stop_handle_rect(int index) const
    {
        QPointF p = stop_point(index);
        return QRectF(p.x() - 10, p.y() - 12, 20, 20);
    }
    int hit_stop(const QPointF &pos) const
    {
        for (int i = 1; i >= 0; --i)
            if (stop_handle_rect(i).adjusted(-4, -4, 4, 4).contains(pos)) return i;
        return -1;
    }

    void changed()
    {
        update();
        if (gradient_changed) gradient_changed();
    }

    QBrush preview_brush(const QRectF &r) const
    {
        QColor a = stop_color(0);
        QColor b = stop_color(1);
        if (type_ == 1) {
            QRadialGradient gradient(QPointF(r.left() + center_x_ * r.width(), r.top() + center_y_ * r.height()),
                                     std::max(r.width(), r.height()) * 0.5 * scale_);
            gradient.setColorAt(positions_[0], a);
            gradient.setColorAt(positions_[1], b);
            return QBrush(gradient);
        }
        if (type_ == 2) {
            QConicalGradient gradient(QPointF(r.left() + center_x_ * r.width(), r.top() + center_y_ * r.height()), -angle_);
            gradient.setColorAt(positions_[0], a);
            gradient.setColorAt(positions_[1], b);
            return QBrush(gradient);
        }
        if (type_ == 4) {
            QRadialGradient gradient(QPointF(r.left() + center_x_ * r.width(), r.top() + center_y_ * r.height()),
                                     std::max(r.width(), r.height()) * 0.42 * scale_);
            gradient.setColorAt(positions_[0], a);
            gradient.setColorAt(positions_[1], b);
            return QBrush(gradient);
        }
        const double radians = angle_ * std::acos(-1.0) / 180.0;
        const QPointF c(r.left() + center_x_ * r.width(), r.top() + center_y_ * r.height());
        const double len = std::max(r.width(), r.height()) * 0.5 * scale_;
        const QPointF d(std::cos(radians) * len, std::sin(radians) * len);
        QLinearGradient gradient(c - d, c + d);
        if (type_ == 3) gradient.setSpread(QGradient::ReflectSpread);
        gradient.setColorAt(positions_[0], a);
        gradient.setColorAt(positions_[1], b);
        return QBrush(gradient);
    }

    static QString type_name(int type)
    {
        switch (type) {
        case 1: return QStringLiteral("Radial");
        case 2: return QStringLiteral("Angle");
        case 3: return QStringLiteral("Reflected");
        case 4: return QStringLiteral("Diamond");
        case 0:
        default: return QStringLiteral("Linear");
        }
    }

    static void draw_checkerboard(QPainter &p, const QRect &rect)
    {
        const int cell = 12;
        for (int y = rect.top(); y < rect.bottom(); y += cell)
            for (int x = rect.left(); x < rect.right(); x += cell)
                p.fillRect(QRect(x, y, cell, cell).intersected(rect),
                           ((x / cell) + (y / cell)) % 2 ? QColor(62, 62, 62) : QColor(82, 82, 82));
    }

    int type_ = 0;
    QColor colors_[2] = {QColor(75, 110, 168), QColor(27, 27, 27)};
    double positions_[2] = {0.0, 1.0};
    double stop_opacities_[2] = {1.0, 1.0};
    double opacity_ = 1.0;
    double angle_ = 0.0;
    double center_x_ = 0.5;
    double center_y_ = 0.5;
    double scale_ = 1.0;
    int selected_stop_ = 0;
    bool dragging_stop_ = false;
};



static QColor rich_text_color_from_argb(uint32_t argb)
{
    return QColor((argb >> 16) & 0xFF,
                  (argb >> 8) & 0xFF,
                  argb & 0xFF,
                  (argb >> 24) & 0xFF);
}

static uint32_t rich_text_argb_from_color(const QColor &color)
{
    return ((uint32_t)color.alpha() << 24) |
           ((uint32_t)color.red() << 16) |
           ((uint32_t)color.green() << 8) |
           (uint32_t)color.blue();
}

static std::vector<QRectF> text_edit_selection_viewport_rects(const QTextEdit *editor)
{
    std::vector<QRectF> rects;
    if (!editor || !editor->textCursor().hasSelection())
        return rects;

    const QTextCursor cursor = editor->textCursor();
    const int selection_start = cursor.selectionStart();
    const int selection_end = cursor.selectionEnd();
    if (selection_start >= selection_end)
        return rects;

    const QTextDocument *doc = editor->document();
    const QAbstractTextDocumentLayout *doc_layout = doc ? doc->documentLayout() : nullptr;
    if (!doc || !doc_layout)
        return rects;

    const QPointF scroll_offset(editor->horizontalScrollBar() ? editor->horizontalScrollBar()->value() : 0.0,
                                editor->verticalScrollBar() ? editor->verticalScrollBar()->value() : 0.0);

    for (QTextBlock block = doc->findBlock(selection_start);
         block.isValid() && block.position() < selection_end;
         block = block.next()) {
        const QTextLayout *layout = block.layout();
        if (!layout)
            continue;

        const int block_start = block.position();
        const int block_text_end = block_start + std::max(0, block.length() - 1);
        const int block_selection_start = std::max(selection_start, block_start);
        const int block_selection_end = std::min(selection_end, block_text_end);
        if (block_selection_start >= block_selection_end)
            continue;

        const QRectF block_rect = doc_layout->blockBoundingRect(block);
        for (int line_index = 0; line_index < layout->lineCount(); ++line_index) {
            const QTextLine line = layout->lineAt(line_index);
            if (!line.isValid())
                continue;

            const int line_start = block_start + line.textStart();
            const int line_end = line_start + line.textLength();
            const int line_selection_start = std::max(block_selection_start, line_start);
            const int line_selection_end = std::min(block_selection_end, line_end);
            if (line_selection_start >= line_selection_end)
                continue;

            const qreal x1 = line.cursorToX(line_selection_start - block_start);
            const qreal x2 = line.cursorToX(line_selection_end - block_start);
            QRectF rect(block_rect.left() + std::min(x1, x2),
                        block_rect.top() + line.y(),
                        std::max<qreal>(1.0, std::abs(x2 - x1)),
                        std::max<qreal>(1.0, line.height()));
            rect.translate(-scroll_offset);
            rects.push_back(rect.adjusted(-1.0, 0.0, 1.0, 0.0));
        }
    }

    return rects;
}

enum RichTextCharFormatMask : uint32_t {
    RichTextCharFontFamily = 1u << 0,
    RichTextCharFontSize = 1u << 1,
    RichTextCharBold = 1u << 2,
    RichTextCharItalic = 1u << 3,
    RichTextCharUnderline = 1u << 4,
    RichTextCharStrikethrough = 1u << 5,
    RichTextCharTracking = 1u << 6,
    RichTextCharScaleX = 1u << 7,
    RichTextCharScaleY = 1u << 8,
    RichTextCharBaselineShift = 1u << 9,
    RichTextCharFillColor = 1u << 10,
    RichTextCharFontStyle = 1u << 11,
    RichTextCharKerning = 1u << 12,
    RichTextCharTextStyle = 1u << 13,
    RichTextCharLigatures = 1u << 14,
    RichTextCharStylisticAlternates = 1u << 15,
    RichTextCharFractions = 1u << 16,
    RichTextCharOpenTypeFeatures = 1u << 17,
    RichTextCharLanguage = 1u << 18,
};

enum RichTextFormatProperty {
    RichTextPropFontFamily = QTextFormat::UserProperty + 100,
    RichTextPropFontSize,
    RichTextPropBold,
    RichTextPropItalic,
    RichTextPropUnderline,
    RichTextPropStrikethrough,
    RichTextPropTracking,
    RichTextPropScaleX,
    RichTextPropScaleY,
    RichTextPropBaselineShift,
    RichTextPropFillType,
    RichTextPropFillColor,
    RichTextPropGradientType,
    RichTextPropGradientStartColor,
    RichTextPropGradientEndColor,
    RichTextPropGradientStartPos,
    RichTextPropGradientEndPos,
    RichTextPropGradientStartOpacity,
    RichTextPropGradientEndOpacity,
    RichTextPropGradientOpacity,
    RichTextPropGradientAngle,
    RichTextPropGradientCenterX,
    RichTextPropGradientCenterY,
    RichTextPropGradientScale,
    RichTextPropGradientFocalX,
    RichTextPropGradientFocalY,
    RichTextPropFontStyle,
    RichTextPropKerning,
    RichTextPropKerningMode,
    RichTextPropManualKerning,
    RichTextPropTextStyle,
    RichTextPropLigatures,
    RichTextPropStylisticAlternates,
    RichTextPropFractions,
    RichTextPropOpenTypeFeatures,
    RichTextPropLanguage,
};

static void store_rich_text_format_properties(QTextCharFormat &out, const RichTextCharFormat &format)
{
    out.setProperty(RichTextPropFontFamily, QString::fromStdString(format.font_family));
    out.setProperty(RichTextPropFontStyle, QString::fromStdString(format.font_style));
    out.setProperty(RichTextPropFontSize, format.font_size);
    out.setProperty(RichTextPropBold, format.bold);
    out.setProperty(RichTextPropItalic, format.italic);
    out.setProperty(RichTextPropUnderline, format.underline);
    out.setProperty(RichTextPropStrikethrough, format.strikethrough);
    out.setProperty(RichTextPropKerning, format.kerning);
    out.setProperty(RichTextPropKerningMode, format.kerning_mode);
    out.setProperty(RichTextPropManualKerning, (double)format.manual_kerning);
    out.setProperty(RichTextPropTracking, (double)format.tracking);
    out.setProperty(RichTextPropScaleX, (double)format.scale_x);
    out.setProperty(RichTextPropScaleY, (double)format.scale_y);
    out.setProperty(RichTextPropBaselineShift, (double)format.baseline_shift);
    out.setProperty(RichTextPropTextStyle, format.text_style);
    out.setProperty(RichTextPropLigatures, format.ligatures);
    out.setProperty(RichTextPropStylisticAlternates, format.stylistic_alternates);
    out.setProperty(RichTextPropFractions, format.fractions);
    out.setProperty(RichTextPropOpenTypeFeatures, format.opentype_features);
    out.setProperty(RichTextPropLanguage, QString::fromStdString(format.language));
    out.setProperty(RichTextPropFillType, format.fill.type);
    out.setProperty(RichTextPropFillColor, (uint)format.fill.color);
    out.setProperty(RichTextPropGradientType, format.fill.gradient_type);
    out.setProperty(RichTextPropGradientStartColor, (uint)format.fill.gradient_start_color);
    out.setProperty(RichTextPropGradientEndColor, (uint)format.fill.gradient_end_color);
    out.setProperty(RichTextPropGradientStartPos, (double)format.fill.gradient_start_pos);
    out.setProperty(RichTextPropGradientEndPos, (double)format.fill.gradient_end_pos);
    out.setProperty(RichTextPropGradientStartOpacity, (double)format.fill.gradient_start_opacity);
    out.setProperty(RichTextPropGradientEndOpacity, (double)format.fill.gradient_end_opacity);
    out.setProperty(RichTextPropGradientOpacity, (double)format.fill.gradient_opacity);
    out.setProperty(RichTextPropGradientAngle, (double)format.fill.gradient_angle);
    out.setProperty(RichTextPropGradientCenterX, (double)format.fill.gradient_center_x);
    out.setProperty(RichTextPropGradientCenterY, (double)format.fill.gradient_center_y);
    out.setProperty(RichTextPropGradientScale, (double)format.fill.gradient_scale);
    out.setProperty(RichTextPropGradientFocalX, (double)format.fill.gradient_focal_x);
    out.setProperty(RichTextPropGradientFocalY, (double)format.fill.gradient_focal_y);
}

static void store_editor_rich_text_format_properties_masked(QTextCharFormat &out, const RichTextCharFormat &format, uint32_t mask)
{
    if (mask & RichTextCharFontFamily)
        out.setProperty(RichTextPropFontFamily, QString::fromStdString(format.font_family));
    if (mask & RichTextCharFontStyle)
        out.setProperty(RichTextPropFontStyle, QString::fromStdString(format.font_style));
    if (mask & RichTextCharFontSize)
        out.setProperty(RichTextPropFontSize, format.font_size);
    if (mask & RichTextCharBold) out.setProperty(RichTextPropBold, format.bold);
    if (mask & RichTextCharItalic) out.setProperty(RichTextPropItalic, format.italic);
    if (mask & RichTextCharUnderline) out.setProperty(RichTextPropUnderline, format.underline);
    if (mask & RichTextCharStrikethrough) out.setProperty(RichTextPropStrikethrough, format.strikethrough);
    if (mask & RichTextCharKerning) {
        out.setProperty(RichTextPropKerning, format.kerning);
        out.setProperty(RichTextPropKerningMode, format.kerning_mode);
        out.setProperty(RichTextPropManualKerning, (double)format.manual_kerning);
    }
    if (mask & RichTextCharTracking) out.setProperty(RichTextPropTracking, (double)format.tracking);
    if (mask & RichTextCharScaleX) out.setProperty(RichTextPropScaleX, (double)format.scale_x);
    if (mask & RichTextCharScaleY) out.setProperty(RichTextPropScaleY, (double)format.scale_y);
    if (mask & RichTextCharBaselineShift) out.setProperty(RichTextPropBaselineShift, (double)format.baseline_shift);
    if (mask & RichTextCharTextStyle) out.setProperty(RichTextPropTextStyle, format.text_style);
    if (mask & RichTextCharLigatures) out.setProperty(RichTextPropLigatures, format.ligatures);
    if (mask & RichTextCharStylisticAlternates)
        out.setProperty(RichTextPropStylisticAlternates, format.stylistic_alternates);
    if (mask & RichTextCharFractions) out.setProperty(RichTextPropFractions, format.fractions);
    if (mask & RichTextCharOpenTypeFeatures) out.setProperty(RichTextPropOpenTypeFeatures, format.opentype_features);
    if (mask & RichTextCharLanguage)
        out.setProperty(RichTextPropLanguage, QString::fromStdString(format.language));
    if (mask & RichTextCharFillColor) {
        out.setProperty(RichTextPropFillType, format.fill.type);
        out.setProperty(RichTextPropFillColor, (uint)format.fill.color);
        out.setProperty(RichTextPropGradientType, format.fill.gradient_type);
        out.setProperty(RichTextPropGradientStartColor, (uint)format.fill.gradient_start_color);
        out.setProperty(RichTextPropGradientEndColor, (uint)format.fill.gradient_end_color);
        out.setProperty(RichTextPropGradientStartPos, (double)format.fill.gradient_start_pos);
        out.setProperty(RichTextPropGradientEndPos, (double)format.fill.gradient_end_pos);
        out.setProperty(RichTextPropGradientStartOpacity, (double)format.fill.gradient_start_opacity);
        out.setProperty(RichTextPropGradientEndOpacity, (double)format.fill.gradient_end_opacity);
        out.setProperty(RichTextPropGradientOpacity, (double)format.fill.gradient_opacity);
        out.setProperty(RichTextPropGradientAngle, (double)format.fill.gradient_angle);
        out.setProperty(RichTextPropGradientCenterX, (double)format.fill.gradient_center_x);
        out.setProperty(RichTextPropGradientCenterY, (double)format.fill.gradient_center_y);
        out.setProperty(RichTextPropGradientScale, (double)format.fill.gradient_scale);
        out.setProperty(RichTextPropGradientFocalX, (double)format.fill.gradient_focal_x);
        out.setProperty(RichTextPropGradientFocalY, (double)format.fill.gradient_focal_y);
    }
}

static void apply_editor_rich_text_extended_font_properties(QFont &font, const RichTextCharFormat &format)
{
    if (!format.font_style.empty())
        font.setStyleName(QString::fromStdString(format.font_style));
    font.setKerning(format.kerning_mode != 2 && format.kerning);
    font.setLetterSpacing(QFont::AbsoluteSpacing,
                          format.tracking + (format.kerning_mode == 2 ? format.manual_kerning : 0.0f));
    font.setStretch(std::clamp((int)std::round(format.scale_x * 100.0f), 1, 4000));
    font.setCapitalization(QFont::MixedCase);
    if (format.text_style == 1)
        font.setCapitalization(QFont::AllUppercase);
    else if (format.text_style == 2)
        font.setCapitalization(QFont::SmallCaps);
    if (format.text_style == 3 || format.text_style == 4)
        font.setPixelSize(std::max(1, (int)std::round(font.pixelSize() * 0.65)));
    if (!format.ligatures)
        font.setStyleStrategy((QFont::StyleStrategy)(font.styleStrategy() | QFont::PreferNoShaping));
}

static void apply_editor_rich_text_extended_char_format(QTextCharFormat &out, const RichTextCharFormat &format)
{
    if (format.text_style == 3)
        out.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    else if (format.text_style == 4)
        out.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    else
        out.setVerticalAlignment(QTextCharFormat::AlignNormal);
    out.setFontKerning(format.kerning_mode != 2 && format.kerning);
    out.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    out.setFontLetterSpacing(format.tracking + (format.kerning_mode == 2 ? format.manual_kerning : 0.0f));
}

static RichTextCharFormat rich_text_format_from_qtext_format(const QTextCharFormat &fmt,
                                                             const RichTextCharFormat &fallback,
                                                             double visual_scale)
{
    RichTextCharFormat out = fallback;
    QFont font = fmt.font();
    out.font_family = fmt.property(RichTextPropFontFamily).toString().isEmpty()
                          ? (!font.family().isEmpty() ? font.family().toStdString() : out.font_family)
                          : fmt.property(RichTextPropFontFamily).toString().toStdString();
    if (fmt.hasProperty(RichTextPropFontStyle))
        out.font_style = fmt.property(RichTextPropFontStyle).toString().toStdString();
    else if (!font.styleName().isEmpty())
        out.font_style = font.styleName().toStdString();
    int px = font.pixelSize();
    if (px <= 0 && font.pointSizeF() > 0.0) px = (int)std::round(font.pointSizeF());
    out.font_size = fmt.hasProperty(RichTextPropFontSize)
                        ? fmt.property(RichTextPropFontSize).toInt()
                        : (px > 0 ? std::max(1, (int)std::round(px / std::max(0.0001, visual_scale))) : out.font_size);
    out.bold = fmt.hasProperty(RichTextPropBold) ? fmt.property(RichTextPropBold).toBool() : (fmt.fontWeight() >= QFont::Bold);
    out.italic = fmt.hasProperty(RichTextPropItalic) ? fmt.property(RichTextPropItalic).toBool() : fmt.fontItalic();
    out.underline = fmt.hasProperty(RichTextPropUnderline) ? fmt.property(RichTextPropUnderline).toBool() : fmt.fontUnderline();
    out.strikethrough = fmt.hasProperty(RichTextPropStrikethrough) ? fmt.property(RichTextPropStrikethrough).toBool() : fmt.fontStrikeOut();
    if (fmt.hasProperty(RichTextPropKerning)) out.kerning = fmt.property(RichTextPropKerning).toBool();
    else out.kerning = font.kerning();
    if (fmt.hasProperty(RichTextPropKerningMode)) out.kerning_mode = fmt.property(RichTextPropKerningMode).toInt();
    if (fmt.hasProperty(RichTextPropManualKerning)) out.manual_kerning = (float)fmt.property(RichTextPropManualKerning).toDouble();
    if (fmt.hasProperty(RichTextPropTracking)) out.tracking = (float)fmt.property(RichTextPropTracking).toDouble();
    if (fmt.hasProperty(RichTextPropScaleX)) out.scale_x = (float)fmt.property(RichTextPropScaleX).toDouble();
    if (fmt.hasProperty(RichTextPropScaleY)) out.scale_y = (float)fmt.property(RichTextPropScaleY).toDouble();
    if (fmt.hasProperty(RichTextPropBaselineShift)) out.baseline_shift = (float)fmt.property(RichTextPropBaselineShift).toDouble();
    if (fmt.hasProperty(RichTextPropTextStyle)) out.text_style = fmt.property(RichTextPropTextStyle).toInt();
    else if (font.capitalization() == QFont::AllUppercase) out.text_style = 1;
    else if (font.capitalization() == QFont::SmallCaps) out.text_style = 2;
    else if (fmt.verticalAlignment() == QTextCharFormat::AlignSuperScript) out.text_style = 3;
    else if (fmt.verticalAlignment() == QTextCharFormat::AlignSubScript) out.text_style = 4;
    if (fmt.hasProperty(RichTextPropLigatures)) out.ligatures = fmt.property(RichTextPropLigatures).toBool();
    if (fmt.hasProperty(RichTextPropStylisticAlternates)) out.stylistic_alternates = fmt.property(RichTextPropStylisticAlternates).toBool();
    if (fmt.hasProperty(RichTextPropFractions)) out.fractions = fmt.property(RichTextPropFractions).toBool();
    if (fmt.hasProperty(RichTextPropOpenTypeFeatures)) out.opentype_features = fmt.property(RichTextPropOpenTypeFeatures).toBool();
    if (fmt.hasProperty(RichTextPropLanguage)) out.language = fmt.property(RichTextPropLanguage).toString().toStdString();
    if (fmt.hasProperty(RichTextPropFillType)) out.fill.type = fmt.property(RichTextPropFillType).toInt();
    if (fmt.hasProperty(RichTextPropFillColor)) out.fill.color = fmt.property(RichTextPropFillColor).toUInt();
    else if (fmt.foreground().style() != Qt::NoBrush) {
        const QColor c = fmt.foreground().color();
        if (c.isValid() && c.alpha() > 0)
            out.fill.color = rich_text_argb_from_color(c);
    }
    if (fmt.hasProperty(RichTextPropGradientType)) out.fill.gradient_type = fmt.property(RichTextPropGradientType).toInt();
    if (fmt.hasProperty(RichTextPropGradientStartColor)) out.fill.gradient_start_color = fmt.property(RichTextPropGradientStartColor).toUInt();
    if (fmt.hasProperty(RichTextPropGradientEndColor)) out.fill.gradient_end_color = fmt.property(RichTextPropGradientEndColor).toUInt();
    if (fmt.hasProperty(RichTextPropGradientStartPos)) out.fill.gradient_start_pos = (float)fmt.property(RichTextPropGradientStartPos).toDouble();
    if (fmt.hasProperty(RichTextPropGradientEndPos)) out.fill.gradient_end_pos = (float)fmt.property(RichTextPropGradientEndPos).toDouble();
    if (fmt.hasProperty(RichTextPropGradientStartOpacity)) out.fill.gradient_start_opacity = (float)fmt.property(RichTextPropGradientStartOpacity).toDouble();
    if (fmt.hasProperty(RichTextPropGradientEndOpacity)) out.fill.gradient_end_opacity = (float)fmt.property(RichTextPropGradientEndOpacity).toDouble();
    if (fmt.hasProperty(RichTextPropGradientOpacity)) out.fill.gradient_opacity = (float)fmt.property(RichTextPropGradientOpacity).toDouble();
    if (fmt.hasProperty(RichTextPropGradientAngle)) out.fill.gradient_angle = (float)fmt.property(RichTextPropGradientAngle).toDouble();
    if (fmt.hasProperty(RichTextPropGradientCenterX)) out.fill.gradient_center_x = (float)fmt.property(RichTextPropGradientCenterX).toDouble();
    if (fmt.hasProperty(RichTextPropGradientCenterY)) out.fill.gradient_center_y = (float)fmt.property(RichTextPropGradientCenterY).toDouble();
    if (fmt.hasProperty(RichTextPropGradientScale)) out.fill.gradient_scale = (float)fmt.property(RichTextPropGradientScale).toDouble();
    if (fmt.hasProperty(RichTextPropGradientFocalX)) out.fill.gradient_focal_x = (float)fmt.property(RichTextPropGradientFocalX).toDouble();
    if (fmt.hasProperty(RichTextPropGradientFocalY)) out.fill.gradient_focal_y = (float)fmt.property(RichTextPropGradientFocalY).toDouble();
    return out;
}

static QTextCharFormat qtext_format_from_rich_text_format(const RichTextCharFormat &format, double visual_scale)
{
    QTextCharFormat out;
    QFont font(QString::fromStdString(format.font_family));
    font.setPixelSize(std::max(1, (int)std::round(format.font_size * visual_scale)));
    font.setBold(format.bold);
    font.setItalic(format.italic);
    font.setUnderline(format.underline);
    font.setStrikeOut(format.strikethrough);
    apply_editor_rich_text_extended_font_properties(font, format);
    out.setFont(font);
    out.setFontUnderline(format.underline);
    out.setFontStrikeOut(format.strikethrough);
    apply_editor_rich_text_extended_char_format(out, format);
    store_rich_text_format_properties(out, format);
    QColor color = rich_text_color_from_argb(format.fill.color);
    color.setAlpha(0);
    out.setForeground(color);
    return out;
}

static void populate_qtext_document_from_rich_text(QTextDocument *doc, const RichTextDocument &model, double visual_scale)
{
    if (!doc) return;
    QSignalBlocker blocker(doc);
    doc->setPlainText(QString::fromStdString(model.plain_text));
    QTextCursor all(doc);
    all.select(QTextCursor::Document);
    all.mergeCharFormat(qtext_format_from_rich_text_format(model.default_format, visual_scale));
    for (const auto &range : model.ranges) {
        if (range.length == 0 || range.start >= model.plain_text.size()) continue;
        QTextCursor cursor(doc);
        cursor.setPosition((int)std::min(range.start, model.plain_text.size()));
        cursor.setPosition((int)std::min(range.start + range.length, model.plain_text.size()), QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(qtext_format_from_rich_text_format(range.format, visual_scale));
    }
}

static void populate_qtext_document_from_plain_layer_text(QTextDocument *doc, const std::string &text,
                                                          const RichTextCharFormat &format, double visual_scale)
{
    if (!doc) return;
    QSignalBlocker blocker(doc);
    doc->setPlainText(QString::fromStdString(text));
    QTextCursor all(doc);
    all.select(QTextCursor::Document);
    all.mergeCharFormat(qtext_format_from_rich_text_format(format, visual_scale));
}


static bool rich_text_fills_equal(const RichTextFill &a, const RichTextFill &b)
{
    return a.type == b.type && a.color == b.color && a.gradient_type == b.gradient_type &&
           a.gradient_start_color == b.gradient_start_color &&
           a.gradient_end_color == b.gradient_end_color &&
           std::abs(a.gradient_start_pos - b.gradient_start_pos) < 0.0001f &&
           std::abs(a.gradient_end_pos - b.gradient_end_pos) < 0.0001f &&
           std::abs(a.gradient_start_opacity - b.gradient_start_opacity) < 0.0001f &&
           std::abs(a.gradient_end_opacity - b.gradient_end_opacity) < 0.0001f &&
           std::abs(a.gradient_opacity - b.gradient_opacity) < 0.0001f &&
           std::abs(a.gradient_angle - b.gradient_angle) < 0.0001f &&
           std::abs(a.gradient_center_x - b.gradient_center_x) < 0.0001f &&
           std::abs(a.gradient_center_y - b.gradient_center_y) < 0.0001f &&
           std::abs(a.gradient_scale - b.gradient_scale) < 0.0001f &&
           std::abs(a.gradient_focal_x - b.gradient_focal_x) < 0.0001f &&
           std::abs(a.gradient_focal_y - b.gradient_focal_y) < 0.0001f;
}

static bool rich_text_char_formats_equal(const RichTextCharFormat &a, const RichTextCharFormat &b)
{
    return a.font_family == b.font_family && a.font_style == b.font_style &&
           a.font_size == b.font_size && a.bold == b.bold && a.italic == b.italic &&
           a.underline == b.underline && a.strikethrough == b.strikethrough &&
           a.kerning == b.kerning && a.kerning_mode == b.kerning_mode &&
           std::abs(a.manual_kerning - b.manual_kerning) < 0.0001f &&
           std::abs(a.tracking - b.tracking) < 0.0001f &&
           std::abs(a.scale_x - b.scale_x) < 0.0001f &&
           std::abs(a.scale_y - b.scale_y) < 0.0001f &&
           std::abs(a.baseline_shift - b.baseline_shift) < 0.0001f &&
           a.text_style == b.text_style && a.ligatures == b.ligatures &&
           a.stylistic_alternates == b.stylistic_alternates && a.fractions == b.fractions &&
           a.opentype_features == b.opentype_features && a.language == b.language &&
           rich_text_fills_equal(a.fill, b.fill);
}

static bool rich_text_ranges_equal(const std::vector<RichTextRange> &a, const std::vector<RichTextRange> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const RichTextRange &x = a[i];
        const RichTextRange &y = b[i];
        if (x.start != y.start || x.length != y.length) return false;
        if (!rich_text_char_formats_equal(x.format, y.format)) return false;
    }
    return true;
}

static RichTextParagraphFormat layer_paragraph_format_for_editor(const Layer &layer)
{
    RichTextParagraphFormat f;
    f.align_h = layer.align_h;
    f.align_v = layer.align_v;
    f.indent_left = layer.paragraph_indent_left;
    f.indent_right = layer.paragraph_indent_right;
    f.indent_first_line = layer.paragraph_indent_first_line;
    f.space_before = layer.paragraph_space_before;
    f.space_after = layer.paragraph_space_after;
    f.hyphenate = layer.paragraph_hyphenate;
    return f;
}

static RichTextDocument rich_text_document_from_qtext_document(const QTextDocument *doc, const Layer &layer, double visual_scale, const QTextCursor &text_cursor = QTextCursor())
{
    RichTextDocument model = layer.rich_text.empty() ? rich_text_document_from_layer_defaults(layer) : layer.rich_text;
    if (!doc) return model;
    model.plain_text = doc->toPlainText().toStdString();
    model.default_paragraph_format = layer_paragraph_format_for_editor(layer);
    model.ranges.clear();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || fragment.text().isEmpty()) continue;
            RichTextRange range;
            range.start = (size_t)std::max(0, fragment.position());
            range.length = (size_t)fragment.text().size();
            range.format = rich_text_format_from_qtext_format(fragment.charFormat(), model.default_format, visual_scale);
            model.ranges.push_back(range);
        }
    }
    if (!text_cursor.isNull()) {
        model.selection.anchor = (size_t)std::max(0, text_cursor.anchor());
        model.selection.head = (size_t)std::max(0, text_cursor.position());
    } else {
        model.selection.anchor = 0;
        model.selection.head = 0;
    }
    model.normalize();
    return model;
}



struct RichTextCharFormatSummary {
    RichTextCharFormat format;
    uint32_t mixed = 0;
    bool valid = false;
};

static RichTextCharFormat layer_char_format_for_editor(const Layer &layer)
{
    RichTextCharFormat f;
    f.font_family = layer.font_family;
    f.font_style = layer.font_style;
    f.font_size = layer.font_size;
    f.bold = layer.font_bold;
    f.italic = layer.font_italic;
    f.underline = layer.text_underline;
    f.strikethrough = layer.text_strikethrough;
    f.kerning = layer.font_kerning;
    f.kerning_mode = layer.kerning_mode;
    f.manual_kerning = layer.manual_kerning;
    f.tracking = layer.char_tracking;
    f.scale_x = layer.char_scale_x;
    f.scale_y = layer.char_scale_y;
    f.baseline_shift = layer.baseline_shift;
    f.text_style = layer.text_style;
    f.ligatures = layer.text_ligatures;
    f.stylistic_alternates = layer.text_stylistic_alternates;
    f.fractions = layer.text_fractions;
    f.opentype_features = layer.text_opentype_features;
    f.language = layer.text_language;
    f.fill.type = layer.fill_type;
    f.fill.color = layer.text_color;
    f.fill.gradient_type = layer.gradient_type;
    f.fill.gradient_start_color = layer.gradient_start_color;
    f.fill.gradient_end_color = layer.gradient_end_color;
    f.fill.gradient_start_pos = layer.gradient_start_pos;
    f.fill.gradient_end_pos = layer.gradient_end_pos;
    f.fill.gradient_start_opacity = layer.gradient_start_opacity;
    f.fill.gradient_end_opacity = layer.gradient_end_opacity;
    f.fill.gradient_opacity = layer.gradient_opacity;
    f.fill.gradient_angle = layer.gradient_angle;
    f.fill.gradient_center_x = layer.gradient_center_x;
    f.fill.gradient_center_y = layer.gradient_center_y;
    f.fill.gradient_scale = layer.gradient_scale;
    f.fill.gradient_focal_x = layer.gradient_focal_x;
    f.fill.gradient_focal_y = layer.gradient_focal_y;
    return f;
}

static bool format_value_differs(const RichTextCharFormat &a, const RichTextCharFormat &b, uint32_t bit)
{
    switch (bit) {
    case RichTextCharFontFamily: return a.font_family != b.font_family;
    case RichTextCharFontStyle: return a.font_style != b.font_style;
    case RichTextCharFontSize: return a.font_size != b.font_size;
    case RichTextCharBold: return a.bold != b.bold;
    case RichTextCharItalic: return a.italic != b.italic;
    case RichTextCharUnderline: return a.underline != b.underline;
    case RichTextCharStrikethrough: return a.strikethrough != b.strikethrough;
    case RichTextCharKerning:
        return a.kerning != b.kerning || a.kerning_mode != b.kerning_mode ||
               std::abs(a.manual_kerning - b.manual_kerning) >= 0.0001f;
    case RichTextCharTracking: return a.tracking != b.tracking;
    case RichTextCharScaleX: return a.scale_x != b.scale_x;
    case RichTextCharScaleY: return a.scale_y != b.scale_y;
    case RichTextCharBaselineShift: return a.baseline_shift != b.baseline_shift;
    case RichTextCharFillColor: return !rich_text_fills_equal(a.fill, b.fill);
    case RichTextCharTextStyle: return a.text_style != b.text_style;
    case RichTextCharLigatures: return a.ligatures != b.ligatures;
    case RichTextCharStylisticAlternates: return a.stylistic_alternates != b.stylistic_alternates;
    case RichTextCharFractions: return a.fractions != b.fractions;
    case RichTextCharOpenTypeFeatures: return a.opentype_features != b.opentype_features;
    case RichTextCharLanguage: return a.language != b.language;
    default: return false;
    }
}

static RichTextCharFormat format_at_offset(const RichTextDocument &doc, size_t offset)
{
    RichTextCharFormat f = doc.default_format;
    for (const auto &range : doc.ranges) {
        if (offset >= range.start && offset < range.start + range.length)
            f = range.format;
    }
    return f;
}

static RichTextCharFormatSummary summarize_rich_text_char_format(const Layer &layer, bool active_selection)
{
    RichTextDocument doc = layer.rich_text.empty() ? rich_text_document_from_layer_defaults(layer) : layer.rich_text;
    /*
     * The layer scalar text fields can represent the most recently applied
     * cursor style while editing.  Do not sync them into an existing rich text
     * document for inspection, or uncovered/default spans would be reported as
     * that cached style instead of the document's actual stored style.
     */
    doc.normalize();
    RichTextCharFormatSummary summary;
    const size_t text_len = doc.plain_text.size();
    size_t start = 0;
    size_t end = text_len;
    if (active_selection) {
        start = std::min(doc.selection.anchor, doc.selection.head);
        end = std::max(doc.selection.anchor, doc.selection.head);
        if (start == end) {
            const size_t sample = text_len == 0 ? 0 : (start < text_len ? start : text_len - 1);
            summary.format = format_at_offset(doc, sample);
            summary.valid = true;
            return summary;
        }
    }
    if (text_len == 0) {
        summary.format = doc.default_format;
        summary.valid = true;
        return summary;
    }
    start = std::min(start, text_len);
    end = std::min(end, text_len);
    if (start >= end) {
        summary.format = doc.default_format;
        summary.valid = true;
        return summary;
    }
    summary.format = format_at_offset(doc, start);
    summary.valid = true;
    constexpr uint32_t bits[] = {RichTextCharFontFamily, RichTextCharFontStyle, RichTextCharFontSize,
        RichTextCharBold, RichTextCharItalic, RichTextCharUnderline, RichTextCharStrikethrough,
        RichTextCharKerning, RichTextCharTracking, RichTextCharScaleX, RichTextCharScaleY,
        RichTextCharBaselineShift, RichTextCharFillColor, RichTextCharTextStyle, RichTextCharLigatures,
        RichTextCharStylisticAlternates, RichTextCharFractions, RichTextCharOpenTypeFeatures,
        RichTextCharLanguage};
    for (size_t i = start + 1; i < end; ++i) {
        RichTextCharFormat f = format_at_offset(doc, i);
        for (uint32_t bit : bits)
            if (format_value_differs(summary.format, f, bit)) summary.mixed |= bit;
    }
    return summary;
}

static void merge_format_bits(RichTextCharFormat &dst, const RichTextCharFormat &src, uint32_t mask)
{
    if (mask & RichTextCharFontFamily) dst.font_family = src.font_family;
    if (mask & RichTextCharFontStyle) dst.font_style = src.font_style;
    if (mask & RichTextCharFontSize) dst.font_size = src.font_size;
    if (mask & RichTextCharBold) dst.bold = src.bold;
    if (mask & RichTextCharItalic) dst.italic = src.italic;
    if (mask & RichTextCharUnderline) dst.underline = src.underline;
    if (mask & RichTextCharStrikethrough) dst.strikethrough = src.strikethrough;
    if (mask & RichTextCharKerning) {
        dst.kerning = src.kerning;
        dst.kerning_mode = src.kerning_mode;
        dst.manual_kerning = src.manual_kerning;
    }
    if (mask & RichTextCharTracking) dst.tracking = src.tracking;
    if (mask & RichTextCharScaleX) dst.scale_x = src.scale_x;
    if (mask & RichTextCharScaleY) dst.scale_y = src.scale_y;
    if (mask & RichTextCharBaselineShift) dst.baseline_shift = src.baseline_shift;
    if (mask & RichTextCharTextStyle) dst.text_style = src.text_style;
    if (mask & RichTextCharLigatures) dst.ligatures = src.ligatures;
    if (mask & RichTextCharStylisticAlternates) dst.stylistic_alternates = src.stylistic_alternates;
    if (mask & RichTextCharFractions) dst.fractions = src.fractions;
    if (mask & RichTextCharOpenTypeFeatures) dst.opentype_features = src.opentype_features;
    if (mask & RichTextCharLanguage) dst.language = src.language;
    if (mask & RichTextCharFillColor) { dst.fill.type = src.fill.type; dst.fill.color = src.fill.color; }
}

static void apply_rich_text_format_to_layer_range(Layer &layer, const RichTextCharFormat &format, uint32_t mask, bool active_selection)
{
    if (layer.type != LayerType::Text && layer.type != LayerType::Ticker) return;
    if (layer.rich_text.empty())
        layer.rich_text = rich_text_document_from_layer_defaults(layer);
    RichTextDocument &doc = layer.rich_text;
    doc.default_paragraph_format = layer_paragraph_format_for_editor(layer);
    doc.normalize();
    const size_t text_len = doc.plain_text.size();
    size_t start = active_selection ? std::min(doc.selection.anchor, doc.selection.head) : 0;
    size_t end = active_selection ? std::max(doc.selection.anchor, doc.selection.head) : text_len;
    start = std::min(start, text_len);
    end = std::min(end, text_len);
    if (start == end) {
        merge_format_bits(doc.default_format, format, mask);
        if (text_len == 0) return;
        const size_t sample = start > 0 ? start - 1 : 0;
        RichTextCharFormat next = format_at_offset(doc, sample);
        merge_format_bits(next, format, mask);
        doc.ranges.push_back({sample, 1, next});
        doc.normalize();
        return;
    }
    std::vector<RichTextRange> next;
    next.reserve(doc.ranges.size() + 3);
    size_t cursor = start;
    auto append_range = [&next](size_t s, size_t len, const RichTextCharFormat &fmt) {
        if (len > 0) next.push_back({s, len, fmt});
    };
    for (const auto &range : doc.ranges) {
        const size_t rs = range.start;
        const size_t re = std::min(text_len, range.start + range.length);
        if (re <= start || rs >= end) {
            next.push_back(range);
            continue;
        }
        append_range(rs, start > rs ? start - rs : 0, range.format);
        if (cursor < std::max(start, rs)) {
            RichTextCharFormat gap = doc.default_format;
            merge_format_bits(gap, format, mask);
            append_range(cursor, std::max(start, rs) - cursor, gap);
        }
        RichTextCharFormat changed = range.format;
        merge_format_bits(changed, format, mask);
        const size_t is = std::max(start, rs);
        const size_t ie = std::min(end, re);
        append_range(is, ie - is, changed);
        cursor = std::max(cursor, ie);
        append_range(end, re > end ? re - end : 0, range.format);
    }
    if (cursor < end) {
        RichTextCharFormat changed = doc.default_format;
        merge_format_bits(changed, format, mask);
        append_range(cursor, end - cursor, changed);
    }
    doc.ranges = std::move(next);
    doc.normalize();
    layer.rich_text_html.clear();
}

static QString rich_text_plain_text(const std::string &html)
{
    if (html.empty()) return QString();
    QTextDocument doc;
    doc.setHtml(QString::fromStdString(html));
    return doc.toPlainText();
}

static QString rich_text_html_with_replaced_plain_text(const std::string &html, const QString &plain)
{
    if (html.empty() || plain.isEmpty())
        return QString();

    QTextDocument source;
    source.setHtml(QString::fromStdString(html));
    QTextDocument replacement;
    replacement.setDocumentMargin(source.documentMargin());
    replacement.setDefaultFont(source.defaultFont());
    replacement.setDefaultStyleSheet(source.defaultStyleSheet());
    replacement.setDefaultTextOption(source.defaultTextOption());
    replacement.setTextWidth(source.textWidth());

    QTextCursor out(&replacement);
    QTextCursor source_cursor(&source);
    QTextBlock source_block = source.begin();
    QTextBlockFormat current_block_format = source_block.isValid() ? source_block.blockFormat() : QTextBlockFormat();
    const int max_source_pos = std::max(0, source.characterCount() - 1);
    int source_pos = 0;
    bool at_block_start = true;

    for (const QChar ch : plain) {
        if (ch == QLatin1Char('\r'))
            continue;
        source_cursor.setPosition(std::min(source_pos, max_source_pos));
        if (ch == QLatin1Char('\n')) {
            out.insertBlock(current_block_format);
            if (source_block.isValid()) {
                source_block = source_block.next();
                if (source_block.isValid())
                    current_block_format = source_block.blockFormat();
            }
            at_block_start = true;
            ++source_pos;
            continue;
        }

        QTextCharFormat fmt = source_cursor.charFormat();
        if (at_block_start) {
            out.setBlockFormat(current_block_format);
            at_block_start = false;
        }
        out.insertText(QString(ch), fmt);
        ++source_pos;
    }

    return replacement.toHtml();
}

static double title_manual_screenshot_time(const Title &title)
{
    if (title.playback_mode == 1)
        return std::clamp(title.loop_start, 0.0, title.duration);
    if (title.playback_mode == 2)
        return std::clamp(title.pause_time, 0.0, title.duration);
    return std::clamp(title.duration * 0.5, 0.0, title.duration);
}

static std::string title_manual_screenshot_png_base64(const Title &title)
{
    const QImage screenshot = render_title_to_image(title, title_manual_screenshot_time(title));
    if (screenshot.isNull())
        return {};

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!screenshot.save(&buffer, "PNG"))
        return {};

    const QByteArray encoded = png.toBase64();
    return std::string(encoded.constData(), (size_t)encoded.size());
}

} // namespace

/* ────────────────────────────────────────────────────────────────── */
/*  Dark AE-style palette constants                                   */
/* ────────────────────────────────────────────────────────────────── */
static const QColor C_BG_DARK  { 0x1a1a1a };
static const QColor C_BG_MID   { 0x252525 };
static const QColor C_BG_LIGHT { 0x2e2e2e };
static const QColor C_ACCENT   { 0x0078d4 };

static bool editor_focus_accepts_text(QWidget *widget);

/* OBS safe area margins: Rec. ITU-R BT.1848-1 / EBU R 95. */
static constexpr double OBS_ACTION_SAFE_PERCENT = 0.035;
static constexpr double OBS_GRAPHICS_SAFE_PERCENT = 0.05;

/* Canvas editing controls are measured in view pixels so object scale never
 * changes their apparent size or proportions. */
static constexpr double CANVAS_CONTROL_SIZE_PX = 8.0;
static constexpr double CANVAS_CONTROL_HIT_RADIUS_PX = 8.0;
static constexpr double CANVAS_ROTATE_HIT_RADIUS_PX = 24.0;
static constexpr double CANVAS_ORIGIN_RADIUS_PX = 3.6;
static constexpr double CANVAS_GRADIENT_HANDLE_RADIUS_PX = 5.0;
static constexpr double CANVAS_ROTATION_SNAP_DEGREES = 15.0;

static bool editor_image_path_is_svg(const QString &path)
{
    return path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive) ||
           path.endsWith(QStringLiteral(".svgz"), Qt::CaseInsensitive);
}

static double radians_to_degrees(double radians)
{
    return radians * 180.0 / 3.14159265358979323846;
}

static double degrees_to_radians(double degrees)
{
    return degrees * 3.14159265358979323846 / 180.0;
}

static double normalize_degrees(double degrees)
{
    while (degrees > 180.0) degrees -= 360.0;
    while (degrees <= -180.0) degrees += 360.0;
    return degrees;
}

static QPointF rotate_point_around(const QPointF &point, const QPointF &pivot, double degrees)
{
    double radians = degrees_to_radians(degrees);
    double c = std::cos(radians);
    double ss = std::sin(radians);
    double dx = point.x() - pivot.x();
    double dy = point.y() - pivot.y();
    return QPointF(pivot.x() + dx * c - dy * ss,
                   pivot.y() + dx * ss + dy * c);
}

static QCursor canvas_rotation_cursor()
{
    static const QCursor cursor = []() {
        QPixmap pixmap(24, 24);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(255, 255, 255), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawArc(QRectF(5.0, 4.0, 14.0, 14.0), 35 * 16, 285 * 16);
        QPolygonF arrow;
        arrow << QPointF(17.0, 4.0) << QPointF(22.0, 5.5) << QPointF(18.8, 10.0);
        painter.setBrush(QColor(255, 255, 255));
        painter.drawPolygon(arrow);
        painter.setPen(QPen(QColor(0, 0, 0, 170), 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(5.0, 4.0, 14.0, 14.0), 35 * 16, 285 * 16);
        return QCursor(pixmap, 12, 12);
    }();
    return cursor;
}

static QSize editor_image_intrinsic_size(const QString &path)
{
    if (editor_image_path_is_svg(path)) {
        QSvgRenderer renderer(path);
        if (!renderer.isValid()) return QSize();
        QSize size = renderer.defaultSize();
        if (!size.isValid() || size.isEmpty())
            size = renderer.viewBox().size();
        return size;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    if (size.isValid() && !size.isEmpty())
        return size;

    QImage image = reader.read();
    return image.isNull() ? QSize() : image.size();
}

static QImage editor_load_layer_image(const QString &path, const QSize &fallback_size = QSize())
{
    if (editor_image_path_is_svg(path)) {
        QSvgRenderer renderer(path);
        if (!renderer.isValid()) return QImage();

        QSize size = fallback_size.isValid() && !fallback_size.isEmpty()
            ? fallback_size
            : renderer.defaultSize();
        if (!size.isValid() || size.isEmpty())
            size = renderer.viewBox().size();
        if (!size.isValid() || size.isEmpty())
            size = QSize(256, 256);

        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        renderer.render(&painter);
        return image;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
}

static QPixmap editor_image_preview_pixmap(const QString &path, const QSize &size)
{
    const QSize target = size.isValid() && !size.isEmpty() ? size : QSize(104, 104);
    QPixmap pixmap(target);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    const QPalette pal = qApp ? qApp->palette() : QPalette();
    const bool dark = pal.color(QPalette::Window).lightness() < 128;
    const QColor checker_a = dark ? QColor(62, 62, 62) : QColor(224, 224, 224);
    const QColor checker_b = dark ? QColor(42, 42, 42) : QColor(188, 188, 188);
    constexpr int checker = 8;
    for (int y = 0; y < target.height(); y += checker) {
        for (int x = 0; x < target.width(); x += checker) {
            painter.fillRect(QRect(x, y, checker, checker),
                             (((x / checker) + (y / checker)) & 1) ? checker_b : checker_a);
        }
    }

    const QString trimmed_path = path.trimmed();
    if (!trimmed_path.isEmpty()) {
        const QSize image_target(std::max(1, target.width() - 8), std::max(1, target.height() - 8));
        QImage image = editor_load_layer_image(trimmed_path, image_target);
        if (!image.isNull()) {
            image = image.scaled(image_target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QPoint top_left((target.width() - image.width()) / 2,
                                  (target.height() - image.height()) / 2);
            painter.drawImage(top_left, image);
        }
    }

    painter.setPen(pal.color(QPalette::Mid));
    painter.drawRect(QRect(QPoint(0, 0), target).adjusted(0, 0, -1, -1));
    return pixmap;
}

static void set_image_preview_label(QLabel *label, const QString &path)
{
    if (!label) return;
    const QSize size = label->size().isValid() && !label->size().isEmpty() ? label->size() : QSize(104, 104);
    label->setPixmap(editor_image_preview_pixmap(path, size));
}

static const QColor C_TEXT     { 0xcccccc };
static const QColor C_RULER    { 0x1e1e1e };
static const QColor C_KF_DOT   { 0xffd23f };

static QIcon keyframe_diamond_icon(bool active, bool outlined = false)
{
    if (active)
        return obsgs_icon("keyframe-active.svg");
    if (outlined)
        return obsgs_icon("keyframe-outline.svg", C_KF_DOT);
    return obsgs_icon("keyframe-inactive.svg");
}





static double obs_frame_rate()
{
    struct obs_video_info ovi = {};
    if (obs_get_video_info(&ovi) && ovi.fps_den > 0 && ovi.fps_num > 0)
        return (double)ovi.fps_num / (double)ovi.fps_den;
    return 30.0;
}

static double obs_frame_duration()
{
    return 1.0 / std::max(1.0, obs_frame_rate());
}

static double snap_to_obs_frame(double t)
{
    double fd = obs_frame_duration();
    return std::round(t / fd) * fd;
}

static QString format_timecode(double t)
{
    double fps_d = obs_frame_rate();
    int fps = std::max(1, (int)std::round(fps_d));
    int total_frames = std::max(0, (int)std::round(t * fps_d));
    int frames = total_frames % fps;
    int total_seconds = total_frames / fps;
    int seconds = total_seconds % 60;
    int minutes = (total_seconds / 60) % 60;
    int hours = total_seconds / 3600;
    return QString("%1:%2:%3:%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(frames, 2, 10, QChar('0'));
}

static QColor layer_type_color(LayerType type)
{
    switch (type) {
    case LayerType::Text:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::TextLayer);
    case LayerType::Clock:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::ClockLayer);
    case LayerType::Ticker:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::TickerLayer);
    case LayerType::SolidRect:
    case LayerType::Shape:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::ObjectLayer);
    case LayerType::Image:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::ImageLayer);
    }
    return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::ObjectLayer);
}

static QColor layer_color(const Layer &layer, int /*row*/)
{
    return layer_type_color(layer.type);
}

static QString layer_type_short(LayerType type)
{
    switch (type) {
    case LayerType::Text: return "T";
    case LayerType::Clock: return "⏱";
    case LayerType::Ticker: return "↔";
    case LayerType::SolidRect: return "■";
    case LayerType::Image: return "▧";
    case LayerType::Shape: return "◆";
    }
    return "•";
}

static QIcon obs_icon(const char *file_name)
{
    return obsgs_icon(file_name);
}

constexpr double kToolIconPi = 3.14159265358979323846;

static QString shape_display_name(ShapeType shape_type)
{
    switch (shape_type) {
    case ShapeType::RoundedRectangle: return QStringLiteral("Rounded Rectangle");
    case ShapeType::Ellipse: return QStringLiteral("Ellipse");
    case ShapeType::Triangle: return QStringLiteral("Triangle");
    case ShapeType::Star: return QStringLiteral("Star");
    case ShapeType::Polygon: return QStringLiteral("Polygon");
    case ShapeType::Diamond: return QStringLiteral("Diamond");
    case ShapeType::Line: return QStringLiteral("Line");
    case ShapeType::Rectangle:
    default: return QStringLiteral("Rectangle");
    }
}

static QPainterPath tool_shape_path(ShapeType shape_type, const QRectF &rect)
{
    QPainterPath path;
    switch (shape_type) {
    case ShapeType::RoundedRectangle:
        path.addRoundedRect(rect, 3.0, 3.0);
        break;
    case ShapeType::Ellipse:
        path.addEllipse(rect);
        break;
    case ShapeType::Triangle: {
        path.moveTo(rect.center().x(), rect.top());
        path.lineTo(rect.right(), rect.bottom());
        path.lineTo(rect.left(), rect.bottom());
        path.closeSubpath();
        break;
    }
    case ShapeType::Star: {
        const QPointF c = rect.center();
        const double rx = rect.width() / 2.0;
        const double ry = rect.height() / 2.0;
        for (int i = 0; i < 10; ++i) {
            const double r = (i % 2 == 0) ? 1.0 : 0.45;
            const double a = -kToolIconPi / 2.0 + kToolIconPi * i / 5.0;
            const QPointF pt(c.x() + std::cos(a) * rx * r, c.y() + std::sin(a) * ry * r);
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        path.closeSubpath();
        break;
    }
    case ShapeType::Polygon: {
        const QPointF c = rect.center();
        const double rx = rect.width() / 2.0;
        const double ry = rect.height() / 2.0;
        for (int i = 0; i < 6; ++i) {
            const double a = -kToolIconPi / 2.0 + 2.0 * kToolIconPi * i / 6.0;
            const QPointF pt(c.x() + std::cos(a) * rx, c.y() + std::sin(a) * ry);
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        path.closeSubpath();
        break;
    }
    case ShapeType::Diamond:
        path.moveTo(rect.center().x(), rect.top());
        path.lineTo(rect.right(), rect.center().y());
        path.lineTo(rect.center().x(), rect.bottom());
        path.lineTo(rect.left(), rect.center().y());
        path.closeSubpath();
        break;
    case ShapeType::Line:
        path.moveTo(rect.left(), rect.center().y());
        path.lineTo(rect.right(), rect.center().y());
        break;
    case ShapeType::Rectangle:
    default:
        path.addRect(rect);
        break;
    }
    return path;
}

static QPainterPath editor_layer_rounded_rect_path(const Layer &layer, const QRectF &rect);

static QPainterPath editor_scene_mask_shape_path(const Layer &layer, const QRectF &rect)
{
    QPainterPath path;
    const ShapeType shape_type = layer.type == LayerType::Shape ? layer.shape_type : ShapeType::RoundedRectangle;
    switch (shape_type) {
    case ShapeType::Ellipse:
        path.addEllipse(rect);
        break;
    case ShapeType::Triangle:
    case ShapeType::Polygon:
    case ShapeType::Diamond: {
        const int sides = shape_type == ShapeType::Triangle
            ? 3
            : (shape_type == ShapeType::Diamond ? 4 : std::clamp(layer.shape_sides, 3, 64));
        const QPointF center = rect.center();
        const double rx = rect.width() / 2.0;
        const double ry = rect.height() / 2.0;
        for (int i = 0; i < sides; ++i) {
            const double a = -kToolIconPi / 2.0 + 2.0 * kToolIconPi * i / sides;
            const QPointF pt(center.x() + std::cos(a) * rx, center.y() + std::sin(a) * ry);
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        path.closeSubpath();
        break;
    }
    case ShapeType::Star: {
        const QPointF center = rect.center();
        const double rx = rect.width() / 2.0;
        const double ry = rect.height() / 2.0;
        const int points = std::clamp(layer.shape_points, 3, 64);
        const double inner = std::clamp((double)layer.shape_inner_radius, 0.0, 1.0) * 2.0;
        const double outer = std::clamp((double)layer.shape_outer_radius, 0.0, 1.0) * 2.0;
        for (int i = 0; i < points * 2; ++i) {
            const double factor = (i % 2 == 0) ? outer : inner;
            const double a = -kToolIconPi / 2.0 + kToolIconPi * i / points;
            const QPointF pt(center.x() + std::cos(a) * rx * factor,
                             center.y() + std::sin(a) * ry * factor);
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        path.closeSubpath();
        break;
    }
    case ShapeType::Line:
        path.moveTo(rect.left(), rect.center().y());
        path.lineTo(rect.right(), rect.center().y());
        break;
    case ShapeType::RoundedRectangle:
        path = editor_layer_rounded_rect_path(layer, rect);
        break;
    case ShapeType::Rectangle:
    default:
        path = editor_layer_rounded_rect_path(layer, rect);
        break;
    }
    return path;
}

static QPainterPath editor_corner_rect_path(const QRectF &rect, double top_left, double top_right,
                                            double bottom_right, double bottom_left, CornerType corner_type)
{
    const double max_radius = std::max(0.0, std::min(rect.width(), rect.height()) / 2.0);
    const double tl = std::clamp(top_left, 0.0, max_radius);
    const double tr = std::clamp(top_right, 0.0, max_radius);
    const double br = std::clamp(bottom_right, 0.0, max_radius);
    const double bl = std::clamp(bottom_left, 0.0, max_radius);
    QPainterPath path;
    if (tl <= 0.0 && tr <= 0.0 && br <= 0.0 && bl <= 0.0) {
        path.addRect(rect);
        return path;
    }
    auto add_corner = [&](const QPointF &corner, const QPointF &from, const QPointF &to,
                          const QPointF &cutout, double radius) {
        if (radius <= 0.0) {
            path.lineTo(corner);
            return;
        }
        switch (corner_type) {
        case CornerType::Straight:
            path.lineTo(to);
            break;
        case CornerType::Cutout:
            path.lineTo(cutout);
            path.lineTo(to);
            break;
        case CornerType::Concave:
            path.cubicTo(cutout, cutout, to);
            break;
        case CornerType::Round:
        default:
            path.quadTo(corner, to);
            break;
        }
    };
    path.moveTo(rect.left() + tl, rect.top());
    path.lineTo(rect.right() - tr, rect.top());
    add_corner(rect.topRight(), QPointF(rect.right() - tr, rect.top()), QPointF(rect.right(), rect.top() + tr),
               QPointF(rect.right() - tr, rect.top() + tr), tr);
    path.lineTo(rect.right(), rect.bottom() - br);
    add_corner(rect.bottomRight(), QPointF(rect.right(), rect.bottom() - br), QPointF(rect.right() - br, rect.bottom()),
               QPointF(rect.right() - br, rect.bottom() - br), br);
    path.lineTo(rect.left() + bl, rect.bottom());
    add_corner(rect.bottomLeft(), QPointF(rect.left() + bl, rect.bottom()), QPointF(rect.left(), rect.bottom() - bl),
               QPointF(rect.left() + bl, rect.bottom() - bl), bl);
    path.lineTo(rect.left(), rect.top() + tl);
    add_corner(rect.topLeft(), QPointF(rect.left(), rect.top() + tl), QPointF(rect.left() + tl, rect.top()),
               QPointF(rect.left() + tl, rect.top() + tl), tl);
    path.closeSubpath();
    return path;
}

static QPainterPath editor_layer_rounded_rect_path(const Layer &layer, const QRectF &rect)
{
    return editor_corner_rect_path(rect, layer.corner_radius_tl, layer.corner_radius_tr,
                                   layer.corner_radius_br, layer.corner_radius_bl, layer.corner_type);
}

static void set_layer_corner_radii(Layer &layer, float top_left, float top_right,
                                   float bottom_right, float bottom_left)
{
    layer.corner_radius_tl = std::max(0.0f, top_left);
    layer.corner_radius_tr = std::max(0.0f, top_right);
    layer.corner_radius_br = std::max(0.0f, bottom_right);
    layer.corner_radius_bl = std::max(0.0f, bottom_left);
    layer.corner_radius = layer.corner_radius_tl;
    if (layer.corner_radius_tl == layer.corner_radius_tr &&
        layer.corner_radius_tl == layer.corner_radius_br &&
        layer.corner_radius_tl == layer.corner_radius_bl)
        layer.shape_roundness = layer.corner_radius_tl;
}

static void set_layer_all_corner_radii(Layer &layer, float radius)
{
    set_layer_corner_radii(layer, radius, radius, radius, radius);
}

static QBrush scene_mask_hatch_brush()
{
    QPixmap pattern(12, 12);
    pattern.fill(Qt::transparent);
    QPainter painter(&pattern);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(255, 0, 200, 180), 2.0));
    painter.drawLine(QPointF(-2.0, 12.0), QPointF(12.0, -2.0));
    painter.drawLine(QPointF(4.0, 14.0), QPointF(14.0, 4.0));
    painter.end();
    return QBrush(pattern);
}

static QIcon shape_tool_icon(ShapeType shape_type)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(220, 220, 220), shape_type == ShapeType::Line ? 2.4 : 1.8));
    painter.setBrush(shape_type == ShapeType::Line ? Qt::NoBrush : QBrush(QColor(120, 120, 120, 60)));
    painter.drawPath(tool_shape_path(shape_type, QRectF(5, 5, 14, 14)));
    return QIcon(pixmap);
}

static QIcon cursor_tool_icon()
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.moveTo(6, 4);
    path.lineTo(6, 19);
    path.lineTo(10, 15);
    path.lineTo(13, 21);
    path.lineTo(16, 19);
    path.lineTo(13, 13);
    path.lineTo(18, 13);
    path.closeSubpath();
    painter.setPen(QPen(QColor(230, 230, 230), 1.4));
    painter.setBrush(QColor(65, 65, 65));
    painter.drawPath(path);
    return QIcon(pixmap);
}


static QString text_tool_display_name(LayerType type)
{
    if (type == LayerType::Clock) return obsgs_tr("OBSTitles.Clock");
    if (type == LayerType::Ticker) return obsgs_tr("OBSTitles.Ticker");
    return obsgs_tr("OBSTitles.Text");
}

static QIcon text_tool_icon(LayerType type)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(230, 230, 230), 1.7));
    painter.setBrush(Qt::NoBrush);

    if (type == LayerType::Clock) {
        painter.drawEllipse(QRectF(5.0, 5.0, 14.0, 14.0));
        painter.drawLine(QPointF(12.0, 12.0), QPointF(12.0, 7.5));
        painter.drawLine(QPointF(12.0, 12.0), QPointF(15.8, 14.2));
    } else if (type == LayerType::Ticker) {
        painter.setPen(QPen(QColor(230, 230, 230), 1.6));
        painter.drawText(QRectF(4.0, 2.0, 16.0, 12.0), Qt::AlignCenter, QStringLiteral("T"));
        painter.drawLine(QPointF(4.0, 15.0), QPointF(20.0, 15.0));
        painter.drawLine(QPointF(7.0, 19.0), QPointF(20.0, 19.0));
        painter.drawLine(QPointF(4.0, 15.0), QPointF(7.0, 12.0));
        painter.drawLine(QPointF(4.0, 15.0), QPointF(7.0, 18.0));
    } else {
        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(18);
        painter.setFont(font);
        painter.drawText(QRectF(3.0, 2.0, 18.0, 20.0), Qt::AlignCenter, QStringLiteral("T"));
    }
    return QIcon(pixmap);
}

class HoldMenuToolButton : public QToolButton {
public:
    explicit HoldMenuToolButton(QWidget *parent = nullptr) : QToolButton(parent)
    {
        hold_timer_.setSingleShot(true);
        hold_timer_.setInterval(500);
        QObject::connect(&hold_timer_, &QTimer::timeout, this, [this]() {
            if (!menu() || !isDown() || !underMouse()) return;
            menu_opened_by_hold_ = true;
            menu()->popup(mapToGlobal(QPoint(width() + 2, 0)));
        });
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        menu_opened_by_hold_ = false;
        if (event->button() == Qt::LeftButton && menu())
            hold_timer_.start();
        QToolButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        hold_timer_.stop();
        if (menu_opened_by_hold_) {
            setDown(false);
            event->accept();
            return;
        }
        QToolButton::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        if (!isDown()) hold_timer_.stop();
        QToolButton::leaveEvent(event);
    }

private:
    QTimer hold_timer_;
    bool menu_opened_by_hold_ = false;
};

static std::string editor_text_std(const char *key)
{
    return obsgs_tr(key).toStdString();
}

ToolsSidebar::ToolsSidebar(QWidget *parent) : QWidget(parent)
{
    constexpr int kSidebarIconSize = 22;
    constexpr int kSidebarButtonSize = 36;
    const QPalette pal = qApp->palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor base = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor button = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor hover = button.lightness() < 128 ? button.lighter(125) : button.darker(108);

    setObjectName(QStringLiteral("OBSGraphicsStudioProToolsSidebarPanel"));
    setMinimumWidth(kSidebarButtonSize + 8);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setStyleSheet(QStringLiteral(
        "QWidget#OBSGraphicsStudioProToolsSidebarPanel{background:%1;color:%2;}"
        "QToolButton{color:%3;background:%4;border:1px solid transparent;border-radius:3px;padding:0;"
        "min-width:%5px;min-height:%5px;max-width:%5px;max-height:%5px;}"
        "QToolButton:hover{background:%6;border-color:%7;}"
        "QToolButton:checked{background:%8;color:%9;border-color:%8;}"
        "QToolButton::menu-indicator{image:none;width:0px;}"
        "QMenu{color:%10;background:%11;border:1px solid %7;}"
        "QMenu::item{padding:5px 22px;}"
        "QMenu::item:selected{background:%8;color:%9;}")
        .arg(window.name(QColor::HexRgb),
             text.name(QColor::HexRgb),
             button_text.name(QColor::HexRgb),
             button.name(QColor::HexRgb),
             QString::number(kSidebarButtonSize),
             hover.name(QColor::HexRgb),
             border.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb),
             highlighted_text.name(QColor::HexRgb),
             pal.color(QPalette::Text).name(QColor::HexRgb),
             base.name(QColor::HexRgb)));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 6, 4, 6);
    layout->setSpacing(4);

    tool_group_ = new QActionGroup(this);
    tool_group_->setExclusive(true);

    auto make_tool_button = [this, layout](const QString &text, const QIcon &icon, const QString &tip) {
        auto *button = new HoldMenuToolButton(this);
        button->setText(text);
        button->setAccessibleName(text);
        button->setToolTip(tip);
        button->setIcon(icon);
        button->setIconSize(QSize(kSidebarIconSize, kSidebarIconSize));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setCheckable(true);
        button->setAutoRaise(false);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setFixedSize(kSidebarButtonSize, kSidebarButtonSize);
        layout->addWidget(button, 0, Qt::AlignHCenter);
        return button;
    };

    selection_button_ = make_tool_button(QStringLiteral("Selection Tool"), cursor_tool_icon(),
                                         QStringLiteral("Selection/Cursor Tool: normal object selection mode"));
    shape_button_ = make_tool_button(QStringLiteral("Shape Tool"), shape_tool_icon(selected_shape_),
                                     QStringLiteral("Shape Tool: choose a shape, then click and drag on the canvas"));
    text_button_ = make_tool_button(QStringLiteral("Text Tool"), text_tool_icon(selected_text_layer_type_),
                                    QStringLiteral("Text Tool: choose text, clock, or ticker, then click and drag on the canvas"));
    color_picker_button_ = make_tool_button(QStringLiteral("Color Picker Tool"), obs_icon("eyedropper.svg"),
                                            QStringLiteral("Color Picker Tool: sample a color from the canvas into the selected layer"));

    auto *selection_action = new QAction(cursor_tool_icon(), QStringLiteral("Selection Tool"), this);
    selection_action->setCheckable(true);
    selection_action->setChecked(true);
    auto *shape_action = new QAction(shape_tool_icon(selected_shape_), QStringLiteral("Shape Tool"), this);
    shape_action->setCheckable(true);
    auto *text_action = new QAction(text_tool_icon(selected_text_layer_type_), QStringLiteral("Text Tool"), this);
    text_action->setCheckable(true);
    auto *color_picker_action = new QAction(obs_icon("eyedropper.svg"), QStringLiteral("Color Picker Tool"), this);
    color_picker_action->setCheckable(true);
    tool_group_->addAction(selection_action);
    tool_group_->addAction(shape_action);
    tool_group_->addAction(text_action);
    tool_group_->addAction(color_picker_action);
    selection_button_->setDefaultAction(selection_action);
    shape_button_->setDefaultAction(shape_action);
    text_button_->setDefaultAction(text_action);
    color_picker_button_->setDefaultAction(color_picker_action);

    shape_menu_ = new QMenu(shape_button_);
    shape_button_->setMenu(shape_menu_);
    rebuild_shape_menu();
    text_menu_ = new QMenu(text_button_);
    text_button_->setMenu(text_menu_);
    rebuild_text_menu();

    connect(selection_action, &QAction::triggered, this, [this]() {
        emit selection_tool_requested();
    });
    connect(shape_action, &QAction::triggered, this, [this]() {
        emit shape_tool_requested(selected_shape_);
    });
    connect(text_action, &QAction::triggered, this, [this]() {
        emit text_tool_requested(selected_text_layer_type_);
    });
    connect(color_picker_action, &QAction::triggered, this, [this]() {
        emit color_picker_tool_requested();
    });

    layout->addStretch(1);
}

void ToolsSidebar::set_selected_shape(ShapeType shape_type)
{
    selected_shape_ = shape_type;
    const QIcon icon = shape_tool_icon(shape_type);
    if (shape_button_) {
        shape_button_->setIcon(icon);
        if (auto *action = shape_button_->defaultAction()) {
            action->setIcon(icon);
            action->setText(shape_display_name(shape_type));
            action->setChecked(true);
        }
        shape_button_->setToolTip(QStringLiteral("Shape Tool: %1. Click and drag on the canvas to draw.").arg(shape_display_name(shape_type)));
        shape_button_->setChecked(true);
    }
}

void ToolsSidebar::rebuild_shape_menu()
{
    if (!shape_menu_) return;
    shape_menu_->clear();
    const std::vector<ShapeType> shapes = {
        ShapeType::Rectangle,
        ShapeType::RoundedRectangle,
        ShapeType::Ellipse,
        ShapeType::Triangle,
        ShapeType::Star,
        ShapeType::Polygon,
        ShapeType::Diamond,
        ShapeType::Line,
    };
    for (ShapeType shape : shapes) {
        QAction *action = shape_menu_->addAction(shape_tool_icon(shape), shape_display_name(shape));
        connect(action, &QAction::triggered, this, [this, shape]() {
            set_selected_shape(shape);
            emit shape_tool_requested(shape);
        });
    }
}


void ToolsSidebar::set_selected_text_layer_type(LayerType type)
{
    if (type != LayerType::Text && type != LayerType::Clock && type != LayerType::Ticker)
        type = LayerType::Text;
    selected_text_layer_type_ = type;
    const QIcon icon = text_tool_icon(type);
    const QString name = text_tool_display_name(type);
    if (text_button_) {
        text_button_->setIcon(icon);
        if (auto *action = text_button_->defaultAction()) {
            action->setIcon(icon);
            action->setText(name);
            action->setChecked(true);
        }
        text_button_->setToolTip(QStringLiteral("Text Tool: %1. Click and drag on the canvas to draw a text box.").arg(name));
        text_button_->setChecked(true);
    }
}

void ToolsSidebar::rebuild_text_menu()
{
    if (!text_menu_) return;
    text_menu_->clear();
    const std::vector<LayerType> types = {LayerType::Text, LayerType::Clock, LayerType::Ticker};
    for (LayerType type : types) {
        QAction *action = text_menu_->addAction(text_tool_icon(type), text_tool_display_name(type));
        connect(action, &QAction::triggered, this, [this, type]() {
            set_selected_text_layer_type(type);
            emit text_tool_requested(type);
        });
    }
}

static QLocale locale_for_text_transform(const QString &text)
{
    QLocale locale;
    for (const QChar ch : text) {
        uint u = ch.unicode();
        if (u >= 0x0370 && u <= 0x03FF)
            return QLocale(QLocale::Greek, QLocale::Greece);
        if (QStringLiteral("ıİşŞğĞçÇ").contains(ch))
            return QLocale(QLocale::Turkish, QLocale::Turkey);
        if (ch == QChar(0x00DF))
            return QLocale(QLocale::German, QLocale::Germany);
    }
    return locale;
}


static QString php_date_format(const QString &format, const QDateTime &date_time)
{
    QString out;
    const QDate date = date_time.date();
    const QTime time = date_time.time();
    for (int i = 0; i < format.size(); ++i) {
        const QChar token = format.at(i);
        if (token == QLatin1Char('\\') && i + 1 < format.size()) {
            out.append(format.at(++i));
            continue;
        }
        switch (token.unicode()) {
        case 'd': out += QString("%1").arg(date.day(), 2, 10, QChar('0')); break;
        case 'D': out += date_time.toString("ddd"); break;
        case 'j': out += QString::number(date.day()); break;
        case 'l': out += date_time.toString("dddd"); break;
        case 'F': out += date_time.toString("MMMM"); break;
        case 'm': out += QString("%1").arg(date.month(), 2, 10, QChar('0')); break;
        case 'M': out += date_time.toString("MMM"); break;
        case 'n': out += QString::number(date.month()); break;
        case 'Y': out += QString::number(date.year()); break;
        case 'y': out += QString("%1").arg(date.year() % 100, 2, 10, QChar('0')); break;
        case 'a': out += (time.hour() < 12 ? "am" : "pm"); break;
        case 'A': out += (time.hour() < 12 ? "AM" : "PM"); break;
        case 'g': { int h = time.hour() % 12; out += QString::number(h == 0 ? 12 : h); break; }
        case 'G': out += QString::number(time.hour()); break;
        case 'h': { int h = time.hour() % 12; out += QString("%1").arg(h == 0 ? 12 : h, 2, 10, QChar('0')); break; }
        case 'H': out += QString("%1").arg(time.hour(), 2, 10, QChar('0')); break;
        case 'i': out += QString("%1").arg(time.minute(), 2, 10, QChar('0')); break;
        case 's': out += QString("%1").arg(time.second(), 2, 10, QChar('0')); break;
        case 'U': out += QString::number(date_time.toSecsSinceEpoch()); break;
        default: out.append(token); break;
        }
    }
    return out;
}

static QString clock_text_for_layer(const Layer &layer)
{
    QString format = QString::fromStdString(layer.clock_format);
    if (format.isEmpty()) format = QStringLiteral("H:i:s");
    return php_date_format(format, QDateTime::currentDateTime());
}

static QString display_text_for_style(const Layer &layer)
{
    QString text = layer.type == LayerType::Clock
        ? clock_text_for_layer(layer)
        : QString::fromStdString(layer.text_content);
    if (layer.text_style == 1)
        return locale_for_text_transform(text).toUpper(text);
    return text;
}

static void apply_text_style_to_font(QFont &font, const Layer &layer)
{
    if (layer.text_style == 2)
        font.setCapitalization(QFont::SmallCaps);
    if (layer.text_style == 3 || layer.text_style == 4)
        font.setPixelSize(std::max(1, (int)std::round(font.pixelSize() * 0.65)));
}

static QFont font_for_layer(const Layer &layer)
{
    const QString family = QString::fromStdString(layer.font_family);
    const QString style = QString::fromStdString(layer.font_style);
    QFontDatabase fdb;
    QFont font = !style.isEmpty()
        ? fdb.font(family, style, layer.font_size)
        : QFont(family);
    font.setFamily(family);
    font.setPixelSize(layer.font_size);
    if (!style.isEmpty())
        font.setStyleName(style);
    font.setBold(layer.font_bold);
    font.setItalic(layer.font_italic);
    font.setKerning(layer.font_kerning);
    font.setLetterSpacing(QFont::AbsoluteSpacing, layer.char_tracking);
    font.setStretch(std::clamp((int)std::round(layer.char_scale_x * 100.0f), 1, 4000));
    apply_text_style_to_font(font, layer);
    return font;
}

static QPainterPath apply_vertical_character_scale(const QPainterPath &path, const QRectF &rect,
                                                   Qt::Alignment alignment, const Layer &layer)
{
    double scale_y = std::clamp((double)layer.char_scale_y, 0.1, 5.0);
    if (std::abs(scale_y - 1.0) < 0.0001)
        return path;

    QRectF bounds = path.boundingRect();
    double anchor_y = bounds.top();
    if (alignment & Qt::AlignVCenter)
        anchor_y = bounds.center().y();
    else if (alignment & Qt::AlignBottom)
        anchor_y = bounds.bottom();
    else if (!bounds.isEmpty())
        anchor_y = rect.top();

    QTransform xf;
    xf.translate(0.0, anchor_y);
    xf.scale(1.0, scale_y);
    xf.translate(0.0, -anchor_y);
    return xf.map(path);
}

static QRectF text_rect_for_style(const QRectF &rect, const Layer &layer)
{
    if (layer.text_style == 3)
        return rect.adjusted(0.0, 0.0, 0.0, -rect.height() * 0.28);
    if (layer.text_style == 4)
        return rect.adjusted(0.0, rect.height() * 0.28, 0.0, 0.0);
    return rect;
}

static QString overflow_layout_text(const QString &text, const Layer &layer)
{
    if (layer.text_overflow_mode == 2) {
        QString single = text;
        single.replace('\r', ' ');
        single.replace('\n', ' ');
        return single;
    }
    return text;
}

static double eval_paragraph_indent_left(const Layer &layer, double t)
{
    return std::clamp(layer.paragraph_indent_left_prop.is_animated()
                          ? layer.paragraph_indent_left_prop.evaluate(t)
                          : (double)layer.paragraph_indent_left,
                      -10000.0, 10000.0);
}

static double eval_paragraph_indent_right(const Layer &layer, double t)
{
    return std::clamp(layer.paragraph_indent_right_prop.is_animated()
                          ? layer.paragraph_indent_right_prop.evaluate(t)
                          : (double)layer.paragraph_indent_right,
                      -10000.0, 10000.0);
}

static double eval_paragraph_indent_first_line(const Layer &layer, double t)
{
    return std::clamp(layer.paragraph_indent_first_line_prop.is_animated()
                          ? layer.paragraph_indent_first_line_prop.evaluate(t)
                          : (double)layer.paragraph_indent_first_line,
                      -10000.0, 10000.0);
}

static double horizontal_fit_scale(const QFont &font, const QRectF &rect,
                                   const QString &text, const Layer &layer, double)
{
    if (layer.text_overflow_mode != 2) return 1.0;
    QFontMetricsF metrics(font);
    const double text_width = static_cast<double>(metrics.horizontalAdvance(overflow_layout_text(text, layer)));
    double natural_width = std::max(1.0, text_width);
    if (natural_width <= rect.width()) return 1.0;
    return std::clamp(rect.width() / natural_width,
                      std::clamp((double)layer.text_fit_min_scale, 0.05, 1.0),
                      1.0);
}

static QPainterPath text_overflow_path(const QFont &font, const QRectF &rect,
                                       Qt::Alignment alignment, const QString &text,
                                       const Layer &layer, double t, double *fit_scale = nullptr)
{
    QPainterPath path;
    QFontMetricsF metrics(font);
    const double indent_left = eval_paragraph_indent_left(layer, t);
    const double indent_right = eval_paragraph_indent_right(layer, t);
    const double first_indent = eval_paragraph_indent_first_line(layer, t);
    const double space_before = std::clamp((double)layer.paragraph_space_before, -10000.0, 10000.0);
    const double space_after = std::clamp((double)layer.paragraph_space_after, -10000.0, 10000.0);
    const double paragraph_left = rect.left() + indent_left;
    const double paragraph_right = rect.right() - indent_right;
    const double paragraph_width = std::max(1.0, paragraph_right - paragraph_left);
    const int align_h = std::clamp(layer.align_h, 0, 6);

    auto aligned_x = [&](double line_left, double line_width, double text_width, int mode) {
        if (mode == 1 || mode == 4)
            return line_left + (line_width - text_width) / 2.0;
        if (mode == 2 || mode == 5)
            return line_left + line_width - text_width;
        return line_left;
    };

    if (layer.text_overflow_mode == 2) {
        QString single = overflow_layout_text(text, layer);
        QRectF bounds = metrics.boundingRect(single);
        QRectF fit_rect(paragraph_left + first_indent, rect.top(),
                        std::max(1.0, paragraph_width - first_indent), rect.height());
        double scale = horizontal_fit_scale(font, fit_rect, text, layer, t);
        if (fit_scale) *fit_scale = scale;
        double visual_width = bounds.width() * scale;
        double x = aligned_x(fit_rect.left(), fit_rect.width(), visual_width, align_h);
        double y = rect.top() - bounds.top();
        if (alignment & Qt::AlignVCenter) y = rect.top() + (rect.height() - bounds.height()) / 2.0 - bounds.top();
        else if (alignment & Qt::AlignBottom) y = rect.bottom() - bounds.height() - bounds.top();
        path.addText(QPointF(0, y), font, single);
        QTransform xf;
        xf.translate(x, 0.0);
        xf.scale(scale, 1.0);
        return xf.map(path);
    }
    if (fit_scale) *fit_scale = 1.0;

    struct Line {
        QString text;
        double width = 0.0;
        double ascent = 0.0;
        double height = 0.0;
        int paragraph = 0;
        bool first_in_paragraph = false;
        bool last_in_paragraph = false;
    };
    std::vector<Line> lines;
    const QStringList paragraphs = text.split('\n');
    QTextOption option;
    option.setWrapMode(layer.text_overflow_mode == 0
                           ? (layer.paragraph_hyphenate ? QTextOption::WrapAnywhere : QTextOption::WrapAtWordBoundaryOrAnywhere)
                           : QTextOption::NoWrap);
    for (int paragraph_index = 0; paragraph_index < paragraphs.size(); ++paragraph_index) {
        const QString &paragraph = paragraphs[paragraph_index];
        const size_t first_line_index = lines.size();
        if (paragraph.isEmpty()) {
            lines.push_back({QString(), 0.0, metrics.ascent(), metrics.lineSpacing(), paragraph_index, true, true});
            continue;
        }
        QTextLayout layout(paragraph, font);
        layout.setTextOption(option);
        layout.beginLayout();
        int line_index = 0;
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            const double line_indent = line_index == 0 ? first_indent : 0.0;
            const double line_width = std::max(1.0, paragraph_width - line_indent);
            line.setLineWidth(layer.text_overflow_mode == 0 ? line_width : 1000000.0);
            int start = line.textStart();
            int len = line.textLength();
            lines.push_back({paragraph.mid(start, len), line.naturalTextWidth(), line.ascent(), line.height(),
                             paragraph_index, line_index == 0, false});
            ++line_index;
            if (layer.text_overflow_mode != 0) break;
        }
        layout.endLayout();
        if (lines.size() > first_line_index)
            lines.back().last_in_paragraph = true;
    }
    double total_height = 0.0;
    const double leading = std::clamp((double)layer.text_leading, -200.0, 500.0);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].first_in_paragraph) total_height += space_before;
        total_height += lines[i].height;
        if (lines[i].last_in_paragraph)
            total_height += space_after;
        else if (i + 1 < lines.size())
            total_height += leading;
    }
    double y = rect.top();
    if (alignment & Qt::AlignVCenter) y = rect.top() + (rect.height() - total_height) / 2.0;
    else if (alignment & Qt::AlignBottom) y = rect.bottom() - total_height;

    auto add_justified_line = [&](const Line &line, double line_left, double line_width, double baseline_y) {
        QStringList words = line.text.simplified().split(' ', Qt::SkipEmptyParts);
        if (words.size() < 2 || line.width >= line_width) {
            path.addText(QPointF(line_left, baseline_y), font, line.text);
            return;
        }
        double words_width = 0.0;
        for (const QString &word : words)
            words_width += metrics.horizontalAdvance(word);
        const double extra_space = std::max(0.0, (line_width - words_width) / (words.size() - 1));
        double word_x = line_left;
        for (int i = 0; i < words.size(); ++i) {
            path.addText(QPointF(word_x, baseline_y), font, words[i]);
            word_x += metrics.horizontalAdvance(words[i]) + extra_space;
        }
    };

    for (const auto &line : lines) {
        if (line.first_in_paragraph) y += space_before;
        const double line_indent = line.first_in_paragraph ? first_indent : 0.0;
        const double line_left = paragraph_left + line_indent;
        const double line_width = std::max(1.0, paragraph_width - line_indent);
        const bool justify_line = align_h == 6 || (align_h >= 3 && align_h <= 5 && !line.last_in_paragraph);
        if (justify_line) {
            add_justified_line(line, line_left, line_width, y + line.ascent);
        } else {
            double x = aligned_x(line_left, line_width, line.width, align_h);
            path.addText(QPointF(x, y + line.ascent), font, line.text);
        }
        y += line.height;
        if (line.last_in_paragraph)
            y += space_after;
        else
            y += leading;
    }
    return path;
}


static double ticker_time_seconds()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

static QStringList ticker_lines(const QString &text)
{
    QString normalized = text;
    normalized.replace('\r', '\n');
    QStringList raw_lines = normalized.split('\n');
    QStringList lines;
    for (const QString &line : raw_lines) {
        if (!line.trimmed().isEmpty())
            lines << line;
    }
    if (lines.isEmpty()) lines << QString();
    return lines;
}

static QPainterPath ticker_text_path(const QFont &font, const QRectF &rect,
                                     Qt::Alignment alignment, const QString &text,
                                     const Layer &layer)
{
    QPainterPath path;
    QFontMetricsF metrics(font);
    const double speed = std::max(1.0, layer.ticker_speed);
    const double now = ticker_time_seconds();

    if (layer.ticker_style == 0) {
        QString single = text;
        single.replace('\r', ' ');
        single.replace('\n', QStringLiteral("     •     "));
        QRectF bounds = metrics.boundingRect(single);
        const double text_w = std::max(1.0, bounds.width());
        const double travel = rect.width() + text_w;
        const double progress = std::fmod(now * speed, travel);
        const double x = layer.ticker_direction == 0
            ? rect.left() - text_w + progress
            : rect.right() - progress;
        double y = rect.top() - bounds.top();
        if (alignment & Qt::AlignVCenter) y = rect.top() + (rect.height() - bounds.height()) / 2.0 - bounds.top();
        else if (alignment & Qt::AlignBottom) y = rect.bottom() - bounds.height() - bounds.top();
        path.addText(QPointF(x, y), font, single);
        return path;
    }

    const QStringList lines = ticker_lines(text);
    const int line_count = std::max(1, static_cast<int>(lines.size()));
    const double line_h = std::max(1.0, metrics.lineSpacing() + std::clamp((double)layer.text_leading, -200.0, 500.0));
    if (layer.ticker_style == 1) {
        const double hold = std::max(0.1, layer.ticker_line_hold);
        int idx = (int)std::floor(now / hold) % line_count;
        if (layer.ticker_direction == 0) idx = line_count - 1 - idx;
        QString line = lines.at(idx);
        double line_w = metrics.horizontalAdvance(line);
        double x = rect.left();
        if (alignment & Qt::AlignHCenter) x = rect.left() + (rect.width() - line_w) / 2.0;
        else if (alignment & Qt::AlignRight) x = rect.right() - line_w;
        QRectF bounds = metrics.boundingRect(line);
        double y = rect.top() + (rect.height() - bounds.height()) / 2.0 - bounds.top();
        path.addText(QPointF(x, y), font, line);
        return path;
    }

    const double content_h = line_count * line_h;
    const double travel = rect.height() + content_h;
    const double progress = std::fmod(now * speed, travel);
    double y = layer.ticker_direction == 0
        ? rect.top() - content_h + progress
        : rect.bottom() - progress;
    for (const QString &line : lines) {
        double line_w = metrics.horizontalAdvance(line);
        double x = rect.left();
        if (alignment & Qt::AlignHCenter) x = rect.left() + (rect.width() - line_w) / 2.0;
        else if (alignment & Qt::AlignRight) x = rect.right() - line_w;
        path.addText(QPointF(x, y + metrics.ascent()), font, line);
        y += line_h;
    }
    return path;
}

static QPainterPath editor_scene_mask_layer_path(const Layer &layer, const QRectF &local_rect, double local_time)
{
    if (layer.type == LayerType::Text || layer.type == LayerType::Clock || layer.type == LayerType::Ticker) {
        QFont font = font_for_layer(layer);
        QRectF text_rect = text_rect_for_style(local_rect, layer);
        Qt::Alignment align = Qt::AlignLeft;
        if (layer.align_h == 1 || layer.align_h == 4) align = Qt::AlignHCenter;
        else if (layer.align_h == 2 || layer.align_h == 5) align = Qt::AlignRight;
        else if (layer.align_h >= 3) align = Qt::AlignJustify;
        if (layer.align_v == 1) align |= Qt::AlignVCenter;
        else if (layer.align_v == 2) align |= Qt::AlignBottom;
        else align |= Qt::AlignTop;

        QString text = layer.type == LayerType::Ticker
            ? QString::fromStdString(layer.text_content)
            : display_text_for_style(layer);
        if (layer.type == LayerType::Text && !layer.rich_text.plain_text.empty())
            text = QString::fromStdString(layer.rich_text.plain_text);

        QPainterPath text_path = layer.type == LayerType::Ticker
            ? ticker_text_path(font, text_rect, align, text, layer)
            : text_overflow_path(font, text_rect, align, text, layer, local_time);
        text_path = apply_vertical_character_scale(text_path, text_rect, align, layer);
        if (std::abs(layer.baseline_shift) > 0.0001)
            text_path.translate(0.0, -layer.baseline_shift);
        if (!text_path.isEmpty())
            return text_path;
    }

    return editor_scene_mask_shape_path(layer, local_rect);
}

static bool title_has_dynamic_text_layer(const std::shared_ptr<Title> &title)
{
    if (!title) return false;
    return std::any_of(title->layers.begin(), title->layers.end(), [](const std::shared_ptr<Layer> &layer) {
        return layer && (layer->type == LayerType::Clock || layer->type == LayerType::Ticker);
    });
}

static QColor color_from_argb(uint32_t argb)
{
    return QColor((argb >> 16) & 0xFF,
                  (argb >> 8) & 0xFF,
                  argb & 0xFF,
                  (argb >> 24) & 0xFF);
}

static uint32_t argb_from_color(const QColor &color)
{
    return ((uint32_t)color.alpha() << 24) |
           ((uint32_t)color.red() << 16) |
           ((uint32_t)color.green() << 8) |
           (uint32_t)color.blue();
}

static QPointF anchor_point_from_index(int index)
{
    static const QPointF anchors[] = {
        {0.0, 0.0}, {0.5, 0.0}, {1.0, 0.0},
        {0.0, 0.5}, {0.5, 0.5}, {1.0, 0.5},
        {0.0, 1.0}, {0.5, 1.0}, {1.0, 1.0},
    };
    if (index < 0 || index >= 9) return anchors[4];
    return anchors[index];
}

static int anchor_index_from_layer(const Layer &layer)
{
    int x = layer.origin_x < 0.25f ? 0 : (layer.origin_x > 0.75f ? 2 : 1);
    int y = layer.origin_y < 0.25f ? 0 : (layer.origin_y > 0.75f ? 2 : 1);
    return y * 3 + x;
}

static QPointF rotated_scaled_delta(double dx, double dy, double rot_deg, double sx, double sy)
{
    double rot = rot_deg * 3.14159265358979323846 / 180.0;
    double x = dx * sx;
    double y = dy * sy;
    double c = std::cos(rot);
    double s = std::sin(rot);
    return QPointF(x * c - y * s, x * s + y * c);
}


static bool is_text_box_auto_size_layer(const Layer &layer)
{
    return layer.type == LayerType::Text || layer.type == LayerType::Clock;
}

static bool is_canvas_text_layer(const Layer &layer)
{
    return layer.type == LayerType::Text || layer.type == LayerType::Clock || layer.type == LayerType::Ticker;
}

static double natural_text_width(const Layer &layer)
{
    if (!is_text_box_auto_size_layer(layer)) return 1.0;
    QFontMetricsF metrics(font_for_layer(layer));
    QString text = display_text_for_style(layer);
    if (layer.text_overflow_mode == 2)
        text = overflow_layout_text(text, layer);

    double width = 1.0;
    for (const QString &line : text.split('\n'))
        width = std::max(width, static_cast<double>(metrics.horizontalAdvance(line)));
    return std::ceil(width);
}

static double natural_text_height(const Layer &layer, double width)
{
    if (!is_text_box_auto_size_layer(layer)) return 1.0;
    QFont font = font_for_layer(layer);
    QFontMetricsF metrics(font);
    QString text = display_text_for_style(layer);
    if (layer.text_overflow_mode == 2)
        text = overflow_layout_text(text, layer);

    QTextOption option;
    option.setWrapMode(layer.text_overflow_mode == 0
                           ? QTextOption::WrapAtWordBoundaryOrAnywhere
                           : QTextOption::NoWrap);

    double total_height = 0.0;
    const double leading = std::clamp((double)layer.text_leading, -200.0, 500.0);
    bool first_line = true;
    for (const QString &paragraph : text.split('\n')) {
        if (paragraph.isEmpty()) {
            if (!first_line) total_height += leading;
            total_height += metrics.lineSpacing();
            first_line = false;
            continue;
        }
        QTextLayout layout(paragraph, font);
        layout.setTextOption(option);
        layout.beginLayout();
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(layer.text_overflow_mode == 0 ? std::max(1.0, width) : 1000000.0);
            if (!first_line) total_height += leading;
            total_height += line.height();
            first_line = false;
            if (layer.text_overflow_mode != 0) break;
        }
        layout.endLayout();
    }
    return std::ceil(std::max(1.0, total_height));
}

static double eval_box_width(const Layer &layer, double t)
{
    double width = layer.box_width.is_animated()
        ? layer.box_width.evaluate(t)
        : static_cast<double>(layer.rect_width);
    if (layer.text_box_width_to_text && is_text_box_auto_size_layer(layer))
        width = std::min(natural_text_width(layer), std::max(1.0, (double)layer.max_text_box_width));
    return std::max(0.0, width);
}

static double eval_box_height(const Layer &layer, double t)
{
    double height = layer.box_height.is_animated()
        ? layer.box_height.evaluate(t)
        : static_cast<double>(layer.rect_height);
    if (layer.text_box_height_to_text && is_text_box_auto_size_layer(layer)) {
        const double width = eval_box_width(layer, t);
        height = std::min(natural_text_height(layer, width), std::max(1.0, (double)layer.max_text_box_height));
    }
    return std::max(0.0, height);
}

static int shadow_pass_count(double blur)
{
    const int passes = static_cast<int>(std::ceil(blur / 3.0));
    return passes < 1 ? 1 : passes;
}

static double eval_origin_x(const Layer &layer, double t)
{
    return std::clamp(layer.origin_x_prop.is_animated()
                          ? layer.origin_x_prop.evaluate(t)
                          : (double)layer.origin_x,
                      0.0, 1.0);
}

static double eval_origin_y(const Layer &layer, double t)
{
    return std::clamp(layer.origin_y_prop.is_animated()
                          ? layer.origin_y_prop.evaluate(t)
                          : (double)layer.origin_y,
                      0.0, 1.0);
}

static int eval_channel(const AnimatedProperty &prop, double fallback, double t)
{
    return (int)std::clamp(std::round(prop.is_animated() ? prop.evaluate(t) : fallback),
                           0.0, 255.0);
}

static uint32_t eval_text_color(const Layer &layer, double t)
{
    return ((uint32_t)eval_channel(layer.text_color_a, (layer.text_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(layer.text_color_r, (layer.text_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(layer.text_color_g, (layer.text_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(layer.text_color_b, layer.text_color & 0xFF, t);
}

static uint32_t eval_fill_color(const Layer &layer, double t)
{
    return ((uint32_t)eval_channel(layer.fill_color_a, (layer.fill_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(layer.fill_color_r, (layer.fill_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(layer.fill_color_g, (layer.fill_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(layer.fill_color_b, layer.fill_color & 0xFF, t);
}

static QColor gradient_color_with_opacity(uint32_t argb, double gradient_opacity, double stop_opacity)
{
    QColor color = color_from_argb(argb);
    color.setAlphaF(std::clamp((double)color.alphaF() * gradient_opacity * stop_opacity, 0.0, 1.0));
    return color;
}

static QBrush gradient_fill_brush(const Layer &layer, const QRectF &box, double layer_opacity = 1.0)
{
    const double opacity = std::clamp((double)layer.gradient_opacity * layer_opacity, 0.0, 1.0);
    const double cx = box.left() + std::clamp((double)layer.gradient_center_x, 0.0, 1.0) * box.width();
    const double cy = box.top() + std::clamp((double)layer.gradient_center_y, 0.0, 1.0) * box.height();
    const double scale = std::clamp((double)layer.gradient_scale, 0.01, 10.0);
    const double start_pos = std::clamp((double)layer.gradient_start_pos, 0.0, 1.0);
    const double end_pos = std::clamp((double)layer.gradient_end_pos, 0.0, 1.0);
    if (layer.gradient_type == 1) {
        const double radius = std::max(box.width(), box.height()) * 0.5 * scale;
        QRadialGradient gradient(QPointF(cx, cy), std::max(1.0, radius),
                                 QPointF(box.left() + std::clamp((double)layer.gradient_focal_x, 0.0, 1.0) * box.width(),
                                         box.top() + std::clamp((double)layer.gradient_focal_y, 0.0, 1.0) * box.height()));
        gradient.setColorAt(start_pos, gradient_color_with_opacity(layer.gradient_start_color, opacity, layer.gradient_start_opacity));
        gradient.setColorAt(end_pos, gradient_color_with_opacity(layer.gradient_end_color, opacity, layer.gradient_end_opacity));
        return QBrush(gradient);
    }
    const double length = std::hypot(box.width(), box.height()) * 0.5 * scale;
    const double angle = layer.gradient_angle * std::acos(-1.0) / 180.0;
    const double dx = std::cos(angle) * length;
    const double dy = std::sin(angle) * length;
    QLinearGradient gradient(QPointF(cx - dx, cy - dy), QPointF(cx + dx, cy + dy));
    gradient.setColorAt(start_pos, gradient_color_with_opacity(layer.gradient_start_color, opacity, layer.gradient_start_opacity));
    gradient.setColorAt(end_pos, gradient_color_with_opacity(layer.gradient_end_color, opacity, layer.gradient_end_opacity));
    return QBrush(gradient);
}

static QBrush background_gradient_fill_brush(const Layer &layer, const QRectF &box, double layer_opacity = 1.0)
{
    const double opacity = std::clamp((double)layer.background_gradient_opacity * layer_opacity, 0.0, 1.0);
    const double cx = box.left() + std::clamp((double)layer.background_gradient_center_x, 0.0, 1.0) * box.width();
    const double cy = box.top() + std::clamp((double)layer.background_gradient_center_y, 0.0, 1.0) * box.height();
    const double scale = std::clamp((double)layer.background_gradient_scale, 0.01, 10.0);
    const double start_pos = std::clamp((double)layer.background_gradient_start_pos, 0.0, 1.0);
    const double end_pos = std::clamp((double)layer.background_gradient_end_pos, 0.0, 1.0);
    if (layer.background_gradient_type == 1) {
        const double radius = std::max(box.width(), box.height()) * 0.5 * scale;
        QRadialGradient gradient(QPointF(cx, cy), std::max(1.0, radius),
                                 QPointF(box.left() + std::clamp((double)layer.background_gradient_focal_x, 0.0, 1.0) * box.width(),
                                         box.top() + std::clamp((double)layer.background_gradient_focal_y, 0.0, 1.0) * box.height()));
        gradient.setColorAt(start_pos, gradient_color_with_opacity(layer.background_gradient_start_color, opacity, layer.background_gradient_start_opacity));
        gradient.setColorAt(end_pos, gradient_color_with_opacity(layer.background_gradient_end_color, opacity, layer.background_gradient_end_opacity));
        return QBrush(gradient);
    }
    const double length = std::hypot(box.width(), box.height()) * 0.5 * scale;
    const double angle = layer.background_gradient_angle * std::acos(-1.0) / 180.0;
    const double dx = std::cos(angle) * length;
    const double dy = std::sin(angle) * length;
    QLinearGradient gradient(QPointF(cx - dx, cy - dy), QPointF(cx + dx, cy + dy));
    gradient.setColorAt(start_pos, gradient_color_with_opacity(layer.background_gradient_start_color, opacity, layer.background_gradient_start_opacity));
    gradient.setColorAt(end_pos, gradient_color_with_opacity(layer.background_gradient_end_color, opacity, layer.background_gradient_end_opacity));
    return QBrush(gradient);
}

static QString effect_type_name(LayerEffectType type)
{
    switch (type) {
    case LayerEffectType::BackgroundColor: return obsgs_tr("OBSTitles.BackgroundColor");
    case LayerEffectType::Outline: return obsgs_tr("OBSTitles.Outline");
    case LayerEffectType::DropShadow: return obsgs_tr("OBSTitles.DropShadow");
    case LayerEffectType::LongShadow: return obsgs_tr("OBSTitles.LongShadow");
    case LayerEffectType::BrightnessContrast: return obsgs_tr("OBSTitles.BrightnessContrast");
    case LayerEffectType::Saturation: return obsgs_tr("OBSTitles.Saturation");
    case LayerEffectType::ColorOverlay: return obsgs_tr("OBSTitles.ColorOverlay");
    case LayerEffectType::Glow: return obsgs_tr("OBSTitles.Glow");
    case LayerEffectType::InnerGlow: return obsgs_tr("OBSTitles.InnerGlow");
    case LayerEffectType::InnerShadow: return obsgs_tr("OBSTitles.InnerShadow");
    case LayerEffectType::Blur: return obsgs_tr("OBSTitles.Blur");
    case LayerEffectType::MotionBlur: return obsgs_tr("OBSTitles.MotionBlur");
    }
    return QStringLiteral("Effect");
}

static void add_shadow_blur_items(QComboBox *combo)
{
    if (!combo) return;
    combo->addItem(obsgs_tr("OBSTitles.BoxBlur"), (int)ShadowBlurType::Box);
    combo->addItem(obsgs_tr("OBSTitles.GaussianBlur"), (int)ShadowBlurType::Gaussian);
    combo->addItem(obsgs_tr("OBSTitles.StackFastBlur"), (int)ShadowBlurType::StackFast);
    combo->addItem(obsgs_tr("OBSTitles.TriangularBlur"), (int)ShadowBlurType::Triangular);
    combo->addItem(obsgs_tr("OBSTitles.DualKawaseBlur"), (int)ShadowBlurType::DualKawase);
    combo->addItem(obsgs_tr("OBSTitles.AlphaMaskBlur"), (int)ShadowBlurType::AlphaMask);
}

static void add_blend_mode_items(QComboBox *combo)
{
    if (!combo) return;
    combo->addItem(obsgs_tr("OBSTitles.BlendModeNormal"), (int)EffectBlendMode::Normal);
    combo->addItem(obsgs_tr("OBSTitles.BlendModeMultiply"), (int)EffectBlendMode::Multiply);
    combo->addItem(obsgs_tr("OBSTitles.BlendModeAdditive"), (int)EffectBlendMode::Additive);
    combo->addItem(obsgs_tr("OBSTitles.BlendModeScreen"), (int)EffectBlendMode::Screen);
    combo->addItem(obsgs_tr("OBSTitles.BlendModeOverlay"), (int)EffectBlendMode::Overlay);
    combo->addItem(obsgs_tr("OBSTitles.BlendModeColor"), (int)EffectBlendMode::Color);
}

static bool layer_effect_enabled(const Layer &layer, LayerEffectType type, bool legacy_enabled)
{
    if (layer.effects.empty())
        return legacy_enabled;
    for (const auto &effect : layer.effects) {
        if (effect.type == type && effect.enabled)
            return true;
    }
    return false;
}

static bool eval_outline_enabled(const Layer &layer, double)
{
    return layer.stroke_fill_type != 0 &&
           layer_effect_enabled(layer, LayerEffectType::Outline, layer.outline_enabled);
}

static uint32_t eval_outline_color(const Layer &layer, double)
{
    return layer.stroke_color;
}

static double eval_outline_width(const Layer &layer, double)
{
    return eval_outline_enabled(layer, 0.0) ? std::max(0.0f, layer.stroke_width) : 0.0;
}

static double eval_outline_opacity(const Layer &layer, double)
{
    return std::clamp((double)layer.outline_opacity, 0.0, 1.0);
}

static bool eval_outline_on_front(const Layer &layer, double)
{
    return layer.outline_on_front;
}

static bool eval_outline_antialias(const Layer &layer, double)
{
    return layer.outline_antialias;
}

static Qt::PenJoinStyle outline_pen_join_style(const Layer &layer)
{
    switch (layer.outline_join_style) {
    case 0: return Qt::MiterJoin;
    case 2: return Qt::BevelJoin;
    case 1:
    default: return Qt::RoundJoin;
    }
}

static bool eval_shadow_enabled(const Layer &layer, double t)
{
    const bool legacy = layer.shadow_enabled_prop.is_animated()
        ? layer.shadow_enabled_prop.evaluate(t) >= 0.5
        : layer.shadow_enabled;
    return layer_effect_enabled(layer, LayerEffectType::DropShadow, legacy);
}

static double eval_shadow_opacity(const Layer &layer, double t)
{
    return std::clamp(layer.shadow_opacity_prop.is_animated() ? layer.shadow_opacity_prop.evaluate(t) : (double)layer.shadow_opacity, 0.0, 1.0);
}

static double eval_shadow_distance(const Layer &layer, double t)
{
    return std::max(0.0, layer.shadow_distance_prop.is_animated() ? layer.shadow_distance_prop.evaluate(t) : (double)layer.shadow_distance);
}

static double eval_shadow_angle(const Layer &layer, double t)
{
    return layer.shadow_angle_prop.is_animated() ? layer.shadow_angle_prop.evaluate(t) : (double)layer.shadow_angle;
}

static double eval_shadow_blur(const Layer &layer, double t)
{
    return std::max(0.0, layer.shadow_blur_prop.is_animated() ? layer.shadow_blur_prop.evaluate(t) : (double)layer.shadow_blur);
}

static double eval_shadow_spread(const Layer &layer, double t)
{
    return std::max(0.0, layer.shadow_spread_prop.is_animated() ? layer.shadow_spread_prop.evaluate(t) : (double)layer.shadow_spread);
}

static uint32_t eval_shadow_color(const Layer &layer, double t)
{
    return ((uint32_t)eval_channel(layer.shadow_color_a, (layer.shadow_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(layer.shadow_color_r, (layer.shadow_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(layer.shadow_color_g, (layer.shadow_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(layer.shadow_color_b, layer.shadow_color & 0xFF, t);
}

static QPointF shadow_offset(const Layer &layer, double t)
{
    double radians = eval_shadow_angle(layer, t) * 3.14159265358979323846 / 180.0;
    double distance = eval_shadow_distance(layer, t);
    return QPointF(std::cos(radians) * distance, std::sin(radians) * distance);
}

static bool eval_background_enabled(const Layer &layer, double t)
{
    const bool legacy = layer.background_enabled_prop.is_animated()
        ? layer.background_enabled_prop.evaluate(t) >= 0.5
        : layer.background_enabled;
    return layer_effect_enabled(layer, LayerEffectType::BackgroundColor, legacy);
}

static double eval_background_opacity(const Layer &layer, double t)
{
    return std::clamp(layer.background_opacity_prop.is_animated()
                          ? layer.background_opacity_prop.evaluate(t)
                          : (double)layer.background_opacity,
                      0.0, 1.0);
}

static double eval_background_padding_x(const Layer &layer, double t)
{
    return std::max(0.0, layer.background_padding_x_prop.is_animated()
                             ? layer.background_padding_x_prop.evaluate(t)
                             : (double)layer.background_padding_x);
}

static double eval_background_padding_y(const Layer &layer, double t)
{
    return std::max(0.0, layer.background_padding_y_prop.is_animated()
                             ? layer.background_padding_y_prop.evaluate(t)
                             : (double)layer.background_padding_y);
}

static double eval_background_corner_radius(const Layer &layer, double t)
{
    return std::max(0.0, layer.background_corner_radius_prop.is_animated()
                             ? layer.background_corner_radius_prop.evaluate(t)
                             : (double)layer.background_corner_radius);
}

static uint32_t eval_background_color(const Layer &layer, double t)
{
    return ((uint32_t)eval_channel(layer.background_color_a, (layer.background_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(layer.background_color_r, (layer.background_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(layer.background_color_g, (layer.background_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(layer.background_color_b, layer.background_color & 0xFF, t);
}

static QColor evaluated_background_color(const Layer &layer, double t)
{
    QColor color = color_from_argb(eval_background_color(layer, t));
    color.setAlphaF(std::clamp((double)color.alphaF() * eval_background_opacity(layer, t), 0.0, 1.0));
    return color;
}

static void set_channel_statics(Layer &layer, bool text, uint32_t argb)
{
    auto &a = text ? layer.text_color_a : layer.fill_color_a;
    auto &r = text ? layer.text_color_r : layer.fill_color_r;
    auto &g = text ? layer.text_color_g : layer.fill_color_g;
    auto &b = text ? layer.text_color_b : layer.fill_color_b;
    a.static_value = (argb >> 24) & 0xFF;
    r.static_value = (argb >> 16) & 0xFF;
    g.static_value = (argb >> 8) & 0xFF;
    b.static_value = argb & 0xFF;
}

static void apply_easing_preset(Keyframe &keyframe, EasingType easing)
{
    keyframe.easing = easing;
    switch (easing) {
    case EasingType::Bezier:
        keyframe.cx1 = 0.33f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.67f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::EaseIn:
        keyframe.cx1 = 0.42f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 1.0f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::EaseOut:
        keyframe.cx1 = 0.0f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.58f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::EaseInOut:
        keyframe.cx1 = 0.42f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.58f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::Linear:
    case EasingType::Hold:
    default:
        keyframe.cx1 = 0.333f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.667f; keyframe.cy2 = 1.0f;
        break;
    }
}

static void add_or_replace_keyframe(AnimatedProperty &prop, double time, double value)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    prop.static_value = value;
    for (auto &kf : prop.keyframes) {
        if (std::abs(kf.time - time) <= kEpsilon) {
            kf.time = time;
            kf.value = value;
            return;
        }
    }
    Keyframe kf;
    kf.time = time;
    kf.value = value;
    apply_easing_preset(kf, EasingType::Linear);
    prop.keyframes.push_back(kf);
    std::sort(prop.keyframes.begin(), prop.keyframes.end(),
              [](const Keyframe &a, const Keyframe &b) { return a.time < b.time; });
}

static void set_animated_value(AnimatedProperty &prop, double time, double value)
{
    if (prop.is_animated())
        add_or_replace_keyframe(prop, time, value);
    else
        prop.static_value = value;
}

static void set_color_channels_at(Layer &layer, bool text, double time, uint32_t argb)
{
    auto &a = text ? layer.text_color_a : layer.fill_color_a;
    auto &r = text ? layer.text_color_r : layer.fill_color_r;
    auto &g = text ? layer.text_color_g : layer.fill_color_g;
    auto &b = text ? layer.text_color_b : layer.fill_color_b;
    set_animated_value(a, time, (argb >> 24) & 0xFF);
    set_animated_value(r, time, (argb >> 16) & 0xFF);
    set_animated_value(g, time, (argb >> 8) & 0xFF);
    set_animated_value(b, time, argb & 0xFF);
}

static void set_shadow_color_channels_at(Layer &layer, double time, uint32_t argb)
{
    set_animated_value(layer.shadow_color_a, time, (argb >> 24) & 0xFF);
    set_animated_value(layer.shadow_color_r, time, (argb >> 16) & 0xFF);
    set_animated_value(layer.shadow_color_g, time, (argb >> 8) & 0xFF);
    set_animated_value(layer.shadow_color_b, time, argb & 0xFF);
}

static void set_background_color_channels_at(Layer &layer, double time, uint32_t argb)
{
    set_animated_value(layer.background_color_a, time, (argb >> 24) & 0xFF);
    set_animated_value(layer.background_color_r, time, (argb >> 16) & 0xFF);
    set_animated_value(layer.background_color_g, time, (argb >> 8) & 0xFF);
    set_animated_value(layer.background_color_b, time, argb & 0xFF);
}

static bool keyframe_at_time(const AnimatedProperty &prop, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    for (const auto &kf : prop.keyframes)
        if (std::abs(kf.time - time) <= kEpsilon) return true;
    return false;
}

static void remove_keyframe_at(AnimatedProperty &prop, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    prop.keyframes.erase(
        std::remove_if(prop.keyframes.begin(), prop.keyframes.end(),
                       [&](const Keyframe &kf) { return std::abs(kf.time - time) <= kEpsilon; }),
        prop.keyframes.end());
}

static void toggle_keyframe(AnimatedProperty &prop, double time, double value)
{
    if (keyframe_at_time(prop, time))
        remove_keyframe_at(prop, time);
    else
        add_or_replace_keyframe(prop, time, value);
}

static bool any_keyframe_at_time(std::initializer_list<const AnimatedProperty *> props, double time)
{
    for (const auto *prop : props)
        if (prop && keyframe_at_time(*prop, time)) return true;
    return false;
}

static bool any_keyframes(std::initializer_list<const AnimatedProperty *> props)
{
    for (const auto *prop : props)
        if (prop && prop->is_animated()) return true;
    return false;
}

static QStringList font_styles_for_family(const QString &family)
{
    QFontDatabase fdb;
    QStringList styles = fdb.styles(family);
    if (styles.isEmpty()) {
        styles << QStringLiteral("Regular");
    } else {
        styles.removeDuplicates();
        styles.sort(Qt::CaseInsensitive);
        int regular = styles.indexOf(QStringLiteral("Regular"));
        if (regular > 0) {
            QString item = styles.takeAt(regular);
            styles.prepend(item);
        }
    }
    return styles;
}

static void populate_font_style_combo(QComboBox *combo, const QString &family, const QString &preferred)
{
    if (!combo) return;
    QSignalBlocker blocker(combo);
    combo->clear();
    QStringList styles = font_styles_for_family(family);
    for (const QString &style : styles)
        combo->addItem(style, style);
    int idx = preferred.isEmpty() ? -1 : combo->findText(preferred);
    if (idx < 0) idx = combo->findText(QStringLiteral("Regular"));
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

static void style_color_button(QPushButton *button, uint32_t argb)
{
    if (!button) return;
    QColor c = color_from_argb(argb);
    button->setText(c.name(QColor::HexArgb));
    button->setToolTip(QString());
    button->setStyleSheet(QString(
        "QPushButton{color:%1;background:%2;border:1px solid #555;"
        "border-radius:3px;padding:3px 8px;}")
        .arg(c.lightness() < 128 ? "#fff" : "#000")
        .arg(c.name(QColor::HexArgb)));
}

static void style_gradient_button(QPushButton *button, uint32_t start_argb, uint32_t end_argb, int gradient_type)
{
    if (!button) return;
    QColor start = color_from_argb(start_argb);
    QColor end = color_from_argb(end_argb);
    button->setText(QString());
    const bool radial = gradient_type == 1;
    button->setToolTip(radial ? QStringLiteral("Radial Gradient") : QStringLiteral("Linear Gradient"));
    const QString fill = radial
        ? QStringLiteral("qradialgradient(cx:0.5,cy:0.5,radius:0.65,fx:0.5,fy:0.5,stop:0 %1,stop:1 %2)")
              .arg(start.name(QColor::HexArgb), end.name(QColor::HexArgb))
        : QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %1,stop:1 %2)")
              .arg(start.name(QColor::HexArgb), end.name(QColor::HexArgb));
    button->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;border:1px solid #555;border-radius:3px;padding:3px 8px;}").arg(fill));
}

static void style_color_button_mixed(QPushButton *button)
{
    if (!button) return;
    button->setText(QString());
    button->setToolTip(QStringLiteral("Mixed values"));
    button->setStyleSheet(
        "QPushButton{color:#d8d8d8;background:#252525;border:1px dashed #777;"
        "border-radius:3px;padding:3px 8px;}");
}

static void set_spin_mixed(QAbstractSpinBox *spin, bool mixed)
{
    if (!spin) return;
    if (auto *edit = spin->findChild<QLineEdit *>()) {
        if (mixed) edit->clear();
    }
    spin->setToolTip(mixed ? QStringLiteral("Mixed values") : QString());
}

static void set_combo_mixed(QComboBox *combo, bool mixed)
{
    if (!combo) return;
    if (mixed) combo->setCurrentIndex(-1);
    combo->setToolTip(mixed ? QStringLiteral("Mixed values") : QString());
}

static void set_button_mixed(QAbstractButton *button, bool mixed)
{
    if (!button) return;
    if (mixed)
        button->setChecked(false);
    button->setToolTip(mixed ? QStringLiteral("Mixed values") : QString());
    button->setStyleSheet(mixed
        ? QStringLiteral("QToolButton{color:#d8d8d8;background:#252525;border:1px dashed #777;"
                         "border-radius:2px;font-size:10px;font-weight:bold;padding:0;}"
                         "QToolButton:hover{background:#303030;border-color:#888;}")
        : QStringLiteral("QToolButton{color:#d8d8d8;background:#242424;border:1px solid #373737;border-radius:2px;"
                         "font-size:10px;font-weight:bold;padding:0;}"
                         "QToolButton:hover{background:#303030;border-color:#4a4a4a;}"
                         "QToolButton:checked{background:#4b6ea8;color:white;border-color:#6f8fc4;}"));
}

static QColor keyframe_color(EasingType)
{
    return C_KF_DOT;
}

static QPainterPath keyframe_shape_path(EasingType easing, const QPointF &center, qreal radius)
{
    const qreal x = center.x();
    const qreal y = center.y();
    QPainterPath path;

    switch (easing) {
    case EasingType::Linear:
        path.moveTo(x, y - radius);
        path.lineTo(x + radius, y);
        path.lineTo(x, y + radius);
        path.lineTo(x - radius, y);
        path.closeSubpath();
        break;
    case EasingType::EaseIn:
        path.moveTo(x - radius, y);
        path.lineTo(x, y - radius);
        path.lineTo(x + radius, y - radius);
        path.lineTo(x + radius, y + radius);
        path.lineTo(x, y + radius);
        path.closeSubpath();
        break;
    case EasingType::EaseOut:
        path.moveTo(x + radius, y);
        path.lineTo(x, y - radius);
        path.lineTo(x - radius, y - radius);
        path.lineTo(x - radius, y + radius);
        path.lineTo(x, y + radius);
        path.closeSubpath();
        break;
    case EasingType::EaseInOut:
        path.moveTo(x - radius, y - radius);
        path.lineTo(x + radius, y - radius);
        path.lineTo(x - radius, y + radius);
        path.lineTo(x + radius, y + radius);
        path.closeSubpath();
        break;
    case EasingType::Bezier:
        path.addEllipse(center, radius, radius);
        break;
    case EasingType::Hold:
        path.addRect(QRectF(x - radius, y - radius, radius * 2.0, radius * 2.0));
        break;
    default:
        path.moveTo(x, y - radius);
        path.lineTo(x + radius, y);
        path.lineTo(x, y + radius);
        path.lineTo(x - radius, y);
        path.closeSubpath();
        break;
    }

    return path;
}

static void draw_keyframe_marker(QPainter &painter, const QPointF &center, EasingType easing,
                                 qreal radius, const QColor &fill,
                                 const QColor &stroke, qreal stroke_width)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(fill);
    painter.setPen(QPen(stroke, stroke_width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.drawPath(keyframe_shape_path(easing, center, radius));

    if (easing == EasingType::Bezier) {
        painter.setPen(QPen(stroke, std::max<qreal>(1.0, stroke_width - 0.5)));
        painter.drawLine(QPointF(center.x() - radius - 3.0, center.y()),
                         QPointF(center.x() - radius, center.y()));
        painter.drawLine(QPointF(center.x() + radius, center.y()),
                         QPointF(center.x() + radius + 3.0, center.y()));
    }

    painter.restore();
}

static QString easing_label(EasingType easing)
{
    switch (easing) {
    case EasingType::Linear: return obsgs_tr("OBSTitles.Linear");
    case EasingType::EaseIn: return obsgs_tr("OBSTitles.EaseIn");
    case EasingType::EaseOut: return obsgs_tr("OBSTitles.EaseOut");
    case EasingType::EaseInOut: return obsgs_tr("OBSTitles.EasyEase");
    case EasingType::Bezier: return obsgs_tr("OBSTitles.CustomBezier");
    case EasingType::Hold: return obsgs_tr("OBSTitles.Hold");
    default: return obsgs_tr("OBSTitles.Linear");
    }
}

static std::vector<AnimatedProperty *> timeline_properties(Layer &layer)
{
    return {&layer.pos_x, &layer.pos_y,
            &layer.scale_x, &layer.scale_y,
            &layer.rotation, &layer.opacity,
            &layer.box_width, &layer.box_height,
            &layer.origin_x_prop, &layer.origin_y_prop,
            &layer.paragraph_indent_left_prop, &layer.paragraph_indent_right_prop,
            &layer.paragraph_indent_first_line_prop,
            &layer.text_color_a, &layer.text_color_r,
            &layer.text_color_g, &layer.text_color_b,
            &layer.fill_color_a, &layer.fill_color_r,
            &layer.fill_color_g, &layer.fill_color_b,
            &layer.background_enabled_prop, &layer.background_opacity_prop,
            &layer.background_padding_x_prop, &layer.background_padding_y_prop,
            &layer.background_corner_radius_prop,
            &layer.background_color_a, &layer.background_color_r,
            &layer.background_color_g, &layer.background_color_b,
            &layer.shadow_enabled_prop, &layer.shadow_opacity_prop,
            &layer.shadow_distance_prop, &layer.shadow_angle_prop,
            &layer.shadow_blur_prop, &layer.shadow_spread_prop,
            &layer.shadow_color_a, &layer.shadow_color_r,
            &layer.shadow_color_g, &layer.shadow_color_b};
}

static QString property_label(const std::string &name)
{
    if (name == "pos_x" || name == "pos_y") return obsgs_tr("OBSTitles.Position");
    if (name == "scale_x" || name == "scale_y") return obsgs_tr("OBSTitles.Scale");
    if (name == "box_width" || name == "box_height") return obsgs_tr("OBSTitles.Size");
    if (name == "origin_x" || name == "origin_y") return obsgs_tr("OBSTitles.Origin");
    if (name == "paragraph_indent_left") return obsgs_tr("OBSTitles.ParagraphIndentLeft");
    if (name == "paragraph_indent_right") return obsgs_tr("OBSTitles.ParagraphIndentRight");
    if (name == "paragraph_indent_first_line") return obsgs_tr("OBSTitles.ParagraphIndentFirstLine");
    if (name == "text_color_a" || name == "text_color_r" ||
        name == "text_color_g" || name == "text_color_b") return obsgs_tr("OBSTitles.TextColor");
    if (name == "fill_color_a" || name == "fill_color_r" ||
        name == "fill_color_g" || name == "fill_color_b") return obsgs_tr("OBSTitles.FillColor");
    if (name == "shadow_color_a" || name == "shadow_color_r" ||
        name == "shadow_color_g" || name == "shadow_color_b") return obsgs_tr("OBSTitles.ShadowColor");
    if (name == "background_color_a" || name == "background_color_r" ||
        name == "background_color_g" || name == "background_color_b") return obsgs_tr("OBSTitles.BackgroundColor");
    if (name == "background_enabled") return obsgs_tr("OBSTitles.EnableColorBackground");
    if (name == "background_opacity") return obsgs_tr("OBSTitles.BackgroundOpacityLabel");
    if (name == "background_padding_x") return obsgs_tr("OBSTitles.BackgroundHorizontalPaddingLabel");
    if (name == "background_padding_y") return obsgs_tr("OBSTitles.BackgroundVerticalPaddingLabel");
    if (name == "background_corner_radius") return obsgs_tr("OBSTitles.BackgroundCornerLabel");
    if (name == "shadow_enabled") return obsgs_tr("OBSTitles.ShadowEnable");
    if (name == "shadow_opacity") return obsgs_tr("OBSTitles.ShadowOpacity");
    if (name == "shadow_distance") return obsgs_tr("OBSTitles.ShadowDistance");
    if (name == "shadow_angle") return obsgs_tr("OBSTitles.ShadowAngle");
    if (name == "shadow_blur") return obsgs_tr("OBSTitles.ShadowBlur");
    if (name == "shadow_spread") return obsgs_tr("OBSTitles.ShadowSpread");
    if (name == "rotation") return obsgs_tr("OBSTitles.Rotation");
    if (name == "opacity") return obsgs_tr("OBSTitles.Opacity");
    return QString::fromStdString(name);
}

static QString property_value_text(const AnimatedProperty &prop, const Layer &layer)
{
    double value = prop.static_value;
    if (prop.name == "pos_x")
        return QString("%1,%2").arg(layer.pos_x.static_value, 0, 'f', 1)
                                .arg(layer.pos_y.static_value, 0, 'f', 1);
    if (prop.name == "scale_x")
        return QString("%1,%2%").arg(layer.scale_x.static_value * 100.0, 0, 'f', 1)
                                 .arg(layer.scale_y.static_value * 100.0, 0, 'f', 1);
    if (prop.name == "box_width")
        return QString("%1 × %2").arg(layer.box_width.static_value, 0, 'f', 0)
                                  .arg(layer.box_height.static_value, 0, 'f', 0);
    if (prop.name == "origin_x")
        return QString("%1,%2").arg(layer.origin_x_prop.static_value, 0, 'f', 2)
                                .arg(layer.origin_y_prop.static_value, 0, 'f', 2);
    if (prop.name == "opacity" || prop.name == "shadow_opacity" || prop.name == "background_opacity") value *= 100.0;
    if (prop.name == "shadow_enabled" || prop.name == "background_enabled") return value >= 0.5 ? obsgs_tr("OBSTitles.On") : obsgs_tr("OBSTitles.Off");
    return QString::number(value, 'f', (prop.name == "opacity" || prop.name == "shadow_opacity" || prop.name == "background_opacity") ? 1 : 2);
}

struct TimelineRow {
    std::shared_ptr<Layer> layer;
    AnimatedProperty *prop = nullptr;
    bool is_property = false;
};

static std::vector<TimelineRow> timeline_rows(const std::shared_ptr<Title> &title)
{
    std::vector<TimelineRow> rows;
    if (!title) return rows;
    for (auto it = title->layers.rbegin(); it != title->layers.rend(); ++it) {
        auto layer = *it;
        rows.push_back({layer, nullptr, false});
        if (!layer->properties_expanded) continue;
        std::set<std::string> seen;
        for (auto *prop : timeline_properties(*layer)) {
            if (!prop->is_animated()) continue;
            QString label = property_label(prop->name);
            std::string key = label.toStdString();
            if (seen.insert(key).second)
                rows.push_back({layer, prop, true});
        }
    }
    return rows;
}

/* ══════════════════════════════════════════════════════════════════
 *  TitleEditor
 * ══════════════════════════════════════════════════════════════════ */
TitleEditor::TitleEditor(QWidget *parent)
    : QMainWindow(parent, Qt::Window)
{
    setWindowTitle(obsgs_tr("OBSTitles.EditorWindowTitle"));
    resize(1280, 760);
    setMinimumSize(900, 600);

    /* Dark background */
    QPalette pal = palette();
    pal.setColor(QPalette::Window,     C_BG_DARK);
    pal.setColor(QPalette::WindowText, C_TEXT);
    pal.setColor(QPalette::Base,       C_BG_MID);
    pal.setColor(QPalette::AlternateBase, C_BG_LIGHT);
    pal.setColor(QPalette::Text,       C_TEXT);
    pal.setColor(QPalette::Button,     C_BG_LIGHT);
    pal.setColor(QPalette::ButtonText, C_TEXT);
    pal.setColor(QPalette::Highlight,  C_ACCENT);
    setPalette(pal);
    setAutoFillBackground(true);
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks |
                   QMainWindow::AnimatedDocks |
                   QMainWindow::GroupedDragging);

    build_ui();

    play_timer_ = new QTimer(this);
    play_timer_->setInterval(std::max(1, (int)std::round(obs_frame_duration() * 1000.0)));
    connect(play_timer_, &QTimer::timeout, this, &TitleEditor::tick);

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(33);
    connect(clock_timer_, &QTimer::timeout, this, [this]() {
        update_title_bar();
        if (canvas_) canvas_->update();
    });
    clock_timer_->start();

    qApp->installEventFilter(this);
}

TitleEditor::~TitleEditor()
{
    save_editor_layout();
    if (qApp)
        qApp->removeEventFilter(this);
}




static uint32_t color_button_argb(QPushButton *button)
{
    return button ? button->property("argb").toUInt() : 0xFFFFFFFF;
}

static void set_color_button_argb(QPushButton *button, uint32_t argb)
{
    if (!button) return;
    button->setProperty("argb", argb);
    QColor c = color_from_argb(argb);
    button->setText(c.name(QColor::HexArgb).toUpper());
    button->setStyleSheet(QStringLiteral("QPushButton{color:#fff;background:%1;border:1px solid #555;border-radius:3px;padding:3px 8px;}")
                          .arg(c.name()));
}

EffectsPanel::EffectsPanel(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *hint = new QLabel(QStringLiteral("Effect stack"), this);
    hint->setStyleSheet(QStringLiteral("color:#b8b8b8;font-weight:bold;"));
    layout->addWidget(hint);

    effect_list_ = new QListWidget(this);
    effect_list_->setObjectName(QStringLiteral("OBSGraphicsStudioProEffectsList"));
    effect_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    effect_list_->setAlternatingRowColors(true);
    effect_list_->setStyleSheet(QStringLiteral("QListWidget{background:#1a1a1a;border:1px solid #303030;color:#ddd;}QListWidget::item{padding:4px;}QListWidget::item:selected{background:#3b4f64;}"));
    layout->addWidget(effect_list_, 1);

    auto *button_bar = new QWidget(this);
    button_bar->setObjectName(QStringLiteral("OBSGraphicsStudioProEffectsButtonBar"));
    auto *button_layout = new QHBoxLayout(button_bar);
    button_layout->setContentsMargins(0, 0, 0, 0);
    button_layout->setSpacing(4);

    auto add_button = [button_bar, button_layout](const char *icon, const QString &tip) {
        auto *button = new QToolButton(button_bar);
        button->setIcon(obs_icon(icon));
        button->setIconSize(QSize(16, 16));
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button_layout->addWidget(button);
        return button;
    };

    auto *btn_add = add_button("add.svg", QStringLiteral("Add Effect"));
    btn_remove_ = add_button("delete.svg", QStringLiteral("Remove Effect"));
    btn_duplicate_ = add_button("duplicate.svg", QStringLiteral("Duplicate Effect"));
    btn_move_up_ = add_button("move-up.svg", QStringLiteral("Move Effect Up"));
    btn_move_down_ = add_button("move-down.svg", QStringLiteral("Move Effect Down"));
    button_layout->addStretch(1);
    layout->addWidget(button_bar);

    auto *settings_scroll = new QScrollArea(this);
    settings_scroll->setWidgetResizable(true);
    settings_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:#151515;border:none;}"));
    settings_container_ = new QWidget(settings_scroll);
    settings_layout_ = new QVBoxLayout(settings_container_);
    settings_layout_->setContentsMargins(0, 6, 0, 0);
    settings_layout_->setSpacing(6);
    settings_scroll->setWidget(settings_container_);
    layout->addWidget(settings_scroll, 2);

    connect(effect_list_, &QListWidget::currentRowChanged, this, [this](int row) {
        selected_index_ = row;
        load_settings();
    });

    connect(effect_list_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (loading_values_ || !layer_) return;
        int row = effect_list_->row(item);
        if (row < 0 || row >= (int)layer_->effects.size()) return;
        layer_->effects[row].enabled = item->checkState() == Qt::Checked;
        emit_effect_changed();
    });

    connect(btn_add, &QToolButton::clicked, this, [this, btn_add]() {
        if (!layer_) return;
        QMenu menu(btn_add);
        menu.setStyleSheet(QStringLiteral("QMenu{color:#ddd;background:#252525;border:1px solid #3a3a3a;}QMenu::item{padding:5px 22px;}QMenu::item:selected{background:#3b4f64;}"));
        const auto add_action = [&menu](const QString &name, LayerEffectType type) {
            auto *action = menu.addAction(name);
            action->setData((int)type);
        };
        add_action(obsgs_tr("OBSTitles.BackgroundColor"), LayerEffectType::BackgroundColor);
        add_action(obsgs_tr("OBSTitles.Outline"), LayerEffectType::Outline);
        add_action(obsgs_tr("OBSTitles.DropShadow"), LayerEffectType::DropShadow);
        add_action(obsgs_tr("OBSTitles.LongShadow"), LayerEffectType::LongShadow);
        add_action(obsgs_tr("OBSTitles.BrightnessContrast"), LayerEffectType::BrightnessContrast);
        add_action(obsgs_tr("OBSTitles.Saturation"), LayerEffectType::Saturation);
        add_action(obsgs_tr("OBSTitles.ColorOverlay"), LayerEffectType::ColorOverlay);
        add_action(obsgs_tr("OBSTitles.Glow"), LayerEffectType::Glow);
        add_action(obsgs_tr("OBSTitles.InnerGlow"), LayerEffectType::InnerGlow);
        add_action(obsgs_tr("OBSTitles.InnerShadow"), LayerEffectType::InnerShadow);
        add_action(obsgs_tr("OBSTitles.Blur"), LayerEffectType::Blur);
        add_action(obsgs_tr("OBSTitles.MotionBlur"), LayerEffectType::MotionBlur);
        QAction *chosen = menu.exec(btn_add->mapToGlobal(QPoint(0, btn_add->height())));
        if (!chosen) return;
        LayerEffect effect;
        effect.type = (LayerEffectType)chosen->data().toInt();
        effect.enabled = true;
        switch (effect.type) {
        case LayerEffectType::DropShadow:
        case LayerEffectType::InnerShadow:
            effect.blend_mode = EffectBlendMode::Multiply;
            effect.effect_color = 0x99000000;
            break;
        case LayerEffectType::LongShadow:
            effect.blend_mode = EffectBlendMode::Multiply;
            effect.effect_color = 0x99000000;
            effect.effect_distance = 120.0f;
            effect.effect_angle = 135.0f;
            effect.effect_size = 0.0f;
            effect.effect_opacity = 0.45f;
            break;
        case LayerEffectType::ColorOverlay:
            effect.blend_mode = EffectBlendMode::Color;
            effect.effect_color = effect.tint_color;
            effect.effect_opacity = effect.tint_amount;
            break;
        case LayerEffectType::Glow:
        case LayerEffectType::InnerGlow:
            effect.blend_mode = EffectBlendMode::Additive;
            effect.effect_color = 0xFFFFFFFF;
            effect.effect_opacity = 0.75f;
            break;
        case LayerEffectType::Blur:
            effect.effect_size = 12.0f;
            effect.effect_opacity = 1.0f;
            effect.effect_blur_type = (int)ShadowBlurType::Gaussian;
            break;
        case LayerEffectType::MotionBlur:
            effect.effect_size = 180.0f;
            effect.effect_angle = 0.0f;
            effect.effect_opacity = 1.0f;
            effect.effect_samples = 8;
            effect.effect_centered = true;
            break;
        default:
            break;
        }
        layer_->effects.push_back(effect);
        if (effect.type == LayerEffectType::BackgroundColor) { layer_->background_enabled = true; layer_->background_enabled_prop.static_value = 1.0; }
        if (effect.type == LayerEffectType::Outline) {
            layer_->outline_enabled = true;
            if (layer_->stroke_fill_type == 0) layer_->stroke_fill_type = 1;
            if (layer_->stroke_width <= 0.0f) layer_->stroke_width = 4.0f;
        }
        if (effect.type == LayerEffectType::DropShadow) { layer_->shadow_enabled = true; layer_->shadow_enabled_prop.static_value = 1.0; }
        if (effect.type == LayerEffectType::LongShadow) { layer_->long_shadow_enabled = true; if (layer_->long_shadow_length <= 0.0f) layer_->long_shadow_length = 120.0f; if (layer_->long_shadow_opacity <= 0.0f) layer_->long_shadow_opacity = 0.45f; }
        selected_index_ = (int)layer_->effects.size() - 1;
        rebuild_stack();
        emit_effect_changed();
    });

    connect(btn_remove_, &QToolButton::clicked, this, [this]() {
        if (!layer_ || selected_index_ < 0 || selected_index_ >= (int)layer_->effects.size()) return;
        layer_->effects.erase(layer_->effects.begin() + selected_index_);
        if (selected_index_ >= (int)layer_->effects.size()) selected_index_ = (int)layer_->effects.size() - 1;
        sync_legacy_enabled_flags();
        rebuild_stack();
        emit_effect_changed();
    });

    connect(btn_duplicate_, &QToolButton::clicked, this, [this]() {
        if (!layer_ || selected_index_ < 0 || selected_index_ >= (int)layer_->effects.size()) return;
        layer_->effects.insert(layer_->effects.begin() + selected_index_ + 1, layer_->effects[selected_index_]);
        ++selected_index_;
        sync_legacy_enabled_flags();
        rebuild_stack();
        emit_effect_changed();
    });

    connect(btn_move_up_, &QToolButton::clicked, this, [this]() {
        if (!layer_ || selected_index_ <= 0 || selected_index_ >= (int)layer_->effects.size()) return;
        std::swap(layer_->effects[selected_index_], layer_->effects[selected_index_ - 1]);
        --selected_index_;
        rebuild_stack();
        emit_effect_changed();
    });

    connect(btn_move_down_, &QToolButton::clicked, this, [this]() {
        if (!layer_ || selected_index_ < 0 || selected_index_ + 1 >= (int)layer_->effects.size()) return;
        std::swap(layer_->effects[selected_index_], layer_->effects[selected_index_ + 1]);
        ++selected_index_;
        rebuild_stack();
        emit_effect_changed();
    });

    rebuild_stack();
}

void EffectsPanel::set_layer(std::shared_ptr<Layer> layer, double playhead)
{
    layer_ = layer;
    playhead_ = playhead;
    selected_index_ = layer_ && !layer_->effects.empty() ? std::clamp(selected_index_, 0, (int)layer_->effects.size() - 1) : -1;
    rebuild_stack();
}

LayerEffect *EffectsPanel::selected_effect()
{
    if (!layer_ || selected_index_ < 0 || selected_index_ >= (int)layer_->effects.size()) return nullptr;
    return &layer_->effects[selected_index_];
}

const LayerEffect *EffectsPanel::selected_effect() const
{
    if (!layer_ || selected_index_ < 0 || selected_index_ >= (int)layer_->effects.size()) return nullptr;
    return &layer_->effects[selected_index_];
}

void EffectsPanel::sync_legacy_enabled_flags()
{
    if (!layer_) return;
    layer_->background_enabled = layer_effect_enabled(*layer_, LayerEffectType::BackgroundColor, false);
    layer_->outline_enabled = layer_effect_enabled(*layer_, LayerEffectType::Outline, false);
    layer_->shadow_enabled = layer_effect_enabled(*layer_, LayerEffectType::DropShadow, false);
    layer_->long_shadow_enabled = layer_effect_enabled(*layer_, LayerEffectType::LongShadow, false);
    layer_->background_enabled_prop.static_value = layer_->background_enabled ? 1.0 : 0.0;
    layer_->shadow_enabled_prop.static_value = layer_->shadow_enabled ? 1.0 : 0.0;
}

void EffectsPanel::emit_effect_changed()
{
    sync_legacy_enabled_flags();
    emit property_changed(!numeric_label_dragging_);
}

void EffectsPanel::rebuild_stack()
{
    loading_values_ = true;
    effect_list_->clear();
    if (!layer_) {
        effect_list_->addItem(QStringLiteral("Select a layer to edit effects"));
        if (auto *item = effect_list_->item(0)) item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        selected_index_ = -1;
    } else if (layer_->effects.empty()) {
        effect_list_->addItem(QStringLiteral("No effects added"));
        if (auto *item = effect_list_->item(0)) item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        selected_index_ = -1;
    } else {
        for (int i = 0; i < (int)layer_->effects.size(); ++i) {
            const auto &effect = layer_->effects[i];
            auto *item = new QListWidgetItem(effect_type_name(effect.type));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            item->setCheckState(effect.enabled ? Qt::Checked : Qt::Unchecked);
            effect_list_->addItem(item);
        }
        selected_index_ = std::clamp(selected_index_, 0, (int)layer_->effects.size() - 1);
        effect_list_->setCurrentRow(selected_index_);
    }
    loading_values_ = false;

    const bool has_selection = selected_effect() != nullptr;
    if (btn_remove_) btn_remove_->setEnabled(has_selection);
    if (btn_duplicate_) btn_duplicate_->setEnabled(has_selection);
    if (btn_move_up_) btn_move_up_->setEnabled(has_selection && selected_index_ > 0);
    if (btn_move_down_) btn_move_down_->setEnabled(has_selection && layer_ && selected_index_ + 1 < (int)layer_->effects.size());

    load_settings();
}

void EffectsPanel::build_settings()
{
    while (QLayoutItem *item = settings_layout_->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void EffectsPanel::load_settings()
{
    build_settings();
    if (!layer_ || !selected_effect()) {
        auto *label = new QLabel(layer_ ? QStringLiteral("Add an effect to edit its settings.") : QStringLiteral("Select a layer to edit effects."), settings_container_);
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral("color:#999;"));
        settings_layout_->addWidget(label);
        settings_layout_->addStretch(1);
        return;
    }

    loading_values_ = true;
    const double lt = std::clamp(playhead_ - layer_->in_time, 0.0, std::max(0.0, layer_->out_time - layer_->in_time));
    auto *box = new QGroupBox(effect_type_name(selected_effect()->type), settings_container_);
    box->setStyleSheet(QStringLiteral("QGroupBox{color:#d0d0d0;background:#1b1b1b;border:1px solid #303030;margin-top:8px;padding-top:14px;}QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 3px;}QDoubleSpinBox,QComboBox{color:#ddd;background:#252525;border:1px solid #363636;border-radius:2px;padding:1px 3px;}QPushButton{color:#ddd;background:#2a2a2a;border:1px solid #3f3f3f;border-radius:3px;padding:3px 8px;}"));
    auto *form = new QFormLayout(box);
    form->setContentsMargins(8, 8, 8, 8);
    form->setSpacing(6);
    auto spin = [box](double min, double max, double step) { auto *s = new QDoubleSpinBox(box); s->setRange(min, max); s->setSingleStep(step); s->setFixedHeight(22); return s; };
    auto combo = [box]() { auto *c = new QComboBox(box); c->setFixedHeight(22); return c; };
    auto color_button = [this, box](uint32_t argb, auto setter) {
        auto *button = new QPushButton(box);
        set_color_button_argb(button, argb);
        connect(button, &QPushButton::clicked, this, [this, button, setter]() {
            QColor picked = QColorDialog::getColor(color_from_argb(color_button_argb(button)), this, QStringLiteral("Choose Color"), QColorDialog::ShowAlphaChannel);
            if (!picked.isValid()) return;
            uint32_t argb = argb_from_color(picked);
            set_color_button_argb(button, argb);
            setter(argb);
            emit_effect_changed();
        });
        return button;
    };

    auto add_effect_row = [this, box, form](const QString &label_text, QWidget *field) {
        if (label_text.isEmpty()) {
            form->addRow(label_text, field);
            return;
        }
        auto *label = new NumericDragLabel(label_text, field, box,
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = true;
                                               emit property_changed(true);
                                           },
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = false;
                                               emit property_changed(true);
                                           });
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->addRow(label, field);
    };

    if (selected_effect()->type == LayerEffectType::BackgroundColor) {
        auto *fill = combo(); fill->addItem(obsgs_tr("OBSTitles.Solid"), 0); fill->addItem(obsgs_tr("OBSTitles.Gradient"), 1); fill->setCurrentIndex(fill->findData(layer_->background_fill_type));
        auto *color = color_button(eval_background_color(*layer_, lt), [this, lt](uint32_t argb){ layer_->background_color = argb; set_background_color_channels_at(*layer_, lt, argb); });
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(eval_background_opacity(*layer_, lt));
        auto *pad_x = spin(0.0, 1000.0, 1.0); pad_x->setValue(eval_background_padding_x(*layer_, lt));
        auto *pad_y = spin(0.0, 1000.0, 1.0); pad_y->setValue(eval_background_padding_y(*layer_, lt));
        auto *corner = spin(0.0, 1000.0, 1.0); corner->setValue(eval_background_corner_radius(*layer_, lt));
        auto *grad_type = combo(); grad_type->addItem(obsgs_tr("OBSTitles.LinearGradient"), 0); grad_type->addItem(obsgs_tr("OBSTitles.RadialGradient"), 1); grad_type->setCurrentIndex(grad_type->findData(layer_->background_gradient_type));
        auto *grad_start = color_button(layer_->background_gradient_start_color, [this](uint32_t argb){ layer_->background_gradient_start_color = argb; });
        auto *grad_end = color_button(layer_->background_gradient_end_color, [this](uint32_t argb){ layer_->background_gradient_end_color = argb; });
        auto *grad_start_pos = spin(0.0, 1.0, 0.01); grad_start_pos->setDecimals(2); grad_start_pos->setValue(layer_->background_gradient_start_pos);
        auto *grad_end_pos = spin(0.0, 1.0, 0.01); grad_end_pos->setDecimals(2); grad_end_pos->setValue(layer_->background_gradient_end_pos);
        auto *grad_start_op = spin(0.0, 1.0, 0.01); grad_start_op->setDecimals(2); grad_start_op->setValue(layer_->background_gradient_start_opacity);
        auto *grad_end_op = spin(0.0, 1.0, 0.01); grad_end_op->setDecimals(2); grad_end_op->setValue(layer_->background_gradient_end_opacity);
        auto *grad_op = spin(0.0, 1.0, 0.01); grad_op->setDecimals(2); grad_op->setValue(layer_->background_gradient_opacity);
        auto *grad_angle = spin(-360.0, 360.0, 1.0); grad_angle->setValue(layer_->background_gradient_angle);
        auto *grad_cx = spin(0.0, 1.0, 0.01); grad_cx->setDecimals(2); grad_cx->setValue(layer_->background_gradient_center_x);
        auto *grad_cy = spin(0.0, 1.0, 0.01); grad_cy->setDecimals(2); grad_cy->setValue(layer_->background_gradient_center_y);
        auto *grad_scale = spin(0.01, 10.0, 0.05); grad_scale->setDecimals(2); grad_scale->setValue(layer_->background_gradient_scale);
        auto *grad_fx = spin(0.0, 1.0, 0.01); grad_fx->setDecimals(2); grad_fx->setValue(layer_->background_gradient_focal_x);
        auto *grad_fy = spin(0.0, 1.0, 0.01); grad_fy->setDecimals(2); grad_fy->setValue(layer_->background_gradient_focal_y);
        add_effect_row(obsgs_tr("OBSTitles.BackgroundFillTypeLabel"), fill);
        add_effect_row(obsgs_tr("OBSTitles.BackgroundColorLabel"), color);
        add_effect_row(obsgs_tr("OBSTitles.BackgroundOpacityLabel"), opacity);
        add_effect_row(obsgs_tr("OBSTitles.BackgroundHorizontalPaddingLabel"), pad_x);
        add_effect_row(obsgs_tr("OBSTitles.BackgroundVerticalPaddingLabel"), pad_y);
        add_effect_row(obsgs_tr("OBSTitles.BackgroundCornerLabel"), corner);
        add_effect_row(obsgs_tr("OBSTitles.GradientTypeLabel"), grad_type);
        add_effect_row(obsgs_tr("OBSTitles.StartColorLabel"), grad_start);
        add_effect_row(obsgs_tr("OBSTitles.EndColorLabel"), grad_end);
        add_effect_row(obsgs_tr("OBSTitles.StartStopLabel"), grad_start_pos);
        add_effect_row(obsgs_tr("OBSTitles.EndStopLabel"), grad_end_pos);
        add_effect_row(obsgs_tr("OBSTitles.StartOpacityLabel"), grad_start_op);
        add_effect_row(obsgs_tr("OBSTitles.EndOpacityLabel"), grad_end_op);
        add_effect_row(obsgs_tr("OBSTitles.OpacityLabel"), grad_op);
        add_effect_row(obsgs_tr("OBSTitles.AngleLabel"), grad_angle);
        add_effect_row(obsgs_tr("OBSTitles.CenterXLabel"), grad_cx);
        add_effect_row(obsgs_tr("OBSTitles.CenterYLabel"), grad_cy);
        add_effect_row(obsgs_tr("OBSTitles.ScaleLabel"), grad_scale);
        add_effect_row(obsgs_tr("OBSTitles.FocalXLabel"), grad_fx);
        add_effect_row(obsgs_tr("OBSTitles.FocalYLabel"), grad_fy);
        connect(fill, QOverload<int>::of(&QComboBox::activated), this, [this, fill](int){ layer_->background_fill_type = fill->currentData().toInt(); emit_effect_changed(); });
        connect(grad_type, QOverload<int>::of(&QComboBox::activated), this, [this, grad_type](int){ layer_->background_gradient_type = grad_type->currentData().toInt(); emit_effect_changed(); });
        connect(grad_start_pos, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_) { layer_->background_gradient_start_pos = v; emit_effect_changed(); }});
        connect(grad_end_pos, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_) { layer_->background_gradient_end_pos = v; emit_effect_changed(); }});
        connect(grad_start_op, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_) { layer_->background_gradient_start_opacity = v; emit_effect_changed(); }});
        connect(grad_end_op, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_) { layer_->background_gradient_end_opacity = v; emit_effect_changed(); }});
        connect(grad_op, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_) { layer_->background_gradient_opacity = v; emit_effect_changed(); }});
        connect(grad_angle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_) { layer_->background_gradient_angle = v; emit_effect_changed(); }});
        connect(grad_cx, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_) { layer_->background_gradient_center_x = v; emit_effect_changed(); }});
        connect(grad_cy, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_) { layer_->background_gradient_center_y = v; emit_effect_changed(); }});
        connect(grad_scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_) { layer_->background_gradient_scale = v; emit_effect_changed(); }});
        connect(grad_fx, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_) { layer_->background_gradient_focal_x = v; emit_effect_changed(); }});
        connect(grad_fy, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_) { layer_->background_gradient_focal_y = v; emit_effect_changed(); }});
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_) { layer_->background_opacity = v; set_animated_value(layer_->background_opacity_prop, lt, v); emit_effect_changed(); }});
        connect(pad_x, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_) { layer_->background_padding_x = v; set_animated_value(layer_->background_padding_x_prop, lt, v); emit_effect_changed(); }});
        connect(pad_y, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_) { layer_->background_padding_y = v; set_animated_value(layer_->background_padding_y_prop, lt, v); emit_effect_changed(); }});
        connect(corner, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_) { layer_->background_corner_radius = v; set_animated_value(layer_->background_corner_radius_prop, lt, v); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::Outline) {
        auto *color = color_button(layer_->stroke_color, [this](uint32_t argb){ layer_->stroke_color = argb; });
        auto *width = spin(0.0, 200.0, 1.0); width->setValue(layer_->stroke_width);
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(layer_->outline_opacity);
        auto *join = combo(); join->addItem(obsgs_tr("OBSTitles.Miter"), 0); join->addItem(obsgs_tr("OBSTitles.Round"), 1); join->addItem(obsgs_tr("OBSTitles.Bevel"), 2); join->setCurrentIndex(join->findData(layer_->outline_join_style));
        auto *position = combo(); position->addItem(obsgs_tr("OBSTitles.Back"), 0); position->addItem(obsgs_tr("OBSTitles.Front"), 1); position->setCurrentIndex(layer_->outline_on_front ? 1 : 0);
        auto *aa = new QCheckBox(obsgs_tr("OBSTitles.AntialiasOutline"), box); aa->setChecked(layer_->outline_antialias);
        add_effect_row(obsgs_tr("OBSTitles.ColorLabel"), color);
        add_effect_row(obsgs_tr("OBSTitles.ThicknessLabel"), width);
        add_effect_row(obsgs_tr("OBSTitles.OpacityLabel"), opacity);
        add_effect_row(obsgs_tr("OBSTitles.JoinLabel"), join);
        add_effect_row(obsgs_tr("OBSTitles.PositionLabelIndented"), position);
        add_effect_row(QString(), aa);
        connect(width, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_) { layer_->stroke_width = v; emit_effect_changed(); }});
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_) { layer_->outline_opacity = v; emit_effect_changed(); }});
        connect(join, QOverload<int>::of(&QComboBox::activated), this, [this, join](int){ layer_->outline_join_style = join->currentData().toInt(); emit_effect_changed(); });
        connect(position, QOverload<int>::of(&QComboBox::activated), this, [this, position](int){ layer_->outline_on_front = position->currentData().toInt() == 1; emit_effect_changed(); });
        connect(aa, &QCheckBox::toggled, this, [this](bool v){ if (!loading_values_) { layer_->outline_antialias = v; emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::DropShadow) {
        LayerEffect *effect = selected_effect();
        auto *preset = combo(); preset->addItems({obsgs_tr("OBSTitles.Custom"), obsgs_tr("OBSTitles.Soft"), obsgs_tr("OBSTitles.Medium"), obsgs_tr("OBSTitles.Strong"), obsgs_tr("OBSTitles.Broadcast")});
        auto *blur_type = combo(); add_shadow_blur_items(blur_type); blur_type->setCurrentIndex(blur_type->findData(effect->effect_blur_type));
        auto *color = color_button(effect->effect_color, [this](uint32_t argb){ if (selected_effect()) selected_effect()->effect_color = argb; });
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(effect->effect_opacity);
        auto *dist = spin(0.0, 4096.0, 1.0); dist->setValue(effect->effect_distance);
        auto *angle = spin(-360.0, 360.0, 5.0); angle->setValue(effect->effect_angle);
        auto *blur = spin(0.0, 512.0, 1.0); blur->setValue(effect->effect_size);
        auto *spread = spin(0.0, 512.0, 1.0); spread->setValue(effect->effect_spread);
        auto *blend = combo(); add_blend_mode_items(blend); blend->setCurrentIndex(blend->findData((int)effect->blend_mode));
        add_effect_row(obsgs_tr("OBSTitles.PresetLabel"), preset);
        add_effect_row(obsgs_tr("OBSTitles.ColorLabel"), color);
        add_effect_row(obsgs_tr("OBSTitles.OpacityLabel"), opacity);
        add_effect_row(obsgs_tr("OBSTitles.DistanceLabel"), dist);
        add_effect_row(obsgs_tr("OBSTitles.AngleLabel"), angle);
        add_effect_row(obsgs_tr("OBSTitles.BlurTypeLabel"), blur_type);
        add_effect_row(obsgs_tr("OBSTitles.BlurLabel"), blur);
        add_effect_row(obsgs_tr("OBSTitles.SpreadLabel"), spread);
        add_effect_row(obsgs_tr("OBSTitles.BlendingModeLabel"), blend);
        connect(blur_type, QOverload<int>::of(&QComboBox::activated), this, [this, blur_type](int){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_blur_type = blur_type->currentData().toInt(); emit_effect_changed(); }});
        connect(blend, QOverload<int>::of(&QComboBox::activated), this, [this, blend](int){ if (!loading_values_ && selected_effect()) { selected_effect()->blend_mode = (EffectBlendMode)blend->currentData().toInt(); emit_effect_changed(); }});
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; emit_effect_changed(); }});
        connect(dist, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_distance = (float)v; emit_effect_changed(); }});
        connect(angle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_angle = (float)v; emit_effect_changed(); }});
        connect(blur, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size = (float)v; emit_effect_changed(); }});
        connect(spread, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_spread = (float)v; emit_effect_changed(); }});
        connect(preset, QOverload<int>::of(&QComboBox::activated), this, [this, preset]() {
            auto *effect = selected_effect();
            if (!effect) return;
            switch (preset->currentIndex()) {
            case 1: effect->effect_opacity = 0.35f; effect->effect_distance = 4.0f; effect->effect_size = 10.0f; effect->effect_spread = 0.0f; break;
            case 2: effect->effect_opacity = 0.55f; effect->effect_distance = 8.0f; effect->effect_size = 8.0f; effect->effect_spread = 0.0f; break;
            case 3: effect->effect_opacity = 0.75f; effect->effect_distance = 12.0f; effect->effect_size = 6.0f; effect->effect_spread = 0.0f; break;
            case 4: effect->effect_opacity = 0.6f; effect->effect_distance = 6.0f; effect->effect_size = 3.0f; effect->effect_spread = 2.0f; break;
            default: return;
            }
            emit_effect_changed();
            load_settings();
        });
    } else if (selected_effect()->type == LayerEffectType::BrightnessContrast) {
        LayerEffect *effect = selected_effect();
        auto *brightness = spin(-1.0, 1.0, 0.01); brightness->setDecimals(2); brightness->setValue(effect->brightness);
        auto *contrast = spin(0.0, 4.0, 0.05); contrast->setDecimals(2); contrast->setValue(effect->contrast);
        add_effect_row(obsgs_tr("OBSTitles.BrightnessLabel"), brightness);
        add_effect_row(obsgs_tr("OBSTitles.ContrastLabel"), contrast);
        connect(brightness, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->brightness = (float)v; emit_effect_changed(); }});
        connect(contrast, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->contrast = (float)v; emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::Saturation) {
        LayerEffect *effect = selected_effect();
        auto *saturation = spin(0.0, 4.0, 0.05); saturation->setDecimals(2); saturation->setValue(effect->saturation);
        add_effect_row(obsgs_tr("OBSTitles.SaturationLabel"), saturation);
        connect(saturation, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->saturation = (float)v; emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::ColorOverlay) {
        LayerEffect *effect = selected_effect();
        auto *color = color_button(effect->effect_color, [this](uint32_t argb){ if (selected_effect()) { selected_effect()->effect_color = argb; selected_effect()->tint_color = argb; } });
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(effect->effect_opacity);
        auto *blend = combo(); add_blend_mode_items(blend); blend->setCurrentIndex(blend->findData((int)effect->blend_mode));
        add_effect_row(obsgs_tr("OBSTitles.ColorOverlayColorLabel"), color);
        add_effect_row(obsgs_tr("OBSTitles.OpacityLabel"), opacity);
        add_effect_row(obsgs_tr("OBSTitles.BlendingModeLabel"), blend);
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; selected_effect()->tint_amount = (float)v; emit_effect_changed(); }});
        connect(blend, QOverload<int>::of(&QComboBox::activated), this, [this, blend](int){ if (!loading_values_ && selected_effect()) { selected_effect()->blend_mode = (EffectBlendMode)blend->currentData().toInt(); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::Glow || selected_effect()->type == LayerEffectType::InnerGlow) {
        LayerEffect *effect = selected_effect();
        auto *color = color_button(effect->effect_color, [this](uint32_t argb){ if (selected_effect()) selected_effect()->effect_color = argb; });
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(effect->effect_opacity);
        auto *size = spin(0.0, 512.0, 1.0); size->setValue(effect->effect_size);
        auto *blur_type = combo(); add_shadow_blur_items(blur_type); blur_type->setCurrentIndex(blur_type->findData(effect->effect_blur_type));
        auto *blend = combo(); add_blend_mode_items(blend); blend->setCurrentIndex(blend->findData((int)effect->blend_mode));
        add_effect_row(obsgs_tr("OBSTitles.ColorLabel"), color);
        add_effect_row(obsgs_tr("OBSTitles.OpacityLabel"), opacity);
        add_effect_row(obsgs_tr("OBSTitles.SizeRadiusLabel"), size);
        add_effect_row(obsgs_tr("OBSTitles.BlurTypeLabel"), blur_type);
        add_effect_row(obsgs_tr("OBSTitles.BlendingModeLabel"), blend);
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; emit_effect_changed(); }});
        connect(size, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size = (float)v; emit_effect_changed(); }});
        connect(blur_type, QOverload<int>::of(&QComboBox::activated), this, [this, blur_type](int){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_blur_type = blur_type->currentData().toInt(); emit_effect_changed(); }});
        connect(blend, QOverload<int>::of(&QComboBox::activated), this, [this, blend](int){ if (!loading_values_ && selected_effect()) { selected_effect()->blend_mode = (EffectBlendMode)blend->currentData().toInt(); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::Blur) {
        LayerEffect *effect = selected_effect();
        auto *amount = spin(0.0, 1.0, 0.05); amount->setDecimals(2); amount->setValue(effect->effect_opacity);
        auto *size = spin(0.0, 512.0, 1.0); size->setValue(effect->effect_size);
        auto *blur_type = combo(); add_shadow_blur_items(blur_type); blur_type->setCurrentIndex(blur_type->findData(effect->effect_blur_type));
        add_effect_row(obsgs_tr("OBSTitles.AmountLabel"), amount);
        add_effect_row(obsgs_tr("OBSTitles.SizeRadiusLabel"), size);
        add_effect_row(obsgs_tr("OBSTitles.BlurTypeLabel"), blur_type);
        connect(amount, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; emit_effect_changed(); }});
        connect(size, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size = (float)v; emit_effect_changed(); }});
        connect(blur_type, QOverload<int>::of(&QComboBox::activated), this, [this, blur_type](int){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_blur_type = blur_type->currentData().toInt(); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::MotionBlur) {
        LayerEffect *effect = selected_effect();
        auto *amount = spin(0.0, 1.0, 0.05); amount->setDecimals(2); amount->setValue(effect->effect_opacity);
        auto *shutter = spin(0.0, 720.0, 5.0); shutter->setValue(effect->effect_size);
        auto *samples = new QSpinBox(box); samples->setRange(2, 64); samples->setValue(effect->effect_samples); samples->setFixedHeight(22);
        auto *centered = new QCheckBox(obsgs_tr("OBSTitles.MotionBlurCentered"), box); centered->setChecked(effect->effect_centered);
        add_effect_row(obsgs_tr("OBSTitles.AmountLabel"), amount);
        add_effect_row(obsgs_tr("OBSTitles.ShutterAngleLabel"), shutter);
        add_effect_row(obsgs_tr("OBSTitles.SamplesLabel"), samples);
        add_effect_row(QString(), centered);
        connect(amount, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; emit_effect_changed(); }});
        connect(shutter, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size = (float)v; emit_effect_changed(); }});
        connect(samples, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_samples = v; emit_effect_changed(); }});
        connect(centered, &QCheckBox::toggled, this, [this](bool v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_centered = v; emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::InnerShadow) {
        LayerEffect *effect = selected_effect();
        auto *color = color_button(effect->effect_color, [this](uint32_t argb){ if (selected_effect()) selected_effect()->effect_color = argb; });
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(effect->effect_opacity);
        auto *dist = spin(0.0, 4096.0, 1.0); dist->setValue(effect->effect_distance);
        auto *angle = spin(-360.0, 360.0, 5.0); angle->setValue(effect->effect_angle);
        auto *size = spin(0.0, 512.0, 1.0); size->setValue(effect->effect_size);
        auto *blur_type = combo(); add_shadow_blur_items(blur_type); blur_type->setCurrentIndex(blur_type->findData(effect->effect_blur_type));
        auto *blend = combo(); add_blend_mode_items(blend); blend->setCurrentIndex(blend->findData((int)effect->blend_mode));
        add_effect_row(obsgs_tr("OBSTitles.ColorLabel"), color);
        add_effect_row(obsgs_tr("OBSTitles.OpacityLabel"), opacity);
        add_effect_row(obsgs_tr("OBSTitles.DistanceLabel"), dist);
        add_effect_row(obsgs_tr("OBSTitles.AngleLabel"), angle);
        add_effect_row(obsgs_tr("OBSTitles.SizeRadiusLabel"), size);
        add_effect_row(obsgs_tr("OBSTitles.BlurTypeLabel"), blur_type);
        add_effect_row(obsgs_tr("OBSTitles.BlendingModeLabel"), blend);
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; emit_effect_changed(); }});
        connect(dist, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_distance = (float)v; emit_effect_changed(); }});
        connect(angle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_angle = (float)v; emit_effect_changed(); }});
        connect(size, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size = (float)v; emit_effect_changed(); }});
        connect(blur_type, QOverload<int>::of(&QComboBox::activated), this, [this, blur_type](int){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_blur_type = blur_type->currentData().toInt(); emit_effect_changed(); }});
        connect(blend, QOverload<int>::of(&QComboBox::activated), this, [this, blend](int){ if (!loading_values_ && selected_effect()) { selected_effect()->blend_mode = (EffectBlendMode)blend->currentData().toInt(); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::LongShadow) {
        LayerEffect *effect = selected_effect();
        auto *color = color_button(effect->effect_color, [this](uint32_t argb){ if (selected_effect()) selected_effect()->effect_color = argb; });
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(effect->effect_opacity);
        auto *length = spin(0.0, 4096.0, 5.0); length->setValue(effect->effect_distance);
        auto *angle = spin(-360.0, 360.0, 5.0); angle->setValue(effect->effect_angle);
        auto *falloff = spin(0.0, 8.0, 0.1); falloff->setDecimals(2); falloff->setValue(effect->effect_falloff);
        auto *blur_type = combo(); blur_type->addItem(obsgs_tr("OBSTitles.NoBlur"), (int)LongShadowBlurType::None); blur_type->addItem(obsgs_tr("OBSTitles.BoxBlur"), (int)LongShadowBlurType::Box); blur_type->addItem(obsgs_tr("OBSTitles.GaussianBlur"), (int)LongShadowBlurType::Gaussian); blur_type->addItem(obsgs_tr("OBSTitles.StackFastBlur"), (int)LongShadowBlurType::StackFast); blur_type->setCurrentIndex(blur_type->findData(effect->effect_blur_type));
        auto *blur = spin(0.0, 512.0, 1.0); blur->setValue(effect->effect_size);
        auto *blend = combo(); add_blend_mode_items(blend); blend->setCurrentIndex(blend->findData((int)effect->blend_mode));
        add_effect_row(obsgs_tr("OBSTitles.LongShadowColor"), color);
        add_effect_row(obsgs_tr("OBSTitles.LongShadowOpacity"), opacity);
        add_effect_row(obsgs_tr("OBSTitles.LongShadowLength"), length);
        add_effect_row(obsgs_tr("OBSTitles.LongShadowAngle"), angle);
        add_effect_row(obsgs_tr("OBSTitles.LongShadowFalloff"), falloff);
        add_effect_row(obsgs_tr("OBSTitles.LongShadowBlurType"), blur_type);
        add_effect_row(obsgs_tr("OBSTitles.LongShadowBlur"), blur);
        add_effect_row(obsgs_tr("OBSTitles.BlendingModeLabel"), blend);
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; emit_effect_changed(); }});
        connect(length, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_distance = (float)v; emit_effect_changed(); }});
        connect(angle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_angle = (float)v; emit_effect_changed(); }});
        connect(falloff, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_falloff = (float)v; emit_effect_changed(); }});
        connect(blur_type, QOverload<int>::of(&QComboBox::activated), this, [this, blur_type](int){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_blur_type = blur_type->currentData().toInt(); emit_effect_changed(); }});
        connect(blur, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size = (float)v; emit_effect_changed(); }});
        connect(blend, QOverload<int>::of(&QComboBox::activated), this, [this, blend](int){ if (!loading_values_ && selected_effect()) { selected_effect()->blend_mode = (EffectBlendMode)blend->currentData().toInt(); emit_effect_changed(); }});
    }
    settings_layout_->addWidget(box);
    settings_layout_->addStretch(1);
    loading_values_ = false;
}

QWidget *TitleEditor::create_effects_panel()
{
    effects_panel_ = new EffectsPanel(this);
    connect(effects_panel_, &EffectsPanel::property_changed, this, &TitleEditor::on_title_modified);
    return effects_panel_;
}

QWidget *TitleEditor::create_styles_panel()
{
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("OBSGraphicsStudioProStylesTabs"));
    tabs->setDocumentMode(true);

    auto make_tab = [](const QString &title, const QString &description) {
        auto *tab = new QWidget;
        auto *layout = new QVBoxLayout(tab);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(6);
        auto *heading = new QLabel(title, tab);
        heading->setStyleSheet(QStringLiteral("color:#f0f0f0;font-weight:bold;"));
        auto *body = new QLabel(description, tab);
        body->setWordWrap(true);
        body->setStyleSheet(QStringLiteral("color:#b8b8b8;"));
        layout->addWidget(heading);
        layout->addWidget(body);
        layout->addStretch(1);
        return tab;
    };

    tabs->addTab(make_tab(QStringLiteral("Text styles"),
                          QStringLiteral("Reusable typography presets, text treatments, and layer text settings will be managed here.")),
                 QStringLiteral("Text"));
    tabs->addTab(make_tab(QStringLiteral("Gradient styles"),
                          QStringLiteral("Reusable foreground and background gradient presets will be managed here.")),
                 QStringLiteral("Gradient"));
    tabs->addTab(make_tab(QStringLiteral("Pattern styles"),
                          QStringLiteral("Reusable pattern, texture, and fill presets will be managed here.")),
                 QStringLiteral("Pattern"));
    tabs->addTab(make_tab(QStringLiteral("Style presets"),
                          QStringLiteral("Saved style preset libraries and shared style settings will be organized here.")),
                 QStringLiteral("Presets"));

    return tabs;
}

QWidget *TitleEditor::create_color_swatches_panel()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *hint = new QLabel(QStringLiteral("Reusable color palettes"), panel);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#b8b8b8;font-weight:bold;"));
    layout->addWidget(hint);

    auto *grid_widget = new QWidget(panel);
    auto *grid = new QGridLayout(grid_widget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);

    const std::array<QColor, 24> colors = {
        QColor("#ffffff"), QColor("#d9d9d9"), QColor("#a6a6a6"), QColor("#6f6f6f"),
        QColor("#262626"), QColor("#000000"), QColor("#ff4b4b"), QColor("#ff9f1c"),
        QColor("#ffd166"), QColor("#2ec4b6"), QColor("#00a8e8"), QColor("#7b61ff"),
        QColor("#f72585"), QColor("#b5179e"), QColor("#7209b7"), QColor("#3a0ca3"),
        QColor("#4361ee"), QColor("#4cc9f0"), QColor("#52b788"), QColor("#95d5b2"),
        QColor("#f4a261"), QColor("#e76f51"), QColor("#8d6e63"), QColor("#3d405b")
    };

    for (int i = 0; i < (int)colors.size(); ++i) {
        auto *swatch = new QToolButton(grid_widget);
        swatch->setObjectName(QStringLiteral("OBSGraphicsStudioProColorSwatch"));
        swatch->setFixedSize(24, 24);
        swatch->setAutoRaise(false);
        swatch->setToolTip(colors[i].name(QColor::HexRgb).toUpper());
        swatch->setStyleSheet(QStringLiteral("QToolButton{background:%1;border:1px solid #555;border-radius:3px;}"
                                             "QToolButton:hover{border:2px solid #fff;}" ).arg(colors[i].name()));
        grid->addWidget(swatch, i / 6, i % 6);
    }

    layout->addWidget(grid_widget, 0, Qt::AlignTop | Qt::AlignLeft);
    auto *footer = new QLabel(QStringLiteral("Palette saving, palette import/export, and quick color application workflows will build on these swatches."), panel);
    footer->setWordWrap(true);
    footer->setStyleSheet(QStringLiteral("color:#9f9f9f;"));
    layout->addWidget(footer);
    layout->addStretch(1);

    return panel;
}

void TitleEditor::create_docked_panel_menu(QMenuBar *menu_bar)
{
    if (!menu_bar) return;

    auto *windows_menu = menu_bar->addMenu(QStringLiteral("Windows"));

    act_lock_panels_ = windows_menu->addAction(QStringLiteral("Lock Panels"));
    act_lock_panels_->setCheckable(true);
    connect(act_lock_panels_, &QAction::toggled, this, &TitleEditor::set_panels_locked);

    QAction *reset_layout_action = windows_menu->addAction(QStringLiteral("Reset to Default Layout"));
    connect(reset_layout_action, &QAction::triggered, this, &TitleEditor::reset_default_layout);

    windows_menu->addSeparator();
    act_tools_visible_ = windows_menu->addAction(QStringLiteral("Tools"));
    act_tools_visible_->setCheckable(true);
    act_tools_visible_->setChecked(true);
    connect(act_tools_visible_, &QAction::triggered, this, [this](bool visible) {
        if (tools_dock_) tools_dock_->setVisible(visible);
    });

    act_graphic_props_visible_ = windows_menu->addAction(QStringLiteral("Graphic Properties"));
    act_graphic_props_visible_->setCheckable(true);
    act_graphic_props_visible_->setChecked(true);
    connect(act_graphic_props_visible_, &QAction::triggered, this, [this](bool visible) {
        if (graphic_props_dock_) graphic_props_dock_->setVisible(visible);
    });

    act_layer_props_visible_ = windows_menu->addAction(QStringLiteral("Layer Properties"));
    act_layer_props_visible_->setCheckable(true);
    act_layer_props_visible_->setChecked(true);
    connect(act_layer_props_visible_, &QAction::triggered, this, [this](bool visible) {
        if (layer_props_dock_) layer_props_dock_->setVisible(visible);
    });

    act_effects_visible_ = windows_menu->addAction(QStringLiteral("Effects"));
    act_effects_visible_->setCheckable(true);
    act_effects_visible_->setChecked(true);
    connect(act_effects_visible_, &QAction::triggered, this, [this](bool visible) {
        if (effects_dock_) effects_dock_->setVisible(visible);
    });

    act_styles_visible_ = windows_menu->addAction(QStringLiteral("Styles"));
    act_styles_visible_->setCheckable(true);
    act_styles_visible_->setChecked(true);
    connect(act_styles_visible_, &QAction::triggered, this, [this](bool visible) {
        if (styles_dock_) styles_dock_->setVisible(visible);
    });

    act_color_swatches_visible_ = windows_menu->addAction(QStringLiteral("Color Swatches"));
    act_color_swatches_visible_->setCheckable(true);
    act_color_swatches_visible_->setChecked(true);
    connect(act_color_swatches_visible_, &QAction::triggered, this, [this](bool visible) {
        if (color_swatches_dock_) color_swatches_dock_->setVisible(visible);
    });

    act_timeline_visible_ = windows_menu->addAction(QStringLiteral("Timeline"));
    act_timeline_visible_->setCheckable(true);
    act_timeline_visible_->setChecked(true);
    connect(act_timeline_visible_, &QAction::triggered, this, [this](bool visible) {
        if (timeline_dock_) timeline_dock_->setVisible(visible);
    });
}

QDockWidget *TitleEditor::create_editor_dock(const QString &object_name, const QString &title, QWidget *panel)
{
    auto *dock = new QDockWidget(title, this);
    dock->setObjectName(object_name);
    const bool timeline_dock = object_name == QString::fromUtf8(kTimelineDockObjectName);
    dock->setAllowedAreas(timeline_dock
                              ? (Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea)
                              : (Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea));
    dock->setWidget(panel);
    dock->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    dock->setMinimumWidth(panel ? panel->minimumWidth() : 220);
    if (timeline_dock)
        dock->setMinimumHeight(panel ? panel->minimumHeight() : 180);

    QAction *visibility_action = nullptr;
    if (object_name == QString::fromUtf8(kGraphicPropertiesDockObjectName))
        visibility_action = act_graphic_props_visible_;
    else if (object_name == QString::fromUtf8(kLayerPropertiesDockObjectName))
        visibility_action = act_layer_props_visible_;
    else if (object_name == QString::fromUtf8(kEffectsDockObjectName))
        visibility_action = act_effects_visible_;
    else if (object_name == QString::fromUtf8(kStylesDockObjectName))
        visibility_action = act_styles_visible_;
    else if (object_name == QString::fromUtf8(kColorSwatchesDockObjectName))
        visibility_action = act_color_swatches_visible_;
    else if (object_name == QString::fromUtf8(kTimelineDockObjectName))
        visibility_action = act_timeline_visible_;
    else if (object_name == QStringLiteral("OBSGraphicsStudioProToolsDock"))
        visibility_action = act_tools_visible_;

    if (visibility_action) {
        connect(dock, &QDockWidget::visibilityChanged, this, [visibility_action](bool visible) {
            QSignalBlocker blocker(visibility_action);
            visibility_action->setChecked(visible);
        });
    }

    connect(dock, &QDockWidget::topLevelChanged, this, [this]() { save_editor_layout(); });
    connect(dock, &QDockWidget::dockLocationChanged, this, [this]() { save_editor_layout(); });
    connect(dock, &QDockWidget::visibilityChanged, this, [this]() { save_editor_layout(); });
    return dock;
}

void TitleEditor::load_editor_layout()
{
    restoring_editor_layout_ = true;

    QSettings settings(QStringLiteral("OBSGraphicsStudioPro"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kEditorLayoutSettingsGroup));

    const QByteArray geometry = settings.value(QString::fromUtf8(kEditorGeometryKey)).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    panels_locked_ = settings.value(QString::fromUtf8(kEditorPanelsLockedKey), panels_locked_).toBool();
    if (act_lock_panels_) {
        QSignalBlocker blocker(act_lock_panels_);
        act_lock_panels_->setChecked(panels_locked_);
    }

    const QByteArray window_state = settings.value(QString::fromUtf8(kEditorWindowStateKey)).toByteArray();
    if (!window_state.isEmpty())
        restoreState(window_state);

    settings.endGroup();

    if (act_tools_visible_ && tools_dock_) {
        QSignalBlocker blocker(act_tools_visible_);
        act_tools_visible_->setChecked(!tools_dock_->isHidden());
    }
    if (act_graphic_props_visible_ && graphic_props_dock_) {
        QSignalBlocker blocker(act_graphic_props_visible_);
        act_graphic_props_visible_->setChecked(!graphic_props_dock_->isHidden());
    }
    if (act_layer_props_visible_ && layer_props_dock_) {
        QSignalBlocker blocker(act_layer_props_visible_);
        act_layer_props_visible_->setChecked(!layer_props_dock_->isHidden());
    }
    if (act_effects_visible_ && effects_dock_) {
        QSignalBlocker blocker(act_effects_visible_);
        act_effects_visible_->setChecked(!effects_dock_->isHidden());
    }
    if (act_styles_visible_ && styles_dock_) {
        QSignalBlocker blocker(act_styles_visible_);
        act_styles_visible_->setChecked(!styles_dock_->isHidden());
    }
    if (act_color_swatches_visible_ && color_swatches_dock_) {
        QSignalBlocker blocker(act_color_swatches_visible_);
        act_color_swatches_visible_->setChecked(!color_swatches_dock_->isHidden());
    }
    if (act_timeline_visible_ && timeline_dock_) {
        QSignalBlocker blocker(act_timeline_visible_);
        act_timeline_visible_->setChecked(!timeline_dock_->isHidden());
    }

    restoring_editor_layout_ = false;
    update_panel_lock_state();
}

void TitleEditor::save_editor_layout() const
{
    if (restoring_editor_layout_ || editor_layout_save_suppressed_)
        return;

    QSettings settings(QStringLiteral("OBSGraphicsStudioPro"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kEditorLayoutSettingsGroup));
    settings.setValue(QString::fromUtf8(kEditorGeometryKey), saveGeometry());
    settings.setValue(QString::fromUtf8(kEditorWindowStateKey), saveState());
    settings.setValue(QString::fromUtf8(kEditorPanelsLockedKey), panels_locked_);
    settings.endGroup();
    settings.sync();
}

void TitleEditor::reset_default_layout()
{
    restoring_editor_layout_ = true;

    const QDockWidget::DockWidgetFeatures reset_features =
        QDockWidget::DockWidgetClosable |
        QDockWidget::DockWidgetMovable |
        QDockWidget::DockWidgetFloatable;
    for (auto *dock : {tools_dock_, graphic_props_dock_, layer_props_dock_, effects_dock_, styles_dock_, color_swatches_dock_, timeline_dock_}) {
        if (!dock) continue;
        dock->setMaximumWidth(dock == tools_dock_ ? 64 : QWIDGETSIZE_MAX);
        dock->setMinimumWidth(dock == tools_dock_ ? 46 : (dock->widget() ? dock->widget()->minimumWidth() : 220));
        dock->setFeatures(reset_features);
        dock->setAllowedAreas(dock == timeline_dock_
                                  ? (Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea)
                                  : (Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea));
    }

    if (tools_dock_) {
        tools_dock_->setFloating(false);
        tools_dock_->show();
        addDockWidget(Qt::RightDockWidgetArea, tools_dock_);
    }
    if (graphic_props_dock_) {
        graphic_props_dock_->setFloating(false);
        graphic_props_dock_->show();
        addDockWidget(Qt::LeftDockWidgetArea, graphic_props_dock_);
    }
    if (layer_props_dock_) {
        layer_props_dock_->setFloating(false);
        layer_props_dock_->show();
        addDockWidget(Qt::RightDockWidgetArea, layer_props_dock_);
        if (tools_dock_) splitDockWidget(tools_dock_, layer_props_dock_, Qt::Horizontal);
    }
    if (styles_dock_) {
        styles_dock_->setFloating(false);
        styles_dock_->show();
        addDockWidget(Qt::LeftDockWidgetArea, styles_dock_);
        if (graphic_props_dock_) splitDockWidget(graphic_props_dock_, styles_dock_, Qt::Horizontal);
    }
    if (color_swatches_dock_) {
        color_swatches_dock_->setFloating(false);
        color_swatches_dock_->show();
        addDockWidget(Qt::LeftDockWidgetArea, color_swatches_dock_);
        if (styles_dock_) tabifyDockWidget(styles_dock_, color_swatches_dock_);
        else if (graphic_props_dock_) splitDockWidget(graphic_props_dock_, color_swatches_dock_, Qt::Horizontal);
    }
    if (effects_dock_) {
        effects_dock_->setFloating(false);
        effects_dock_->show();
        addDockWidget(Qt::RightDockWidgetArea, effects_dock_);
        if (layer_props_dock_) splitDockWidget(layer_props_dock_, effects_dock_, Qt::Horizontal);
    }
    if (timeline_dock_) {
        timeline_dock_->setFloating(false);
        timeline_dock_->show();
        addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);
    }
    if (tools_dock_) tools_dock_->raise();
    if (graphic_props_dock_) graphic_props_dock_->raise();
    if (layer_props_dock_) layer_props_dock_->raise();
    if (styles_dock_) styles_dock_->raise();

    if (act_tools_visible_) {
        QSignalBlocker blocker(act_tools_visible_);
        act_tools_visible_->setChecked(true);
    }
    if (act_graphic_props_visible_) {
        QSignalBlocker blocker(act_graphic_props_visible_);
        act_graphic_props_visible_->setChecked(true);
    }
    if (act_layer_props_visible_) {
        QSignalBlocker blocker(act_layer_props_visible_);
        act_layer_props_visible_->setChecked(true);
    }
    if (act_effects_visible_) {
        QSignalBlocker blocker(act_effects_visible_);
        act_effects_visible_->setChecked(true);
    }
    if (act_styles_visible_) {
        QSignalBlocker blocker(act_styles_visible_);
        act_styles_visible_->setChecked(true);
    }
    if (act_color_swatches_visible_) {
        QSignalBlocker blocker(act_color_swatches_visible_);
        act_color_swatches_visible_->setChecked(true);
    }
    if (act_timeline_visible_) {
        QSignalBlocker blocker(act_timeline_visible_);
        act_timeline_visible_->setChecked(true);
    }

    resize(1280, 760);
    update_panel_lock_state();
    restoring_editor_layout_ = false;
    save_editor_layout();
}

void TitleEditor::set_panels_locked(bool locked)
{
    panels_locked_ = locked;
    update_panel_lock_state();
    save_editor_layout();
}

void TitleEditor::update_panel_lock_state()
{
    const QDockWidget::DockWidgetFeatures unlocked_features =
        QDockWidget::DockWidgetClosable |
        QDockWidget::DockWidgetMovable |
        QDockWidget::DockWidgetFloatable;
    const QDockWidget::DockWidgetFeatures locked_features = QDockWidget::DockWidgetClosable;

    for (auto *dock : {tools_dock_, graphic_props_dock_, layer_props_dock_, effects_dock_, styles_dock_, color_swatches_dock_, timeline_dock_}) {
        if (!dock) continue;
        dock->setFeatures(panels_locked_ ? locked_features : unlocked_features);
        dock->setAllowedAreas(panels_locked_ ? Qt::NoDockWidgetArea
                                             : (dock == timeline_dock_
                                                    ? (Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea)
                                                    : (Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea)));
        if (panels_locked_) {
            const int locked_width = std::max(dock->minimumWidth(), dock->width());
            if (dock == timeline_dock_) {
                const int locked_height = std::max(dock->minimumHeight(), dock->height());
                dock->setMinimumHeight(locked_height);
                dock->setMaximumHeight(locked_height);
            } else {
                dock->setMinimumWidth(locked_width);
                dock->setMaximumWidth(locked_width);
            }
        } else {
            dock->setMinimumWidth(dock == tools_dock_ ? 46 : (dock->widget() ? dock->widget()->minimumWidth() : 220));
            dock->setMaximumWidth(dock == tools_dock_ ? 64 : QWIDGETSIZE_MAX);
            if (dock == timeline_dock_) {
                dock->setMinimumHeight(dock->widget() ? dock->widget()->minimumHeight() : 180);
                dock->setMaximumHeight(QWIDGETSIZE_MAX);
            }
        }
    }
}


static bool text_has_rtl_direction(const QString &text, bool fallback_rtl = false)
{
    for (const QChar ch : text) {
        switch (ch.direction()) {
        case QChar::DirL:
        case QChar::DirLRE:
        case QChar::DirLRO:
            return false;
        case QChar::DirR:
        case QChar::DirAL:
        case QChar::DirRLE:
        case QChar::DirRLO:
            return true;
        default:
            break;
        }
    }
    return fallback_rtl;
}

static void set_text_layer_direction_defaults(Layer &layer, bool paragraph_box, bool rtl)
{
    layer.align_h = rtl ? 2 : 0;
    layer.align_v = 0;
    layer.origin_x = rtl ? 1.0f : 0.0f;
    layer.origin_y = 0.0f;
    layer.origin_x_prop.static_value = layer.origin_x;
    layer.origin_y_prop.static_value = layer.origin_y;
    layer.text_overflow_mode = paragraph_box ? 0 : 1;
    layer.text_box_width_to_text = !paragraph_box;
    layer.text_box_height_to_text = !paragraph_box;
    layer.max_text_box_width = 9999.0f;
    layer.max_text_box_height = 9999.0f;
    layer.rich_text.default_paragraph_format.align_h = layer.align_h;
    layer.rich_text.default_paragraph_format.align_v = layer.align_v;
    for (auto &block : layer.rich_text.blocks) {
        block.format.align_h = layer.align_h;
        block.format.align_v = layer.align_v;
    }
}

static void set_new_text_layer_contents_empty(Layer &layer)
{
    layer.text_content.clear();
    layer.rich_text_html.clear();
    layer.rich_text = rich_text_document_from_layer_defaults(layer);
    layer.rich_text.plain_text.clear();
    layer.rich_text.ranges.clear();
    layer.rich_text.blocks.clear();
    layer.rich_text.selection = {0, 0};
}

std::shared_ptr<Layer> TitleEditor::create_basic_layer(LayerType type, const QString &name_override)
{
    if (!title_) return nullptr;

    auto l = std::make_shared<Layer>();
    l->id = TitleDataStore::make_uuid();
    if (!name_override.isEmpty()) {
        l->name = name_override.toStdString();
    } else {
        l->name = (type == LayerType::Text) ? editor_text_std("OBSTitles.Text") :
                  (type == LayerType::Clock) ? editor_text_std("OBSTitles.Clock") :
                  (type == LayerType::Ticker) ? editor_text_std("OBSTitles.Ticker") :
                  (type == LayerType::Image) ? editor_text_std("OBSTitles.Image") : editor_text_std("OBSTitles.Shape");
    }
    l->type = type;
    l->text_content = (type == LayerType::Text) ? editor_text_std("OBSTitles.NewText") :
                      (type == LayerType::Ticker) ? editor_text_std("OBSTitles.NewTickerText") : "";
    l->rich_text = rich_text_document_from_layer_defaults(*l);
    if (type == LayerType::Text) {
        set_new_text_layer_contents_empty(*l);
        set_text_layer_direction_defaults(*l, false, false);
    }
    l->clock_format = (type == LayerType::Clock) ? "H:i:s" : l->clock_format;
    l->pos_x.static_value = title_->width / 2.0;
    l->pos_y.static_value = title_->height / 2.0;
    l->rect_width = title_->width * 0.5f;
    l->rect_height = (type == LayerType::Image) ? title_->height * 0.4f : 160.0f;
    l->box_width.static_value = l->rect_width;
    l->box_height.static_value = l->rect_height;
    l->origin_x_prop.static_value = l->origin_x;
    l->origin_y_prop.static_value = l->origin_y;
    set_channel_statics(*l, true, l->text_color);
    set_channel_statics(*l, false, l->fill_color);
    l->out_time = title_->duration;
    return l;
}

void TitleEditor::create_shape_layer_from_canvas(ShapeType shape_type, const QPointF &canvas_pt)
{
    if (!title_) return;

    auto layer = create_basic_layer(LayerType::Shape, shape_display_name(shape_type));
    if (!layer) return;
    layer->shape_type = shape_type;
    layer->pos_x.static_value = canvas_pt.x();
    layer->pos_y.static_value = canvas_pt.y();
    if (shape_type == ShapeType::Ellipse || shape_type == ShapeType::Triangle ||
        shape_type == ShapeType::Star || shape_type == ShapeType::Polygon ||
        shape_type == ShapeType::Diamond) {
        const float size = std::min(layer->rect_width, layer->rect_height);
        layer->rect_width = size;
        layer->rect_height = size;
        layer->box_width.static_value = layer->rect_width;
        layer->box_height.static_value = layer->rect_height;
    }
    if (shape_type == ShapeType::RoundedRectangle) {
        set_layer_all_corner_radii(*layer, 18.0f);
        layer->corner_radius_locked = true;
    }

    canvas_created_shape_layer_id_ = layer->id;
    title_->add_layer(layer);
    layers_->refresh();
    on_layer_selected(layer->id);
}


void TitleEditor::create_text_layer_from_canvas(LayerType type, const QPointF &canvas_pt)
{
    if (!title_) return;
    if (type != LayerType::Text && type != LayerType::Clock && type != LayerType::Ticker)
        type = LayerType::Text;

    auto layer = create_basic_layer(type, text_tool_display_name(type));
    if (!layer) return;
    layer->pos_x.static_value = canvas_pt.x();
    layer->pos_y.static_value = canvas_pt.y();
    if (type == LayerType::Text) {
        set_new_text_layer_contents_empty(*layer);
        set_text_layer_direction_defaults(*layer, false, false);
    } else {
        layer->rich_text_html.clear();
    }

    canvas_created_shape_layer_id_ = layer->id;
    title_->add_layer(layer);
    layers_->refresh();
    on_layer_selected(layer->id);
}

void TitleEditor::update_canvas_created_shape(const QRectF &canvas_rect)
{
    if (!title_ || canvas_created_shape_layer_id_.empty()) return;
    auto layer = title_->find_layer(canvas_created_shape_layer_id_);
    if (!layer) return;

    QRectF rect = canvas_rect.normalized();
    const double width = std::max(1.0, rect.width());
    const double height = std::max(1.0, rect.height());
    if (rect.width() < 1.0) rect.setWidth(width);
    if (rect.height() < 1.0) rect.setHeight(height);

    if (is_canvas_text_layer(*layer)) {
        const bool rtl = text_has_rtl_direction(QString::fromStdString(layer->text_content), layer->origin_x > 0.5f);
        set_text_layer_direction_defaults(*layer, true, rtl);
        layer->pos_x.static_value = rtl ? rect.right() : rect.left();
        layer->pos_y.static_value = rect.top();
    } else {
        layer->pos_x.static_value = rect.center().x();
        layer->pos_y.static_value = rect.center().y();
    }
    layer->rect_width = (float)width;
    layer->rect_height = (float)height;
    layer->box_width.static_value = layer->rect_width;
    layer->box_height.static_value = layer->rect_height;
    if (!is_canvas_text_layer(*layer) && layer->shape_type == ShapeType::RoundedRectangle) {
        set_layer_all_corner_radii(*layer, (float)std::min(width, height) * 0.12f);
        layer->corner_radius_locked = true;
    }

    update_layer_panels(layer, playhead_);
}

void TitleEditor::finish_canvas_created_shape(bool keep_layer)
{
    if (!title_ || canvas_created_shape_layer_id_.empty()) return;
    const std::string layer_id = canvas_created_shape_layer_id_;
    canvas_created_shape_layer_id_.clear();

    auto layer = title_->find_layer(layer_id);
    if (!layer) return;
    const bool is_text_layer = is_canvas_text_layer(*layer);
    const bool too_small = is_text_layer
        ? false
        : (layer->shape_type == ShapeType::Line
               ? layer->rect_width < 2.0f
               : (layer->rect_width < 2.0f || layer->rect_height < 2.0f));
    if (!keep_layer || too_small) {
        title_->remove_layer(layer_id);
        layers_->refresh();
        timeline_->set_title(title_);
        sel_layer_id_.clear();
        if (canvas_) canvas_->set_selected_layers({});
        update_layer_panels(nullptr, playhead_);
        return;
    }

    layers_->refresh();
    timeline_->set_title(title_);
    on_layer_selected(layer_id);
    on_title_modified();
    if (is_text_layer && canvas_)
        canvas_->begin_text_edit_for_layer(layer_id);
}

void TitleEditor::build_ui()
{
    restoring_editor_layout_ = true;

    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *menu_bar = new QMenuBar(this);
    auto *file_menu = menu_bar->addMenu(obsgs_tr("OBSTitles.FileMenu"));
    QAction *new_action = file_menu->addAction(obsgs_tr("OBSTitles.New"));
    new_action->setShortcut(QKeySequence::New);
    connect(new_action, &QAction::triggered, this, &TitleEditor::new_title_contents);
    QAction *save_action = file_menu->addAction(obs_icon("save.svg"), obsgs_tr("OBSTitles.Save"));
    save_action->setShortcut(QKeySequence::Save);
    connect(save_action, &QAction::triggered, this, &TitleEditor::save_title);
    QAction *save_as_new_action = file_menu->addAction(obsgs_tr("OBSTitles.SaveAsNew"));
    connect(save_as_new_action, &QAction::triggered, this, &TitleEditor::save_title_as_new);
    QAction *save_library_action = file_menu->addAction(obsgs_tr("OBSTitles.SaveInLibrary"));
    connect(save_library_action, &QAction::triggered, this, [this]() { export_title_template(true); });
    QAction *export_action = file_menu->addAction(obs_icon("export.svg"), obsgs_tr("OBSTitles.Export"));
    connect(export_action, &QAction::triggered, this, [this]() { export_title_template(false); });
    file_menu->addSeparator();
    QAction *exit_action = file_menu->addAction(obs_icon("file-exit.svg"), obsgs_tr("OBSTitles.Exit"));
    connect(exit_action, &QAction::triggered, this, &TitleEditor::close);

    auto *edit_menu = menu_bar->addMenu(obsgs_tr("OBSTitles.EditMenu"));
    edit_menu->addAction(act_undo_ = new QAction(obs_icon("undo.svg"), obsgs_tr("OBSTitles.Undo"), this));
    act_undo_->setShortcut(QKeySequence::Undo);
    connect(act_undo_, &QAction::triggered, this, [this]() {
        if (undo_index_ > 0) restore_undo_snapshot(undo_index_ - 1);
    });
    edit_menu->addAction(act_redo_ = new QAction(obs_icon("redo.svg"), obsgs_tr("OBSTitles.Redo"), this));
    act_redo_->setShortcut(QKeySequence::Redo);
    connect(act_redo_, &QAction::triggered, this, [this]() {
        if (undo_index_ + 1 < (int)undo_stack_.size()) restore_undo_snapshot(undo_index_ + 1);
    });
    edit_menu->addSeparator();
    QAction *copy_action = edit_menu->addAction(obsgs_tr("OBSTitles.Copy"));
    copy_action->setShortcut(QKeySequence::Copy);
    connect(copy_action, &QAction::triggered, this, [this]() {
        if (editor_focus_accepts_text(focusWidget())) return;
        if (timeline_ && timeline_->has_selected_keyframes()) {
            timeline_->copy_keyframe_selection();
            return;
        }
        copy_selected_layer();
    });
    QAction *cut_action = edit_menu->addAction(obsgs_tr("OBSTitles.Cut"));
    cut_action->setShortcut(QKeySequence::Cut);
    connect(cut_action, &QAction::triggered, this, [this]() {
        if (editor_focus_accepts_text(focusWidget())) return;
        if (timeline_ && timeline_->has_selected_keyframes()) {
            timeline_->cut_keyframe_selection();
            return;
        }
        cut_selected_layer();
    });
    QAction *paste_action = edit_menu->addAction(obsgs_tr("OBSTitles.Paste"));
    paste_action->setShortcut(QKeySequence::Paste);
    connect(paste_action, &QAction::triggered, this, [this]() {
        if (editor_focus_accepts_text(focusWidget())) return;
        if (timeline_ && timeline_->has_keyframe_clipboard()) {
            timeline_->paste_keyframes_at_playhead();
            return;
        }
        paste_layer_from_clipboard();
    });
    QAction *delete_action = edit_menu->addAction(obsgs_tr("OBSTitles.Delete"));
    delete_action->setShortcut(QKeySequence::Delete);
    connect(delete_action, &QAction::triggered, this, [this]() {
        if (editor_focus_accepts_text(focusWidget())) return;
        if (timeline_ && timeline_->has_selected_keyframes()) {
            timeline_->delete_keyframe_selection();
            return;
        }
        delete_selected_layer();
    });
    edit_menu->addSeparator();
    QAction *preferences_action = edit_menu->addAction(obsgs_tr("OBSTitles.Preferences"));
    connect(preferences_action, &QAction::triggered, this, &TitleEditor::show_preferences);

    auto *view_menu = menu_bar->addMenu(QStringLiteral("View"));
    QAction *snap_action = view_menu->addAction(QStringLiteral("Snap"));
    snap_action->setCheckable(true);
    snap_action->setChecked(true);
    snap_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
    snap_action->setToolTip(QStringLiteral("Globally enable or disable snapping without changing Snap To targets."));
    connect(snap_action, &QAction::toggled, this, [this](bool enabled) {
        if (canvas_) canvas_->set_snap_enabled(enabled);
    });

    auto *snap_to_menu = view_menu->addMenu(QStringLiteral("Snap To"));
    auto add_snap_to_action = [this, snap_to_menu](const QString &text, bool checked, auto setter) {
        QAction *action = snap_to_menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(checked);
        connect(action, &QAction::toggled, this, [this, setter](bool enabled) {
            if (canvas_) (canvas_->*setter)(enabled);
        });
        return action;
    };
    add_snap_to_action(QStringLiteral("Guides"), true, &CanvasPreview::set_snap_to_guides);
    add_snap_to_action(QStringLiteral("Grid"), false, &CanvasPreview::set_snap_to_grid);
    add_snap_to_action(QStringLiteral("Object Edges"), true, &CanvasPreview::set_snap_to_object_edges);
    add_snap_to_action(QStringLiteral("Object Centers"), true, &CanvasPreview::set_snap_to_object_centers);
    add_snap_to_action(QStringLiteral("Canvas Bounds"), true, &CanvasPreview::set_snap_to_canvas_bounds);
    add_snap_to_action(QStringLiteral("Spacing / Alignment"), true, &CanvasPreview::set_snap_to_spacing);

    create_docked_panel_menu(menu_bar);

    auto *help_menu = menu_bar->addMenu(obsgs_tr("OBSTitles.HelpMenu"));
    QAction *about_action = help_menu->addAction(obs_icon("about.svg"), obsgs_tr("OBSTitles.About"));
    connect(about_action, &QAction::triggered, this, &TitleEditor::show_about);
    setMenuBar(menu_bar);

    /* ── Toolbar ── */
    build_toolbar();
    root->addWidget(toolbar_);

    /* ── Title name label bar ── */
    const QPalette editor_pal = qApp->palette();
    const QColor editor_window = editor_pal.color(QPalette::Window);
    const QColor editor_base = editor_pal.color(QPalette::Base);
    const QColor editor_text = editor_pal.color(QPalette::WindowText);
    const QColor editor_field_text = editor_pal.color(QPalette::Text);
    const QColor editor_button = editor_pal.color(QPalette::Button);
    const QColor editor_button_text = editor_pal.color(QPalette::ButtonText);
    const QColor editor_border = editor_pal.color(QPalette::Mid);
    const QColor editor_highlight = editor_pal.color(QPalette::Highlight);
    const QColor editor_hover = editor_button.lightness() < 128 ? editor_button.lighter(125) : editor_button.darker(108);

    auto *title_bar = new QWidget(this);
    title_bar->setStyleSheet(QStringLiteral("background:%1;color:%2;")
                                 .arg(editor_window.name(QColor::HexRgb),
                                      editor_text.name(QColor::HexRgb)));
    auto *title_bar_layout = new QHBoxLayout(title_bar);
    title_bar_layout->setContentsMargins(0, 3, 0, 3);
    title_bar_layout->setSpacing(6);
    title_bar_layout->addStretch(1);
    dirty_indicator_ = new QLabel(title_bar);
    dirty_indicator_->setFixedSize(10, 10);
    dirty_indicator_->setStyleSheet("background:#e33;border-radius:5px;");
    dirty_indicator_->setToolTip(obsgs_tr("OBSTitles.UnsavedChangesIndicator"));
    dirty_indicator_->hide();
    title_bar_layout->addWidget(dirty_indicator_, 0, Qt::AlignVCenter);
    title_lbl_ = new QLabel("—", title_bar);
    title_lbl_->setAlignment(Qt::AlignCenter);
    QFont tf = title_lbl_->font();
    tf.setPointSize(tf.pointSize() + 1);
    tf.setBold(true);
    title_lbl_->setFont(tf);
    title_lbl_->setStyleSheet(QStringLiteral("color:%1;").arg(editor_text.name(QColor::HexRgb)));
    title_lbl_->setToolTip(QStringLiteral("Double-click to rename"));
    title_lbl_->installEventFilter(this);
    title_bar_layout->addWidget(title_lbl_, 0, Qt::AlignVCenter);
    title_name_edit_ = new QLineEdit(title_bar);
    title_name_edit_->setAlignment(Qt::AlignCenter);
    title_name_edit_->setFont(tf);
    title_name_edit_->setMinimumWidth(180);
    title_name_edit_->setStyleSheet(QStringLiteral(
        "QLineEdit{color:%1;background:%2;border:1px solid %3;border-radius:3px;padding:1px 6px;}")
        .arg(editor_field_text.name(QColor::HexRgb),
             editor_base.name(QColor::HexRgb),
             editor_highlight.name(QColor::HexRgb)));
    title_name_edit_->hide();
    title_name_edit_->installEventFilter(this);
    connect(title_name_edit_, &QLineEdit::returnPressed, this, [this]() {
        commit_title_name_edit(true);
    });
    title_bar_layout->addWidget(title_name_edit_, 0, Qt::AlignVCenter);
    gpu_warning_lbl_ = new QLabel(title_bar);
    QFont gpu_warning_font = gpu_warning_lbl_->font();
    gpu_warning_font.setBold(true);
    gpu_warning_lbl_->setFont(gpu_warning_font);
    gpu_warning_lbl_->setStyleSheet("color:#ffca4a;");
    gpu_warning_lbl_->hide();
    title_bar_layout->addWidget(gpu_warning_lbl_, 0, Qt::AlignVCenter);
    title_bar_layout->addStretch(1);
    root->addWidget(title_bar);

    /* ── Upper split: Canvas (dockable property panels live in QMainWindow dock areas) ── */
    auto *upper_split = new QSplitter(Qt::Horizontal, central);

    title_props_ = new TitlePropertiesPanel(this);
    title_props_->setMinimumWidth(240);

    auto *canvas_panel = new QWidget(upper_split);
    canvas_panel->setStyleSheet(QStringLiteral("background:%1;color:%2;")
                                    .arg(editor_window.name(QColor::HexRgb),
                                         editor_text.name(QColor::HexRgb)));
    auto *canvas_layout = new QVBoxLayout(canvas_panel);
    canvas_layout->setContentsMargins(0, 0, 0, 0);
    canvas_layout->setSpacing(0);
    canvas_ = new CanvasPreview(canvas_panel);
    canvas_->setMinimumSize(300, 200);
    canvas_layout->addWidget(canvas_, 1);

    auto *canvas_zoom_bar = new QWidget(canvas_panel);
    canvas_zoom_bar->setFixedHeight(34);
    canvas_zoom_bar->setStyleSheet(QStringLiteral(
        "QWidget{background:%1;border-top:1px solid %2;color:%3;}"
        "QPushButton,QToolButton{color:%4;background:%5;border:1px solid %2;border-radius:3px;padding:3px 8px;}"
        "QPushButton:hover,QToolButton:hover{background:%6;}"
        "QToolButton::menu-indicator{image:none;}"
        "QSpinBox{color:%7;background:%8;border:1px solid %2;border-radius:3px;padding:2px 6px;}"
        "QSpinBox::up-button,QSpinBox::down-button{width:0;border:none;}"
        "QSlider::groove:horizontal{height:4px;background:%8;border-radius:2px;}"
        "QSlider::handle:horizontal{width:12px;margin:-5px 0;background:%4;border-radius:6px;}"
        "QSlider::sub-page:horizontal{background:%9;border-radius:2px;}")
        .arg(editor_window.name(QColor::HexRgb),
             editor_border.name(QColor::HexRgb),
             editor_text.name(QColor::HexRgb),
             editor_button_text.name(QColor::HexRgb),
             editor_button.name(QColor::HexRgb),
             editor_hover.name(QColor::HexRgb),
             editor_field_text.name(QColor::HexRgb),
             editor_base.name(QColor::HexRgb),
             editor_highlight.name(QColor::HexRgb)));
    auto *canvas_zoom_layout = new QHBoxLayout(canvas_zoom_bar);
    canvas_zoom_layout->setContentsMargins(10, 0, 10, 0);
    canvas_zoom_layout->setSpacing(8);
    auto *canvas_zoom_out = new QPushButton(canvas_zoom_bar);
    canvas_zoom_out->setIcon(obs_icon("zoom-out.svg"));
    canvas_zoom_out->setFixedWidth(30);
    auto *canvas_zoom_slider = new QSlider(Qt::Horizontal, canvas_zoom_bar);
    canvas_zoom_slider->setRange(5, 1600);
    canvas_zoom_slider->setValue(canvas_->zoom_percent());
    canvas_zoom_slider->setMinimumWidth(220);
    canvas_zoom_slider->setMaximumWidth(360);
    auto *canvas_zoom_in = new QPushButton(canvas_zoom_bar);
    canvas_zoom_in->setIcon(obs_icon("zoom-in.svg"));
    canvas_zoom_in->setFixedWidth(30);
    auto *canvas_zoom_percent = new QSpinBox(canvas_zoom_bar);
    canvas_zoom_percent->setRange(5, 1600);
    canvas_zoom_percent->setSuffix("%");
    canvas_zoom_percent->setAlignment(Qt::AlignCenter);
    canvas_zoom_percent->setButtonSymbols(QAbstractSpinBox::NoButtons);
    canvas_zoom_percent->setFixedWidth(72);
    canvas_zoom_percent->setValue(canvas_->zoom_percent());
    auto *fit_canvas = new QToolButton(canvas_zoom_bar);
    fit_canvas->setText("Fit");
    fit_canvas->setPopupMode(QToolButton::InstantPopup);
    auto *fit_canvas_menu = new QMenu(fit_canvas);
    auto add_canvas_zoom_action = [fit_canvas_menu](const QString &text, int percent) {
        QAction *action = fit_canvas_menu->addAction(text);
        action->setData(percent);
        return action;
    };
    QAction *fit_action = fit_canvas_menu->addAction("Fit");
    fit_action->setData(-1);
    QAction *fit_100_action = fit_canvas_menu->addAction("Fit up to 100%");
    fit_100_action->setData(-2);
    add_canvas_zoom_action("50%", 50);
    add_canvas_zoom_action("100%", 100);
    add_canvas_zoom_action("200%", 200);
    add_canvas_zoom_action("400%", 400);
    add_canvas_zoom_action("800%", 800);
    add_canvas_zoom_action("1600%", 1600);
    fit_canvas->setMenu(fit_canvas_menu);
    QSettings canvas_settings(QStringLiteral("OBSGraphicsStudioPro"), QStringLiteral("Dock"));
    canvas_settings.beginGroup(QString::fromUtf8(kEditorLayoutSettingsGroup));
    const int saved_checkerboard_pattern = std::clamp(
        canvas_settings.value(QString::fromUtf8(kEditorCanvasTransparencyKey), 1).toInt(), 0, 5);
    const bool saved_safe_guides_visible =
        canvas_settings.value(QString::fromUtf8(kEditorSafeGuidesVisibleKey), false).toBool();
    canvas_settings.endGroup();

    auto *checkerboard = new QToolButton(canvas_zoom_bar);
    checkerboard->setText("Transparency: Medium");
    checkerboard->setPopupMode(QToolButton::InstantPopup);
    auto *checkerboard_menu = new QMenu(checkerboard);
    auto *checkerboard_group = new QActionGroup(checkerboard_menu);
    checkerboard_group->setExclusive(true);
    auto add_checkerboard_action = [checkerboard_menu, checkerboard_group, saved_checkerboard_pattern](const QString &text, int pattern) {
        QAction *action = checkerboard_menu->addAction(text);
        action->setData(pattern);
        action->setCheckable(true);
        action->setChecked(pattern == saved_checkerboard_pattern);
        checkerboard_group->addAction(action);
        return action;
    };
    add_checkerboard_action("Light", 0);
    add_checkerboard_action("Medium", 1);
    add_checkerboard_action("Dark", 2);
    add_checkerboard_action("Solid white", 3);
    add_checkerboard_action("Solid black", 4);
    add_checkerboard_action("Solid grey", 5);
    if (auto *checked = checkerboard_group->checkedAction())
        checkerboard->setText(QString("Transparency: %1").arg(checked->text()));
    checkerboard->setMenu(checkerboard_menu);
    canvas_->set_checkerboard_pattern(saved_checkerboard_pattern);
    if (act_safe_guides_) {
        act_safe_guides_->setChecked(saved_safe_guides_visible);
        canvas_->set_safe_guides_visible(saved_safe_guides_visible);
    }
    canvas_zoom_layout->addWidget(canvas_zoom_out);
    canvas_zoom_layout->addWidget(canvas_zoom_slider);
    canvas_zoom_layout->addWidget(canvas_zoom_in);
    canvas_zoom_layout->addWidget(canvas_zoom_percent);
    canvas_zoom_layout->addWidget(fit_canvas);
    canvas_zoom_layout->addWidget(checkerboard);
    auto *safe_guides = new QToolButton(canvas_zoom_bar);
    safe_guides->setDefaultAction(act_safe_guides_);
    safe_guides->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    canvas_zoom_layout->addWidget(safe_guides);
    canvas_zoom_layout->addStretch(1);
    canvas_layout->addWidget(canvas_zoom_bar);
    connect(canvas_zoom_slider, &QSlider::valueChanged, canvas_, &CanvasPreview::set_zoom_percent);
    connect(canvas_zoom_percent, qOverload<int>(&QSpinBox::valueChanged), canvas_, &CanvasPreview::set_zoom_percent);
    connect(canvas_, &CanvasPreview::zoom_percent_changed, this, [canvas_zoom_slider, canvas_zoom_percent](int percent) {
        QSignalBlocker slider_blocker(canvas_zoom_slider);
        QSignalBlocker spin_blocker(canvas_zoom_percent);
        canvas_zoom_slider->setValue(percent);
        canvas_zoom_percent->setValue(percent);
    });
    connect(canvas_zoom_out, &QPushButton::clicked, this, [this]() {
        canvas_->set_zoom_percent((int)std::round(canvas_->zoom_percent() / 1.18));
    });
    connect(canvas_zoom_in, &QPushButton::clicked, this, [this]() {
        canvas_->set_zoom_percent((int)std::round(canvas_->zoom_percent() * 1.18));
    });
    connect(fit_canvas_menu, &QMenu::triggered, this, [this, fit_canvas](QAction *action) {
        int value = action->data().toInt();
        fit_canvas->setText(action->text());
        if (value == -1) canvas_->fit_canvas(false);
        else if (value == -2) canvas_->fit_canvas(true);
        else canvas_->set_zoom_percent(value);
    });
    connect(checkerboard_menu, &QMenu::triggered, this, [this, checkerboard](QAction *action) {
        checkerboard->setText(QString("Transparency: %1").arg(action->text()));
        canvas_->set_checkerboard_pattern(action->data().toInt());
    });
    upper_split->addWidget(canvas_panel);

    props_ = new PropertiesPanel(this);
    props_->setMinimumWidth(260);
    upper_split->setStretchFactor(0, 1);

    graphic_props_dock_ = create_editor_dock(QString::fromUtf8(kGraphicPropertiesDockObjectName),
                                             QStringLiteral("Graphic Properties"),
                                             title_props_);
    layer_props_dock_ = create_editor_dock(QString::fromUtf8(kLayerPropertiesDockObjectName),
                                           QStringLiteral("Layer Properties"),
                                           props_);
    effects_dock_ = create_editor_dock(QString::fromUtf8(kEffectsDockObjectName),
                                       QStringLiteral("Effects"),
                                       create_effects_panel());
    styles_dock_ = create_editor_dock(QString::fromUtf8(kStylesDockObjectName),
                                      QStringLiteral("Styles"),
                                      create_styles_panel());
    color_swatches_dock_ = create_editor_dock(QString::fromUtf8(kColorSwatchesDockObjectName),
                                              QStringLiteral("Color Swatches"),
                                              create_color_swatches_panel());
    tools_sidebar_ = new ToolsSidebar(this);
    tools_dock_ = create_editor_dock(QStringLiteral("OBSGraphicsStudioProToolsDock"),
                                     QStringLiteral("Tools"),
                                     tools_sidebar_);
    tools_dock_->setMinimumWidth(46);
    tools_dock_->setMaximumWidth(64);
    addDockWidget(Qt::LeftDockWidgetArea, graphic_props_dock_);
    addDockWidget(Qt::RightDockWidgetArea, tools_dock_);
    addDockWidget(Qt::RightDockWidgetArea, layer_props_dock_);
    splitDockWidget(tools_dock_, layer_props_dock_, Qt::Horizontal);
    addDockWidget(Qt::LeftDockWidgetArea, styles_dock_);
    splitDockWidget(graphic_props_dock_, styles_dock_, Qt::Horizontal);
    addDockWidget(Qt::LeftDockWidgetArea, color_swatches_dock_);
    tabifyDockWidget(styles_dock_, color_swatches_dock_);
    addDockWidget(Qt::RightDockWidgetArea, effects_dock_);
    splitDockWidget(layer_props_dock_, effects_dock_, Qt::Horizontal);
    tools_dock_->raise();
    graphic_props_dock_->raise();
    layer_props_dock_->raise();
    styles_dock_->raise();

    /* ── Timeline editor: full-width transport | LayerStack + Timeline | full-width zoom ── */
    auto *timeline_editor = new QWidget(this);
    auto *timeline_editor_layout = new QVBoxLayout(timeline_editor);
    timeline_editor_layout->setContentsMargins(0, 0, 0, 0);
    timeline_editor_layout->setSpacing(0);

    auto *timeline_transport = new QWidget(timeline_editor);
    timeline_transport->setFixedHeight(34);
    const QPalette timeline_pal = qApp->palette();
    const QColor timeline_window = timeline_pal.color(QPalette::Window);
    const QColor timeline_base = timeline_pal.color(QPalette::Base);
    const QColor timeline_text = timeline_pal.color(QPalette::WindowText);
    const QColor timeline_button = timeline_pal.color(QPalette::Button);
    const QColor timeline_button_text = timeline_pal.color(QPalette::ButtonText);
    const QColor timeline_border = timeline_pal.color(QPalette::Mid);
    const QColor timeline_highlight = timeline_pal.color(QPalette::Highlight);
    const QColor timeline_hover = timeline_button.lightness() < 128 ? timeline_button.lighter(125) : timeline_button.darker(108);
    timeline_transport->setStyleSheet(QStringLiteral(
        "QWidget{background:%1;border-bottom:1px solid %2;color:%3;}"
        "QToolButton{color:%4;background:transparent;padding:3px 7px;border:none;}"
        "QToolButton:hover{background:%5;border-radius:2px;}"
        "QToolButton:checked{background:%6;color:%7;border-radius:2px;}"
        "QLabel{color:%6;font-family:monospace;}")
        .arg(timeline_window.name(QColor::HexRgb),
             timeline_border.name(QColor::HexRgb),
             timeline_text.name(QColor::HexRgb),
             timeline_button_text.name(QColor::HexRgb),
             timeline_hover.name(QColor::HexRgb),
             timeline_highlight.name(QColor::HexRgb),
             timeline_pal.color(QPalette::HighlightedText).name(QColor::HexRgb)));
    auto *transport_layout = new QHBoxLayout(timeline_transport);
    transport_layout->setContentsMargins(8, 0, 8, 0);
    transport_layout->setSpacing(2);
    auto make_transport_button = [timeline_transport](QAction *action) {
        auto *button = new QToolButton(timeline_transport);
        button->setDefaultAction(action);
        button->setIconSize(QSize(14, 14));
        button->setAutoRaise(true);
        return button;
    };
    transport_layout->addStretch(1);
    time_lbl_ = new QLabel("0.000 s", timeline_transport);
    time_lbl_->setMinimumWidth(116);
    time_lbl_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    time_lbl_->setStyleSheet(QStringLiteral("color:%1;font-family:monospace;")
                                 .arg(timeline_highlight.name(QColor::HexRgb)));
    transport_layout->addWidget(time_lbl_, 0, Qt::AlignVCenter);
    transport_layout->addSpacing(8);
    transport_layout->addWidget(make_transport_button(act_rew_));
    transport_layout->addWidget(make_transport_button(act_prev_kf_));
    transport_layout->addWidget(make_transport_button(act_play_));
    transport_layout->addWidget(make_transport_button(act_full_loop_));
    QAction *step_forward_action = new QAction(obs_icon("step-forward.svg"), obsgs_tr("OBSTitles.StepForward"), timeline_transport);
    connect(step_forward_action, &QAction::triggered, this, &TitleEditor::step_forward);
    transport_layout->addWidget(make_transport_button(step_forward_action));
    transport_layout->addWidget(make_transport_button(act_next_kf_));
    transport_layout->addStretch(1);
    timeline_editor_layout->addWidget(timeline_transport);

    auto *lower_split = new QSplitter(Qt::Horizontal, timeline_editor);

    auto *layers_panel = new QWidget(lower_split);
    auto *layers_layout = new QVBoxLayout(layers_panel);
    layers_layout->setContentsMargins(0, 0, 0, 0);
    layers_layout->setSpacing(0);

    layers_ = new LayerStack(layers_panel);
    layers_->setMinimumHeight(140);
    layers_layout->addWidget(layers_, 1);
    layers_panel->setMinimumWidth(280);
    layers_panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    lower_split->addWidget(layers_panel);

    auto *timeline_panel = new QWidget(lower_split);
    auto *timeline_panel_layout = new QVBoxLayout(timeline_panel);
    timeline_panel_layout->setContentsMargins(0, 0, 0, 0);
    timeline_panel_layout->setSpacing(0);

    timeline_ = new TimelineWidget(timeline_panel);
    timeline_->setMinimumHeight(140);
    timeline_panel_layout->addWidget(timeline_, 1);

    auto *timeline_zoom_bar = new QWidget(timeline_panel);
    timeline_zoom_bar->setFixedHeight(34);
    timeline_zoom_bar->setStyleSheet(QStringLiteral(
        "QWidget{background:%1;border-top:1px solid %2;color:%3;}"
        "QPushButton{color:%4;background:%5;border:1px solid %2;border-radius:3px;padding:3px 8px;}"
        "QPushButton:hover{background:%6;}"
        "QSlider::groove:horizontal{height:4px;background:%7;border-radius:2px;}"
        "QSlider::handle:horizontal{width:12px;margin:-5px 0;background:%4;border-radius:6px;}"
        "QSlider::sub-page:horizontal{background:%8;border-radius:2px;}")
        .arg(timeline_window.name(QColor::HexRgb),
             timeline_border.name(QColor::HexRgb),
             timeline_text.name(QColor::HexRgb),
             timeline_button_text.name(QColor::HexRgb),
             timeline_button.name(QColor::HexRgb),
             timeline_hover.name(QColor::HexRgb),
             timeline_base.name(QColor::HexRgb),
             timeline_highlight.name(QColor::HexRgb)));
    auto *zoom_layout = new QHBoxLayout(timeline_zoom_bar);
    zoom_layout->setContentsMargins(10, 0, 10, 0);
    zoom_layout->setSpacing(8);
    auto *zoom_out = new QPushButton(timeline_zoom_bar);
    zoom_out->setIcon(obs_icon("zoom-out.svg"));
    zoom_out->setFixedWidth(30);
    auto *zoom_slider = new QSlider(Qt::Horizontal, timeline_zoom_bar);
    zoom_slider->setRange(5, 1200);
    zoom_slider->setValue(timeline_->zoom_percent());
    zoom_slider->setMinimumWidth(220);
    zoom_slider->setMaximumWidth(360);
    auto *zoom_in = new QPushButton(timeline_zoom_bar);
    zoom_in->setIcon(obs_icon("zoom-in.svg"));
    zoom_in->setFixedWidth(30);
    auto *fit_timeline = new QPushButton(obsgs_tr("OBSTitles.FitTimeline"), timeline_zoom_bar);
    zoom_layout->addWidget(zoom_out);
    zoom_layout->addWidget(zoom_slider);
    zoom_layout->addWidget(zoom_in);
    zoom_layout->addWidget(fit_timeline);
    zoom_layout->addStretch(1);
    connect(zoom_slider, &QSlider::valueChanged, timeline_, &TimelineWidget::set_zoom_percent);
    connect(timeline_, &TimelineWidget::zoom_percent_changed, this, [zoom_slider](int percent) {
        QSignalBlocker blocker(zoom_slider);
        zoom_slider->setValue(percent);
    });
    connect(zoom_out, &QPushButton::clicked, this, [this]() {
        timeline_->set_zoom_percent((int)std::round(timeline_->zoom_percent() / 1.18));
    });
    connect(zoom_in, &QPushButton::clicked, this, [this]() {
        timeline_->set_zoom_percent((int)std::round(timeline_->zoom_percent() * 1.18));
    });
    connect(fit_timeline, &QPushButton::clicked, timeline_, &TimelineWidget::fit_timeline);
    timeline_panel_layout->addWidget(timeline_zoom_bar);
    lower_split->addWidget(timeline_panel);

    if (auto *scroll_bar = layers_->vertical_scroll_bar()) {
        connect(scroll_bar, &QScrollBar::valueChanged, timeline_, &TimelineWidget::set_vertical_scroll);
        connect(timeline_, &TimelineWidget::vertical_scroll_delta_requested, this,
                [scroll_bar](int delta) { scroll_bar->setValue(scroll_bar->value() + delta); });
    }
    lower_split->setStretchFactor(0, 1);
    lower_split->setStretchFactor(1, 3);
    lower_split->setCollapsible(0, false);
    lower_split->setCollapsible(1, false);
    timeline_editor_layout->addWidget(lower_split, 1);

    root->addWidget(upper_split, 1);

    timeline_editor->setMinimumHeight(210);
    timeline_dock_ = create_editor_dock(QString::fromUtf8(kTimelineDockObjectName),
                                        QStringLiteral("Timeline"),
                                        timeline_editor);
    timeline_dock_->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    timeline_dock_->setMinimumHeight(220);
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);

    load_editor_layout();

    /* ── Connect sub-widget signals ── */
    connect(layers_, &LayerStack::layer_selected,
            this, &TitleEditor::on_layer_selected);
    connect(layers_, &LayerStack::layers_selected,
            this, [this](const std::vector<std::string> &ids) {
                sel_layer_id_ = ids.size() == 1 ? ids.back() : std::string();
                canvas_->set_selected_layers(ids);
                timeline_->set_selected_layer(sel_layer_id_);
                if (!title_ || ids.size() != 1) {
                    update_layer_panels(nullptr, playhead_);
                    return;
                }
                auto layer = title_->find_layer(sel_layer_id_);
                update_layer_panels(layer, playhead_);
            });

    connect(layers_, &LayerStack::add_layer_requested,
            this, [this](LayerType type) {
                if (!title_) return;
                auto l = create_basic_layer(type);
                if (!l) return;
                if (type == LayerType::Image) {
                    l->lock_aspect_ratio = true;
                    QString path = QFileDialog::getOpenFileName(
                        this, obsgs_tr("OBSTitles.ChooseImage"), QString(),
                        obsgs_tr("OBSTitles.ImageFileFilter"));
                    if (path.isEmpty()) return;
                    l->image_path = path.toStdString();
                    QSize image_size = editor_image_intrinsic_size(path);
                    if (image_size.isValid() && !image_size.isEmpty()) {
                        l->rect_width = (float)image_size.width();
                        l->rect_height = (float)image_size.height();
                        l->box_width.static_value = l->rect_width;
                        l->box_height.static_value = l->rect_height;
                    }
                }
                title_->add_layer(l);
                layers_->refresh();
                on_layer_selected(l->id);
                on_title_modified();
                if (type == LayerType::Text && canvas_)
                    canvas_->begin_text_edit_for_layer(l->id);
            });

    connect(layers_, &LayerStack::clone_layer_requested,
            this, [this](const std::string &lid) {
                if (!title_) return;
                auto selected = layers_ ? layers_->selected_ids() : std::vector<std::string>{};
                if (std::find(selected.begin(), selected.end(), lid) == selected.end())
                    on_layer_selected(lid);
                duplicate_selected_layers();
            });

    connect(layers_, &LayerStack::copy_layer_requested,
            this, [this](const std::string &lid) {
                auto selected = layers_ ? layers_->selected_ids() : std::vector<std::string>{};
                if (std::find(selected.begin(), selected.end(), lid) == selected.end())
                    on_layer_selected(lid);
                copy_selected_layer();
            });

    connect(layers_, &LayerStack::paste_layer_requested,
            this, [this](const std::string &anchor_id) {
                if (!anchor_id.empty()) on_layer_selected(anchor_id);
                paste_layer_from_clipboard();
            });

    connect(layers_, &LayerStack::delete_layer_requested,
            this, [this](const std::string &lid) {
                auto selected = layers_ ? layers_->selected_ids() : std::vector<std::string>{};
                if (std::find(selected.begin(), selected.end(), lid) == selected.end())
                    on_layer_selected(lid);
                delete_selected_layer();
            });

    connect(layers_, &LayerStack::layer_visibility_changed,
            this, [this](const std::string &lid, bool visible) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->visible = visible;
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::layer_lock_changed,
            this, [this](const std::string &lid, bool locked) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->locked = locked;
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::layer_expand_changed,
            this, [this](const std::string &lid, bool expanded) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->properties_expanded = expanded;
                    layers_->refresh();
                    timeline_->set_title(title_);
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::layer_parent_changed,
            this, [this](const std::string &lid, const std::string &parent_id) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->parent_id = parent_id;
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::layer_mask_changed,
            this, [this](const std::string &lid, const std::string &mask_source_id, MaskMode mask_mode) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->mask_source_id = mask_source_id;
                    layer->mask_mode = mask_source_id.empty() ? MaskMode::None : mask_mode;
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::layer_name_changed,
            this, [this](const std::string &lid, const std::string &name) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    if (layer->name == name) return;
                    layer->name = name.empty() ? editor_text_std("OBSTitles.Layer") : name;
                    timeline_->set_title(title_);
                    on_title_modified();
                    QTimer::singleShot(0, layers_, [this]() {
                        if (layers_) layers_->refresh();
                    });
                }
            });

    connect(timeline_, &TimelineWidget::playhead_changed,
            this, &TitleEditor::on_playhead_changed);
    connect(timeline_, &TimelineWidget::layer_selected,
            this, &TitleEditor::on_layer_selected);
    connect(timeline_, &TimelineWidget::keyframe_easing_changed,
            this, [this]() { on_title_modified(); });

    connect(props_, &PropertiesPanel::property_changed,
            this, &TitleEditor::on_title_modified);
    connect(props_, &PropertiesPanel::text_char_format_changed,
            this, [this](const std::string &layer_id, const RichTextCharFormat &format, uint32_t mask) {
                if (canvas_) canvas_->apply_active_text_char_format(layer_id, format, mask);
            });
    connect(title_props_, &TitlePropertiesPanel::title_changed,
            this, [this](bool push_undo_snapshot) {
                if (!title_) return;
                playhead_ = std::clamp(playhead_, 0.0, title_->duration);
                on_title_modified(push_undo_snapshot);
                timeline_->set_title(title_);
                on_playhead_changed(playhead_);
            });
    connect(layers_, &LayerStack::layer_order_changed,
            this, [this]() {
                layers_->refresh();
                canvas_->refresh_preview();
                timeline_->set_title(title_);
                on_title_modified();
            });

    connect(canvas_, &CanvasPreview::layer_clicked,
            this, &TitleEditor::on_layer_selected);
    connect(canvas_, &CanvasPreview::layers_selected,
            this, [this](const std::vector<std::string> &ids) {
                sel_layer_id_ = ids.size() == 1 ? ids.back() : std::string();
                layers_->set_selected_layers(ids);
                canvas_->set_selected_layers(ids);
                timeline_->set_selected_layer(sel_layer_id_);
                if (!title_ || ids.size() != 1) {
                    update_layer_panels(nullptr, playhead_);
                    return;
                }
                auto layer = title_->find_layer(sel_layer_id_);
                update_layer_panels(layer, playhead_);
            });
    connect(canvas_, &CanvasPreview::layer_geometry_changed,
            this, [this]() {
                on_title_modified();
                if (title_ && !sel_layer_id_.empty()) {
                    if (auto layer = title_->find_layer(sel_layer_id_))
                        update_layer_panels(layer, playhead_);
                }
            });
    connect(canvas_, &CanvasPreview::text_edit_changed,
            this, [this](const std::string &layer_id) {
                if (!title_) return;
                if (props_) props_->set_active_text_edit_layer(layer_id);
                on_title_modified(false);
                if (auto layer = title_->find_layer(layer_id))
                    update_layer_panels(layer, playhead_);
            });
    connect(canvas_, &CanvasPreview::text_edit_cursor_changed,
            this, [this](const std::string &layer_id) {
                if (!title_) return;
                if (props_) props_->set_active_text_edit_layer(layer_id);
                if (auto layer = title_->find_layer(layer_id))
                    update_layer_panels(layer, playhead_);
            });
    connect(canvas_, &CanvasPreview::text_edit_committed,
            this, [this](const std::string &layer_id) {
                if (!title_) return;
                if (props_) props_->set_active_text_edit_layer(std::string());
                on_title_modified();
                if (auto layer = title_->find_layer(layer_id))
                    update_layer_panels(layer, playhead_);
            });
    connect(canvas_, &CanvasPreview::color_picked,
            this, &TitleEditor::apply_picked_color_to_selection);
    connect(canvas_, &CanvasPreview::layer_structure_changed,
            this, [this]() {
                layers_->refresh();
                timeline_->set_title(title_);
            });
    connect(canvas_, &CanvasPreview::shape_drawing_started,
            this, &TitleEditor::create_shape_layer_from_canvas);
    connect(canvas_, &CanvasPreview::text_drawing_started,
            this, &TitleEditor::create_text_layer_from_canvas);
    connect(canvas_, &CanvasPreview::shape_drawing_changed,
            this, &TitleEditor::update_canvas_created_shape);
    connect(canvas_, &CanvasPreview::shape_drawing_finished,
            this, &TitleEditor::finish_canvas_created_shape);
    if (tools_sidebar_) {
        connect(tools_sidebar_, &ToolsSidebar::selection_tool_requested, this, [this]() {
            if (canvas_) canvas_->set_selection_tool_active();
        });
        connect(tools_sidebar_, &ToolsSidebar::shape_tool_requested, this, [this](ShapeType shape_type) {
            if (tools_sidebar_) tools_sidebar_->set_selected_shape(shape_type);
            if (canvas_) canvas_->set_shape_tool_active(shape_type);
        });
        connect(tools_sidebar_, &ToolsSidebar::text_tool_requested, this, [this](LayerType type) {
            if (tools_sidebar_) tools_sidebar_->set_selected_text_layer_type(type);
            if (canvas_) canvas_->set_text_tool_active(type);
        });
        connect(tools_sidebar_, &ToolsSidebar::color_picker_tool_requested, this, [this]() {
            if (canvas_) canvas_->set_color_picker_tool_active();
        });
    }
}

void TitleEditor::align_selected_to_canvas(int x_mode, int y_mode)
{
    if (!title_ || sel_layer_id_.empty()) return;
    auto ids = layers_ ? layers_->selected_ids() : std::vector<std::string>{sel_layer_id_};
    std::shared_ptr<Layer> last_layer;
    for (const auto &id : ids) {
        auto layer = title_->find_layer(id);
        if (!layer) continue;
        double lt = std::clamp(playhead_ - layer->in_time, 0.0, std::max(0.0, layer->out_time - layer->in_time));
        double w = eval_box_width(*layer, lt);
        double h = eval_box_height(*layer, lt);
        double x = layer->origin_x * w;
        if (x_mode == 1) x = title_->width / 2.0;
        if (x_mode == 2) x = title_->width - (1.0 - layer->origin_x) * w;
        double y = layer->origin_y * h;
        if (y_mode == 1) y = title_->height / 2.0;
        if (y_mode == 2) y = title_->height - (1.0 - layer->origin_y) * h;
        set_animated_value(layer->pos_x, lt, x);
        set_animated_value(layer->pos_y, lt, y);
        last_layer = layer;
    }
    on_title_modified();
    if (last_layer) update_layer_panels(last_layer, playhead_);
}


void TitleEditor::flip_selected_layers(bool horizontal)
{
    if (!title_ || sel_layer_id_.empty()) return;
    auto ids = layers_ ? layers_->selected_ids() : std::vector<std::string>{sel_layer_id_};
    if (ids.empty()) return;

    std::shared_ptr<Layer> last_layer;
    for (const auto &id : ids) {
        auto layer = title_->find_layer(id);
        if (!layer || layer->locked) continue;
        const double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                                     std::max(0.0, layer->out_time - layer->in_time));
        AnimatedProperty &prop = horizontal ? layer->scale_x : layer->scale_y;
        const double current = prop.evaluate(lt);
        set_animated_value(prop, lt, -current);
        last_layer = layer;
    }

    if (!last_layer) return;
    on_title_modified();
    update_layer_panels(last_layer, playhead_);
}

void TitleEditor::rotate_selected_layers(double degrees)
{
    if (!title_ || sel_layer_id_.empty()) return;
    auto ids = layers_ ? layers_->selected_ids() : std::vector<std::string>{sel_layer_id_};
    if (ids.empty()) return;

    std::shared_ptr<Layer> last_layer;
    for (const auto &id : ids) {
        auto layer = title_->find_layer(id);
        if (!layer || layer->locked) continue;
        const double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                                     std::max(0.0, layer->out_time - layer->in_time));
        set_animated_value(layer->rotation, lt, layer->rotation.evaluate(lt) + degrees);
        last_layer = layer;
    }

    if (!last_layer) return;
    on_title_modified();
    update_layer_panels(last_layer, playhead_);
}

void TitleEditor::align_selected_layers_horizontal()
{
    align_selected_layers(1, -1);
}

void TitleEditor::align_selected_layers_vertical()
{
    align_selected_layers(-1, 1);
}

void TitleEditor::align_selected_layers(int x_mode, int y_mode)
{
    if (!title_ || sel_layer_id_.empty()) return;
    auto ids = layers_ ? layers_->selected_ids() : std::vector<std::string>{sel_layer_id_};
    if (ids.empty()) return;

    struct Entry {
        std::shared_ptr<Layer> layer;
        double lt;
        double width;
        double height;
        double scale_x;
        double scale_y;
    };

    std::vector<Entry> entries;
    double min_left = std::numeric_limits<double>::infinity();
    double max_right = -std::numeric_limits<double>::infinity();
    double min_top = std::numeric_limits<double>::infinity();
    double max_bottom = -std::numeric_limits<double>::infinity();

    for (const auto &id : ids) {
        auto layer = title_->find_layer(id);
        if (!layer || layer->locked) continue;
        double lt = std::clamp(playhead_ - layer->in_time, 0.0, std::max(0.0, layer->out_time - layer->in_time));
        double width = eval_box_width(*layer, lt);
        double height = eval_box_height(*layer, lt);
        double sx = layer->scale_x.evaluate(lt);
        double sy = layer->scale_y.evaluate(lt);
        double x0 = layer->pos_x.evaluate(lt) - layer->origin_x * width * sx;
        double x1 = layer->pos_x.evaluate(lt) + (1.0 - layer->origin_x) * width * sx;
        double y0 = layer->pos_y.evaluate(lt) - layer->origin_y * height * sy;
        double y1 = layer->pos_y.evaluate(lt) + (1.0 - layer->origin_y) * height * sy;
        double left = std::min(x0, x1);
        double right = std::max(x0, x1);
        double top = std::min(y0, y1);
        double bottom = std::max(y0, y1);
        min_left = std::min(min_left, left);
        max_right = std::max(max_right, right);
        min_top = std::min(min_top, top);
        max_bottom = std::max(max_bottom, bottom);
        entries.push_back({layer, lt, width, height, sx, sy});
    }

    if (entries.empty()) return;
    if (alignment_target_ == 0 && entries.size() < 2) return;

    double target_left = min_left;
    double target_hcenter = (min_left + max_right) / 2.0;
    double target_right = max_right;
    double target_top = min_top;
    double target_vcenter = (min_top + max_bottom) / 2.0;
    double target_bottom = max_bottom;

    if (alignment_target_ == 1 || alignment_target_ == 2) {
        const double safe_inset = alignment_target_ == 1 ? OBS_GRAPHICS_SAFE_PERCENT : OBS_ACTION_SAFE_PERCENT;
        target_left = title_->width * safe_inset;
        target_hcenter = title_->width / 2.0;
        target_right = title_->width * (1.0 - safe_inset);
        target_top = title_->height * safe_inset;
        target_vcenter = title_->height / 2.0;
        target_bottom = title_->height * (1.0 - safe_inset);
    } else if (alignment_target_ == 3) {
        target_left = 0.0;
        target_hcenter = title_->width / 2.0;
        target_right = title_->width;
        target_top = 0.0;
        target_vcenter = title_->height / 2.0;
        target_bottom = title_->height;
    }

    std::shared_ptr<Layer> last_layer;
    for (const auto &entry : entries) {
        if (x_mode >= 0) {
            const double x0 = -entry.layer->origin_x * entry.width * entry.scale_x;
            const double x1 = (1.0 - entry.layer->origin_x) * entry.width * entry.scale_x;
            const double left_offset = std::min(x0, x1);
            const double right_offset = std::max(x0, x1);
            double next_x = entry.layer->pos_x.evaluate(entry.lt);
            if (x_mode == 0) next_x = target_left - left_offset;
            if (x_mode == 1) next_x = target_hcenter - (left_offset + right_offset) / 2.0;
            if (x_mode == 2) next_x = target_right - right_offset;
            set_animated_value(entry.layer->pos_x, entry.lt, next_x);
        }
        if (y_mode >= 0) {
            const double y0 = -entry.layer->origin_y * entry.height * entry.scale_y;
            const double y1 = (1.0 - entry.layer->origin_y) * entry.height * entry.scale_y;
            const double top_offset = std::min(y0, y1);
            const double bottom_offset = std::max(y0, y1);
            double next_y = entry.layer->pos_y.evaluate(entry.lt);
            if (y_mode == 0) next_y = target_top - top_offset;
            if (y_mode == 1) next_y = target_vcenter - (top_offset + bottom_offset) / 2.0;
            if (y_mode == 2) next_y = target_bottom - bottom_offset;
            set_animated_value(entry.layer->pos_y, entry.lt, next_y);
        }
        last_layer = entry.layer;
    }
    on_title_modified();
    if (last_layer) update_layer_panels(last_layer, playhead_);
}

void TitleEditor::build_toolbar()
{
    constexpr int kEditorToolbarIconSize = 18;
    constexpr int kEditorToolbarButtonExtent = 30;
    const QPalette pal = qApp->palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor button = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor base = pal.color(QPalette::Base);
    const QColor hover = button.lightness() < 128 ? button.lighter(125) : button.darker(108);

    toolbar_ = new QToolBar(this);
    toolbar_->setMovable(false);
    toolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar_->setIconSize(QSize(kEditorToolbarIconSize, kEditorToolbarIconSize));
    toolbar_->setStyleSheet(QStringLiteral(
        "QToolBar{background:%1;border-bottom:1px solid %2;spacing:2px;}"
        "QToolButton{color:%3;background:transparent;min-width:%4px;min-height:%4px;max-width:%4px;max-height:%4px;padding:0;border:none;}"
        "QToolButton:hover{background:%5;border-radius:3px;}"
        "QToolButton:pressed{background:%6;color:%7;border-radius:3px;}"
        "QToolButton:checked{background:%6;color:%7;border-radius:3px;}"
        "QToolButton[toolButtonStyle=\"1\"]{min-width:auto;max-width:none;padding:2px 8px;}"
        "QDoubleSpinBox{color:%8;background:%9;border:1px solid %2;border-radius:3px;padding:2px 4px;}"
        "QDoubleSpinBox::up-button,QDoubleSpinBox::down-button{width:0;border:none;}")
        .arg(window.name(QColor::HexRgb),
             border.name(QColor::HexRgb),
             button_text.name(QColor::HexRgb),
             QString::number(kEditorToolbarButtonExtent),
             hover.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb),
             highlighted_text.name(QColor::HexRgb),
             text.name(QColor::HexRgb),
             base.name(QColor::HexRgb)));

    act_rew_ = new QAction(obs_icon("rewind.svg"), obsgs_tr("OBSTitles.Rewind"), this);
    act_prev_kf_ = new QAction(obs_icon("previous-keyframe.svg"), obsgs_tr("OBSTitles.PreviousKeyframe"), this);
    act_play_ = new QAction(obs_icon("play.svg"), obsgs_tr("OBSTitles.Play"), this);
    act_full_loop_ = new QAction(obs_icon("loop.svg"), obsgs_tr("OBSTitles.LoopPreview"), this);
    act_full_loop_->setToolTip(obsgs_tr("OBSTitles.LoopPreviewTooltip"));
    act_next_kf_ = new QAction(obs_icon("next-keyframe.svg"), obsgs_tr("OBSTitles.NextKeyframe"), this);

    connect(act_rew_, &QAction::triggered, this, &TitleEditor::rewind);
    connect(act_prev_kf_, &QAction::triggered, this, &TitleEditor::previous_keyframe);
    connect(act_play_, &QAction::triggered, this, &TitleEditor::play_pause);
    connect(act_full_loop_, &QAction::triggered, this, &TitleEditor::play_full_loop);
    connect(act_next_kf_, &QAction::triggered, this, &TitleEditor::next_keyframe);

    toolbar_->addSeparator();
    auto *align_target = new QToolButton(toolbar_);
    align_target->setIcon(obs_icon("alignment-target.svg"));
    align_target->setIconSize(toolbar_->iconSize());
    align_target->setToolButtonStyle(Qt::ToolButtonIconOnly);
    align_target->setToolTip(obsgs_tr("OBSTitles.AlignmentTarget"));
    align_target->setAccessibleName(obsgs_tr("OBSTitles.AlignmentTarget"));
    align_target->setPopupMode(QToolButton::InstantPopup);
    align_target->setFixedSize(kEditorToolbarButtonExtent, kEditorToolbarButtonExtent);
    align_target->setStyleSheet(QStringLiteral("QToolButton::menu-indicator{image:none;}"));
    auto *align_menu = new QMenu(align_target);
    QAction *target_selection = align_menu->addAction(obsgs_tr("OBSTitles.AlignToSelection"));
    QAction *target_title_safe = align_menu->addAction(obsgs_tr("OBSTitles.AlignToTitleSafeGuides"));
    QAction *target_action_safe = align_menu->addAction(obsgs_tr("OBSTitles.AlignToActionSafeGuides"));
    QAction *target_artboard = align_menu->addAction(obsgs_tr("OBSTitles.AlignToArtboard"));
    target_selection->setCheckable(true);
    target_title_safe->setCheckable(true);
    target_action_safe->setCheckable(true);
    target_artboard->setCheckable(true);
    target_artboard->setChecked(true);
    auto update_alignment_target = [this, align_target, target_selection, target_title_safe, target_action_safe, target_artboard](int target) {
        alignment_target_ = target;
        target_selection->setChecked(target == 0);
        target_title_safe->setChecked(target == 1);
        target_action_safe->setChecked(target == 2);
        target_artboard->setChecked(target == 3);
        QString tooltip = obsgs_tr("OBSTitles.AlignToArtboard");
        if (target == 0)
            tooltip = obsgs_tr("OBSTitles.AlignToSelection");
        else if (target == 1)
            tooltip = obsgs_tr("OBSTitles.AlignToTitleSafeGuides");
        else if (target == 2)
            tooltip = obsgs_tr("OBSTitles.AlignToActionSafeGuides");
        align_target->setToolTip(tooltip);
    };
    connect(target_selection, &QAction::triggered, this, [update_alignment_target]() { update_alignment_target(0); });
    connect(target_title_safe, &QAction::triggered, this, [update_alignment_target]() { update_alignment_target(1); });
    connect(target_action_safe, &QAction::triggered, this, [update_alignment_target]() { update_alignment_target(2); });
    connect(target_artboard, &QAction::triggered, this, [update_alignment_target]() { update_alignment_target(3); });
    align_target->setMenu(align_menu);
    toolbar_->addWidget(align_target);

    auto add_align_action = [this](const char *icon_name, const QString &tip, int x_mode, int y_mode) {
        QAction *action = toolbar_->addAction(obs_icon(icon_name), tip);
        action->setToolTip(tip);
        connect(action, &QAction::triggered, this, [this, x_mode, y_mode]() {
            align_selected_layers(x_mode, y_mode);
        });
        return action;
    };
    add_align_action("align-left.svg", obsgs_tr("OBSTitles.AlignLeft"), 0, -1);
    add_align_action("align-horizontal-center.svg", obsgs_tr("OBSTitles.AlignHorizontalCenter"), 1, -1);
    add_align_action("align-right.svg", obsgs_tr("OBSTitles.AlignRight"), 2, -1);
    add_align_action("align-top.svg", obsgs_tr("OBSTitles.AlignTop"), -1, 0);
    add_align_action("align-vertical-center.svg", obsgs_tr("OBSTitles.AlignVerticalCenter"), -1, 1);
    add_align_action("align-bottom.svg", obsgs_tr("OBSTitles.AlignBottom"), -1, 2);
    add_align_action("align-center-artboard.svg", obsgs_tr("OBSTitles.AlignCenterToArtboard"), 1, 1);

    toolbar_->addSeparator();
    auto add_flip_action = [this](const char *icon_name, const QString &text, bool horizontal) {
        QAction *action = toolbar_->addAction(obs_icon(icon_name), text);
        action->setToolTip(text);
        connect(action, &QAction::triggered, this, [this, horizontal]() {
            flip_selected_layers(horizontal);
        });
        return action;
    };
    add_flip_action("flip-horizontal.svg", obsgs_tr("OBSTitles.FlipHorizontal"), true);
    add_flip_action("flip-vertical.svg", obsgs_tr("OBSTitles.FlipVertical"), false);

    toolbar_->addSeparator();
    auto *rotation_degrees = new QDoubleSpinBox(toolbar_);
    rotation_degrees->setRange(-9999.0, 9999.0);
    rotation_degrees->setDecimals(1);
    rotation_degrees->setSingleStep(1.0);
    rotation_degrees->setValue(90.0);
    rotation_degrees->setSuffix(QStringLiteral("°"));
    rotation_degrees->setToolTip(obsgs_tr("OBSTitles.RotateDegreesTooltip"));
    rotation_degrees->setAccessibleName(obsgs_tr("OBSTitles.RotateDegrees"));
    rotation_degrees->setFixedWidth(78);
    toolbar_->addWidget(rotation_degrees);
    auto add_rotate_action = [this, rotation_degrees](const char *icon_name, const QString &text, double direction) {
        QAction *action = toolbar_->addAction(obs_icon(icon_name), text);
        action->setToolTip(text);
        connect(action, &QAction::triggered, this, [this, rotation_degrees, direction]() {
            rotate_selected_layers(rotation_degrees->value() * direction);
        });
        return action;
    };
    add_rotate_action("rotate-left.svg", obsgs_tr("OBSTitles.RotateLeft"), -1.0);
    add_rotate_action("rotate-right.svg", obsgs_tr("OBSTitles.RotateRight"), 1.0);

    act_safe_guides_ = new QAction(obs_icon("safe.svg"), obsgs_tr("OBSTitles.Safe"), this);
    act_safe_guides_->setCheckable(true);
    act_safe_guides_->setToolTip(obsgs_tr("OBSTitles.SafeTooltip"));
    connect(act_safe_guides_, &QAction::toggled, this, [this](bool visible) {
        if (canvas_) canvas_->set_safe_guides_visible(visible);
    });

    toolbar_->addSeparator();
    toolbar_->addAction(act_undo_);
    toolbar_->addAction(act_redo_);
    update_undo_redo_actions();

    auto *toolbar_spacer = new QWidget(toolbar_);
    toolbar_spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(toolbar_spacer);

    act_live_editing_ = new QAction(obsgs_tr("OBSTitles.LiveEditing"), this);
    act_live_editing_->setCheckable(true);
    act_live_editing_->setToolTip(obsgs_tr("OBSTitles.LiveEditingTooltip"));
    connect(act_live_editing_, &QAction::toggled, this, &TitleEditor::set_live_editing_enabled);

    auto *live_editing_button = new QToolButton(toolbar_);
    live_editing_button->setDefaultAction(act_live_editing_);
    live_editing_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    live_editing_button->setStyleSheet(QStringLiteral(
        "QToolButton{color:%1;background:transparent;min-width:0;max-width:none;min-height:%2px;max-height:%2px;padding:2px 8px;border:none;}"
        "QToolButton:hover{background:%3;border-radius:3px;}"
        "QToolButton:checked{background:%4;color:%5;border-radius:3px;}")
        .arg(button_text.name(QColor::HexRgb),
             QString::number(kEditorToolbarButtonExtent),
             hover.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb),
             highlighted_text.name(QColor::HexRgb)));
    toolbar_->addWidget(live_editing_button);

}


static QString editor_template_library_root_path()
{
    char *path = obs_module_config_path("template-library");
    QString root = path ? QString::fromUtf8(path) : QDir::homePath();
    if (path) bfree(path);
    QDir().mkpath(root);
    return root;
}

static int editor_dialog_layout_spacing(QWidget *widget)
{
    const int spacing = widget->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing, nullptr, widget);
    return spacing > 0 ? spacing : 6;
}

static QStringList editor_template_library_category_paths(const QString &root_path)
{
    QStringList categories;
    QDirIterator it(root_path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString rel = QDir(root_path).relativeFilePath(it.filePath());
        if (!rel.isEmpty()) categories << rel;
    }
    if (categories.isEmpty()) categories << QStringLiteral("Custom");
    categories.sort(Qt::CaseInsensitive);
    return categories;
}

static QString editor_sanitized_template_category_path(const QString &category)
{
    QString cleaned = QDir::cleanPath(category.trimmed());
    cleaned.replace(QRegularExpression(QStringLiteral("[\\\\:*?\"<>|]")), QStringLiteral("_"));
    while (cleaned.startsWith(QStringLiteral("../")))
        cleaned.remove(0, 3);
    if (cleaned == QStringLiteral(".") || cleaned == QStringLiteral(".."))
        cleaned.clear();
    return cleaned;
}

static bool prompt_editor_template_library_category(QWidget *parent, QString &category)
{
    const QString root_path = editor_template_library_root_path();
    QDialog dialog(parent);
    dialog.setWindowTitle(obsgs_tr("OBSTitles.TemplateLibraryCategoryTitle"));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(editor_dialog_layout_spacing(&dialog));

    auto *prompt = new QLabel(obsgs_tr("OBSTitles.TemplateLibraryCategoryPrompt"), &dialog);
    prompt->setWordWrap(true);
    layout->addWidget(prompt);

    auto *combo = new QComboBox(&dialog);
    combo->setEditable(true);
    combo->addItems(editor_template_library_category_paths(root_path));
    layout->addWidget(combo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString safe_category = editor_sanitized_template_category_path(combo->currentText());
        if (safe_category.isEmpty()) {
            QMessageBox::warning(&dialog, obsgs_tr("OBSTitles.TemplateLibraryCategoryTitle"),
                                 obsgs_tr("OBSTitles.TemplateLibraryCategoryRequired"));
            return;
        }
        category = safe_category;
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    return dialog.exec() == QDialog::Accepted;
}

static bool prompt_editor_template_metadata(QWidget *parent, const Title &title,
                                           TitleTemplateExportMetadata &metadata,
                                           const QString &window_title = obsgs_tr("OBSTitles.ExportTemplateDetails"))
{
    if (metadata.title.empty()) metadata.title = title.name;
    if (metadata.description.empty()) metadata.description = title.description;
    if (metadata.creator.empty()) metadata.creator = title.creator;
    if (metadata.creation_date.empty()) {
        metadata.creation_date = title.creation_date.empty()
            ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()
            : title.creation_date;
    }
    if (metadata.screenshot_png_base64.empty())
        metadata.screenshot_png_base64 = title.preview_screenshot_png_base64;

    QDialog dialog(parent);
    dialog.setWindowTitle(window_title);
    dialog.setModal(true);
    dialog.resize(560, 500);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(editor_dialog_layout_spacing(&dialog));

    auto *preview_label = new QLabel(obsgs_tr("OBSTitles.TemplateScreenshotPreviewLabel"), &dialog);
    QFont label_font = preview_label->font();
    label_font.setBold(true);
    preview_label->setFont(label_font);
    layout->addWidget(preview_label);

    auto *preview = new QLabel(&dialog);
    preview->setAlignment(Qt::AlignCenter);
    preview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    preview->setMinimumHeight(160);
    QPixmap pixmap;
    const QByteArray png = QByteArray::fromBase64(QByteArray::fromStdString(metadata.screenshot_png_base64));
    if (!png.isEmpty() && pixmap.loadFromData(png, "PNG"))
        preview->setPixmap(pixmap.scaled(QSize(480, 180), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else
        preview->setText(obsgs_tr("OBSTitles.TemplateScreenshotFailed"));
    layout->addWidget(preview);

    auto *form = new QFormLayout();
    auto *title_edit = new QLineEdit(QString::fromStdString(metadata.title), &dialog);
    auto *description_edit = new QTextEdit(&dialog);
    description_edit->setAcceptRichText(false);
    description_edit->setPlainText(QString::fromStdString(metadata.description));
    description_edit->setMinimumHeight(96);
    auto *creator_edit = new QLineEdit(QString::fromStdString(metadata.creator), &dialog);
    auto *date_edit = new QLineEdit(QString::fromStdString(metadata.creation_date), &dialog);

    form->addRow(obsgs_tr("OBSTitles.TemplateExportTitleLabel"), title_edit);
    form->addRow(obsgs_tr("OBSTitles.TemplateExportDescriptionLabel"), description_edit);
    form->addRow(obsgs_tr("OBSTitles.TemplateExportCreatorLabel"), creator_edit);
    form->addRow(obsgs_tr("OBSTitles.TemplateCreationDateLabel"), date_edit);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (title_edit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, obsgs_tr("OBSTitles.ExportTemplateDetails"),
                                 obsgs_tr("OBSTitles.TemplateExportTitleRequired"));
            return;
        }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    metadata.title = title_edit->text().trimmed().toStdString();
    metadata.description = description_edit->toPlainText().trimmed().toStdString();
    metadata.creator = creator_edit->text().trimmed().toStdString();
    metadata.creation_date = date_edit->text().trimmed().toStdString();
    return true;
}

void TitleEditor::copy_title_to_store(const std::shared_ptr<Title> &source,
                                      const std::shared_ptr<Title> &dest) const
{
    if (!source || !dest) return;
    const std::string dest_id = dest->id;
    *dest = *source;
    dest->id = dest_id;
    dest->layers.clear();
    dest->layers.reserve(source->layers.size());
    for (const auto &layer : source->layers) {
        if (layer) dest->layers.push_back(std::make_shared<Layer>(*layer));
    }
}

void TitleEditor::new_title_contents()
{
    if (!title_) return;
    if (QMessageBox::question(this, obsgs_tr("OBSTitles.New"),
                              obsgs_tr("OBSTitles.NewTitleConfirm")) != QMessageBox::Yes)
        return;

    title_->layers.clear();
    sel_layer_id_.clear();
    layers_->refresh();
    canvas_->set_selected_layers({});
    update_layer_panels(nullptr, playhead_);
    on_title_modified();
}

bool TitleEditor::persist_title_changes(bool update_preview_screenshot, bool show_saved_status)
{
    if (!title_) return false;
    auto stored = TitleDataStore::instance().get_title(editing_title_id_.empty() ? title_->id : editing_title_id_);
    if (!stored) {
        stored = TitleDataStore::instance().create_title(title_->name);
        editing_title_id_ = stored->id;
        title_->id = stored->id;
    }
    copy_title_to_store(title_, stored);
    if (update_preview_screenshot) {
        title_->preview_screenshot_png_base64 = title_manual_screenshot_png_base64(*title_);
        stored->preview_screenshot_png_base64 = title_->preview_screenshot_png_base64;
    }
    TitleDataStore::instance().notify_change();
    TitleDataStore::instance().save();
    emit title_saved(stored->id);
    set_dirty(false);
    if (show_saved_status)
        setWindowTitle(obsgs_tr("OBSTitles.EditorSavedTitle"));
    return true;
}

bool TitleEditor::save_title()
{
    return persist_title_changes(true, true);
}

void TitleEditor::set_live_editing_enabled(bool enabled)
{
    live_editing_ = enabled;
    if (act_live_editing_ && act_live_editing_->isChecked() != enabled) {
        QSignalBlocker blocker(act_live_editing_);
        act_live_editing_->setChecked(enabled);
    }
    if (live_editing_ && dirty_)
        save_live_edit();
}

void TitleEditor::set_gpu_pipeline_enabled(bool enabled)
{
    if (TitlePreferences::use_gpu() == enabled)
        return;

    TitlePreferences::set_use_gpu(enabled);
    TitlePreferences::notify_changed(this);
    update_title_bar();
    if (canvas_)
        canvas_->refresh_preview();
}

void TitleEditor::save_live_edit()
{
    if (!live_editing_ || !title_) return;
    persist_title_changes(false, false);
}

void TitleEditor::save_title_as_new()
{
    if (!title_) return;

    Title temp = *title_;
    temp.preview_screenshot_png_base64 = title_manual_screenshot_png_base64(temp);
    TitleTemplateExportMetadata metadata;
    metadata.screenshot_png_base64 = temp.preview_screenshot_png_base64;
    if (!prompt_editor_template_metadata(this, temp, metadata, obsgs_tr("OBSTitles.SaveAsNew")))
        return;

    auto created = TitleDataStore::instance().create_title(metadata.title);
    title_->name = metadata.title;
    title_->description = metadata.description;
    title_->creator = metadata.creator;
    title_->creation_date = metadata.creation_date;
    title_->preview_screenshot_png_base64 = metadata.screenshot_png_base64;
    copy_title_to_store(title_, created);
    created->name = metadata.title;
    created->description = metadata.description;
    created->creator = metadata.creator;
    created->creation_date = metadata.creation_date;
    created->preview_screenshot_png_base64 = metadata.screenshot_png_base64;
    editing_title_id_ = created->id;
    title_->id = created->id;
    update_title_bar();
    TitleDataStore::instance().notify_change();
    TitleDataStore::instance().save();
    emit title_saved(created->id);
    set_dirty(false);
    setWindowTitle(obsgs_tr("OBSTitles.EditorSavedTitle"));
}

void TitleEditor::export_title_template(bool save_in_library)
{
    if (!title_) return;

    Title temp = *title_;
    temp.preview_screenshot_png_base64 = title_manual_screenshot_png_base64(temp);
    TitleTemplateExportMetadata metadata;
    metadata.screenshot_png_base64 = temp.preview_screenshot_png_base64;
    if (!prompt_editor_template_metadata(this, temp, metadata))
        return;

    QString safe_name = QString::fromStdString(metadata.title).trimmed();
    if (safe_name.isEmpty()) safe_name = obsgs_tr("OBSTitles.TemplateFileDialogTitle");
    safe_name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));

    QString path;
    if (save_in_library) {
        QString category;
        if (!prompt_editor_template_library_category(this, category))
            return;
        QDir root(editor_template_library_root_path());
        root.mkpath(category);
        path = root.filePath(QStringLiteral("%1/%2.ogspt").arg(category, safe_name));
    } else {
        path = QFileDialog::getSaveFileName(this, obsgs_tr("OBSTitles.ExportTitleTemplate"),
                                            QDir(editor_template_library_root_path()).filePath(safe_name + QStringLiteral(".ogspt")),
                                            obsgs_tr("OBSTitles.TemplateFileFilter"));
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".ogspt");
    }

    auto stored = TitleDataStore::instance().create_title(metadata.title);
    copy_title_to_store(title_, stored);
    stored->name = metadata.title;
    stored->description = metadata.description;
    stored->creator = metadata.creator;
    stored->creation_date = metadata.creation_date;
    stored->preview_screenshot_png_base64 = metadata.screenshot_png_base64;

    std::string error;
    if (!TitleDataStore::instance().export_title(stored->id, path.toStdString(), metadata, &error)) {
        QMessageBox::warning(this, obsgs_tr("OBSTitles.ExportTitleTemplate"), QString::fromStdString(error));
    } else {
        QMessageBox::information(this, obsgs_tr("OBSTitles.ExportTitleTemplate"),
                                 obsgs_tr("OBSTitles.ExportedStatusFormat").arg(QFileInfo(path).fileName()));
    }
    TitleDataStore::instance().delete_title(stored->id);
}

/* ── open_title ──────────────────────────────────────────────────── */
void TitleEditor::open_title(const std::string &tid)
{
    play_timer_->stop();
    playing_ = false;
    act_play_->setText("▶");
    act_play_->setIcon(obs_icon("play.svg"));
    playhead_ = 0.0;
    playback_reverse_ = false;
    full_loop_playback_ = false;

    auto stored_title = TitleDataStore::instance().get_title(tid);
    if (!stored_title) return;
    editing_title_id_ = tid;
    title_ = clone_title(*stored_title);

    update_title_bar();
    canvas_->set_title(title_);
    layers_->set_title(title_);
    layers_->set_layer_clipboard_available(!layer_clipboard_.empty());
    timeline_->set_title(title_);
    props_->set_title(title_);
    title_props_->set_title(title_);

    if (!title_->layers.empty())
        on_layer_selected(title_->layers.back()->id);
    else
        update_layer_panels(nullptr, playhead_);

    QTimer::singleShot(0, timeline_, [this]() {
        if (timeline_) timeline_->fit_timeline();
    });

    undo_stack_.clear();
    undo_index_ = -1;
    push_undo_snapshot();
    update_undo_redo_actions();

    on_playhead_changed(0.0);
    set_dirty(false);
}

std::shared_ptr<Title> TitleEditor::clone_title(const Title &title) const
{
    auto clone = std::make_shared<Title>(title);
    clone->layers.clear();
    clone->layers.reserve(title.layers.size());
    for (const auto &layer : title.layers) {
        if (layer) clone->layers.push_back(std::make_shared<Layer>(*layer));
    }
    return clone;
}


std::shared_ptr<Layer> TitleEditor::clone_layer_for_insert(const Layer &layer, bool suffix_name) const
{
    auto clone = std::make_shared<Layer>(layer);
    clone->id = TitleDataStore::make_uuid();
    if (suffix_name)
        clone->name = clone->name.empty() ? editor_text_std("OBSTitles.LayerCopy") : clone->name + editor_text_std("OBSTitles.CopySuffix");
    if (!clone->parent_id.empty() && (!title_ || !title_->find_layer(clone->parent_id)))
        clone->parent_id.clear();
    if (!clone->mask_source_id.empty() && (!title_ || !title_->find_layer(clone->mask_source_id))) {
        clone->mask_source_id.clear();
        clone->mask_mode = MaskMode::None;
    }
    return clone;
}

void TitleEditor::insert_layer_above(const std::string &anchor_id, std::shared_ptr<Layer> layer)
{
    if (!title_ || !layer) return;

    auto it = std::find_if(title_->layers.begin(), title_->layers.end(),
                           [&](const auto &candidate) {
                               return candidate && candidate->id == anchor_id;
                           });
    if (it == title_->layers.end())
        title_->layers.push_back(layer);
    else
        title_->layers.insert(it + 1, layer);
}

void TitleEditor::select_after_layer_list_mutation(const std::string &layer_id)
{
    layers_->refresh();
    timeline_->set_title(title_);
    on_layer_selected(layer_id);
}


std::vector<std::string> TitleEditor::selected_layer_ids_for_operation() const
{
    std::vector<std::string> requested = layers_ ? layers_->selected_ids() : std::vector<std::string>{};
    if (requested.empty() && !sel_layer_id_.empty())
        requested.push_back(sel_layer_id_);

    std::set<std::string> requested_set(requested.begin(), requested.end());
    std::vector<std::string> ordered_ids;
    if (!title_ || requested_set.empty()) return ordered_ids;

    for (const auto &layer : title_->layers) {
        if (layer && requested_set.find(layer->id) != requested_set.end())
            ordered_ids.push_back(layer->id);
    }
    return ordered_ids;
}

std::vector<std::shared_ptr<Layer>> TitleEditor::clone_layers_for_insert(const std::vector<std::shared_ptr<Layer>> &layers,
                                                                         bool suffix_name) const
{
    std::map<std::string, std::string> cloned_ids_by_original;
    std::vector<std::shared_ptr<Layer>> clones;
    clones.reserve(layers.size());

    for (const auto &layer : layers) {
        if (!layer) continue;
        auto clone = std::make_shared<Layer>(*layer);
        clone->id = TitleDataStore::make_uuid();
        if (suffix_name)
            clone->name = clone->name.empty() ? editor_text_std("OBSTitles.LayerCopy")
                                              : clone->name + editor_text_std("OBSTitles.CopySuffix");
        cloned_ids_by_original[layer->id] = clone->id;
        clones.push_back(clone);
    }

    for (auto &clone : clones) {
        auto parent_clone = cloned_ids_by_original.find(clone->parent_id);
        if (parent_clone != cloned_ids_by_original.end()) {
            clone->parent_id = parent_clone->second;
        } else if (!clone->parent_id.empty() && (!title_ || !title_->find_layer(clone->parent_id))) {
            clone->parent_id.clear();
        }

        auto mask_clone = cloned_ids_by_original.find(clone->mask_source_id);
        if (mask_clone != cloned_ids_by_original.end()) {
            clone->mask_source_id = mask_clone->second;
        } else if (!clone->mask_source_id.empty() && (!title_ || !title_->find_layer(clone->mask_source_id))) {
            clone->mask_source_id.clear();
            clone->mask_mode = MaskMode::None;
        }
    }

    return clones;
}

void TitleEditor::apply_picked_color_to_selection(const QColor &color)
{
    if (!title_ || !color.isValid()) return;

    const auto ids = selected_layer_ids_for_operation();
    if (ids.empty()) return;

    const uint32_t argb = argb_from_color(color);
    std::shared_ptr<Layer> last_changed;
    for (const auto &id : ids) {
        auto layer = title_->find_layer(id);
        if (!layer || layer->locked) continue;

        const double local_time = std::clamp(playhead_ - layer->in_time, 0.0,
                                             std::max(0.0, layer->out_time - layer->in_time));
        if (is_canvas_text_layer(*layer)) {
            layer->fill_type = 0;
            layer->text_color = argb;
            set_color_channels_at(*layer, true, local_time, argb);
            RichTextCharFormat fmt = layer_char_format_for_editor(*layer);
            apply_rich_text_format_to_layer_range(*layer, fmt, RichTextCharFillColor, false);
            last_changed = layer;
        } else if (layer->type == LayerType::Shape || layer->type == LayerType::SolidRect) {
            layer->fill_type = 0;
            layer->fill_color = argb;
            set_color_channels_at(*layer, false, local_time, argb);
            last_changed = layer;
        }
    }

    if (!last_changed) return;
    on_title_modified();
    update_layer_panels(last_changed, playhead_);
}

void TitleEditor::duplicate_selected_layers()
{
    if (!title_) return;
    const auto ids = selected_layer_ids_for_operation();
    if (ids.empty()) return;

    std::set<std::string> selected_ids(ids.begin(), ids.end());
    std::vector<std::shared_ptr<Layer>> originals;
    originals.reserve(ids.size());
    for (const auto &layer : title_->layers) {
        if (layer && selected_ids.find(layer->id) != selected_ids.end())
            originals.push_back(layer);
    }

    auto clones = clone_layers_for_insert(originals, true);
    if (clones.empty()) return;

    std::map<std::string, std::shared_ptr<Layer>> clones_by_original;
    for (size_t i = 0; i < originals.size() && i < clones.size(); ++i)
        clones_by_original[originals[i]->id] = clones[i];

    std::vector<std::shared_ptr<Layer>> next_layers;
    next_layers.reserve(title_->layers.size() + clones.size());
    for (const auto &layer : title_->layers) {
        next_layers.push_back(layer);
        if (!layer) continue;
        auto clone = clones_by_original.find(layer->id);
        if (clone != clones_by_original.end())
            next_layers.push_back(clone->second);
    }
    title_->layers = std::move(next_layers);

    std::vector<std::string> clone_ids;
    clone_ids.reserve(clones.size());
    for (const auto &clone : clones)
        if (clone) clone_ids.push_back(clone->id);

    sel_layer_id_ = clone_ids.empty() ? std::string() : clone_ids.back();
    layers_->refresh();
    layers_->set_selected_layers(clone_ids);
    canvas_->set_selected_layers(clone_ids);
    timeline_->set_title(title_);
    timeline_->set_selected_layer(sel_layer_id_);
    if (!sel_layer_id_.empty()) {
        if (auto layer = title_->find_layer(sel_layer_id_)) update_layer_panels(layer, playhead_);
    }
    on_title_modified();
}

void TitleEditor::copy_selected_layer()
{
    if (!title_) return;
    const auto ids = selected_layer_ids_for_operation();
    if (ids.empty()) return;

    layer_clipboard_.clear();
    layer_clipboard_.reserve(ids.size());
    for (const auto &id : ids) {
        auto layer = title_->find_layer(id);
        if (layer) layer_clipboard_.push_back(std::make_shared<Layer>(*layer));
    }
    if (layers_) layers_->set_layer_clipboard_available(!layer_clipboard_.empty());
}

void TitleEditor::paste_layer_from_clipboard()
{
    if (!title_ || layer_clipboard_.empty()) return;

    std::vector<std::shared_ptr<Layer>> clipboard_layers;
    clipboard_layers.reserve(layer_clipboard_.size());
    for (const auto &layer : layer_clipboard_)
        if (layer) clipboard_layers.push_back(layer);

    auto pasted_layers = clone_layers_for_insert(clipboard_layers, true);
    if (pasted_layers.empty()) return;

    auto insert_pos = title_->layers.end();
    if (!sel_layer_id_.empty()) {
        auto anchor = std::find_if(title_->layers.begin(), title_->layers.end(),
                                   [this](const auto &layer) { return layer && layer->id == sel_layer_id_; });
        if (anchor != title_->layers.end()) insert_pos = anchor + 1;
    }
    title_->layers.insert(insert_pos, pasted_layers.begin(), pasted_layers.end());

    std::vector<std::string> pasted_ids;
    pasted_ids.reserve(pasted_layers.size());
    for (const auto &layer : pasted_layers)
        if (layer) pasted_ids.push_back(layer->id);

    sel_layer_id_ = pasted_ids.empty() ? std::string() : pasted_ids.back();
    layers_->refresh();
    layers_->set_selected_layers(pasted_ids);
    canvas_->set_selected_layers(pasted_ids);
    timeline_->set_title(title_);
    timeline_->set_selected_layer(sel_layer_id_);
    if (!sel_layer_id_.empty()) {
        if (auto layer = title_->find_layer(sel_layer_id_)) update_layer_panels(layer, playhead_);
    }
    on_title_modified();
}

void TitleEditor::delete_selected_layer()
{
    if (!title_) return;
    const auto ids = selected_layer_ids_for_operation();
    if (ids.empty()) return;

    std::set<std::string> removed_ids(ids.begin(), ids.end());
    int first_removed_index = (int)title_->layers.size();
    std::vector<std::shared_ptr<Layer>> remaining;
    remaining.reserve(title_->layers.size());
    for (int i = 0; i < (int)title_->layers.size(); ++i) {
        auto &layer = title_->layers[(size_t)i];
        if (!layer || removed_ids.find(layer->id) != removed_ids.end()) {
            first_removed_index = std::min(first_removed_index, i);
            continue;
        }
        remaining.push_back(layer);
    }

    if (remaining.size() == title_->layers.size()) return;

    for (auto &layer : remaining) {
        if (!layer) continue;
        if (removed_ids.find(layer->parent_id) != removed_ids.end()) layer->parent_id.clear();
        if (removed_ids.find(layer->mask_source_id) != removed_ids.end()) {
            layer->mask_source_id.clear();
            layer->mask_mode = MaskMode::None;
        }
    }

    title_->layers = std::move(remaining);
    sel_layer_id_.clear();
    layers_->refresh();

    if (!title_->layers.empty()) {
        const int select_index = std::clamp(first_removed_index, 0, (int)title_->layers.size() - 1);
        on_layer_selected(title_->layers[(size_t)select_index]->id);
    } else {
        layers_->set_selected_layers({});
        canvas_->set_selected_layers({});
        timeline_->set_title(title_);
        timeline_->set_selected_layer(std::string());
        update_layer_panels(nullptr, playhead_);
    }

    on_title_modified();
}

void TitleEditor::cut_selected_layer()
{
    copy_selected_layer();
    delete_selected_layer();
}

void TitleEditor::push_undo_snapshot()
{
    if (!title_ || restoring_undo_) return;
    if (undo_index_ + 1 < (int)undo_stack_.size())
        undo_stack_.erase(undo_stack_.begin() + undo_index_ + 1, undo_stack_.end());
    undo_stack_.push_back(clone_title(*title_));
    if (undo_stack_.size() > 30)
        undo_stack_.erase(undo_stack_.begin());
    undo_index_ = (int)undo_stack_.size() - 1;
    update_undo_redo_actions();
}

void TitleEditor::restore_undo_snapshot(int index)
{
    if (!title_ || index < 0 || index >= (int)undo_stack_.size()) return;
    restoring_undo_ = true;
    auto snapshot = undo_stack_[(size_t)index];
    title_->name = snapshot->name;
    title_->description = snapshot->description;
    title_->creator = snapshot->creator;
    title_->creation_date = snapshot->creation_date;
    title_->duration = snapshot->duration;
    title_->loop_start = snapshot->loop_start;
    title_->loop_end = snapshot->loop_end;
    title_->playback_mode = snapshot->playback_mode;
    title_->loop_type = snapshot->loop_type;
    title_->pause_time = snapshot->pause_time;
    title_->bg_color = snapshot->bg_color;
    title_->width = snapshot->width;
    title_->height = snapshot->height;
    title_->live_text_rows = snapshot->live_text_rows;
    title_->live_text_column_order = snapshot->live_text_column_order;
    title_->live_text_header_state = snapshot->live_text_header_state;
    title_->external_data_enabled = snapshot->external_data_enabled;
    title_->current_cue_row = snapshot->current_cue_row;
    title_->pending_cue_row = snapshot->pending_cue_row;
    title_->cue_revision = snapshot->cue_revision;
    title_->layers.clear();
    title_->layers.reserve(snapshot->layers.size());
    for (const auto &layer : snapshot->layers) {
        if (layer) title_->layers.push_back(std::make_shared<Layer>(*layer));
    }
    undo_index_ = index;
    if (!sel_layer_id_.empty() && !title_->find_layer(sel_layer_id_))
        sel_layer_id_.clear();
    if (sel_layer_id_.empty() && !title_->layers.empty())
        sel_layer_id_ = title_->layers.back()->id;
    update_title_bar();
    canvas_->set_title(title_, true);
    layers_->set_title(title_);
    timeline_->set_title(title_);
    props_->set_title(title_);
    title_props_->set_title(title_);
    if (!sel_layer_id_.empty()) on_layer_selected(sel_layer_id_);
    else update_layer_panels(nullptr, playhead_);
    on_playhead_changed(std::clamp(playhead_, 0.0, title_->duration));
    restoring_undo_ = false;
    update_undo_redo_actions();
    set_dirty(true);
    save_live_edit();
}

void TitleEditor::update_undo_redo_actions()
{
    if (act_undo_) act_undo_->setEnabled(undo_index_ > 0);
    if (act_redo_) act_redo_->setEnabled(undo_index_ >= 0 && undo_index_ + 1 < (int)undo_stack_.size());
}

void TitleEditor::update_title_bar()
{
    if (title_ && title_lbl_)
        title_lbl_->setText(QString::fromStdString(title_->name));
    if (graphic_props_dock_) {
        graphic_props_dock_->setWindowTitle(title_
            ? QStringLiteral("Properties: %1").arg(QString::fromStdString(title_->name))
            : QStringLiteral("Properties"));
    }
    if (dirty_indicator_)
        dirty_indicator_->setVisible(dirty_);
    if (gpu_warning_lbl_) {
        const bool show_gpu_warning = TitlePreferences::use_gpu() && !TitlePreferences::gpu_available();
        gpu_warning_lbl_->setVisible(show_gpu_warning);
        gpu_warning_lbl_->setText(show_gpu_warning ? obsgs_tr("OBSTitles.GPUFallbackWarning") : QString());
        gpu_warning_lbl_->setToolTip(show_gpu_warning
            ? QString::fromUtf8(TitlePreferences::gpu_unavailable_reason())
            : QString());
    }
}

void TitleEditor::begin_title_name_edit()
{
    if (!title_ || !title_lbl_ || !title_name_edit_)
        return;

    title_name_edit_->setText(QString::fromStdString(title_->name));
    title_lbl_->hide();
    title_name_edit_->show();
    title_name_edit_->setFocus(Qt::MouseFocusReason);
    title_name_edit_->selectAll();
}

void TitleEditor::commit_title_name_edit(bool accept)
{
    if (!title_name_edit_ || !title_name_edit_->isVisible())
        return;

    const QString next_name = title_name_edit_->text().trimmed();
    title_name_edit_->hide();
    if (title_lbl_)
        title_lbl_->show();

    if (accept && title_ && !next_name.isEmpty() && next_name.toStdString() != title_->name) {
        title_->name = next_name.toStdString();
        update_title_bar();
        if (title_props_)
            title_props_->set_title(title_);
        set_dirty(true);
        push_undo_snapshot();
        save_live_edit();
    } else {
        update_title_bar();
    }
}

void TitleEditor::set_dirty(bool dirty)
{
    dirty_ = dirty;
    if (dirty_indicator_)
        dirty_indicator_->setVisible(dirty_);
    update_title_bar();
    setWindowTitle(obsgs_tr(dirty_ ? "OBSTitles.EditorModifiedTitle" : "OBSTitles.EditorWindowTitle"));
}

bool TitleEditor::confirm_save_before_close()
{
    if (!dirty_)
        return true;

    QMessageBox dialog(this);
    dialog.setIcon(QMessageBox::Warning);
    dialog.setWindowTitle(obsgs_tr("OBSTitles.UnsavedChangesTitle"));
    dialog.setText(obsgs_tr("OBSTitles.UnsavedChangesPrompt"));
    dialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    dialog.setDefaultButton(QMessageBox::Yes);
    dialog.setEscapeButton(QMessageBox::Cancel);

    const auto result = static_cast<QMessageBox::StandardButton>(dialog.exec());
    if (result == QMessageBox::Yes)
        return save_title();
    if (result == QMessageBox::No)
        return true;
    return false;
}

/* ── Transport ───────────────────────────────────────────────────── */
void TitleEditor::play_pause()
{
    if (!title_) return;
    if (!playing_)
        full_loop_playback_ = false;
    playing_ = !playing_;
    if (playing_) {
        if (title_->playback_mode != 2 && playhead_ >= title_->duration)
            on_playhead_changed(0.0);
        if (title_->playback_mode == 2 && playhead_ >= std::clamp(title_->pause_time, 0.0, title_->duration))
            on_playhead_changed(0.0);
        act_play_->setText("⏸");
        act_play_->setIcon(obs_icon("pause.svg"));
        playback_clock_.restart();
        play_timer_->start();
    } else {
        act_play_->setText("▶");
        act_play_->setIcon(obs_icon("play.svg"));
        play_timer_->stop();
    }
}

void TitleEditor::play_full_loop()
{
    if (!title_) return;
    full_loop_playback_ = true;
    playback_reverse_ = false;
    if (!playing_ || playhead_ >= title_->duration)
        on_playhead_changed(0.0);
    playing_ = true;
    act_play_->setText("⏸");
    act_play_->setIcon(obs_icon("pause.svg"));
    playback_clock_.restart();
    play_timer_->start();
}

void TitleEditor::rewind()
{
    full_loop_playback_ = false;
    playback_reverse_ = false;
    on_playhead_changed(0.0);
}

void TitleEditor::step_forward()
{
    if (!title_) return;
    on_playhead_changed(std::min(snap_to_obs_frame(playhead_ + obs_frame_duration()), title_->duration));
}


static void collect_timeline_keyframes(const std::shared_ptr<Layer> &layer,
                                       std::vector<double> &times)
{
    if (!layer) return;
    for (auto *prop : timeline_properties(*layer)) {
        for (const auto &kf : prop->keyframes)
            times.push_back(layer->in_time + kf.time);
    }
}

void TitleEditor::previous_keyframe()
{
    if (!title_) return;
    std::vector<double> times;
    if (!sel_layer_id_.empty())
        collect_timeline_keyframes(title_->find_layer(sel_layer_id_), times);
    if (times.empty())
        for (const auto &layer : title_->layers) collect_timeline_keyframes(layer, times);

    constexpr double kEpsilon = 1.0 / 240.0;
    double target = -1.0;
    for (double t : times) {
        if (t < playhead_ - kEpsilon)
            target = std::max(target, t);
    }
    if (target >= 0.0) on_playhead_changed(target);
}

void TitleEditor::next_keyframe()
{
    if (!title_) return;
    std::vector<double> times;
    if (!sel_layer_id_.empty())
        collect_timeline_keyframes(title_->find_layer(sel_layer_id_), times);
    if (times.empty())
        for (const auto &layer : title_->layers) collect_timeline_keyframes(layer, times);

    constexpr double kEpsilon = 1.0 / 240.0;
    double target = title_->duration + 1.0;
    for (double t : times) {
        if (t > playhead_ + kEpsilon)
            target = std::min(target, t);
    }
    if (target <= title_->duration) on_playhead_changed(target);
}

void TitleEditor::tick()
{
    if (!title_ || !playing_) return;
    double dt = playback_clock_.isValid() ? playback_clock_.restart() / 1000.0 : 0.0;
    if (dt <= 0.0 || dt > 0.25) dt = play_timer_->interval() / 1000.0;

    double duration = std::max(0.001, title_->duration);
    double loop_start = std::clamp(title_->loop_start, 0.0, title_->duration);
    double loop_end = std::clamp(title_->loop_end, loop_start, title_->duration);
    double loop_len = std::max(0.001, loop_end - loop_start);
    double t = playhead_;

    if (full_loop_playback_) {
        t = std::fmod(playhead_ + dt, duration);
    } else {
        switch (title_->playback_mode) {
        case 1: /* Loop in/out between Loop Start and Loop End */
            if (loop_end <= loop_start + 0.0001) {
                t = std::fmod(playhead_ + dt, duration);
            } else if (title_->loop_type == 1) {
                t += (playback_reverse_ ? -dt : dt);
                if (!playback_reverse_ && t >= loop_end) {
                    t = loop_end - std::fmod(t - loop_end, loop_len);
                    playback_reverse_ = true;
                } else if (playback_reverse_ && t <= loop_start) {
                    t = loop_start + std::fmod(loop_start - t, loop_len);
                    playback_reverse_ = false;
                }
            } else {
                t = playhead_ + dt;
                if (t >= loop_end)
                    t = loop_start + std::fmod(t - loop_end, loop_len);
            }
            break;
        case 2: { /* Pause at timeline position */
            double pause_time = std::clamp(title_->pause_time, 0.0, title_->duration);
            t = playhead_ + dt;
            if (t >= pause_time) {
                t = pause_time;
                playing_ = false;
                play_timer_->stop();
                act_play_->setText("▶");
                act_play_->setIcon(obs_icon("play.svg"));
            }
            break;
        }
        default: /* Play once */
            t = playhead_ + dt;
            if (t >= title_->duration) {
                t = title_->duration;
                playing_ = false;
                play_timer_->stop();
                act_play_->setText("▶");
                act_play_->setIcon(obs_icon("play.svg"));
            }
            break;
        }
    }
    on_playhead_changed(snap_to_obs_frame(t));
}

static bool editor_focus_accepts_text(QWidget *widget)
{
    return qobject_cast<QLineEdit *>(widget) ||
           qobject_cast<QTextEdit *>(widget) ||
           qobject_cast<QAbstractSpinBox *>(widget) ||
           qobject_cast<QComboBox *>(widget);
}

void TitleEditor::show_about()
{
    QMessageBox::about(
        this,
        obsgs_tr("OBSTitles.AboutTitle"),
        obsgs_tr("OBSTitles.AboutTextFormat").arg(QStringLiteral(PLUGIN_VERSION)));
}

void TitleEditor::show_preferences()
{
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(obsgs_tr("OBSTitles.Preferences"));
    dialog->setModal(false);
    dialog->resize(620, 420);

    const QPalette pal = dialog->palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor base = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor button = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor disabled_text = pal.color(QPalette::Disabled, QPalette::WindowText);
    const QColor hover = button.lightness() < 128 ? button.lighter(125) : button.darker(108);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *content = new QWidget(dialog);
    auto *content_layout = new QHBoxLayout(content);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(12);

    auto *tabs = new QListWidget(content);
    tabs->setFixedWidth(138);
    tabs->setFrameShape(QFrame::NoFrame);
    tabs->setSelectionMode(QAbstractItemView::SingleSelection);
    tabs->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabs->addItem(QStringLiteral("Appearance"));
    tabs->addItem(QStringLiteral("Advanced"));
    tabs->setStyleSheet(QStringLiteral(
        "QListWidget{background:%1;border:1px solid %2;color:%3;outline:none;}"
        "QListWidget::item{padding:8px 10px;border-bottom:1px solid %2;}"
        "QListWidget::item:hover{background:%4;}"
        "QListWidget::item:selected{background:%5;color:%6;}")
        .arg(base.name(QColor::HexRgb),
             border.name(QColor::HexRgb),
             text.name(QColor::HexRgb),
             hover.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb),
             highlighted_text.name(QColor::HexRgb)));

    auto *pages = new QStackedWidget(content);
    pages->setStyleSheet(QStringLiteral("QWidget{background:%1;color:%2;}")
                             .arg(window.name(QColor::HexRgb),
                                  text.name(QColor::HexRgb)));

    auto *colors_page = new QWidget(pages);
    auto *colors_layout = new QVBoxLayout(colors_page);
    colors_layout->setContentsMargins(0, 0, 0, 0);
    colors_layout->setSpacing(10);

    auto *colors_title = new QLabel(QStringLiteral("Timeline Colors"), colors_page);
    QFont title_font = colors_title->font();
    title_font.setPointSize(title_font.pointSize() + 2);
    title_font.setBold(true);
    colors_title->setFont(title_font);
    colors_layout->addWidget(colors_title);

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    colors_layout->addLayout(grid);

    auto update_appearance = [this]() {
        if (layers_)
            layers_->refresh();
        if (timeline_)
            timeline_->update();
        if (canvas_)
            canvas_->update();
    };

    auto color_to_text = [](const QColor &color) {
        return color.alpha() < 255 ? color.name(QColor::HexArgb).toUpper()
                                   : color.name(QColor::HexRgb).toUpper();
    };
    auto apply_color_button = [border, highlight, color_to_text](QPushButton *button, const QColor &color) {
        const QColor label_color = color.lightness() < 128 ? Qt::white : Qt::black;
        button->setText(color_to_text(color));
        button->setStyleSheet(QStringLiteral(
            "QPushButton{color:%1;background:%2;border:1px solid %3;border-radius:3px;padding:4px 8px;text-align:left;}"
            "QPushButton:hover{border-color:%4;}")
            .arg(label_color.name(QColor::HexRgb),
                 color.name(QColor::HexArgb),
                 border.name(QColor::HexRgb),
                 highlight.name(QColor::HexRgb)));
    };
    auto add_color_button_row = [&](int row, const QString &label, std::function<QColor()> current_color,
                                    std::function<void(const QColor &)> apply_color) {
        auto *name = new QLabel(label, colors_page);
        name->setStyleSheet(QStringLiteral("color:%1;").arg(text.name(QColor::HexRgb)));
        auto *button = new QPushButton(colors_page);
        button->setMinimumWidth(116);
        button->setCursor(Qt::PointingHandCursor);
        apply_color_button(button, current_color());
        grid->addWidget(name, row, 0);
        grid->addWidget(button, row, 1);
        connect(button, &QPushButton::clicked, dialog, [dialog, button, label, apply_color_button, apply_color, current_color]() mutable {
            auto *picker = new QColorDialog(current_color(), dialog);
            picker->setAttribute(Qt::WA_DeleteOnClose);
            picker->setWindowTitle(label);
            picker->setOption(QColorDialog::ShowAlphaChannel, true);
            connect(picker, &QColorDialog::currentColorChanged, dialog, [button, apply_color_button, apply_color](const QColor &color) mutable {
                if (!color.isValid())
                    return;
                apply_color(color);
                apply_color_button(button, color);
            });
            picker->open();
        });
    };
    auto add_timeline_color_row = [&](int row, const QString &label, TitlePreferences::TimelineColorRole role) {
        add_color_button_row(row, label, [role]() { return TitlePreferences::timeline_color(role); }, [role, update_appearance](const QColor &color) {
            TitlePreferences::set_timeline_color(role, color);
            update_appearance();
        });
    };

    int color_row = 0;
    add_timeline_color_row(color_row++, QStringLiteral("Text layers"), TitlePreferences::TimelineColorRole::TextLayer);
    add_timeline_color_row(color_row++, QStringLiteral("Clock layers"), TitlePreferences::TimelineColorRole::ClockLayer);
    add_timeline_color_row(color_row++, QStringLiteral("Ticker layers"), TitlePreferences::TimelineColorRole::TickerLayer);
    add_timeline_color_row(color_row++, QStringLiteral("Object layers"), TitlePreferences::TimelineColorRole::ObjectLayer);
    add_timeline_color_row(color_row++, QStringLiteral("Image layers"), TitlePreferences::TimelineColorRole::ImageLayer);
    add_timeline_color_row(color_row++, QStringLiteral("Current time"), TitlePreferences::TimelineColorRole::Current);
    add_timeline_color_row(color_row++, QStringLiteral("Pause marker"), TitlePreferences::TimelineColorRole::Pause);
    add_timeline_color_row(color_row++, QStringLiteral("Loop start/end"), TitlePreferences::TimelineColorRole::Loop);
    add_color_button_row(color_row++, QStringLiteral("Scene mask objects"), []() { return TitlePreferences::scene_mask_color(); }, [update_appearance](const QColor &color) {
        TitlePreferences::set_scene_mask_color(color);
        update_appearance();
    });
    grid->setColumnStretch(2, 1);
    colors_layout->addStretch(1);

    auto *advanced_page = new QWidget(pages);
    auto *advanced_layout = new QVBoxLayout(advanced_page);
    advanced_layout->setContentsMargins(0, 0, 0, 0);
    advanced_layout->setSpacing(10);

    auto *advanced_title = new QLabel(QStringLiteral("Advanced"), advanced_page);
    advanced_title->setFont(title_font);
    advanced_layout->addWidget(advanced_title);

    auto *use_gpu = new QCheckBox(obsgs_tr("OBSTitles.UseGPU"), advanced_page);
    use_gpu->setChecked(TitlePreferences::use_gpu());
    use_gpu->setToolTip(obsgs_tr("OBSTitles.UseGPUTooltip"));
    use_gpu->setStyleSheet(QStringLiteral("QCheckBox{color:%1;}QCheckBox:disabled{color:%2;}")
                               .arg(text.name(QColor::HexRgb),
                                    disabled_text.name(QColor::HexRgb)));
    advanced_layout->addWidget(use_gpu);
    advanced_layout->addStretch(1);

    pages->addWidget(colors_page);
    pages->addWidget(advanced_page);
    content_layout->addWidget(tabs);
    content_layout->addWidget(pages, 1);
    layout->addWidget(content, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    layout->addWidget(buttons);

    tabs->setCurrentRow(0);
    connect(tabs, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    connect(use_gpu, &QCheckBox::toggled, this, &TitleEditor::set_gpu_pipeline_enabled);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}


bool TitleEditor::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == title_lbl_ && event->type() == QEvent::MouseButtonDblClick) {
        auto *mouse_event = static_cast<QMouseEvent *>(event);
        if (mouse_event->button() == Qt::LeftButton) {
            begin_title_name_edit();
            event->accept();
            return true;
        }
    }

    if (watched == title_name_edit_) {
        if (event->type() == QEvent::KeyPress) {
            auto *key_event = static_cast<QKeyEvent *>(event);
            if (key_event->key() == Qt::Key_Escape) {
                commit_title_name_edit(false);
                key_event->accept();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            commit_title_name_edit(false);
            return false;
        }
    }

    if (event->type() == QEvent::KeyPress && isActiveWindow()) {
        auto *key_event = static_cast<QKeyEvent *>(event);
        auto *widget = qobject_cast<QWidget *>(watched);
        const bool in_editor = widget && (widget == this || isAncestorOf(widget));
        if (in_editor && key_event->key() == Qt::Key_Space && !key_event->isAutoRepeat()) {
            if (!editor_focus_accepts_text(focusWidget())) {
                play_pause();
                key_event->accept();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void TitleEditor::keyPressEvent(QKeyEvent *ev)
{
    if (ev->matches(QKeySequence::Undo)) {
        if (undo_index_ > 0) restore_undo_snapshot(undo_index_ - 1);
        ev->accept();
        return;
    }
    if (ev->matches(QKeySequence::Redo)) {
        if (undo_index_ + 1 < (int)undo_stack_.size()) restore_undo_snapshot(undo_index_ + 1);
        ev->accept();
        return;
    }
    QWidget *fw = focusWidget();
    bool editing_value = editor_focus_accepts_text(fw);
    if (!editing_value && ev->key() == Qt::Key_Escape) {
        close();
        ev->accept();
        return;
    }
    if (!editing_value && timeline_ && ev->matches(QKeySequence::Copy) &&
        timeline_->has_selected_keyframes()) {
        timeline_->copy_keyframe_selection();
        ev->accept();
        return;
    }
    if (!editing_value && timeline_ && ev->matches(QKeySequence::Cut) &&
        timeline_->has_selected_keyframes()) {
        timeline_->cut_keyframe_selection();
        ev->accept();
        return;
    }
    if (!editing_value && timeline_ && ev->matches(QKeySequence::Paste) &&
        timeline_->has_keyframe_clipboard()) {
        timeline_->paste_keyframes_at_playhead();
        ev->accept();
        return;
    }
    if (!editing_value && timeline_ &&
        (ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace) &&
        timeline_->has_selected_keyframes()) {
        timeline_->delete_keyframe_selection();
        ev->accept();
        return;
    }
    if (!editing_value && ev->matches(QKeySequence::Copy) && !sel_layer_id_.empty()) {
        copy_selected_layer();
        ev->accept();
        return;
    }
    if (!editing_value && ev->matches(QKeySequence::Cut) && !sel_layer_id_.empty()) {
        cut_selected_layer();
        ev->accept();
        return;
    }
    if (!editing_value && ev->matches(QKeySequence::Paste) && !layer_clipboard_.empty()) {
        paste_layer_from_clipboard();
        ev->accept();
        return;
    }
    if (!editing_value && ev->key() == Qt::Key_Delete && !sel_layer_id_.empty()) {
        delete_selected_layer();
        ev->accept();
        return;
    }
    if (ev->key() == Qt::Key_Space && !ev->isAutoRepeat()) {
        if (!editor_focus_accepts_text(focusWidget())) {
            play_pause();
            ev->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(ev);
}


void TitleEditor::closeEvent(QCloseEvent *ev)
{
    if (confirm_save_before_close()) {
        save_editor_layout();
        editor_layout_save_suppressed_ = true;
        ev->accept();
    } else {
        ev->ignore();
    }
}

void TitleEditor::reject()
{
    close();
}

/* ── Signal handlers ─────────────────────────────────────────────── */
void TitleEditor::update_layer_panels(std::shared_ptr<Layer> layer, double playhead)
{
    if (layer_props_dock_) {
        layer_props_dock_->setWindowTitle(layer
            ? QStringLiteral("Properties: %1").arg(QString::fromStdString(layer->name))
            : QStringLiteral("Properties: No selection"));
    }
    if (props_) props_->set_layer(layer, playhead);
    if (effects_panel_) effects_panel_->set_layer(layer, playhead);
}

void TitleEditor::on_layer_selected(const std::string &lid)
{
    sel_layer_id_ = lid;
    layers_->set_selected_layer(lid);
    canvas_->set_selected_layer(lid);
    timeline_->set_selected_layer(lid);

    if (!title_ || lid.empty()) {
        update_layer_panels(nullptr, playhead_);
        return;
    }
    auto layer = title_->find_layer(lid);
    if (layer) update_layer_panels(layer, playhead_);
}

void TitleEditor::on_playhead_changed(double t)
{
    t = title_ ? std::clamp(snap_to_obs_frame(t), 0.0, title_->duration) : snap_to_obs_frame(t);
    playhead_ = t;
    canvas_->set_playhead(t);
    timeline_->set_playhead(t);

    if (!sel_layer_id_.empty() && title_) {
        auto l = title_->find_layer(sel_layer_id_);
        if (l) update_layer_panels(l, t);
    }

    if (time_lbl_)
        time_lbl_->setText(obsgs_tr("OBSTitles.TimeFpsFormat").arg(format_timecode(t)).arg(obs_frame_rate(), 0, 'f', 2));
}

void TitleEditor::on_title_modified(bool push_undo)
{
    if (title_) set_dirty(true);
    canvas_->refresh_preview();
    if (title_props_) title_props_->set_title(title_);
    if (timeline_) timeline_->set_title(title_);
    if (push_undo)
        push_undo_snapshot();
    save_live_edit();
}

/* ══════════════════════════════════════════════════════════════════
 *  CanvasPreview
 * ══════════════════════════════════════════════════════════════════ */
CanvasPreview::CanvasPreview(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(400, 225);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    inline_text_editor_ = new QTextEdit(this);
    inline_text_editor_->hide();
    inline_text_editor_->setAcceptRichText(true);
    inline_text_editor_->setFrameShape(QFrame::NoFrame);
    inline_text_editor_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inline_text_editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inline_text_editor_->setLineWrapMode(QTextEdit::FixedPixelWidth);
    inline_text_editor_->setCursorWidth(2);
    inline_text_editor_->setContentsMargins(0, 0, 0, 0);
    inline_text_editor_->viewport()->setContentsMargins(0, 0, 0, 0);
    inline_text_editor_->setAttribute(Qt::WA_TranslucentBackground, true);
    inline_text_editor_->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
    inline_text_editor_->setStyleSheet(
        "QTextEdit{background:transparent;border:0px;padding:0px;"
        "color:rgba(255,255,255,0);selection-background-color:rgba(255,255,255,0);"
        "selection-color:rgba(255,255,255,0);}");
    inline_text_editor_->installEventFilter(this);
    connect(inline_text_editor_->document(), &QTextDocument::contentsChanged, this, [this]() {
        if (updating_inline_text_editor_ || refreshing_inline_text_) return;
        refresh_inline_text_edit(true, true);
    });
    if (auto *layout = inline_text_editor_->document()->documentLayout()) {
        connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged, this, [this](const QSizeF &) {
            if (updating_inline_text_editor_ || refreshing_inline_text_) return;
            refresh_inline_text_edit(true, true);
        });
        connect(layout, &QAbstractTextDocumentLayout::update, this, [this](const QRectF &) {
            if (committing_inline_text_ || updating_inline_text_editor_ || inline_text_layer_id_.empty()) return;
            if (inline_text_editor_)
                inline_text_editor_->viewport()->update();
        });
    }
    auto emit_cursor_changed = [this]() {
        if (committing_inline_text_ || updating_inline_text_editor_ || inline_text_layer_id_.empty()) return;
        const std::string layer_id = inline_text_layer_id_;
        sync_inline_text_layer(false);
        if (inline_text_editor_) {
            inline_text_editor_->viewport()->update();
            update(inline_text_editor_->geometry().adjusted(-4, -4, 4, 4));
        }
        emit text_edit_cursor_changed(layer_id);
    };
    connect(inline_text_editor_, &QTextEdit::cursorPositionChanged, this, emit_cursor_changed);
    connect(inline_text_editor_, &QTextEdit::selectionChanged, this, emit_cursor_changed);
}



void CanvasPreview::begin_text_edit_for_layer(const std::string &layer_id)
{
    if (!title_ || layer_id.empty()) return;
    auto layer = title_->find_layer(layer_id);
    if (!layer || !is_canvas_text_layer(*layer) || layer->type == LayerType::Clock) return;
    selected_layer_ids_ = {layer_id};
    sel_layer_id_ = layer_id;
    begin_text_edit(layer);
}

void CanvasPreview::apply_active_text_char_format(const std::string &layer_id, const RichTextCharFormat &format, uint32_t mask)
{
    if (!inline_text_editor_ || inline_text_layer_id_.empty() || inline_text_layer_id_ != layer_id)
        return;
    auto layer = title_ ? title_->find_layer(layer_id) : nullptr;
    const double visual_scale = layer ? inline_text_visual_scale(*layer) : 1.0;
    QTextCharFormat qfmt;
    if (mask & (RichTextCharFontFamily | RichTextCharFontStyle | RichTextCharFontSize |
                RichTextCharBold | RichTextCharItalic | RichTextCharUnderline | RichTextCharStrikethrough)) {
        QFont font = inline_text_editor_->currentFont();
        if (mask & RichTextCharFontFamily) font.setFamily(QString::fromStdString(format.font_family));
        if (mask & RichTextCharFontStyle) font.setStyleName(QString::fromStdString(format.font_style));
        if (mask & RichTextCharFontSize) font.setPixelSize(std::max(1, (int)std::round(format.font_size * visual_scale)));
        if (mask & RichTextCharBold) font.setBold(format.bold);
        if (mask & RichTextCharItalic) font.setItalic(format.italic);
        if (mask & RichTextCharUnderline) font.setUnderline(format.underline);
        if (mask & RichTextCharStrikethrough) font.setStrikeOut(format.strikethrough);
        qfmt.setFont(font);
    }
    if (mask & RichTextCharUnderline) qfmt.setFontUnderline(format.underline);
    if (mask & RichTextCharStrikethrough) qfmt.setFontStrikeOut(format.strikethrough);
    if (mask & RichTextCharKerning) qfmt.setFontKerning(format.kerning_mode != 2 && format.kerning);
    if (mask & (RichTextCharKerning | RichTextCharTracking)) {
        qfmt.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        qfmt.setFontLetterSpacing(format.tracking +
                                  (format.kerning_mode == 2 ? format.manual_kerning : 0.0f));
    }
    if (mask & RichTextCharScaleX)
        qfmt.setFontStretch(std::clamp((int)std::round(format.scale_x * 100.0f), 1, 4000));
    if (mask & RichTextCharTextStyle) {
        qfmt.setFontCapitalization(format.text_style == 1 ? QFont::AllUppercase
                                  : (format.text_style == 2 ? QFont::SmallCaps : QFont::MixedCase));
        if (format.text_style == 3)
            qfmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
        else if (format.text_style == 4)
            qfmt.setVerticalAlignment(QTextCharFormat::AlignSubScript);
        else
            qfmt.setVerticalAlignment(QTextCharFormat::AlignNormal);
    }
    store_editor_rich_text_format_properties_masked(qfmt, format, mask);
    if (mask & RichTextCharFillColor) {
        QColor transparent_color = rich_text_color_from_argb(format.fill.color);
        transparent_color.setAlpha(0);
        qfmt.setForeground(transparent_color);
    }

    QTextCursor cursor = inline_text_editor_->textCursor();
    cursor.mergeCharFormat(qfmt);
    inline_text_editor_->mergeCurrentCharFormat(qfmt);
    inline_text_editor_->setTextCursor(cursor);
    refresh_inline_text_edit(true, true);
}

void CanvasPreview::set_title(std::shared_ptr<Title> t, bool preserve_view)
{
    commit_text_edit(true);
    title_ = t;
    dirty_ = true;
    if (!preserve_view) {
        pan_offset_ = QPointF(0, 0);
        if (title_) fit_canvas(fit_zoom_up_to_100_);
        else update();
    } else {
        update();
    }
    position_text_editor();
}

CanvasPreview::ViewState CanvasPreview::view_state() const
{
    return ViewState{zoom_percent_, fit_zoom_active_, fit_zoom_up_to_100_, pan_offset_};
}

void CanvasPreview::restore_view_state(const ViewState &state)
{
    zoom_percent_ = std::clamp(state.zoom_percent, 5, 1600);
    fit_zoom_active_ = state.fit_zoom_active;
    fit_zoom_up_to_100_ = state.fit_zoom_up_to_100;
    pan_offset_ = state.pan_offset;
    dirty_ = true;
    emit zoom_percent_changed(zoom_percent_);
    position_text_editor();
    update();
}

void CanvasPreview::set_playhead(double t)
{
    playhead_ = t; dirty_ = true; position_text_editor(); update();
}

void CanvasPreview::set_selected_layer(const std::string &lid)
{
    sel_layer_id_ = lid;
    selected_layer_ids_.clear();
    if (!lid.empty()) selected_layer_ids_.push_back(lid);
    position_text_editor();
    update();
}

void CanvasPreview::set_selected_layers(const std::vector<std::string> &ids)
{
    selected_layer_ids_ = ids;
    sel_layer_id_ = ids.empty() ? std::string() : ids.back();
    position_text_editor();
    update();
}

void CanvasPreview::set_safe_guides_visible(bool visible)
{
    safe_guides_visible_ = visible;
    QSettings settings(QStringLiteral("OBSGraphicsStudioPro"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kEditorLayoutSettingsGroup));
    settings.setValue(QString::fromUtf8(kEditorSafeGuidesVisibleKey), visible);
    settings.endGroup();
    settings.sync();
    update();
}

void CanvasPreview::refresh_preview()
{
    dirty_ = true;
    position_text_editor();
    if (!inline_text_layer_id_.empty())
        render_to_pixmap();
    update();
    if (inline_text_editor_ && inline_text_editor_->isVisible()) {
        inline_text_editor_->viewport()->update();
        repaint(inline_text_editor_->geometry().adjusted(-4, -4, 4, 4));
    }
}


void CanvasPreview::set_snap_enabled(bool enabled)
{
    snap_settings_.enabled = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_snap_to_guides(bool enabled)
{
    snap_settings_.guides = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_snap_to_grid(bool enabled)
{
    snap_settings_.grid = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_snap_to_object_edges(bool enabled)
{
    snap_settings_.object_edges = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_snap_to_object_centers(bool enabled)
{
    snap_settings_.object_centers = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_snap_to_canvas_bounds(bool enabled)
{
    snap_settings_.canvas_bounds = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_snap_to_spacing(bool enabled)
{
    snap_settings_.spacing = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_zoom_percent(int percent)
{
    int clamped = std::clamp(percent, 5, 1600);
    if (zoom_percent_ == clamped && !fit_zoom_active_) return;
    zoom_percent_ = clamped;
    fit_zoom_active_ = false;
    emit zoom_percent_changed(zoom_percent_);
    position_text_editor();
    update();
}

int CanvasPreview::zoom_percent() const
{
    return zoom_percent_;
}

void CanvasPreview::set_checkerboard_pattern(int pattern)
{
    checkerboard_pattern_ = std::clamp(pattern, 0, 5);
    QSettings settings(QStringLiteral("OBSGraphicsStudioPro"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kEditorLayoutSettingsGroup));
    settings.setValue(QString::fromUtf8(kEditorCanvasTransparencyKey), checkerboard_pattern_);
    settings.endGroup();
    settings.sync();
    update();
}

void CanvasPreview::set_selection_tool_active()
{
    commit_text_edit(true);
    active_tool_ = CanvasTool::Selection;
    drawing_shape_ = false;
    color_picker_tooltip_visible_ = false;
    unsetCursor();
    update();
}

void CanvasPreview::set_shape_tool_active(ShapeType shape_type)
{
    active_tool_ = CanvasTool::Shape;
    active_shape_type_ = shape_type;
    drawing_shape_ = false;
    color_picker_tooltip_visible_ = false;
    setCursor(Qt::CrossCursor);
    update();
}


void CanvasPreview::set_text_tool_active(LayerType type)
{
    if (type != LayerType::Text && type != LayerType::Clock && type != LayerType::Ticker)
        type = LayerType::Text;
    active_tool_ = CanvasTool::Text;
    active_text_layer_type_ = type;
    drawing_shape_ = false;
    color_picker_tooltip_visible_ = false;
    setCursor(Qt::IBeamCursor);
    update();
}

void CanvasPreview::set_color_picker_tool_active()
{
    commit_text_edit(true);
    active_tool_ = CanvasTool::ColorPicker;
    drawing_shape_ = false;
    color_picker_tooltip_visible_ = false;
    setCursor(Qt::CrossCursor);
    update();
}

void CanvasPreview::fit_canvas(bool up_to_100)
{
    fit_zoom_active_ = true;
    fit_zoom_up_to_100_ = up_to_100;
    pan_offset_ = QPointF(0, 0);
    double scale = fit_scale();
    if (up_to_100) scale = std::min(scale, 1.0);
    int next_percent = std::clamp((int)std::round(scale * 100.0), 5, 1600);
    if (zoom_percent_ != next_percent) {
        zoom_percent_ = next_percent;
        emit zoom_percent_changed(zoom_percent_);
    }
    update();
}

std::shared_ptr<Layer> CanvasPreview::selected_layer() const
{
    return title_ ? title_->find_layer(sel_layer_id_) : nullptr;
}

std::vector<std::shared_ptr<Layer>> CanvasPreview::selected_layers() const
{
    std::vector<std::shared_ptr<Layer>> layers;
    if (!title_) return layers;
    if (selected_layer_ids_.empty()) {
        if (auto layer = selected_layer()) layers.push_back(layer);
        return layers;
    }
    std::set<std::string> seen;
    for (const auto &id : selected_layer_ids_) {
        if (!seen.insert(id).second) continue;
        auto layer = title_->find_layer(id);
        if (layer) layers.push_back(layer);
    }
    return layers;
}

QRectF CanvasPreview::layer_local_rect(const Layer &layer) const
{
    double lt = playhead_ - layer.in_time;
    double w = eval_box_width(layer, lt);
    double h = eval_box_height(layer, lt);
    double ox = eval_origin_x(layer, lt);
    double oy = eval_origin_y(layer, lt);
    return QRectF(-ox * w, -oy * h, w, h);
}

double CanvasPreview::fit_scale() const
{
    if (!title_ || title_->width <= 0 || title_->height <= 0) return 1.0;
    return std::min((double)width() / title_->width,
                    (double)height() / title_->height);
}

double CanvasPreview::view_scale() const
{
    return std::max(0.05, (double)zoom_percent_ / 100.0);
}

QPointF CanvasPreview::centered_view_origin() const
{
    if (!title_) return QPointF(0, 0);
    double scale = view_scale();
    return QPointF((width() - title_->width * scale) / 2.0,
                   (height() - title_->height * scale) / 2.0);
}

QPointF CanvasPreview::view_origin() const
{
    return centered_view_origin() + pan_offset_;
}

QPointF CanvasPreview::view_to_canvas(const QPointF &view_pt) const
{
    double scale = view_scale();
    QPointF origin = view_origin();
    return QPointF((view_pt.x() - origin.x()) / scale,
                   (view_pt.y() - origin.y()) / scale);
}

QPointF CanvasPreview::canvas_to_view(const QPointF &canvas_pt) const
{
    double scale = view_scale();
    QPointF origin = view_origin();
    return QPointF(origin.x() + canvas_pt.x() * scale,
                   origin.y() + canvas_pt.y() * scale);
}

static const Layer *editor_find_layer_by_id(const std::shared_ptr<Title> &title, const std::string &id)
{
    if (!title || id.empty()) return nullptr;
    for (const auto &candidate : title->layers) {
        if (candidate && candidate->id == id) return candidate.get();
    }
    return nullptr;
}

static QTransform editor_layer_world_transform(const std::shared_ptr<Title> &title,
                                               const Layer &layer, double playhead, int depth = 0)
{
    QTransform xf;
    if (depth > 64) return xf;
    if (!layer.parent_id.empty()) {
        if (const Layer *parent = editor_find_layer_by_id(title, layer.parent_id))
            xf = editor_layer_world_transform(title, *parent, playhead, depth + 1);
    }
    const double lt = std::max(0.0, playhead - layer.in_time);
    xf.translate(layer.pos_x.evaluate(lt), layer.pos_y.evaluate(lt));
    xf.rotate(layer.rotation.evaluate(lt));
    xf.scale(layer.scale_x.evaluate(lt), layer.scale_y.evaluate(lt));
    return xf;
}

QPointF CanvasPreview::canvas_to_layer(const Layer &layer, const QPointF &canvas_pt) const
{
    return editor_layer_world_transform(title_, layer, playhead_).inverted().map(canvas_pt);
}

QPointF CanvasPreview::layer_to_canvas(const Layer &layer, const QPointF &layer_pt) const
{
    return editor_layer_world_transform(title_, layer, playhead_).map(layer_pt);
}

QRectF CanvasPreview::layer_canvas_bounds(const Layer &layer) const
{
    QRectF r = layer_local_rect(layer);
    const QPointF corners[] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};

    double min_x = std::numeric_limits<double>::infinity();
    double min_y = std::numeric_limits<double>::infinity();
    double max_x = -std::numeric_limits<double>::infinity();
    double max_y = -std::numeric_limits<double>::infinity();

    for (const QPointF &corner : corners) {
        QPointF canvas = layer_to_canvas(layer, corner);
        min_x = std::min(min_x, canvas.x());
        min_y = std::min(min_y, canvas.y());
        max_x = std::max(max_x, canvas.x());
        max_y = std::max(max_y, canvas.y());
    }

    if (!std::isfinite(min_x) || !std::isfinite(min_y) ||
        !std::isfinite(max_x) || !std::isfinite(max_y))
        return QRectF();

    return QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y)).normalized();
}

QRectF CanvasPreview::selected_canvas_bounds() const
{
    QRectF bounds;
    bool have_bounds = false;
    for (auto &layer : selected_layers()) {
        if (!layer || !layer->visible) continue;
        QRectF layer_bounds = layer_canvas_bounds(*layer);
        if (!layer_bounds.isValid()) continue;
        if (!have_bounds) {
            bounds = layer_bounds;
            have_bounds = true;
        } else {
            bounds = bounds.united(layer_bounds);
        }
    }
    return bounds.normalized();
}

bool CanvasPreview::layer_supports_gradient_handles(const Layer &layer) const
{
    if (layer.locked || !layer.visible || layer.fill_type != 1)
        return false;
    if (playhead_ < layer.in_time || playhead_ > layer.out_time)
        return false;
    return layer.type == LayerType::SolidRect || layer.type == LayerType::Shape ||
           is_canvas_text_layer(layer);
}

CanvasPreview::GradientHandleGeometry CanvasPreview::gradient_handle_geometry(const Layer &layer) const
{
    GradientHandleGeometry g;
    if (!layer_supports_gradient_handles(layer))
        return g;

    const QRectF box = layer_local_rect(layer);
    if (!box.isValid() || box.width() <= 0.0 || box.height() <= 0.0)
        return g;

    g.valid = true;
    g.radial = layer.gradient_type == 1;
    g.local_rect = box;
    g.center = QPointF(box.left() + std::clamp((double)layer.gradient_center_x, 0.0, 1.0) * box.width(),
                       box.top() + std::clamp((double)layer.gradient_center_y, 0.0, 1.0) * box.height());

    const double scale = std::clamp((double)layer.gradient_scale, 0.01, 10.0);
    const double angle = degrees_to_radians(layer.gradient_angle);
    const QPointF axis(std::cos(angle), std::sin(angle));
    if (g.radial) {
        const double radius = std::max(box.width(), box.height()) * 0.5 * scale;
        g.radius = g.center + axis * std::max(1.0, radius);
        g.focal = QPointF(box.left() + std::clamp((double)layer.gradient_focal_x, 0.0, 1.0) * box.width(),
                          box.top() + std::clamp((double)layer.gradient_focal_y, 0.0, 1.0) * box.height());
        g.start = g.center;
        g.end = g.radius;
    } else {
        const double length = std::hypot(box.width(), box.height()) * 0.5 * scale;
        const QPointF delta = axis * std::max(1.0, length);
        g.start = g.center - delta;
        g.end = g.center + delta;
        g.radius = g.end;
        g.focal = g.center;
    }
    return g;
}

CanvasPreview::DragMode CanvasPreview::hit_test_gradient_handles(const Layer &layer, const QPointF &view_pt) const
{
    GradientHandleGeometry g = gradient_handle_geometry(layer);
    if (!g.valid)
        return DragMode::None;

    auto near_local = [&](const QPointF &local, double radius = CANVAS_CONTROL_HIT_RADIUS_PX) {
        const QPointF view = canvas_to_view(layer_to_canvas(layer, local));
        return QLineF(view_pt, view).length() <= radius;
    };

    if (g.radial) {
        if (near_local(g.focal, CANVAS_CONTROL_HIT_RADIUS_PX * 1.2)) return DragMode::GradientFocal;
        if (near_local(g.radius, CANVAS_CONTROL_HIT_RADIUS_PX * 1.2)) return DragMode::GradientRadius;
        if (near_local(g.center, CANVAS_CONTROL_HIT_RADIUS_PX * 1.35)) return DragMode::GradientCenter;
    } else {
        if (near_local(g.start, CANVAS_CONTROL_HIT_RADIUS_PX * 1.2)) return DragMode::GradientStart;
        if (near_local(g.end, CANVAS_CONTROL_HIT_RADIUS_PX * 1.2)) return DragMode::GradientEnd;
        if (near_local(g.center, CANVAS_CONTROL_HIT_RADIUS_PX * 1.35)) return DragMode::GradientCenter;
    }
    return DragMode::None;
}

void CanvasPreview::draw_gradient_handles(QPainter &p, const Layer &layer)
{
    GradientHandleGeometry g = gradient_handle_geometry(layer);
    if (!g.valid)
        return;

    auto to_view = [&](const QPointF &local) {
        return canvas_to_view(layer_to_canvas(layer, local));
    };
    auto draw_handle = [&](const QPointF &view_pt, const QColor &fill, double radius = CANVAS_GRADIENT_HANDLE_RADIUS_PX) {
        p.setPen(QPen(QColor(10, 10, 10, 210), 3.0));
        p.setBrush(fill);
        p.drawEllipse(view_pt, radius, radius);
        p.setPen(QPen(QColor(255, 255, 255, 235), 1.0));
        p.drawEllipse(view_pt, radius, radius);
    };

    const QPointF center = to_view(g.center);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen guide_pen(QColor(255, 255, 255, 215), 1.4, Qt::SolidLine);
    guide_pen.setCosmetic(true);
    p.setPen(guide_pen);
    p.setBrush(Qt::NoBrush);

    if (g.radial) {
        const QPointF radius = to_view(g.radius);
        const QPointF focal = to_view(g.focal);
        p.drawLine(center, radius);
        QPen focal_pen(QColor(255, 215, 80, 210), 1.2, Qt::DashLine);
        focal_pen.setCosmetic(true);
        p.setPen(focal_pen);
        p.drawLine(center, focal);
        draw_handle(center, QColor(0, 145, 255));
        draw_handle(radius, QColor(255, 255, 255));
        draw_handle(focal, QColor(255, 210, 60), CANVAS_GRADIENT_HANDLE_RADIUS_PX - 0.5);
    } else {
        const QPointF start = to_view(g.start);
        const QPointF end = to_view(g.end);
        p.drawLine(start, end);
        draw_handle(start, QColor(255, 255, 255));
        draw_handle(end, QColor(255, 255, 255));
        draw_handle(center, QColor(0, 145, 255));
    }
    p.restore();
}

void CanvasPreview::begin_gradient_drag(const Layer &layer)
{
    GradientHandleGeometry g = gradient_handle_geometry(layer);
    gradient_drag_ = GradientDragState{};
    if (!g.valid)
        return;

    gradient_drag_.active = true;
    gradient_drag_.radial = g.radial;
    gradient_drag_.local_rect = g.local_rect;
    gradient_drag_.center = g.center;
    gradient_drag_.start = g.start;
    gradient_drag_.end = g.end;
    gradient_drag_.radius = g.radius;
    gradient_drag_.focal = g.focal;
    gradient_drag_.center_x = layer.gradient_center_x;
    gradient_drag_.center_y = layer.gradient_center_y;
    gradient_drag_.focal_x = layer.gradient_focal_x;
    gradient_drag_.focal_y = layer.gradient_focal_y;
    gradient_drag_.scale = layer.gradient_scale;
    gradient_drag_.angle = layer.gradient_angle;
}

bool CanvasPreview::apply_gradient_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (!gradient_drag_.active)
        return false;
    if (drag_mode_ != DragMode::GradientStart && drag_mode_ != DragMode::GradientEnd &&
        drag_mode_ != DragMode::GradientCenter && drag_mode_ != DragMode::GradientRadius &&
        drag_mode_ != DragMode::GradientFocal)
        return false;

    auto layer = selected_layer();
    if (!layer || layer->locked)
        return false;

    const QRectF box = gradient_drag_.local_rect;
    if (!box.isValid() || box.width() <= 0.0 || box.height() <= 0.0)
        return false;

    const QPointF local = canvas_to_layer(*layer, view_to_canvas(view_pt));
    auto normalized = [&](const QPointF &pt) {
        return QPointF(std::clamp((pt.x() - box.left()) / box.width(), 0.0, 1.0),
                       std::clamp((pt.y() - box.top()) / box.height(), 0.0, 1.0));
    };
    auto assign_center = [&](const QPointF &pt) {
        const QPointF n = normalized(pt);
        layer->gradient_center_x = (float)n.x();
        layer->gradient_center_y = (float)n.y();
    };
    auto assign_focal = [&](const QPointF &pt) {
        const QPointF n = normalized(pt);
        layer->gradient_focal_x = (float)n.x();
        layer->gradient_focal_y = (float)n.y();
    };
    auto assign_axis = [&](const QPointF &a, const QPointF &b) {
        const QPointF c((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5);
        assign_center(c);
        const QPointF delta = b - a;
        const double distance = std::max(1.0, QLineF(a, b).length());
        double angle = radians_to_degrees(std::atan2(delta.y(), delta.x()));
        if (modifiers.testFlag(Qt::ShiftModifier))
            angle = std::round(angle / CANVAS_ROTATION_SNAP_DEGREES) * CANVAS_ROTATION_SNAP_DEGREES;
        layer->gradient_angle = (float)normalize_degrees(angle);
        const double base = std::max(1.0, std::hypot(box.width(), box.height()));
        layer->gradient_scale = (float)std::clamp(distance / base, 0.01, 10.0);
    };

    clear_snap_feedback();
    if (drag_mode_ == DragMode::GradientCenter) {
        const QPointF delta = local - gradient_drag_.center;
        assign_center(gradient_drag_.center + delta);
        if (gradient_drag_.radial)
            assign_focal(gradient_drag_.focal + delta);
    } else if (drag_mode_ == DragMode::GradientFocal) {
        assign_focal(local);
    } else if (drag_mode_ == DragMode::GradientRadius) {
        assign_center(gradient_drag_.center);
        const QPointF delta = local - gradient_drag_.center;
        const double radius = std::max(1.0, QLineF(gradient_drag_.center, local).length());
        const double base = std::max(1.0, std::max(box.width(), box.height()) * 0.5);
        double angle = radians_to_degrees(std::atan2(delta.y(), delta.x()));
        if (modifiers.testFlag(Qt::ShiftModifier))
            angle = std::round(angle / CANVAS_ROTATION_SNAP_DEGREES) * CANVAS_ROTATION_SNAP_DEGREES;
        layer->gradient_angle = (float)normalize_degrees(angle);
        layer->gradient_scale = (float)std::clamp(radius / base, 0.01, 10.0);
    } else if (drag_mode_ == DragMode::GradientStart) {
        assign_axis(local, gradient_drag_.end);
    } else if (drag_mode_ == DragMode::GradientEnd) {
        assign_axis(gradient_drag_.start, local);
    }

    dirty_ = true;
    drag_changed_ = true;
    drag_current_view_ = view_pt;
    update();
    return true;
}

bool CanvasPreview::layer_supports_corner_radius_handles(const Layer &layer) const
{
    if (layer.locked || !layer.visible || layer.type != LayerType::Shape)
        return false;
    if (playhead_ < layer.in_time || playhead_ > layer.out_time)
        return false;
    return layer.shape_type == ShapeType::Rectangle || layer.shape_type == ShapeType::RoundedRectangle;
}

QPointF CanvasPreview::corner_radius_handle_local_pos(const Layer &layer, const QRectF &box, DragMode mode) const
{
    const double max_radius = std::max(0.0, std::min(box.width(), box.height()) / 2.0);
    auto radius = [max_radius](float value) {
        return std::clamp((double)value, 0.0, max_radius);
    };
    switch (mode) {
    case DragMode::CornerRadiusTL: {
        const double r = radius(layer.corner_radius_tl);
        return QPointF(box.left() + r, box.top() + r);
    }
    case DragMode::CornerRadiusTR: {
        const double r = radius(layer.corner_radius_tr);
        return QPointF(box.right() - r, box.top() + r);
    }
    case DragMode::CornerRadiusBR: {
        const double r = radius(layer.corner_radius_br);
        return QPointF(box.right() - r, box.bottom() - r);
    }
    case DragMode::CornerRadiusBL: {
        const double r = radius(layer.corner_radius_bl);
        return QPointF(box.left() + r, box.bottom() - r);
    }
    default:
        return QPointF();
    }
}

CanvasPreview::DragMode CanvasPreview::hit_test_corner_radius_handles(const Layer &layer, const QPointF &view_pt) const
{
    if (!layer_supports_corner_radius_handles(layer))
        return DragMode::None;
    const QRectF box = layer_local_rect(layer);
    if (!box.isValid() || box.width() <= 0.0 || box.height() <= 0.0)
        return DragMode::None;
    for (DragMode mode : {DragMode::CornerRadiusTL, DragMode::CornerRadiusTR,
                          DragMode::CornerRadiusBR, DragMode::CornerRadiusBL}) {
        const QPointF view = canvas_to_view(layer_to_canvas(layer, corner_radius_handle_local_pos(layer, box, mode)));
        if (QLineF(view_pt, view).length() <= CANVAS_CONTROL_HIT_RADIUS_PX * 1.25)
            return mode;
    }
    return DragMode::None;
}

void CanvasPreview::draw_corner_radius_handles(QPainter &p, const Layer &layer)
{
    if (!layer_supports_corner_radius_handles(layer))
        return;
    const QRectF box = layer_local_rect(layer);
    if (!box.isValid() || box.width() <= 0.0 || box.height() <= 0.0)
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen guide_pen(QColor(255, 255, 255, 180), 1.0, Qt::DashLine);
    guide_pen.setCosmetic(true);
    p.setPen(guide_pen);
    p.setBrush(Qt::NoBrush);

    auto to_view = [&](const QPointF &local) {
        return canvas_to_view(layer_to_canvas(layer, local));
    };
    auto draw_handle = [&](DragMode mode) {
        const QPointF handle = corner_radius_handle_local_pos(layer, box, mode);
        QPointF corner;
        switch (mode) {
        case DragMode::CornerRadiusTL: corner = box.topLeft(); break;
        case DragMode::CornerRadiusTR: corner = box.topRight(); break;
        case DragMode::CornerRadiusBR: corner = box.bottomRight(); break;
        case DragMode::CornerRadiusBL: corner = box.bottomLeft(); break;
        default: return;
        }
        p.drawLine(to_view(corner), to_view(handle));
        const QPointF view = to_view(handle);
        p.setPen(QPen(QColor(25, 25, 25, 230), 3.0));
        p.setBrush(QColor(255, 255, 255, 245));
        p.drawEllipse(view, 5.0, 5.0);
        p.setPen(QPen(QColor(0, 120, 255, 255), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(view, 5.0, 5.0);
        p.setPen(guide_pen);
    };
    draw_handle(DragMode::CornerRadiusTL);
    draw_handle(DragMode::CornerRadiusTR);
    draw_handle(DragMode::CornerRadiusBR);
    draw_handle(DragMode::CornerRadiusBL);
    p.restore();
}

void CanvasPreview::begin_corner_radius_drag(const Layer &layer)
{
    corner_radius_drag_ = CornerRadiusDragState{};
    if (!layer_supports_corner_radius_handles(layer))
        return;
    const QRectF box = layer_local_rect(layer);
    if (!box.isValid() || box.width() <= 0.0 || box.height() <= 0.0)
        return;
    corner_radius_drag_.active = true;
    corner_radius_drag_.local_rect = box;
    corner_radius_drag_.top_left = layer.corner_radius_tl;
    corner_radius_drag_.top_right = layer.corner_radius_tr;
    corner_radius_drag_.bottom_right = layer.corner_radius_br;
    corner_radius_drag_.bottom_left = layer.corner_radius_bl;
    corner_radius_drag_.locked = layer.corner_radius_locked;
}

bool CanvasPreview::apply_corner_radius_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (!corner_radius_drag_.active)
        return false;
    if (drag_mode_ != DragMode::CornerRadiusTL && drag_mode_ != DragMode::CornerRadiusTR &&
        drag_mode_ != DragMode::CornerRadiusBR && drag_mode_ != DragMode::CornerRadiusBL)
        return false;
    auto layer = selected_layer();
    if (!layer || layer->locked)
        return false;

    const QRectF box = corner_radius_drag_.local_rect;
    const QPointF local = canvas_to_layer(*layer, view_to_canvas(view_pt));
    double radius = 0.0;
    switch (drag_mode_) {
    case DragMode::CornerRadiusTL:
        radius = std::min(local.x() - box.left(), local.y() - box.top());
        break;
    case DragMode::CornerRadiusTR:
        radius = std::min(box.right() - local.x(), local.y() - box.top());
        break;
    case DragMode::CornerRadiusBR:
        radius = std::min(box.right() - local.x(), box.bottom() - local.y());
        break;
    case DragMode::CornerRadiusBL:
        radius = std::min(local.x() - box.left(), box.bottom() - local.y());
        break;
    default:
        return false;
    }
    const double max_radius = std::max(0.0, std::min(box.width(), box.height()) / 2.0);
    radius = std::clamp(radius, 0.0, max_radius);
    if (modifiers.testFlag(Qt::ShiftModifier))
        radius = std::round(radius);

    if (corner_radius_drag_.locked || layer->corner_radius_locked) {
        set_layer_all_corner_radii(*layer, (float)radius);
        layer->corner_radius_locked = true;
    } else {
        switch (drag_mode_) {
        case DragMode::CornerRadiusTL: layer->corner_radius_tl = (float)radius; break;
        case DragMode::CornerRadiusTR: layer->corner_radius_tr = (float)radius; break;
        case DragMode::CornerRadiusBR: layer->corner_radius_br = (float)radius; break;
        case DragMode::CornerRadiusBL: layer->corner_radius_bl = (float)radius; break;
        default: break;
        }
        set_layer_corner_radii(*layer, layer->corner_radius_tl, layer->corner_radius_tr,
                               layer->corner_radius_br, layer->corner_radius_bl);
        layer->corner_radius_locked = false;
    }

    clear_snap_feedback();
    dirty_ = true;
    drag_changed_ = true;
    drag_current_view_ = view_pt;
    update();
    return true;
}

CanvasPreview::DragMode CanvasPreview::hit_test_selected(const QPointF &view_pt) const
{
    auto layers = selected_layers();
    if (layers.empty()) return DragMode::None;

    if (layers.size() > 1) {
        QRectF r = selected_canvas_bounds();
        if (!r.isValid() || r.isEmpty()) return DragMode::None;
        auto canvas_handle_to_view = [&](const QPointF &p) { return canvas_to_view(p); };
        auto near_pt = [&](const QPointF &p) {
            QPointF view = canvas_handle_to_view(p);
            return std::abs(view_pt.x() - view.x()) <= CANVAS_CONTROL_HIT_RADIUS_PX &&
                   std::abs(view_pt.y() - view.y()) <= CANVAS_CONTROL_HIT_RADIUS_PX;
        };
        QRectF view_bounds(canvas_to_view(r.topLeft()), canvas_to_view(r.bottomRight()));
        view_bounds = view_bounds.normalized();
        auto near_rotation_corner = [&](const QPointF &p) {
            return QLineF(view_pt, canvas_handle_to_view(p)).length() <= CANVAS_ROTATE_HIT_RADIUS_PX &&
                   !view_bounds.adjusted(-CANVAS_CONTROL_SIZE_PX, -CANVAS_CONTROL_SIZE_PX,
                                         CANVAS_CONTROL_SIZE_PX, CANVAS_CONTROL_SIZE_PX).contains(view_pt);
        };
        if (near_pt(r.topLeft())) return DragMode::ResizeNW;
        if (near_pt(QPointF(r.center().x(), r.top()))) return DragMode::ResizeN;
        if (near_pt(r.topRight())) return DragMode::ResizeNE;
        if (near_pt(QPointF(r.right(), r.center().y()))) return DragMode::ResizeE;
        if (near_pt(r.bottomRight())) return DragMode::ResizeSE;
        if (near_pt(QPointF(r.center().x(), r.bottom()))) return DragMode::ResizeS;
        if (near_pt(r.bottomLeft())) return DragMode::ResizeSW;
        if (near_pt(QPointF(r.left(), r.center().y()))) return DragMode::ResizeW;
        if (near_rotation_corner(r.topLeft()) || near_rotation_corner(r.topRight()) ||
            near_rotation_corner(r.bottomRight()) || near_rotation_corner(r.bottomLeft()))
            return DragMode::Rotate;
        QPointF canvas = view_to_canvas(view_pt);
        for (const auto &layer : layers) {
            if (!layer || layer->locked) continue;
            QPointF local = canvas_to_layer(*layer, canvas);
            if (layer_local_rect(*layer).contains(local))
                return DragMode::Move;
        }
        return DragMode::None;
    }

    auto layer = layers.front();
    if (!layer || layer->locked) return DragMode::None;

    DragMode gradient_hit = hit_test_gradient_handles(*layer, view_pt);
    if (gradient_hit != DragMode::None)
        return gradient_hit;
    DragMode corner_radius_hit = hit_test_corner_radius_handles(*layer, view_pt);
    if (corner_radius_hit != DragMode::None)
        return corner_radius_hit;

    QRectF r = layer_local_rect(*layer);
    auto layer_point_to_view = [&](const QPointF &p) {
        return canvas_to_view(layer_to_canvas(*layer, p));
    };
    auto near_pt = [&](const QPointF &p) {
        QPointF view = layer_point_to_view(p);
        return std::abs(view_pt.x() - view.x()) <= CANVAS_CONTROL_HIT_RADIUS_PX &&
               std::abs(view_pt.y() - view.y()) <= CANVAS_CONTROL_HIT_RADIUS_PX;
    };
    QPainterPath layer_path;
    layer_path.moveTo(layer_point_to_view(r.topLeft()));
    layer_path.lineTo(layer_point_to_view(r.topRight()));
    layer_path.lineTo(layer_point_to_view(r.bottomRight()));
    layer_path.lineTo(layer_point_to_view(r.bottomLeft()));
    layer_path.closeSubpath();
    auto near_rotation_corner = [&](const QPointF &p) {
        return QLineF(view_pt, layer_point_to_view(p)).length() <= CANVAS_ROTATE_HIT_RADIUS_PX &&
               !layer_path.contains(view_pt);
    };

    if (near_pt(r.topLeft())) return DragMode::ResizeNW;
    if (near_pt(QPointF(r.center().x(), r.top()))) return DragMode::ResizeN;
    if (near_pt(r.topRight())) return DragMode::ResizeNE;
    if (near_pt(QPointF(r.right(), r.center().y()))) return DragMode::ResizeE;
    if (near_pt(r.bottomRight())) return DragMode::ResizeSE;
    if (near_pt(QPointF(r.center().x(), r.bottom()))) return DragMode::ResizeS;
    if (near_pt(r.bottomLeft())) return DragMode::ResizeSW;
    if (near_pt(QPointF(r.left(), r.center().y()))) return DragMode::ResizeW;
    if (near_rotation_corner(r.topLeft()) || near_rotation_corner(r.topRight()) ||
        near_rotation_corner(r.bottomRight()) || near_rotation_corner(r.bottomLeft()))
        return DragMode::Rotate;
    if (QLineF(view_pt, layer_point_to_view(QPointF(0, 0))).length() <= CANVAS_CONTROL_HIT_RADIUS_PX * 1.25)
        return DragMode::Origin;

    if (layer_path.contains(view_pt)) return DragMode::Move;
    return DragMode::None;
}
void CanvasPreview::begin_marquee(const QPointF &view_pt, Qt::KeyboardModifiers)
{
    drag_mode_ = DragMode::Marquee;
    marquee_active_ = false;
    drag_start_view_ = view_pt;
    drag_current_view_ = view_pt;
    marquee_base_selection_ = selected_layer_ids_;
    drag_changed_ = false;
    clear_snap_feedback();
}

void CanvasPreview::update_marquee(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (!title_) return;
    drag_current_view_ = view_pt;
    if ((drag_current_view_ - drag_start_view_).manhattanLength() < QApplication::startDragDistance()) {
        update();
        return;
    }

    marquee_active_ = true;
    QRectF view_rect(drag_start_view_, drag_current_view_);
    view_rect = view_rect.normalized();
    QRectF canvas_rect(view_to_canvas(view_rect.topLeft()), view_to_canvas(view_rect.bottomRight()));
    canvas_rect = canvas_rect.normalized();
    auto intersects_or_touches = [](const QRectF &a, const QRectF &b) {
        if (!a.isValid() || !b.isValid()) return false;
        return a.left() <= b.right() && a.right() >= b.left() &&
               a.top() <= b.bottom() && a.bottom() >= b.top();
    };

    std::set<std::string> selected;
    if (modifiers & (Qt::ShiftModifier | Qt::ControlModifier))
        selected.insert(marquee_base_selection_.begin(), marquee_base_selection_.end());

    std::vector<std::string> hits;
    for (const auto &layer : title_->layers) {
        if (!layer || !layer->visible || layer->locked) continue;
        if (playhead_ < layer->in_time || playhead_ > layer->out_time) continue;
        QRectF bounds = layer_canvas_bounds(*layer);
        if (intersects_or_touches(canvas_rect, bounds))
            hits.push_back(layer->id);
    }

    if (modifiers & Qt::ControlModifier) {
        for (const auto &id : hits) {
            auto it = selected.find(id);
            if (it == selected.end()) selected.insert(id);
            else selected.erase(it);
        }
    } else {
        selected.insert(hits.begin(), hits.end());
    }

    selected_layer_ids_.clear();
    for (const auto &layer : title_->layers) {
        if (layer && selected.find(layer->id) != selected.end())
            selected_layer_ids_.push_back(layer->id);
    }
    sel_layer_id_ = selected_layer_ids_.empty() ? std::string() : selected_layer_ids_.back();
    emit layers_selected(selected_layer_ids_);
    update();
}

bool CanvasPreview::duplicate_selected_layers_for_drag()
{
    if (!title_ || selected_layer_ids_.empty()) return false;

    std::set<std::string> selected_ids(selected_layer_ids_.begin(), selected_layer_ids_.end());
    std::map<std::string, std::string> cloned_ids_by_original;
    std::map<std::string, std::shared_ptr<Layer>> clones_by_original;
    std::vector<std::shared_ptr<Layer>> clones;

    for (const auto &layer : title_->layers) {
        if (!layer || layer->locked || selected_ids.find(layer->id) == selected_ids.end())
            continue;

        auto clone = std::make_shared<Layer>(*layer);
        clone->id = TitleDataStore::make_uuid();
        clone->name = clone->name.empty() ? editor_text_std("OBSTitles.LayerCopy") :
                                            clone->name + editor_text_std("OBSTitles.CopySuffix");
        cloned_ids_by_original[layer->id] = clone->id;
        clones_by_original[layer->id] = clone;
        clones.push_back(clone);
    }

    if (clones.empty()) return false;

    for (auto &clone : clones) {
        auto parent_clone = cloned_ids_by_original.find(clone->parent_id);
        if (parent_clone != cloned_ids_by_original.end()) {
            clone->parent_id = parent_clone->second;
        } else if (!clone->parent_id.empty() && !title_->find_layer(clone->parent_id)) {
            clone->parent_id.clear();
        }
        auto mask_clone = cloned_ids_by_original.find(clone->mask_source_id);
        if (mask_clone != cloned_ids_by_original.end()) {
            clone->mask_source_id = mask_clone->second;
        } else if (!clone->mask_source_id.empty() && !title_->find_layer(clone->mask_source_id)) {
            clone->mask_source_id.clear();
            clone->mask_mode = MaskMode::None;
        }
    }

    std::vector<std::shared_ptr<Layer>> next_layers;
    next_layers.reserve(title_->layers.size() + clones.size());
    for (const auto &layer : title_->layers) {
        next_layers.push_back(layer);
        if (!layer) continue;
        auto clone = clones_by_original.find(layer->id);
        if (clone != clones_by_original.end())
            next_layers.push_back(clone->second);
    }
    title_->layers = std::move(next_layers);

    selected_layer_ids_.clear();
    drag_layer_states_.clear();
    for (const auto &clone : clones) {
        if (!clone) continue;
        selected_layer_ids_.push_back(clone->id);
        double lt = std::clamp(playhead_ - clone->in_time, 0.0,
                               std::max(0.0, clone->out_time - clone->in_time));
        drag_layer_states_.push_back({clone->id,
                                      clone->pos_x.evaluate(lt),
                                      clone->pos_y.evaluate(lt),
                                      (float)eval_box_width(*clone, lt),
                                      (float)eval_box_height(*clone, lt),
                                      clone->scale_x.evaluate(lt),
                                      clone->scale_y.evaluate(lt),
                                      clone->rotation.evaluate(lt)});
    }
    sel_layer_id_ = selected_layer_ids_.empty() ? std::string() : selected_layer_ids_.back();
    drag_start_selection_bounds_ = selected_canvas_bounds();

    emit layer_structure_changed();
    emit layers_selected(selected_layer_ids_);
    dirty_ = true;
    update();
    return true;
}

bool CanvasPreview::nudge_selected_layers(double dx, double dy)
{
    auto layers = selected_layers();
    if (!title_ || layers.empty()) return false;

    bool changed = false;
    for (const auto &layer : layers) {
        if (!layer || layer->locked) continue;
        double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                               std::max(0.0, layer->out_time - layer->in_time));
        set_animated_value(layer->pos_x, lt, layer->pos_x.evaluate(lt) + dx);
        set_animated_value(layer->pos_y, lt, layer->pos_y.evaluate(lt) + dy);
        changed = true;
    }

    if (!changed) return false;

    dirty_ = true;
    update();
    emit layer_geometry_changed();
    return true;
}


void CanvasPreview::clear_snap_feedback()
{
    if (snap_feedback_.empty()) return;
    snap_feedback_.clear();
    update();
}

void CanvasPreview::add_snap_feedback(bool x_axis, double value, const QString &label)
{
    snap_feedback_.push_back({x_axis, value, label});
}

void CanvasPreview::collect_snap_targets(bool x_axis, std::vector<double> &targets, std::vector<QString> &labels) const
{
    if (!title_) return;
    auto add = [&](double value, const QString &label) {
        if (!std::isfinite(value)) return;
        targets.push_back(value);
        labels.push_back(label);
    };

    if (snap_settings_.canvas_bounds) {
        const double size = x_axis ? title_->width : title_->height;
        add(0.0, QStringLiteral("Canvas"));
        add(size * 0.5, QStringLiteral("Canvas center"));
        add(size, QStringLiteral("Canvas"));
    }

    if (snap_settings_.guides) {
        const double size = x_axis ? title_->width : title_->height;
        add(size * OBS_ACTION_SAFE_PERCENT, QStringLiteral("Action safe"));
        add(size * (1.0 - OBS_ACTION_SAFE_PERCENT), QStringLiteral("Action safe"));
        add(size * OBS_GRAPHICS_SAFE_PERCENT, QStringLiteral("Title safe"));
        add(size * (1.0 - OBS_GRAPHICS_SAFE_PERCENT), QStringLiteral("Title safe"));
    }

    if (snap_settings_.grid) {
        const double size = x_axis ? title_->width : title_->height;
        constexpr double grid = 10.0;
        for (double v = 0.0; v <= size + 0.01; v += grid)
            add(v, QStringLiteral("Grid"));
    }

    if (!snap_settings_.object_edges && !snap_settings_.object_centers) return;

    std::set<std::string> selected_ids(selected_layer_ids_.begin(), selected_layer_ids_.end());
    if (!sel_layer_id_.empty())
        selected_ids.insert(sel_layer_id_);

    for (const auto &layer : title_->layers) {
        if (!layer || !layer->visible) continue;
        if (playhead_ < layer->in_time || playhead_ > layer->out_time) continue;
        if (selected_ids.find(layer->id) != selected_ids.end())
            continue;
        QRectF bounds = layer_canvas_bounds(*layer);
        if (!bounds.isValid() || bounds.isEmpty()) continue;
        if (snap_settings_.object_edges) {
            add(x_axis ? bounds.left() : bounds.top(), QStringLiteral("Object edge"));
            add(x_axis ? bounds.right() : bounds.bottom(), QStringLiteral("Object edge"));
        }
        if (snap_settings_.object_centers)
            add(x_axis ? bounds.center().x() : bounds.center().y(), QStringLiteral("Object center"));
    }
}

void CanvasPreview::collect_spacing_targets(bool x_axis, std::vector<double> &targets, std::vector<QString> &labels) const
{
    if (!title_ || !snap_settings_.spacing) return;

    struct Span { double start; double end; };
    std::vector<Span> spans;
    std::set<std::string> selected_ids(selected_layer_ids_.begin(), selected_layer_ids_.end());
    if (!sel_layer_id_.empty())
        selected_ids.insert(sel_layer_id_);

    for (const auto &layer : title_->layers) {
        if (!layer || !layer->visible) continue;
        if (playhead_ < layer->in_time || playhead_ > layer->out_time) continue;
        if (selected_ids.find(layer->id) != selected_ids.end())
            continue;
        QRectF bounds = layer_canvas_bounds(*layer);
        if (!bounds.isValid() || bounds.isEmpty()) continue;
        spans.push_back({x_axis ? bounds.left() : bounds.top(), x_axis ? bounds.right() : bounds.bottom()});
    }
    if (spans.empty()) return;
    std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) { return a.start < b.start; });

    std::vector<double> gaps;
    for (size_t i = 1; i < spans.size(); ++i) {
        double gap = spans[i].start - spans[i - 1].end;
        if (gap >= 0.0) gaps.push_back(gap);
    }
    if (gaps.empty()) gaps.push_back(0.0);

    auto add = [&](double value) {
        if (!std::isfinite(value)) return;
        targets.push_back(value);
        labels.push_back(QStringLiteral("Spacing"));
    };
    for (const Span &span : spans) {
        for (double gap : gaps) {
            add(span.start - gap);
            add(span.end + gap);
        }
    }
}

QPointF CanvasPreview::snap_delta_for_bounds(const QRectF &start_bounds, const QPointF &delta, bool snap_x, bool snap_y,
                                             bool allow_snap)
{
    if (!allow_snap || !title_ || !snap_settings_.enabled || !start_bounds.isValid()) {
        clear_snap_feedback();
        return delta;
    }

    snap_feedback_.clear();
    QPointF snapped_delta = delta;
    const double tolerance = 6.0 / std::max(0.1, view_scale());

    auto snap_axis = [&](bool x_axis) {
        std::vector<double> targets;
        std::vector<QString> labels;
        collect_snap_targets(x_axis, targets, labels);
        collect_spacing_targets(x_axis, targets, labels);
        if (targets.empty()) return;

        const double offset = x_axis ? snapped_delta.x() : snapped_delta.y();
        const double start_min = x_axis ? start_bounds.left() : start_bounds.top();
        const double start_center = x_axis ? start_bounds.center().x() : start_bounds.center().y();
        const double start_max = x_axis ? start_bounds.right() : start_bounds.bottom();
        const double points[] = {start_min + offset, start_center + offset, start_max + offset};
        double best_adjust = 0.0;
        double best_distance = tolerance + 1.0;
        double best_target = 0.0;
        QString best_label;
        for (size_t i = 0; i < targets.size(); ++i) {
            for (double point : points) {
                double adjust = targets[i] - point;
                double distance = std::abs(adjust);
                if (distance < best_distance) {
                    best_distance = distance;
                    best_adjust = adjust;
                    best_target = targets[i];
                    best_label = labels[i];
                }
            }
        }
        if (best_distance <= tolerance) {
            if (x_axis) snapped_delta.setX(snapped_delta.x() + best_adjust);
            else snapped_delta.setY(snapped_delta.y() + best_adjust);
            add_snap_feedback(x_axis, best_target, best_label);
        }
    };

    if (snap_x) snap_axis(true);
    if (snap_y) snap_axis(false);
    return snapped_delta;
}

QPointF CanvasPreview::snap_canvas_point(const QPointF &canvas_pt, bool snap_x, bool snap_y, bool allow_snap)
{
    QRectF point_bounds(canvas_pt, QSizeF(0.0, 0.0));
    QPointF delta = snap_delta_for_bounds(point_bounds, QPointF(0.0, 0.0), snap_x, snap_y, allow_snap);
    return canvas_pt + delta;
}

static QRectF editor_modifier_rect(const QPointF &anchor, const QPointF &current,
                                   Qt::KeyboardModifiers modifiers, double aspect,
                                   double fixed_width = 0.0, double fixed_height = 0.0);

void CanvasPreview::apply_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (drag_mode_ == DragMode::Marquee) {
        update_marquee(view_pt, modifiers);
        return;
    }

    if (apply_gradient_drag(view_pt, modifiers))
        return;
    if (apply_corner_radius_drag(view_pt, modifiers))
        return;

    if (drag_mode_ == DragMode::Move && alt_duplicate_pending_ && !alt_duplicate_done_) {
        alt_duplicate_done_ = true;
        duplicate_selected_layers_for_drag();
    }

    auto layers = selected_layers();
    if (layers.empty() || drag_mode_ == DragMode::None) return;

    drag_current_view_ = view_pt;
    QPointF canvas = view_to_canvas(view_pt);
    QPointF delta = canvas - drag_start_canvas_;
    const bool allow_snap = !modifiers.testFlag(Qt::ControlModifier);
    const double snap_tolerance = 6.0 / std::max(0.1, view_scale());
    auto snap_coordinate = [&](bool x_axis, double value, double *snapped) {
        if (!allow_snap || !snapped)
            return false;

        std::vector<double> targets;
        std::vector<QString> labels;
        collect_snap_targets(x_axis, targets, labels);
        collect_spacing_targets(x_axis, targets, labels);
        if (targets.empty())
            return false;

        double best_distance = snap_tolerance + 1.0;
        double best_target = value;
        QString best_label;
        for (size_t i = 0; i < targets.size(); ++i) {
            const double distance = std::abs(targets[i] - value);
            if (distance < best_distance) {
                best_distance = distance;
                best_target = targets[i];
                best_label = labels[i];
            }
        }
        if (best_distance > snap_tolerance)
            return false;

        *snapped = best_target;
        add_snap_feedback(x_axis, best_target, best_label);
        return true;
    };

    if (drag_mode_ == DragMode::Rotate) {
        clear_snap_feedback();
        QPointF pivot_view = canvas_to_view(drag_rotation_pivot_canvas_);
        double current_angle = radians_to_degrees(std::atan2(view_pt.y() - pivot_view.y(),
                                                             view_pt.x() - pivot_view.x()));
        double rotation_delta = normalize_degrees(current_angle - drag_start_rotation_angle_);
        if (modifiers & Qt::ShiftModifier)
            rotation_delta = std::round(rotation_delta / CANVAS_ROTATION_SNAP_DEGREES) * CANVAS_ROTATION_SNAP_DEGREES;
        drag_current_rotation_delta_ = rotation_delta;

        for (const auto &state : drag_layer_states_) {
            auto layer = title_->find_layer(state.id);
            if (!layer || layer->locked) continue;
            double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                                   std::max(0.0, layer->out_time - layer->in_time));
            if (layers.size() > 1) {
                QPointF next_pos = rotate_point_around(QPointF(state.x, state.y), drag_rotation_pivot_canvas_, rotation_delta);
                set_animated_value(layer->pos_x, lt, next_pos.x());
                set_animated_value(layer->pos_y, lt, next_pos.y());
            }
            set_animated_value(layer->rotation, lt, state.rotation + rotation_delta);
        }
        dirty_ = true;
        drag_changed_ = true;
        update();
        return;
    }

    if (layers.size() > 1) {
        if (drag_mode_ == DragMode::Move) {
            if (modifiers & Qt::ShiftModifier) {
                if (std::abs(delta.x()) >= std::abs(delta.y())) delta.setY(0.0);
                else delta.setX(0.0);
            }
            delta = snap_delta_for_bounds(drag_start_selection_bounds_, delta, true, true, allow_snap);
            for (const auto &state : drag_layer_states_) {
                auto layer = title_->find_layer(state.id);
                if (!layer || layer->locked) continue;
                double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                                       std::max(0.0, layer->out_time - layer->in_time));
                set_animated_value(layer->pos_x, lt, state.x + delta.x());
                set_animated_value(layer->pos_y, lt, state.y + delta.y());
            }
        } else {
            QRectF start = drag_start_selection_bounds_;
            if (!start.isValid() || start.width() <= 0.0 || start.height() <= 0.0) return;
            if (allow_snap)
                snap_feedback_.clear();
            QRectF next = start;
            bool resize_left = drag_mode_ == DragMode::ResizeNW || drag_mode_ == DragMode::ResizeSW || drag_mode_ == DragMode::ResizeW;
            bool resize_right = drag_mode_ == DragMode::ResizeNE || drag_mode_ == DragMode::ResizeSE || drag_mode_ == DragMode::ResizeE;
            bool resize_top = drag_mode_ == DragMode::ResizeNW || drag_mode_ == DragMode::ResizeNE || drag_mode_ == DragMode::ResizeN;
            bool resize_bottom = drag_mode_ == DragMode::ResizeSW || drag_mode_ == DragMode::ResizeSE || drag_mode_ == DragMode::ResizeS;
            if (resize_left) next.setLeft(std::min(canvas.x(), start.right()));
            if (resize_right) next.setRight(std::max(canvas.x(), start.left()));
            if (resize_top) next.setTop(std::min(canvas.y(), start.bottom()));
            if (resize_bottom) next.setBottom(std::max(canvas.y(), start.top()));
            double sx = next.width() / start.width();
            double sy = next.height() / start.height();
            if (modifiers & Qt::ShiftModifier) {
                double uniform = std::abs(sx) >= std::abs(sy) ? sx : sy;
                sx = sy = uniform;
            }
            next = QRectF(start.topLeft(), QSizeF(start.width() * sx, start.height() * sy)).normalized();
            if (resize_left)
                next.moveRight(start.right());
            else
                next.moveLeft(start.left());
            if (resize_top)
                next.moveBottom(start.bottom());
            else
                next.moveTop(start.top());

            double snapped_value = 0.0;
            if (resize_left && snap_coordinate(true, next.left(), &snapped_value))
                next.setLeft(std::min(snapped_value, next.right()));
            else if (resize_right && snap_coordinate(true, next.right(), &snapped_value))
                next.setRight(std::max(snapped_value, next.left()));
            if (resize_top && snap_coordinate(false, next.top(), &snapped_value))
                next.setTop(std::min(snapped_value, next.bottom()));
            else if (resize_bottom && snap_coordinate(false, next.bottom(), &snapped_value))
                next.setBottom(std::max(snapped_value, next.top()));

            sx = next.width() / start.width();
            sy = next.height() / start.height();
            for (const auto &state : drag_layer_states_) {
                auto layer = title_->find_layer(state.id);
                if (!layer || layer->locked) continue;
                double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                                       std::max(0.0, layer->out_time - layer->in_time));
                double rx = (state.x - start.left()) / start.width();
                double ry = (state.y - start.top()) / start.height();
                set_animated_value(layer->pos_x, lt, next.left() + rx * next.width());
                set_animated_value(layer->pos_y, lt, next.top() + ry * next.height());
                const bool scale_text_object = drag_text_object_scaling_ && is_canvas_text_layer(*layer);
                if (scale_text_object) {
                    set_animated_value(layer->scale_x, lt, state.scale_x * sx);
                    set_animated_value(layer->scale_y, lt, state.scale_y * sy);
                } else {
                    layer->rect_width = std::max(0.0f, (float)(state.w * sx));
                    layer->rect_height = std::max(0.0f, (float)(state.h * sy));
                    set_animated_value(layer->box_width, lt, layer->rect_width);
                    set_animated_value(layer->box_height, lt, layer->rect_height);
                }
            }
        }
        dirty_ = true;
        drag_changed_ = true;
        update();
        return;
    }

    auto layer = layers.front();
    if (!layer) return;
    double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                           std::max(0.0, layer->out_time - layer->in_time));

    if (drag_mode_ == DragMode::Move) {
        if (modifiers & Qt::ShiftModifier) {
            if (std::abs(delta.x()) >= std::abs(delta.y()))
                delta.setY(0.0);
            else
                delta.setX(0.0);
        }
        delta = snap_delta_for_bounds(drag_start_selection_bounds_, delta, true, true, allow_snap);
        set_animated_value(layer->pos_x, lt, drag_start_x_ + delta.x());
        set_animated_value(layer->pos_y, lt, drag_start_y_ + delta.y());
    } else if (drag_mode_ == DragMode::Origin) {
        clear_snap_feedback();
        double w = std::max(1.0f, drag_start_w_);
        double h = std::max(1.0f, drag_start_h_);
        layer->origin_x = (float)std::clamp(drag_start_origin_x_ + delta.x() / w, 0.0, 1.0);
        layer->origin_y = (float)std::clamp(drag_start_origin_y_ + delta.y() / h, 0.0, 1.0);
        set_animated_value(layer->origin_x_prop, lt, layer->origin_x);
        set_animated_value(layer->origin_y_prop, lt, layer->origin_y);
        set_animated_value(layer->pos_x, lt, drag_start_x_ + delta.x());
        set_animated_value(layer->pos_y, lt, drag_start_y_ + delta.y());
    } else {
        bool resize_left = drag_mode_ == DragMode::ResizeNW || drag_mode_ == DragMode::ResizeSW || drag_mode_ == DragMode::ResizeW;
        bool resize_right = drag_mode_ == DragMode::ResizeNE || drag_mode_ == DragMode::ResizeSE || drag_mode_ == DragMode::ResizeE;
        bool resize_top = drag_mode_ == DragMode::ResizeNW || drag_mode_ == DragMode::ResizeNE || drag_mode_ == DragMode::ResizeN;
        bool resize_bottom = drag_mode_ == DragMode::ResizeSW || drag_mode_ == DragMode::ResizeSE || drag_mode_ == DragMode::ResizeS;
        if (allow_snap)
            snap_feedback_.clear();
        const bool scale_text_object = drag_text_object_scaling_ && is_canvas_text_layer(*layer);
        const LayerDragState *start_state = drag_layer_states_.empty() ? nullptr : &drag_layer_states_.front();
        auto non_zero_scale = [](double value) {
            if (std::abs(value) >= 0.0001) return value;
            return value < 0.0 ? -0.0001 : 0.0001;
        };
        auto start_canvas_to_layer = [&](const QPointF &canvas_pt) {
            QTransform start_xf;
            start_xf.translate(start_state->x, start_state->y);
            start_xf.rotate(start_state->rotation);
            start_xf.scale(non_zero_scale(start_state->scale_x),
                           non_zero_scale(start_state->scale_y));
            return start_xf.inverted().map(canvas_pt);
        };
        const bool alt_resize = modifiers.testFlag(Qt::AltModifier);
        const bool lock_aspect_resize = layer->type == LayerType::Image &&
                                        layer->lock_aspect_ratio &&
                                        drag_start_h_ > 0.0f;
        Qt::KeyboardModifiers resize_modifiers = modifiers;
        if (lock_aspect_resize)
            resize_modifiers |= Qt::ShiftModifier;
        QPointF local = start_state ? start_canvas_to_layer(canvas)
                                    : canvas_to_layer(*layer, canvas);
        double left = -drag_start_origin_x_ * drag_start_w_;
        double right = (1.0 - drag_start_origin_x_) * drag_start_w_;
        double top = -drag_start_origin_y_ * drag_start_h_;
        double bottom = (1.0 - drag_start_origin_y_) * drag_start_h_;
        const QRectF start_rect(QPointF(left, top), QPointF(right, bottom));
        const bool corner_resize = (resize_left || resize_right) && (resize_top || resize_bottom);
        const double aspect = drag_start_h_ > 0.0f ? (double)drag_start_w_ / drag_start_h_ : 1.0;

        QPointF anchor = start_rect.center();
        double fixed_w = 0.0;
        double fixed_h = 0.0;
        if (!resize_modifiers.testFlag(Qt::AltModifier)) {
            if (corner_resize) {
                anchor = QPointF(resize_left ? start_rect.right() : start_rect.left(),
                                 resize_top ? start_rect.bottom() : start_rect.top());
            } else if (resize_left || resize_right) {
                anchor = QPointF(resize_left ? start_rect.right() : start_rect.left(),
                                 start_rect.center().y());
                fixed_h = start_rect.height();
            } else {
                anchor = QPointF(start_rect.center().x(),
                                 resize_top ? start_rect.bottom() : start_rect.top());
                fixed_w = start_rect.width();
            }
        }

        const QRectF resized_rect = editor_modifier_rect(anchor, local, resize_modifiers, aspect, fixed_w, fixed_h);
        QTransform start_xf;
        if (start_state) {
            start_xf.translate(start_state->x, start_state->y);
            start_xf.rotate(start_state->rotation);
            start_xf.scale(non_zero_scale(start_state->scale_x),
                           non_zero_scale(start_state->scale_y));
        }
        QRectF final_rect = resized_rect;
        if (allow_snap && start_state) {
            QPointF handle_local = local;
            if (resize_left || resize_right)
                handle_local.setX(resize_left ? resized_rect.left() : resized_rect.right());
            if (resize_top || resize_bottom)
                handle_local.setY(resize_top ? resized_rect.top() : resized_rect.bottom());

            QPointF handle_canvas = start_xf.map(handle_local);
            bool snapped_any = false;
            double snapped_value = 0.0;
            QPolygonF proposed_poly;
            proposed_poly << start_xf.map(resized_rect.topLeft())
                          << start_xf.map(resized_rect.topRight())
                          << start_xf.map(resized_rect.bottomRight())
                          << start_xf.map(resized_rect.bottomLeft());
            const QRectF proposed_bounds = proposed_poly.boundingRect();
            if (resize_left && snap_coordinate(true, proposed_bounds.left(), &snapped_value)) {
                handle_canvas.setX(handle_canvas.x() + snapped_value - proposed_bounds.left());
                snapped_any = true;
            } else if (resize_right && snap_coordinate(true, proposed_bounds.right(), &snapped_value)) {
                handle_canvas.setX(handle_canvas.x() + snapped_value - proposed_bounds.right());
                snapped_any = true;
            }
            if (resize_top && snap_coordinate(false, proposed_bounds.top(), &snapped_value)) {
                handle_canvas.setY(handle_canvas.y() + snapped_value - proposed_bounds.top());
                snapped_any = true;
            } else if (resize_bottom && snap_coordinate(false, proposed_bounds.bottom(), &snapped_value)) {
                handle_canvas.setY(handle_canvas.y() + snapped_value - proposed_bounds.bottom());
                snapped_any = true;
            }
            if (snapped_any) {
                const QPointF snapped_local = start_xf.inverted().map(handle_canvas);
                final_rect = editor_modifier_rect(anchor, snapped_local, resize_modifiers, aspect, fixed_w, fixed_h);
            }
        }
        double new_w = std::max(0.0, final_rect.width());
        double new_h = std::max(0.0, final_rect.height());
        const QPointF fixed_center_canvas = start_xf.map(start_rect.center());
        if (scale_text_object) {
            double sx = drag_start_w_ > 0.0f ? new_w / drag_start_w_ : 1.0;
            double sy = drag_start_h_ > 0.0f ? new_h / drag_start_h_ : 1.0;
            double start_scale_x = start_state ? start_state->scale_x : layer->scale_x.evaluate(lt);
            double start_scale_y = start_state ? start_state->scale_y : layer->scale_y.evaluate(lt);
            set_animated_value(layer->scale_x, lt, start_scale_x * sx);
            set_animated_value(layer->scale_y, lt, start_scale_y * sy);
            if (alt_resize && start_state) {
                QTransform next_xf;
                next_xf.translate(start_state->x, start_state->y);
                next_xf.rotate(start_state->rotation);
                next_xf.scale(non_zero_scale(start_scale_x * sx),
                              non_zero_scale(start_scale_y * sy));
                const QPointF current_center_canvas = next_xf.map(start_rect.center());
                const QPointF shift = fixed_center_canvas - current_center_canvas;
                set_animated_value(layer->pos_x, lt, start_state->x + shift.x());
                set_animated_value(layer->pos_y, lt, start_state->y + shift.y());
            }
        } else {
            layer->rect_width = (float)new_w;
            layer->rect_height = (float)new_h;
            set_animated_value(layer->box_width, lt, new_w);
            set_animated_value(layer->box_height, lt, new_h);
            if (start_state) {
                const QRectF actual_rect(-drag_start_origin_x_ * new_w,
                                         -drag_start_origin_y_ * new_h,
                                         new_w, new_h);
                const QPointF local_shift = final_rect.center() - actual_rect.center();
                const QPointF canvas_shift = start_xf.map(local_shift) - start_xf.map(QPointF(0.0, 0.0));
                set_animated_value(layer->pos_x, lt, start_state->x + canvas_shift.x());
                set_animated_value(layer->pos_y, lt, start_state->y + canvas_shift.y());
            }
            if (alt_resize && start_state) {
                const QRectF actual_rect(-drag_start_origin_x_ * new_w,
                                         -drag_start_origin_y_ * new_h,
                                         new_w, new_h);
                QTransform next_xf;
                next_xf.translate(start_state->x, start_state->y);
                next_xf.rotate(start_state->rotation);
                next_xf.scale(non_zero_scale(start_state->scale_x),
                              non_zero_scale(start_state->scale_y));
                const QPointF current_center_canvas = next_xf.map(actual_rect.center());
                const QPointF shift = fixed_center_canvas - current_center_canvas;
                set_animated_value(layer->pos_x, lt, start_state->x + shift.x());
                set_animated_value(layer->pos_y, lt, start_state->y + shift.y());
            }
        }
    }

    dirty_ = true;
    drag_changed_ = true;
    update();
}
void CanvasPreview::render_to_pixmap()
{
    if (!title_) { frame_pixmap_ = QPixmap(); return; }

    frame_pixmap_ = QPixmap::fromImage(render_title_to_image(*title_, playhead_));
    dirty_ = false;
}

void CanvasPreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Window));

    if (!title_) return;

    if (!drawing_shape_ && title_has_dynamic_text_layer(title_)) dirty_ = true;
    if (dirty_) render_to_pixmap();
    if (frame_pixmap_.isNull()) return;

    double scale = view_scale();
    QPointF origin = view_origin();
    int dw = (int)(title_->width * scale);
    int dh = (int)(title_->height * scale);
    int ox = (int)origin.x();
    int oy = (int)origin.y();

    auto checkerboard_colors = [this]() {
        if (checkerboard_pattern_ == 3)
            return std::pair<QColor, QColor>(Qt::white, Qt::white);
        if (checkerboard_pattern_ == 4)
            return std::pair<QColor, QColor>(Qt::black, Qt::black);
        if (checkerboard_pattern_ == 5)
            return std::pair<QColor, QColor>(QColor(0x80, 0x80, 0x80), QColor(0x80, 0x80, 0x80));
        if (checkerboard_pattern_ == 0)
            return std::pair<QColor, QColor>(QColor(0xee, 0xee, 0xee), QColor(0xc8, 0xc8, 0xc8));
        if (checkerboard_pattern_ == 2)
            return std::pair<QColor, QColor>(QColor(0x1f, 0x1f, 0x1f), QColor(0x36, 0x36, 0x36));
        return std::pair<QColor, QColor>(QColor(0x33, 0x33, 0x33), QColor(0x4a, 0x4a, 0x4a));
    };
    const auto [checker_a, checker_b] = checkerboard_colors();
    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(checker_a));
    p.drawRect(ox, oy, dw, dh);
    if (checker_a != checker_b) {
        p.setBrush(QBrush(checker_b));
        for (int cy = oy; cy < oy + dh; cy += 12)
            for (int cx = ox; cx < ox + dw; cx += 12)
                if ((((cx - ox) / 12) + ((cy - oy) / 12)) % 2 == 0)
                    p.drawRect(cx, cy, 12, 12);
    }

    p.drawPixmap(ox, oy, dw, dh, frame_pixmap_);

    p.save();
    p.setClipRect(QRectF(ox, oy, dw, dh));
    p.translate(origin);
    p.scale(scale, scale);
    for (const auto &layer : title_->layers) {
        if (!layer || !layer->visible || !layer->use_as_scene_mask)
            continue;

        const QRectF local_rect = layer_local_rect(*layer);
        if (!local_rect.isValid() || local_rect.isEmpty())
            continue;

        const double local_time = std::clamp(playhead_ - layer->in_time, 0.0,
                                             std::max(0.0, layer->out_time - layer->in_time));
        QPainterPath path = editor_scene_mask_layer_path(*layer, local_rect, local_time);
        QPen border(TitlePreferences::scene_mask_color(), layer->type == LayerType::Shape && layer->shape_type == ShapeType::Line ? 3.0 : 2.0);
        border.setCosmetic(true);

        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setTransform(editor_layer_world_transform(title_, *layer, playhead_), true);
        p.setPen(border);
        p.setBrush(layer->type == LayerType::Shape && layer->shape_type == ShapeType::Line
                       ? Qt::NoBrush
                       : scene_mask_hatch_brush());
        p.drawPath(path);
        p.restore();
    }
    p.restore();

    if (inline_text_editor_ && inline_text_editor_->isVisible()) {
        const QPoint viewport_origin = inline_text_editor_->viewport()->mapTo(this, QPoint(0, 0));
        const std::vector<QRectF> selection_rects = text_edit_selection_viewport_rects(inline_text_editor_);
        if (!selection_rects.empty()) {
            p.save();
            p.setClipRect(inline_text_editor_->viewport()->rect().translated(viewport_origin));
            p.setCompositionMode(QPainter::CompositionMode_Difference);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            for (QRectF selection_rect : selection_rects) {
                selection_rect.translate(viewport_origin);
                p.drawRect(selection_rect);
            }
            p.restore();
        }
    }

    if (safe_guides_visible_) {
        auto draw_guide = [&](double inset, const QColor &color) {
            QRectF r(ox + dw * inset, oy + dh * inset, dw * (1.0 - 2.0 * inset), dh * (1.0 - 2.0 * inset));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(color, 1.0, Qt::DashLine));
            p.drawRect(r);
        };
        draw_guide(OBS_ACTION_SAFE_PERCENT, QColor(0, 200, 255, 190));
        draw_guide(OBS_GRAPHICS_SAFE_PERCENT, QColor(255, 220, 0, 190));
    }

    if (!snap_feedback_.empty()) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        QPen guide_pen(QColor(0, 220, 255, 235), 1.0, Qt::DashLine);
        guide_pen.setDashPattern({5.0, 3.0});
        p.setPen(guide_pen);
        p.setBrush(QColor(0, 20, 30, 180));
        for (const auto &feedback : snap_feedback_) {
            if (feedback.x_axis) {
                double x = canvas_to_view(QPointF(feedback.value, 0.0)).x();
                p.drawLine(QPointF(x, oy), QPointF(x, oy + dh));
                if (!feedback.label.isEmpty())
                    p.drawText(QRectF(x + 5.0, oy + 5.0, 120.0, 18.0), feedback.label);
            } else {
                double y = canvas_to_view(QPointF(0.0, feedback.value)).y();
                p.drawLine(QPointF(ox, y), QPointF(ox + dw, y));
                if (!feedback.label.isEmpty())
                    p.drawText(QRectF(ox + 5.0, y + 5.0, 120.0, 18.0), feedback.label);
            }
        }
        p.restore();
    }

    auto layers = selected_layers();

    auto draw_layer_box = [&](const Layer &layer, bool handles) {
        QRectF box = layer_local_rect(layer);
        auto layer_point_to_view = [&](const QPointF &pt) {
            return canvas_to_view(layer_to_canvas(layer, pt));
        };
        const QPointF corners[] = {
            layer_point_to_view(box.topLeft()),
            layer_point_to_view(box.topRight()),
            layer_point_to_view(box.bottomRight()),
            layer_point_to_view(box.bottomLeft())
        };

        const bool editing_text_layer = !inline_text_layer_id_.empty() &&
            inline_text_layer_id_ == layer.id && is_canvas_text_layer(layer);
        const QColor outline_color = editing_text_layer
            ? QColor(255, 220, 0, handles ? 255 : 210)
            : QColor(0, 120, 255, handles ? 230 : 150);
        const QColor handle_stroke_color = editing_text_layer
            ? QColor(255, 220, 0, 255)
            : QColor(0, 120, 255, 255);

        p.save();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(outline_color, editing_text_layer ? 2.0 : 1.5, Qt::DashLine));
        QPolygonF outline;
        for (const QPointF &corner : corners)
            outline << corner;
        p.drawPolygon(outline);
        if (handles) {
            p.setPen(QPen(handle_stroke_color, 1.0));
            p.setBrush(QColor(255, 255, 255));
            const QPointF handle_points[] = {
                corners[0], layer_point_to_view(QPointF(box.center().x(), box.top())), corners[1],
                layer_point_to_view(QPointF(box.right(), box.center().y())), corners[2],
                layer_point_to_view(QPointF(box.center().x(), box.bottom())), corners[3],
                layer_point_to_view(QPointF(box.left(), box.center().y()))
            };
            const double half_handle = CANVAS_CONTROL_SIZE_PX / 2.0;
            for (const QPointF &pt : handle_points)
                p.drawRect(QRectF(pt.x() - half_handle, pt.y() - half_handle,
                                  CANVAS_CONTROL_SIZE_PX, CANVAS_CONTROL_SIZE_PX));

            QPointF origin = layer_point_to_view(QPointF(0, 0));
            p.setPen(QPen(QColor(255, 160, 0), 1.5));
            p.setBrush(QColor(255, 220, 80));
            p.drawEllipse(origin, CANVAS_ORIGIN_RADIUS_PX, CANVAS_ORIGIN_RADIUS_PX);
            p.drawLine(QPointF(origin.x() - CANVAS_CONTROL_SIZE_PX, origin.y()),
                       QPointF(origin.x() + CANVAS_CONTROL_SIZE_PX, origin.y()));
            p.drawLine(QPointF(origin.x(), origin.y() - CANVAS_CONTROL_SIZE_PX),
                       QPointF(origin.x(), origin.y() + CANVAS_CONTROL_SIZE_PX));

            draw_gradient_handles(p, layer);
            draw_corner_radius_handles(p, layer);
        }
        p.restore();
    };

    if (layers.size() == 1) {
        draw_layer_box(*layers.front(), true);
    } else if (layers.size() > 1) {
        for (const auto &layer : layers)
            if (layer) draw_layer_box(*layer, false);

        QRectF bounds = selected_canvas_bounds();
        if (bounds.isValid() && !bounds.isEmpty()) {
            QRectF view_bounds(canvas_to_view(bounds.topLeft()), canvas_to_view(bounds.bottomRight()));
            view_bounds = view_bounds.normalized();
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(0, 160, 255, 240), 1.5, Qt::SolidLine));
            p.drawRect(view_bounds);
            p.setPen(QPen(QColor(0, 120, 255, 255), 1.0));
            p.setBrush(QColor(255, 255, 255));
            const QPointF points[] = {
                view_bounds.topLeft(), QPointF(view_bounds.center().x(), view_bounds.top()), view_bounds.topRight(),
                QPointF(view_bounds.right(), view_bounds.center().y()), view_bounds.bottomRight(),
                QPointF(view_bounds.center().x(), view_bounds.bottom()), view_bounds.bottomLeft(),
                QPointF(view_bounds.left(), view_bounds.center().y())
            };
            for (const QPointF &pt : points)
                p.drawRect(QRectF(pt.x() - CANVAS_CONTROL_SIZE_PX / 2.0,
                                  pt.y() - CANVAS_CONTROL_SIZE_PX / 2.0,
                                  CANVAS_CONTROL_SIZE_PX, CANVAS_CONTROL_SIZE_PX));
        }
    }

    draw_toolbar_preview(p);

    if (drag_mode_ == DragMode::Rotate) {
        QPointF pivot = canvas_to_view(drag_rotation_pivot_canvas_);
        double start_angle = std::atan2(drag_start_view_.y() - pivot.y(), drag_start_view_.x() - pivot.x());
        double radius = std::max(24.0, QLineF(pivot, drag_current_view_).length());
        QRectF arc_rect(pivot.x() - radius, pivot.y() - radius, radius * 2.0, radius * 2.0);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 190, 40, 235), 1.5, Qt::DashLine));
        p.drawLine(pivot, drag_current_view_);
        p.setPen(QPen(QColor(255, 190, 40, 235), 2.0));
        p.drawEllipse(pivot, CANVAS_ORIGIN_RADIUS_PX + 2.0, CANVAS_ORIGIN_RADIUS_PX + 2.0);
        p.drawArc(arc_rect, (int)std::round(-radians_to_degrees(start_angle) * 16.0),
                  (int)std::round(-drag_current_rotation_delta_ * 16.0));
        p.restore();
    }

    if (drag_mode_ == DragMode::Marquee && marquee_active_) {
        QRectF marquee(drag_start_view_, drag_current_view_);
        marquee = marquee.normalized();
        p.setBrush(QColor(0, 120, 255, 45));
        p.setPen(QPen(QColor(0, 160, 255, 220), 1.0, Qt::DashLine));
        p.drawRect(marquee);
    }

    draw_canvas_drag_tooltip(p);
    draw_color_picker_tooltip(p);
}
double CanvasPreview::toolbar_draw_aspect_ratio() const
{
    if (active_tool_ == CanvasTool::Text) {
        const double default_width = title_ ? std::max(1.0, title_->width * 0.5) : 960.0;
        return default_width / 160.0;
    }
    return 1.0;
}

static QRectF editor_modifier_rect(const QPointF &anchor, const QPointF &current,
                                   Qt::KeyboardModifiers modifiers, double aspect,
                                   double fixed_width, double fixed_height)
{
    QPointF delta = current - anchor;
    double width = fixed_width > 0.0 ? fixed_width : std::abs(delta.x());
    double height = fixed_height > 0.0 ? fixed_height : std::abs(delta.y());

    if (modifiers.testFlag(Qt::ShiftModifier)) {
        aspect = std::max(0.001, aspect);
        if (fixed_width > 0.0 && fixed_height <= 0.0)
            width = height * aspect;
        else if (fixed_height > 0.0 && fixed_width <= 0.0)
            height = width / aspect;
        else if (width / std::max(1.0, height) > aspect)
            height = width / aspect;
        else
            width = height * aspect;
    }

    const double sign_x = delta.x() < 0.0 ? -1.0 : 1.0;
    const double sign_y = delta.y() < 0.0 ? -1.0 : 1.0;
    QRectF rect;
    if (modifiers.testFlag(Qt::AltModifier)) {
        rect = QRectF(anchor.x() - width,
                      anchor.y() - height,
                      width * 2.0, height * 2.0);
    } else {
        const bool center_fixed_width = fixed_width > 0.0;
        const bool center_fixed_height = fixed_height > 0.0;
        const double left = center_fixed_width ? anchor.x() - width * 0.5 : std::min(anchor.x(), anchor.x() + sign_x * width);
        const double right = center_fixed_width ? anchor.x() + width * 0.5 : std::max(anchor.x(), anchor.x() + sign_x * width);
        const double top = center_fixed_height ? anchor.y() - height * 0.5 : std::min(anchor.y(), anchor.y() + sign_y * height);
        const double bottom = center_fixed_height ? anchor.y() + height * 0.5 : std::max(anchor.y(), anchor.y() + sign_y * height);
        rect = QRectF(QPointF(left, top), QPointF(right, bottom));
    }

    rect = rect.normalized();
    if (rect.width() < 1.0) rect.setWidth(1.0);
    if (rect.height() < 1.0) rect.setHeight(1.0);
    return rect;
}

QRectF CanvasPreview::toolbar_draw_rect(const QPointF &canvas_pt, Qt::KeyboardModifiers modifiers) const
{
    return editor_modifier_rect(shape_draw_start_canvas_, canvas_pt, modifiers, toolbar_draw_aspect_ratio());
}

QRectF CanvasPreview::snapped_toolbar_draw_rect(const QRectF &raw_rect, bool allow_snap)
{
    QRectF rect = raw_rect.normalized();
    if (!allow_snap || !rect.isValid()) {
        clear_snap_feedback();
        return rect;
    }

    return rect;
}

QRect CanvasPreview::toolbar_preview_update_rect() const
{
    if (!title_ || !drawing_shape_)
        return QRect();

    const QRectF canvas_rect = shape_draw_current_rect_.isValid()
                                  ? shape_draw_current_rect_
                                  : toolbar_draw_rect(shape_draw_current_canvas_, shape_draw_modifiers_);
    QRectF view_rect(canvas_to_view(canvas_rect.topLeft()), canvas_to_view(canvas_rect.bottomRight()));
    view_rect = view_rect.normalized();
    return view_rect.adjusted(-24.0, -24.0, 24.0, 24.0).toAlignedRect();
}

void CanvasPreview::draw_toolbar_preview(QPainter &p)
{
    if (!title_ || !drawing_shape_)
        return;

    const QRectF canvas_rect = shape_draw_current_rect_.isValid()
                                  ? shape_draw_current_rect_
                                  : toolbar_draw_rect(shape_draw_current_canvas_, shape_draw_modifiers_);
    QRectF view_rect(canvas_to_view(canvas_rect.topLeft()), canvas_to_view(canvas_rect.bottomRight()));
    view_rect = view_rect.normalized();

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool text_preview = active_tool_ == CanvasTool::Text;
    const QColor accent = text_preview ? layer_type_color(active_text_layer_type_) : layer_type_color(LayerType::Shape);
    QColor fill = accent;
    fill.setAlpha(text_preview ? 38 : (active_shape_type_ == ShapeType::Line ? 0 : 42));
    QColor stroke = accent.lighter(135);
    stroke.setAlpha(235);

    QPen pen(stroke, active_shape_type_ == ShapeType::Line && !text_preview ? 2.0 : 1.6, Qt::DashLine);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(text_preview ? QColor(20, 20, 20, 90) : fill);

    if (text_preview) {
        p.drawRoundedRect(view_rect, 4.0, 4.0);
        QString label = text_tool_display_name(active_text_layer_type_);
        p.setPen(stroke);
        QFont label_font = p.font();
        label_font.setBold(true);
        p.setFont(label_font);
        p.drawText(view_rect.adjusted(8.0, 6.0, -8.0, -6.0), Qt::AlignLeft | Qt::AlignTop, label);
    } else {
        p.drawPath(tool_shape_path(active_shape_type_, view_rect));
    }

    p.restore();
}

bool CanvasPreview::sample_color_at_view(const QPointF &view_pt, QColor &color)
{
    if (!title_) return false;
    if (dirty_ || frame_pixmap_.isNull())
        render_to_pixmap();
    if (frame_pixmap_.isNull()) return false;

    const QPointF canvas = view_to_canvas(view_pt);
    const QImage image = frame_pixmap_.toImage();
    if (image.isNull()) return false;

    const int x = (int)std::floor(canvas.x());
    const int y = (int)std::floor(canvas.y());
    if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
        return false;

    color = image.pixelColor(x, y);
    return color.isValid();
}

void CanvasPreview::update_color_picker_tooltip(const QPointF &view_pt)
{
    color_picker_tooltip_pos_ = view_pt;
    QColor color;
    color_picker_tooltip_visible_ = sample_color_at_view(view_pt, color);
    if (color_picker_tooltip_visible_)
        color_picker_tooltip_color_ = color;
    update();
}

static QString editor_color_hex(const QColor &color)
{
    if (color.alpha() < 255) {
        return QStringLiteral("#%1%2%3%4")
            .arg(color.red(), 2, 16, QLatin1Char('0'))
            .arg(color.green(), 2, 16, QLatin1Char('0'))
            .arg(color.blue(), 2, 16, QLatin1Char('0'))
            .arg(color.alpha(), 2, 16, QLatin1Char('0'))
            .toUpper();
    }
    return QStringLiteral("#%1%2%3")
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'))
        .toUpper();
}

void CanvasPreview::draw_color_picker_tooltip(QPainter &p)
{
    if (active_tool_ != CanvasTool::ColorPicker || !color_picker_tooltip_visible_)
        return;

    const QString hex = editor_color_hex(color_picker_tooltip_color_);
    const QFontMetrics fm(font());
    const int swatch = 18;
    const int pad = 8;
    const int gap = 7;
    const int width = pad * 2 + swatch + gap + fm.horizontalAdvance(hex);
    const int height = std::max(30, pad * 2 + swatch);
    QPointF pos = color_picker_tooltip_pos_ + QPointF(16.0, 18.0);
    if (pos.x() + width + 4 > rect().right())
        pos.setX(color_picker_tooltip_pos_.x() - width - 16.0);
    if (pos.y() + height + 4 > rect().bottom())
        pos.setY(color_picker_tooltip_pos_.y() - height - 16.0);
    pos.setX(std::max(4.0, pos.x()));
    pos.setY(std::max(4.0, pos.y()));

    QRectF box(pos, QSizeF(width, height));
    QRectF swatch_rect(box.left() + pad, box.top() + (box.height() - swatch) / 2.0, swatch, swatch);
    QRectF text_rect(swatch_rect.right() + gap, box.top(), width - pad - swatch - gap, height);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(18, 18, 18), 1.0));
    p.setBrush(QColor(48, 48, 48, 235));
    p.drawRoundedRect(box, 4.0, 4.0);

    const int cell = 5;
    p.setPen(Qt::NoPen);
    for (int y = (int)swatch_rect.top(); y < (int)swatch_rect.bottom(); y += cell) {
        for (int x = (int)swatch_rect.left(); x < (int)swatch_rect.right(); x += cell) {
            const bool dark = ((x / cell) + (y / cell)) % 2;
            p.fillRect(QRect(x, y, cell, cell).intersected(swatch_rect.toAlignedRect()),
                       dark ? QColor(95, 95, 95) : QColor(165, 165, 165));
        }
    }
    p.fillRect(swatch_rect, color_picker_tooltip_color_);
    p.setPen(QPen(QColor(16, 16, 16), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(swatch_rect.adjusted(0.0, 0.0, -1.0, -1.0));

    p.setPen(QColor(235, 235, 235));
    p.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft, hex);
    p.restore();
}

QString CanvasPreview::canvas_drag_tooltip_text() const
{
    if (drag_mode_ == DragMode::None || drag_mode_ == DragMode::Marquee || !drag_changed_)
        return QString();

    auto is_resize_drag = [](DragMode mode) {
        return mode == DragMode::ResizeNW || mode == DragMode::ResizeN || mode == DragMode::ResizeNE ||
               mode == DragMode::ResizeE || mode == DragMode::ResizeSE || mode == DragMode::ResizeS ||
               mode == DragMode::ResizeSW || mode == DragMode::ResizeW;
    };
    auto is_corner_radius_drag = [](DragMode mode) {
        return mode == DragMode::CornerRadiusTL || mode == DragMode::CornerRadiusTR ||
               mode == DragMode::CornerRadiusBR || mode == DragMode::CornerRadiusBL;
    };

    if (drag_mode_ == DragMode::Move) {
        const QPointF canvas = view_to_canvas(drag_current_view_);
        return QStringLiteral("X: %1 px  Y: %2 px")
            .arg(std::round(canvas.x()), 0, 'f', 0)
            .arg(std::round(canvas.y()), 0, 'f', 0);
    }

    if (is_resize_drag(drag_mode_)) {
        const auto layers = selected_layers();
        double w = 0.0;
        double h = 0.0;
        if (layers.size() > 1) {
            const QRectF bounds = selected_canvas_bounds();
            w = bounds.width();
            h = bounds.height();
        } else if (!layers.empty() && layers.front()) {
            const double lt = std::clamp(playhead_ - layers.front()->in_time, 0.0,
                                         std::max(0.0, layers.front()->out_time - layers.front()->in_time));
            w = eval_box_width(*layers.front(), lt);
            h = eval_box_height(*layers.front(), lt);
        }
        return QStringLiteral("W: %1 px  H: %2 px")
            .arg(std::round(w), 0, 'f', 0)
            .arg(std::round(h), 0, 'f', 0);
    }

    if (drag_mode_ == DragMode::Rotate) {
        const double radians = degrees_to_radians(drag_current_rotation_delta_);
        return QStringLiteral("Rad: %1").arg(radians, 0, 'f', 3);
    }

    if (is_corner_radius_drag(drag_mode_)) {
        auto layer = selected_layer();
        if (!layer)
            return QString();
        double radius = 0.0;
        switch (drag_mode_) {
        case DragMode::CornerRadiusTL: radius = layer->corner_radius_tl; break;
        case DragMode::CornerRadiusTR: radius = layer->corner_radius_tr; break;
        case DragMode::CornerRadiusBR: radius = layer->corner_radius_br; break;
        case DragMode::CornerRadiusBL: radius = layer->corner_radius_bl; break;
        default: break;
        }
        return QStringLiteral("R: %1 px").arg(std::round(radius), 0, 'f', 0);
    }

    return QString();
}

void CanvasPreview::draw_canvas_drag_tooltip(QPainter &p)
{
    const QString text = canvas_drag_tooltip_text();
    if (text.isEmpty())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QFontMetrics fm(font());
    const int width = fm.horizontalAdvance(text) + 18;
    const int height = std::max(24, fm.height() + 8);
    QPointF pos = drag_current_view_ + QPointF(14.0, 22.0);
    if (pos.x() + width + 6 > rect().right())
        pos.setX(drag_current_view_.x() - width - 14.0);
    if (pos.y() + height + 6 > rect().bottom())
        pos.setY(drag_current_view_.y() - height - 14.0);
    pos.setX(std::max(6.0, pos.x()));
    pos.setY(std::max(6.0, pos.y()));

    const QRectF box(pos, QSizeF(width, height));
    p.setPen(QPen(QColor(18, 18, 18), 1.0));
    p.setBrush(QColor(48, 48, 48, 235));
    p.drawRoundedRect(box, 4.0, 4.0);
    p.setPen(QColor(245, 245, 245));
    p.drawText(box.adjusted(9.0, 0.0, -9.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft, text);
    p.restore();
}

void CanvasPreview::update_shape_drawing(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (!drawing_shape_) return;

    const QRect old_update_rect = last_toolbar_preview_update_rect_;
    shape_draw_current_canvas_ = view_to_canvas(view_pt);
    shape_draw_modifiers_ = modifiers;
    const bool allow_snap = !modifiers.testFlag(Qt::ControlModifier);
    if (allow_snap)
        snap_feedback_.clear();

    const QPointF raw_current = shape_draw_current_canvas_;
    QRectF raw_rect = toolbar_draw_rect(raw_current, shape_draw_modifiers_);
    if (allow_snap && raw_rect.isValid()) {
        const QPointF delta = raw_current - shape_draw_start_canvas_;
        const bool alt = modifiers.testFlag(Qt::AltModifier);
        const bool active_left = alt ? delta.x() < 0.0 : raw_rect.left() < shape_draw_start_canvas_.x();
        const bool active_right = alt ? delta.x() >= 0.0 : raw_rect.right() > shape_draw_start_canvas_.x();
        const bool active_top = alt ? delta.y() < 0.0 : raw_rect.top() < shape_draw_start_canvas_.y();
        const bool active_bottom = alt ? delta.y() >= 0.0 : raw_rect.bottom() > shape_draw_start_canvas_.y();

        auto snap_value = [&](bool x_axis, double value, double *snapped) {
            if (!snapped)
                return false;
            std::vector<double> targets;
            std::vector<QString> labels;
            collect_snap_targets(x_axis, targets, labels);
            collect_spacing_targets(x_axis, targets, labels);
            const double tolerance = 6.0 / std::max(0.1, view_scale());
            double best_distance = tolerance + 1.0;
            double best_target = value;
            QString best_label;
            for (size_t i = 0; i < targets.size(); ++i) {
                const double distance = std::abs(targets[i] - value);
                if (distance < best_distance) {
                    best_distance = distance;
                    best_target = targets[i];
                    best_label = labels[i];
                }
            }
            if (best_distance > tolerance)
                return false;
            *snapped = best_target;
            add_snap_feedback(x_axis, best_target, best_label);
            return true;
        };

        QPointF snapped_current = raw_current;
        double snapped = 0.0;
        if (active_left && snap_value(true, raw_rect.left(), &snapped))
            snapped_current.setX(raw_current.x() + snapped - raw_rect.left());
        else if (active_right && snap_value(true, raw_rect.right(), &snapped))
            snapped_current.setX(raw_current.x() + snapped - raw_rect.right());
        if (active_top && snap_value(false, raw_rect.top(), &snapped))
            snapped_current.setY(raw_current.y() + snapped - raw_rect.top());
        else if (active_bottom && snap_value(false, raw_rect.bottom(), &snapped))
            snapped_current.setY(raw_current.y() + snapped - raw_rect.bottom());

        shape_draw_current_canvas_ = snapped_current;
        raw_rect = toolbar_draw_rect(shape_draw_current_canvas_, shape_draw_modifiers_);
    }
    shape_draw_current_rect_ = raw_rect.normalized();
    drawing_shape_changed_ = shape_draw_current_rect_.width() >= 2.0 || shape_draw_current_rect_.height() >= 2.0;

    last_toolbar_preview_update_rect_ = toolbar_preview_update_rect();
    QRect repaint_rect = old_update_rect.united(last_toolbar_preview_update_rect_);
    if (repaint_rect.isEmpty())
        repaint_rect = this->rect();
    update(repaint_rect);
}


std::shared_ptr<Layer> CanvasPreview::text_layer_at_view_pos(const QPointF &view_pt) const
{
    if (!title_) return nullptr;
    QPointF canvas = view_to_canvas(view_pt);
    for (auto it = title_->layers.rbegin(); it != title_->layers.rend(); ++it) {
        const auto &layer = *it;
        if (!layer || !is_canvas_text_layer(*layer) || layer->type == LayerType::Clock) continue;
        if (!layer->visible || layer->locked) continue;
        if (playhead_ < layer->in_time || playhead_ > layer->out_time) continue;
        if (layer_local_rect(*layer).contains(canvas_to_layer(*layer, canvas)))
            return layer;
    }
    return nullptr;
}


static QString scale_rich_text_font_sizes(const QString &html, double scale)
{
    if (html.isEmpty() || std::abs(scale - 1.0) < 0.0001)
        return html;

    QString scaled = html;
    QRegularExpression re(
        QStringLiteral("((?:font-size|margin-left|margin-right|margin-top|margin-bottom|text-indent)\\s*:\\s*)(-?[0-9]+(?:\\.[0-9]+)?)(px|pt)"),
        QRegularExpression::CaseInsensitiveOption);
    qsizetype offset = 0;
    QRegularExpressionMatch match;
    while ((match = re.match(scaled, offset)).hasMatch()) {
        const QString property = match.captured(1);
        const double value = match.captured(2).toDouble();
        const QString unit = match.captured(3);
        const double scaled_value = property.trimmed().startsWith(QStringLiteral("font-size"), Qt::CaseInsensitive)
                                      ? std::max(1.0, value * scale)
                                      : value * scale;
        const QString replacement = QStringLiteral("%1%2%3")
                                        .arg(property)
                                        .arg(scaled_value, 0, 'f', 3)
                                        .arg(unit);
        scaled.replace(match.capturedStart(0), match.capturedLength(0), replacement);
        offset = match.capturedStart(0) + replacement.size();
    }
    return scaled;
}


static bool inline_document_has_style_overrides(const QTextDocument *doc, const Layer &layer, double t, double visual_scale)
{
    if (!doc) return false;

    QFont expected_font = font_for_layer(layer);
    if (expected_font.pixelSize() > 0)
        expected_font.setPixelSize(std::max(1, (int)std::round(expected_font.pixelSize() * visual_scale)));
    const QColor expected_color = color_from_argb(eval_text_color(layer, t));
    const int expected_weight = expected_font.weight();
    const bool expected_italic = expected_font.italic();
    const bool expected_underline = layer.text_underline;
    const bool expected_strike = layer.text_strikethrough;

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || fragment.text().isEmpty()) continue;
            const QTextCharFormat fmt = fragment.charFormat();
            if (fmt.fontWeight() != expected_weight) return true;
            if (fmt.fontItalic() != expected_italic) return true;
            if (fmt.fontUnderline() != expected_underline) return true;
            if (fmt.fontStrikeOut() != expected_strike) return true;
            if (fmt.hasProperty(QTextFormat::FontFamily) && fmt.fontFamily() != expected_font.family()) return true;
            if (fmt.hasProperty(QTextFormat::FontPixelSize) && std::abs(fmt.font().pixelSize() - expected_font.pixelSize()) > 1) return true;
            if (fmt.hasProperty(QTextFormat::FontPointSize) && expected_font.pointSizeF() > 0.0 &&
                std::abs(fmt.fontPointSize() - expected_font.pointSizeF()) > 0.5) return true;
            if (fmt.foreground().style() != Qt::NoBrush) {
                const QColor color = fmt.foreground().color();
                if (layer.fill_type == 1 && color.isValid() && color.alpha() == 0)
                    continue;
                if (color.isValid() && color != expected_color) return true;
            }
        }
    }
    return false;
}

double CanvasPreview::inline_text_visual_scale(const Layer &layer) const
{
    const double lt = std::max(0.0, playhead_ - layer.in_time);
    const double sx = std::abs(layer.scale_x.evaluate(lt));
    const double sy = std::abs(layer.scale_y.evaluate(lt));
    return std::clamp(view_scale() * std::sqrt(std::max(0.0001, sx * sy)), 0.05, 16.0);
}

void CanvasPreview::configure_inline_text_editor(const Layer &layer)
{
    if (!inline_text_editor_) return;

    QSignalBlocker blocker(inline_text_editor_);
    QTextCursor saved_cursor = inline_text_editor_->textCursor();

    const double local_time = std::max(0.0, playhead_ - layer.in_time);
    const double visual_scale = inline_text_visual_scale(layer);
    QFont font = font_for_layer(layer);
    if (font.pixelSize() > 0)
        font.setPixelSize(std::max(1, (int)std::round(font.pixelSize() * visual_scale)));
    inline_text_editor_->setFont(font);
    QColor transparent_text_color = color_from_argb(eval_text_color(layer, local_time));
    transparent_text_color.setAlpha(0);
    inline_text_editor_->setTextColor(transparent_text_color);

    QTextDocument *doc = inline_text_editor_->document();
    doc->setDocumentMargin(0.0);
    doc->setDefaultFont(font);

    QTextOption option = doc->defaultTextOption();
    option.setUseDesignMetrics(true);
    option.setWrapMode(layer.text_overflow_mode == 0
                           ? (layer.paragraph_hyphenate ? QTextOption::WrapAnywhere
                                                         : QTextOption::WrapAtWordBoundaryOrAnywhere)
                           : QTextOption::NoWrap);
    Qt::Alignment align = Qt::AlignLeft;
    if (layer.align_h == 1 || layer.align_h == 4) align = Qt::AlignHCenter;
    else if (layer.align_h == 2 || layer.align_h == 5) align = Qt::AlignRight;
    else if (layer.align_h >= 3) align = Qt::AlignJustify;
    option.setAlignment(align);
    doc->setDefaultTextOption(option);

    const QRectF local = layer_local_rect(layer);
    const QRectF text_rect = text_rect_for_style(local, layer);
    const int wrap_width_px = std::max(1, (int)std::ceil(text_rect.width() * visual_scale));
    doc->setTextWidth(layer.text_overflow_mode == 2 ? -1.0 : (qreal)wrap_width_px);
    doc->setPageSize(layer.text_overflow_mode == 2
                         ? QSizeF(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX)
                         : QSizeF(wrap_width_px, QWIDGETSIZE_MAX));
    inline_text_editor_->setLineWrapMode(layer.text_overflow_mode == 0 ? QTextEdit::FixedPixelWidth : QTextEdit::NoWrap);
    inline_text_editor_->setLineWrapColumnOrWidth(wrap_width_px);
    inline_text_editor_->setWordWrapMode(option.wrapMode());

    QTextBlockFormat block_format;
    block_format.setAlignment(align);
    block_format.setLeftMargin(std::max(0.0, eval_paragraph_indent_left(layer, local_time)) * visual_scale);
    block_format.setRightMargin(std::max(0.0, eval_paragraph_indent_right(layer, local_time)) * visual_scale);
    block_format.setTextIndent(eval_paragraph_indent_first_line(layer, local_time) * visual_scale);
    block_format.setTopMargin(std::max(0.0f, layer.paragraph_space_before) * visual_scale);
    block_format.setBottomMargin(std::max(0.0f, layer.paragraph_space_after) * visual_scale);

    const bool has_structured_rich_text = !layer.rich_text.plain_text.empty() || !layer.rich_text.ranges.empty();
    QTextCharFormat char_format;
    char_format.setFont(font);
    RichTextCharFormat layer_format = layer_char_format_for_editor(layer);
    store_rich_text_format_properties(char_format, layer_format);
    QColor editor_text_color = color_from_argb(eval_text_color(layer, local_time));
    editor_text_color.setAlpha(0);
    char_format.setForeground(editor_text_color);
    char_format.setFontUnderline(layer.text_underline);
    char_format.setFontStrikeOut(layer.text_strikethrough);

    if (layer.rich_text_html.empty()) {
        QTextCursor format_cursor(doc);
        format_cursor.select(QTextCursor::Document);
        format_cursor.mergeBlockFormat(block_format);
        if (!has_structured_rich_text)
            format_cursor.mergeCharFormat(char_format);
    } else {
        QTextCursor format_cursor(doc);
        format_cursor.select(QTextCursor::Document);
        format_cursor.mergeCharFormat(char_format);
    }
    /*
     * Do not call mergeCurrentCharFormat() while the saved cursor owns a
     * selection. QTextEdit applies that merge to the selected document text,
     * so re-positioning the inline editor after begin_text_edit() could repaint
     * every character with the current/layer style and hide mixed per-character
     * sizes or colors until edit mode was committed.
     */
    if (!saved_cursor.hasSelection())
        inline_text_editor_->mergeCurrentCharFormat(char_format);
    if (auto *layout = doc->documentLayout())
        layout->documentSize();
    inline_text_editor_->setTextCursor(saved_cursor);
}

bool CanvasPreview::sync_inline_text_layer(bool mark_dirty)
{
    if (!inline_text_editor_ || inline_text_layer_id_.empty() || !title_) return false;
    auto layer = title_->find_layer(inline_text_layer_id_);
    if (!layer) return false;

    QTextDocument *editor_doc = inline_text_editor_->document();
    const std::string plain = inline_text_editor_->toPlainText().toStdString();
    if (plain == layer->text_content && editor_doc && !editor_doc->isModified()) {
        const QTextCursor cursor = inline_text_editor_->textCursor();
        RichTextSelection selection{(size_t)std::max(0, cursor.anchor()),
                                    (size_t)std::max(0, cursor.position())};
        const size_t text_len = layer->rich_text.plain_text.size();
        selection.anchor = std::min(selection.anchor, text_len);
        selection.head = std::min(selection.head, text_len);
        const bool selection_changed = layer->rich_text.selection.anchor != selection.anchor ||
                                       layer->rich_text.selection.head != selection.head;
        if (selection_changed)
            layer->rich_text.selection = selection;
        return false;
    }

    const double visual_scale = inline_text_visual_scale(*layer);
    RichTextDocument next_model = rich_text_document_from_qtext_document(editor_doc, *layer, visual_scale, inline_text_editor_->textCursor());
    const bool selection_changed = layer->rich_text.selection.anchor != next_model.selection.anchor ||
                                   layer->rich_text.selection.head != next_model.selection.head;
    const bool changed = layer->text_content != plain || layer->rich_text.plain_text != next_model.plain_text ||
                         !rich_text_char_formats_equal(layer->rich_text.default_format, next_model.default_format) ||
                         !rich_text_ranges_equal(layer->rich_text.ranges, next_model.ranges);
    if (!changed) {
        if (selection_changed)
            layer->rich_text.selection = next_model.selection;
        return false;
    }

    layer->text_content = plain;
    layer->rich_text = std::move(next_model);
    layer->rich_text_html.clear();
    if (editor_doc)
        editor_doc->setModified(false);
    if (mark_dirty) dirty_ = true;
    return true;
}

void CanvasPreview::refresh_inline_text_edit(bool mark_dirty, bool emit_changed)
{
    if (committing_inline_text_ || updating_inline_text_editor_ || refreshing_inline_text_ ||
        !inline_text_editor_ || inline_text_layer_id_.empty())
        return;

    refreshing_inline_text_ = true;
    const std::string layer_id = inline_text_layer_id_;
    const bool model_changed = sync_inline_text_layer(mark_dirty);

    if (mark_dirty || model_changed)
        dirty_ = true;

    position_text_editor();

    if (dirty_)
        render_to_pixmap();

    if (inline_text_editor_) {
        const QRect editor_rect = inline_text_editor_->geometry().adjusted(-4, -4, 4, 4);
        update(editor_rect);
        inline_text_editor_->update();
        inline_text_editor_->viewport()->update();
        repaint(editor_rect);
    } else {
        update();
    }

    refreshing_inline_text_ = false;

    if (emit_changed && (mark_dirty || model_changed))
        emit text_edit_changed(layer_id);
}

QRectF CanvasPreview::inline_text_document_local_rect(const Layer &layer) const
{
    const QRectF local = layer_local_rect(layer);
    const QRectF text_rect = text_rect_for_style(local, layer);

    const double visual_scale = inline_text_visual_scale(layer);
    const double local_time = std::max(0.0, playhead_ - layer.in_time);
    QFont font = font_for_layer(layer);
    if (font.pixelSize() > 0)
        font.setPixelSize(std::max(1, (int)std::round(font.pixelSize() * visual_scale)));

    QTextDocument measure_doc;
    measure_doc.setDocumentMargin(0.0);
    measure_doc.setDefaultFont(font);

    QTextOption option = measure_doc.defaultTextOption();
    option.setUseDesignMetrics(true);
    option.setWrapMode(layer.text_overflow_mode == 0
                           ? (layer.paragraph_hyphenate ? QTextOption::WrapAnywhere
                                                         : QTextOption::WrapAtWordBoundaryOrAnywhere)
                           : QTextOption::NoWrap);
    Qt::Alignment align = Qt::AlignLeft;
    if (layer.align_h == 1 || layer.align_h == 4) align = Qt::AlignHCenter;
    else if (layer.align_h == 2 || layer.align_h == 5) align = Qt::AlignRight;
    else if (layer.align_h >= 3) align = Qt::AlignJustify;
    option.setAlignment(align);
    measure_doc.setDefaultTextOption(option);

    const int wrap_width_px = std::max(1, (int)std::ceil(text_rect.width() * visual_scale));
    measure_doc.setTextWidth(layer.text_overflow_mode == 2 ? -1.0 : (qreal)wrap_width_px);
    measure_doc.setPageSize(layer.text_overflow_mode == 2
                                ? QSizeF(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX)
                                : QSizeF(wrap_width_px, QWIDGETSIZE_MAX));

    if (!layer.rich_text.plain_text.empty() || !layer.rich_text.ranges.empty()) {
        populate_qtext_document_from_rich_text(&measure_doc, layer.rich_text, visual_scale);
    } else if (!layer.rich_text_html.empty()) {
        measure_doc.setHtml(scale_rich_text_font_sizes(QString::fromStdString(layer.rich_text_html), visual_scale));
    } else {
        populate_qtext_document_from_plain_layer_text(&measure_doc, layer.text_content,
                                                      layer_char_format_for_editor(layer), visual_scale);
    }

    QTextBlockFormat block_format;
    block_format.setAlignment(align);
    block_format.setLeftMargin(std::max(0.0, eval_paragraph_indent_left(layer, local_time)) * visual_scale);
    block_format.setRightMargin(std::max(0.0, eval_paragraph_indent_right(layer, local_time)) * visual_scale);
    block_format.setTextIndent(eval_paragraph_indent_first_line(layer, local_time) * visual_scale);
    block_format.setTopMargin(std::max(0.0f, layer.paragraph_space_before) * visual_scale);
    block_format.setBottomMargin(std::max(0.0f, layer.paragraph_space_after) * visual_scale);
    QTextCursor format_cursor(&measure_doc);
    format_cursor.select(QTextCursor::Document);
    format_cursor.mergeBlockFormat(block_format);

    QSizeF doc_size;
    if (auto *layout = measure_doc.documentLayout()) {
        doc_size = layout->documentSize();
        if (!doc_size.isValid() || doc_size.isEmpty())
            doc_size = measure_doc.size();
    }

    const double doc_width = layer.text_overflow_mode == 2 && doc_size.width() > 0.0
                                 ? doc_size.width() / std::max(0.0001, visual_scale)
                                 : text_rect.width();
    const double doc_height = doc_size.height() > 0.0
                                  ? doc_size.height() / std::max(0.0001, visual_scale)
                                  : text_rect.height();

    double y = text_rect.top();
    if (layer.align_v == 1)
        y = text_rect.top() + (text_rect.height() - doc_height) / 2.0;
    else if (layer.align_v == 2)
        y = text_rect.bottom() - doc_height;
    y -= layer.baseline_shift;

    double x = text_rect.left();
    if (layer.text_overflow_mode == 2 && doc_width < text_rect.width()) {
        if (layer.align_h == 1 || layer.align_h == 4)
            x = text_rect.left() + (text_rect.width() - doc_width) / 2.0;
        else if (layer.align_h == 2 || layer.align_h == 5)
            x = text_rect.right() - doc_width;
    }

    return QRectF(x, y, std::max(1.0, doc_width), std::max(1.0, doc_height));
}

void CanvasPreview::position_text_editor()
{
    if (!inline_text_editor_ || inline_text_layer_id_.empty() || !title_) return;
    auto layer = title_->find_layer(inline_text_layer_id_);
    if (!layer) {
        inline_text_editor_->hide();
        return;
    }

    const double visual_scale = inline_text_visual_scale(*layer);
    const bool was_updating_inline_text_editor = updating_inline_text_editor_;
    updating_inline_text_editor_ = true;
    configure_inline_text_editor(*layer);
    {
        QSignalBlocker blocker(inline_text_editor_);
        const QTextCursor saved_cursor = inline_text_editor_->textCursor();
        const int anchor = saved_cursor.anchor();
        const int position = saved_cursor.position();
        const QString layer_plain = !layer->rich_text.empty()
                                        ? QString::fromStdString(layer->rich_text.plain_text)
                                        : QString::fromStdString(layer->text_content);
        const bool scale_changed = std::abs(inline_text_last_visual_scale_ - visual_scale) > 0.001;
        const bool text_changed_externally = inline_text_editor_->toPlainText() != layer_plain;
        if (scale_changed || text_changed_externally) {
            if (!layer->rich_text.plain_text.empty() || !layer->rich_text.ranges.empty()) {
                populate_qtext_document_from_rich_text(inline_text_editor_->document(), layer->rich_text, visual_scale);
            } else if (!layer->rich_text_html.empty()) {
                inline_text_editor_->setHtml(scale_rich_text_font_sizes(QString::fromStdString(layer->rich_text_html), visual_scale));
            } else {
                populate_qtext_document_from_plain_layer_text(inline_text_editor_->document(), layer->text_content,
                                                              layer_char_format_for_editor(*layer), visual_scale);
            }
            inline_text_last_visual_scale_ = visual_scale;
            QTextCursor restored(inline_text_editor_->document());
            const int text_len = inline_text_editor_->toPlainText().size();
            restored.setPosition(std::clamp(anchor, 0, text_len));
            restored.setPosition(std::clamp(position, 0, text_len), QTextCursor::KeepAnchor);
            inline_text_editor_->setTextCursor(restored);
            if (auto *doc = inline_text_editor_->document())
                doc->setModified(false);
        }
        if (auto *doc = inline_text_editor_->document())
            if (auto *layout = doc->documentLayout())
                layout->documentSize();
    }
    updating_inline_text_editor_ = was_updating_inline_text_editor;

    const QRectF document_rect = inline_text_document_local_rect(*layer);
    QPolygonF poly;
    poly << canvas_to_view(layer_to_canvas(*layer, document_rect.topLeft()))
         << canvas_to_view(layer_to_canvas(*layer, document_rect.topRight()))
         << canvas_to_view(layer_to_canvas(*layer, document_rect.bottomRight()))
         << canvas_to_view(layer_to_canvas(*layer, document_rect.bottomLeft()));
    QRectF bounds = poly.boundingRect();
    const int left = (int)std::floor(bounds.left());
    const int top = (int)std::floor(bounds.top());
    const int right = (int)std::ceil(bounds.right());
    const int bottom = (int)std::ceil(bounds.bottom());
    inline_text_editor_->setGeometry(QRect(left, top, std::max(1, right - left), std::max(1, bottom - top)));
}

void CanvasPreview::begin_text_edit(const std::shared_ptr<Layer> &layer)
{
    if (!layer || !inline_text_editor_) return;
    if (!inline_text_layer_id_.empty() && inline_text_layer_id_ != layer->id)
        commit_text_edit(true);

    inline_text_layer_id_ = layer->id;
    updating_inline_text_editor_ = true;
    QSignalBlocker blocker(inline_text_editor_);
    configure_inline_text_editor(*layer);
    const double visual_scale = inline_text_visual_scale(*layer);
    if (!layer->rich_text.plain_text.empty() || !layer->rich_text.ranges.empty()) {
        populate_qtext_document_from_rich_text(inline_text_editor_->document(), layer->rich_text, visual_scale);
    } else if (!layer->rich_text_html.empty()) {
        inline_text_editor_->setHtml(scale_rich_text_font_sizes(QString::fromStdString(layer->rich_text_html), visual_scale));
    } else {
        populate_qtext_document_from_plain_layer_text(inline_text_editor_->document(), layer->text_content,
                                                      layer_char_format_for_editor(*layer), visual_scale);
    }
    if (inline_text_editor_->toPlainText().isEmpty())
        inline_text_editor_->setCurrentCharFormat(qtext_format_from_rich_text_format(layer_char_format_for_editor(*layer), visual_scale));
    inline_text_last_visual_scale_ = visual_scale;

    QTextCursor cursor = inline_text_editor_->textCursor();
    cursor.select(QTextCursor::Document);
    inline_text_editor_->setTextCursor(cursor);
    if (!layer->rich_text.empty()) {
        layer->rich_text.selection.anchor = 0;
        layer->rich_text.selection.head = layer->rich_text.plain_text.size();
    }
    position_text_editor();
    updating_inline_text_editor_ = false;
    inline_text_editor_->show();
    inline_text_editor_->raise();
    inline_text_editor_->setFocus(Qt::MouseFocusReason);
    if (auto *doc = inline_text_editor_->document())
        doc->setModified(false);
    emit text_edit_cursor_changed(layer->id);
    dirty_ = true;
    update();
}

void CanvasPreview::commit_text_edit(bool accept_changes)
{
    if (committing_inline_text_ || !inline_text_editor_ || inline_text_layer_id_.empty()) return;
    committing_inline_text_ = true;
    const std::string layer_id = inline_text_layer_id_;

    if (accept_changes)
        sync_inline_text_layer(true);

    inline_text_layer_id_.clear();
    inline_text_last_visual_scale_ = 0.0;
    inline_text_editor_->hide();
    {
        updating_inline_text_editor_ = true;
        QSignalBlocker blocker(inline_text_editor_);
        inline_text_editor_->clear();
        inline_text_editor_->setCurrentCharFormat(QTextCharFormat());
        inline_text_editor_->mergeCurrentCharFormat(QTextCharFormat());
        updating_inline_text_editor_ = false;
    }
    committing_inline_text_ = false;
    dirty_ = true;
    update();
    emit text_edit_committed(layer_id);
}

bool CanvasPreview::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == inline_text_editor_) {
        if (event->type() == QEvent::FocusOut) {
            return false;
        }
        if (event->type() == QEvent::KeyPress) {
            auto *key_event = static_cast<QKeyEvent *>(event);
            auto merge_char_format = [this](const QTextCharFormat &format) {
                QTextCursor cursor = inline_text_editor_->textCursor();
                cursor.mergeCharFormat(format);
                inline_text_editor_->mergeCurrentCharFormat(format);
                inline_text_editor_->setTextCursor(cursor);
                refresh_inline_text_edit(true, true);
            };
            if (key_event->key() == Qt::Key_B && key_event->modifiers().testFlag(Qt::ControlModifier)) {
                QTextCharFormat format;
                format.setFontWeight(inline_text_editor_->fontWeight() == QFont::Bold ? QFont::Normal : QFont::Bold);
                merge_char_format(format);
                key_event->accept();
                return true;
            }
            if (key_event->key() == Qt::Key_I && key_event->modifiers().testFlag(Qt::ControlModifier)) {
                QTextCharFormat format;
                format.setFontItalic(!inline_text_editor_->fontItalic());
                merge_char_format(format);
                key_event->accept();
                return true;
            }
            if (key_event->key() == Qt::Key_U && key_event->modifiers().testFlag(Qt::ControlModifier)) {
                QTextCharFormat format;
                format.setFontUnderline(!inline_text_editor_->fontUnderline());
                merge_char_format(format);
                key_event->accept();
                return true;
            }
            if (key_event->key() == Qt::Key_Escape) {
                commit_text_edit(true);
                key_event->accept();
                return true;
            }
            if (key_event->key() == Qt::Key_Return && key_event->modifiers().testFlag(Qt::ControlModifier)) {
                commit_text_edit(true);
                key_event->accept();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CanvasPreview::mouseDoubleClickEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton) {
        auto layer = text_layer_at_view_pos(ev->pos());
        if (layer) {
            emit layer_clicked(layer->id);
            begin_text_edit(layer);
            ev->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(ev);
}

void CanvasPreview::mousePressEvent(QMouseEvent *ev)
{
    if (!title_) return;

    if (!inline_text_layer_id_.empty()) {
        commit_text_edit(true);
        ev->accept();
        return;
    }

    setFocus(Qt::MouseFocusReason);

    if (ev->button() == Qt::MiddleButton) {
        panning_ = true;
        pan_start_view_ = QPointF(ev->pos());
        pan_start_offset_ = pan_offset_;
        setCursor(Qt::ClosedHandCursor);
        ev->accept();
        return;
    }

    if (ev->button() != Qt::LeftButton) return;

    if (active_tool_ == CanvasTool::ColorPicker) {
        update_color_picker_tooltip(ev->pos());
        if (color_picker_tooltip_visible_)
            emit color_picked(color_picker_tooltip_color_);
        ev->accept();
        return;
    }

    if (active_tool_ == CanvasTool::Text) {
        if (auto layer = text_layer_at_view_pos(ev->pos())) {
            emit layer_clicked(layer->id);
            begin_text_edit(layer);
            ev->accept();
            return;
        }
    }

    if (active_tool_ == CanvasTool::Shape || active_tool_ == CanvasTool::Text) {
        drawing_shape_ = true;
        drawing_shape_changed_ = false;
        drag_mode_ = DragMode::None;
        selected_layer_ids_.clear();
        sel_layer_id_.clear();
        shape_draw_start_canvas_ = snap_canvas_point(view_to_canvas(ev->pos()), true, true,
                                                     !ev->modifiers().testFlag(Qt::ControlModifier));
        shape_draw_current_canvas_ = shape_draw_start_canvas_;
        shape_draw_modifiers_ = ev->modifiers();
        shape_draw_current_rect_ = toolbar_draw_rect(shape_draw_current_canvas_, shape_draw_modifiers_);
        last_toolbar_preview_update_rect_ = toolbar_preview_update_rect();
        update(last_toolbar_preview_update_rect_.isEmpty() ? rect() : last_toolbar_preview_update_rect_);
        ev->accept();
        return;
    }

    drag_mode_ = hit_test_selected(ev->pos());
    if (drag_mode_ == DragMode::None) {
        QPointF canvas = view_to_canvas(ev->pos());
        for (auto it = title_->layers.rbegin(); it != title_->layers.rend(); ++it) {
            auto &l = *it;
            if (!l || !l->visible || l->locked) continue;
            if (playhead_ < l->in_time || playhead_ > l->out_time) continue;
            QPointF local = canvas_to_layer(*l, canvas);
            if (layer_local_rect(*l).contains(local)) {
                std::vector<std::string> next_ids;
                if (ev->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))
                    next_ids = selected_layer_ids_;
                auto existing = std::find(next_ids.begin(), next_ids.end(), l->id);
                if (ev->modifiers() & Qt::ControlModifier) {
                    if (existing == next_ids.end()) next_ids.push_back(l->id);
                    else next_ids.erase(existing);
                } else if (existing == next_ids.end()) {
                    if (!(ev->modifiers() & Qt::ShiftModifier)) next_ids.clear();
                    next_ids.push_back(l->id);
                }
                emit layers_selected(next_ids);
                selected_layer_ids_ = next_ids;
                sel_layer_id_ = selected_layer_ids_.empty() ? std::string() : selected_layer_ids_.back();
                drag_mode_ = DragMode::Move;
                break;
            }
        }
    }

    if (drag_mode_ == DragMode::None) {
        begin_marquee(ev->pos(), ev->modifiers());
        ev->accept();
        return;
    }

    drag_changed_ = false;
    alt_duplicate_pending_ = (drag_mode_ == DragMode::Move) && ev->modifiers().testFlag(Qt::AltModifier);
    alt_duplicate_done_ = false;
    drag_start_view_ = ev->pos();
    drag_current_view_ = ev->pos();
    drag_start_canvas_ = view_to_canvas(ev->pos());
    drag_layer_states_.clear();
    gradient_drag_ = GradientDragState{};
    corner_radius_drag_ = CornerRadiusDragState{};
    drag_start_selection_bounds_ = selected_canvas_bounds();

    auto layers = selected_layers();
    drag_text_object_scaling_ = false;
    auto layer = selected_layer();
    if (!layer && !layers.empty()) layer = layers.front();
    if (!layer) return;

    auto is_resize_drag = [](DragMode mode) {
        return mode == DragMode::ResizeNW || mode == DragMode::ResizeN || mode == DragMode::ResizeNE ||
               mode == DragMode::ResizeE || mode == DragMode::ResizeSE || mode == DragMode::ResizeS ||
               mode == DragMode::ResizeSW || mode == DragMode::ResizeW;
    };
    drag_text_object_scaling_ = is_resize_drag(drag_mode_) && ev->modifiers().testFlag(Qt::AltModifier) &&
        std::any_of(layers.begin(), layers.end(), [](const std::shared_ptr<Layer> &selected) {
            return selected && is_canvas_text_layer(*selected);
        });

    for (const auto &selected : layers) {
        if (!selected || selected->locked) continue;
        double lt = std::clamp(playhead_ - selected->in_time, 0.0,
                               std::max(0.0, selected->out_time - selected->in_time));
        drag_layer_states_.push_back({selected->id,
                                      selected->pos_x.evaluate(lt),
                                      selected->pos_y.evaluate(lt),
                                      (float)eval_box_width(*selected, lt),
                                      (float)eval_box_height(*selected, lt),
                                      selected->scale_x.evaluate(lt),
                                      selected->scale_y.evaluate(lt),
                                      selected->rotation.evaluate(lt)});
    }

    double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                           std::max(0.0, layer->out_time - layer->in_time));
    drag_start_x_ = layer->pos_x.evaluate(lt);
    drag_start_y_ = layer->pos_y.evaluate(lt);
    drag_start_w_ = (float)eval_box_width(*layer, lt);
    drag_start_h_ = (float)eval_box_height(*layer, lt);
    drag_start_origin_x_ = layer->origin_x;
    drag_start_origin_y_ = layer->origin_y;
    begin_gradient_drag(*layer);
    begin_corner_radius_drag(*layer);
    drag_rotation_pivot_canvas_ = layers.size() > 1
        ? drag_start_selection_bounds_.center()
        : layer_to_canvas(*layer, QPointF(0, 0));
    QPointF pivot_view = canvas_to_view(drag_rotation_pivot_canvas_);
    drag_start_rotation_angle_ = radians_to_degrees(std::atan2(drag_start_view_.y() - pivot_view.y(),
                                                               drag_start_view_.x() - pivot_view.x()));
    drag_current_rotation_delta_ = 0.0;
    auto set_cursor_for_mode = [this](DragMode mode) {
        if (mode == DragMode::Move) setCursor(Qt::ClosedHandCursor);
        else if (mode == DragMode::Origin) setCursor(Qt::CrossCursor);
        else if (mode == DragMode::Rotate) setCursor(canvas_rotation_cursor());
        else if (mode == DragMode::GradientCenter || mode == DragMode::GradientFocal) setCursor(Qt::CrossCursor);
        else if (mode == DragMode::GradientStart || mode == DragMode::GradientEnd ||
                 mode == DragMode::GradientRadius) setCursor(Qt::SizeAllCursor);
        else if (mode == DragMode::CornerRadiusTL || mode == DragMode::CornerRadiusTR ||
                 mode == DragMode::CornerRadiusBR || mode == DragMode::CornerRadiusBL) setCursor(Qt::SizeAllCursor);
        else if (mode == DragMode::ResizeN || mode == DragMode::ResizeS) setCursor(Qt::SizeVerCursor);
        else if (mode == DragMode::ResizeE || mode == DragMode::ResizeW) setCursor(Qt::SizeHorCursor);
        else if (mode == DragMode::ResizeNE || mode == DragMode::ResizeSW) setCursor(Qt::SizeBDiagCursor);
        else setCursor(Qt::SizeFDiagCursor);
    };
    set_cursor_for_mode(drag_mode_);
    ev->accept();
}

void CanvasPreview::mouseMoveEvent(QMouseEvent *ev)
{
    if (panning_ && (ev->buttons() & Qt::MiddleButton)) {
        pan_offset_ = pan_start_offset_ + (QPointF(ev->pos()) - pan_start_view_);
        fit_zoom_active_ = false;
        position_text_editor();
        update();
        ev->accept();
        return;
    }

    if (drawing_shape_ && (ev->buttons() & Qt::LeftButton)) {
        update_shape_drawing(ev->pos(), ev->modifiers());
        ev->accept();
        return;
    }

    if (drag_mode_ != DragMode::None && (ev->buttons() & Qt::LeftButton)) {
        apply_drag(ev->pos(), ev->modifiers());
        ev->accept();
        return;
    }

    if (active_tool_ == CanvasTool::Shape) {
        setCursor(Qt::CrossCursor);
        return;
    }
    if (active_tool_ == CanvasTool::Text) {
        setCursor(Qt::IBeamCursor);
        return;
    }
    if (active_tool_ == CanvasTool::ColorPicker) {
        update_color_picker_tooltip(ev->pos());
        setCursor(Qt::CrossCursor);
        return;
    }

    DragMode mode = hit_test_selected(ev->pos());
    if (mode == DragMode::Move) setCursor(Qt::OpenHandCursor);
    else if (mode == DragMode::Origin) setCursor(Qt::CrossCursor);
    else if (mode == DragMode::Rotate) setCursor(canvas_rotation_cursor());
    else if (mode == DragMode::GradientCenter || mode == DragMode::GradientFocal) setCursor(Qt::CrossCursor);
    else if (mode == DragMode::GradientStart || mode == DragMode::GradientEnd ||
             mode == DragMode::GradientRadius) setCursor(Qt::SizeAllCursor);
    else if (mode == DragMode::CornerRadiusTL || mode == DragMode::CornerRadiusTR ||
             mode == DragMode::CornerRadiusBR || mode == DragMode::CornerRadiusBL) setCursor(Qt::SizeAllCursor);
    else if (mode == DragMode::ResizeN || mode == DragMode::ResizeS) setCursor(Qt::SizeVerCursor);
    else if (mode == DragMode::ResizeE || mode == DragMode::ResizeW) setCursor(Qt::SizeHorCursor);
    else if (mode == DragMode::ResizeNE || mode == DragMode::ResizeSW) setCursor(Qt::SizeBDiagCursor);
    else if (mode != DragMode::None) setCursor(Qt::SizeFDiagCursor);
    else unsetCursor();
}

void CanvasPreview::leaveEvent(QEvent *ev)
{
    if (color_picker_tooltip_visible_) {
        color_picker_tooltip_visible_ = false;
        update();
    }
    QWidget::leaveEvent(ev);
}

void CanvasPreview::keyPressEvent(QKeyEvent *ev)
{
    if (!inline_text_layer_id_.empty() && ev->key() == Qt::Key_Escape) {
        commit_text_edit(true);
        ev->accept();
        return;
    }

    double dx = 0.0;
    double dy = 0.0;
    const double amount = ev->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;

    switch (ev->key()) {
    case Qt::Key_Left:
        dx = -amount;
        break;
    case Qt::Key_Right:
        dx = amount;
        break;
    case Qt::Key_Up:
        dy = -amount;
        break;
    case Qt::Key_Down:
        dy = amount;
        break;
    default:
        QWidget::keyPressEvent(ev);
        return;
    }

    if (nudge_selected_layers(dx, dy))
        ev->accept();
    else
        QWidget::keyPressEvent(ev);
}

void CanvasPreview::mouseReleaseEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::MiddleButton && panning_) {
        panning_ = false;
        unsetCursor();
        ev->accept();
        return;
    }

    if (ev->button() == Qt::LeftButton && drawing_shape_) {
        const QRect old_update_rect = last_toolbar_preview_update_rect_;
        const QPointF release_canvas = view_to_canvas(ev->pos());
        const bool dragged_far_enough = QLineF(shape_draw_start_canvas_, release_canvas).length() >= 2.0;
        if (drawing_shape_changed_ || dragged_far_enough)
            update_shape_drawing(ev->pos(), ev->modifiers());

        const QRect repaint_rect = old_update_rect.united(last_toolbar_preview_update_rect_);
        const QRectF final_rect = shape_draw_current_rect_.isValid()
                                    ? shape_draw_current_rect_.normalized()
                                    : toolbar_draw_rect(shape_draw_current_canvas_, shape_draw_modifiers_);
        const bool has_drawn_size = drawing_shape_changed_ || dragged_far_enough;
        const bool was_text_tool = active_tool_ == CanvasTool::Text;
        const ShapeType final_shape_type = active_shape_type_;
        const LayerType final_text_type = active_text_layer_type_;
        const QPointF start_canvas = shape_draw_start_canvas_;

        drawing_shape_ = false;
        drawing_shape_changed_ = false;
        shape_draw_current_rect_ = QRectF();
        last_toolbar_preview_update_rect_ = QRect();
        clear_snap_feedback();
        update(repaint_rect.isEmpty() ? rect() : repaint_rect);

        if (was_text_tool)
            emit text_drawing_started(final_text_type, start_canvas);
        else
            emit shape_drawing_started(final_shape_type, start_canvas);
        if (has_drawn_size)
            emit shape_drawing_changed(final_rect);
        emit shape_drawing_finished(true);

        setCursor(active_tool_ == CanvasTool::Shape ? Qt::CrossCursor :
                  (active_tool_ == CanvasTool::Text ? Qt::IBeamCursor :
                   (active_tool_ == CanvasTool::ColorPicker ? Qt::CrossCursor : Qt::ArrowCursor)));
        ev->accept();
        return;
    }

    if (ev->button() != Qt::LeftButton || drag_mode_ == DragMode::None) return;

    if (drag_mode_ == DragMode::Marquee) {
        update_marquee(ev->pos(), ev->modifiers());
        if (!marquee_active_)
            emit layers_selected(std::vector<std::string>{});
        drag_mode_ = DragMode::None;
        marquee_active_ = false;
        drag_changed_ = false;
        alt_duplicate_pending_ = false;
        alt_duplicate_done_ = false;
        drag_text_object_scaling_ = false;
        gradient_drag_ = GradientDragState{};
        corner_radius_drag_ = CornerRadiusDragState{};
        clear_snap_feedback();
        unsetCursor();
        update();
        ev->accept();
        return;
    }

    bool changed = drag_changed_;
    drag_mode_ = DragMode::None;
    drag_changed_ = false;
    alt_duplicate_pending_ = false;
    alt_duplicate_done_ = false;
    drag_text_object_scaling_ = false;
    gradient_drag_ = GradientDragState{};
    corner_radius_drag_ = CornerRadiusDragState{};
    drag_layer_states_.clear();
    clear_snap_feedback();
    unsetCursor();
    if (changed)
        emit layer_geometry_changed();
    ev->accept();
}


void CanvasPreview::wheelEvent(QWheelEvent *ev)
{
    if (!title_) return;
    QPointF anchor_canvas = view_to_canvas(ev->position());
    int next = zoom_percent_;
    if (ev->angleDelta().y() > 0)
        next = (int)std::round(next * 1.1);
    else
        next = (int)std::round(next / 1.1);
    zoom_percent_ = std::clamp(next, 5, 1600);
    fit_zoom_active_ = false;
    QPointF origin_without_pan = centered_view_origin();
    double scale = view_scale();
    pan_offset_ = ev->position() - origin_without_pan - QPointF(anchor_canvas.x() * scale, anchor_canvas.y() * scale);
    emit zoom_percent_changed(zoom_percent_);
    position_text_editor();
    update();
    ev->accept();
}

void CanvasPreview::resizeEvent(QResizeEvent *)
{
    dirty_ = true;
    if (fit_zoom_active_) fit_canvas(fit_zoom_up_to_100_);
    position_text_editor();
}

/* ══════════════════════════════════════════════════════════════════
 *  LayerStack
 * ══════════════════════════════════════════════════════════════════ */
LayerStack::LayerStack(QWidget *parent) : QWidget(parent)
{
    const QPalette pal = qApp->palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor base = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor disabled_text = pal.color(QPalette::Disabled, QPalette::WindowText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor button = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor hover = button.lightness() < 128 ? button.lighter(125) : button.darker(108);
    setStyleSheet(QStringLiteral("background:%1;color:%2;")
                      .arg(window.name(QColor::HexRgb),
                           text.name(QColor::HexRgb)));
    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);


    QWidget *columns = new QWidget(this);
    columns->setFixedHeight(72);
    columns->setStyleSheet(QStringLiteral("background:%1;border-top:1px solid %2;border-bottom:1px solid %2;")
                                .arg(window.lightness() < 128 ? window.darker(112).name(QColor::HexRgb)
                                                              : window.darker(104).name(QColor::HexRgb),
                                     border.name(QColor::HexRgb)));
    auto *ch = new QHBoxLayout(columns);
    ch->setContentsMargins(4, 0, 4, 0);
    ch->setSpacing(4);
    auto add_header = [&](const QString &txt, int w, Qt::Alignment align = Qt::AlignCenter) {
        QLabel *label = new QLabel(txt, columns);
        label->setFixedWidth(w);
        label->setAlignment(align);
        label->setStyleSheet(QStringLiteral("color:%1;font-size:10px;font-weight:bold;")
                                 .arg(disabled_text.name(QColor::HexRgb)));
        ch->addWidget(label);
    };
    add_header("◉", 20);
    add_header("🔒", 20);
    add_header("", 12);
    add_header("#", 24);
    QLabel *name = new QLabel(obsgs_tr("OBSTitles.LayerNameHeader"), columns);
    name->setStyleSheet(QStringLiteral("color:%1;font-size:10px;font-weight:bold;")
                            .arg(disabled_text.name(QColor::HexRgb)));
    ch->addWidget(name, 1);
    add_header(obsgs_tr("OBSTitles.ModeHeader"), 46, Qt::AlignLeft | Qt::AlignVCenter);
    add_header(obsgs_tr("OBSTitles.ParentHeader"), 58, Qt::AlignLeft | Qt::AlignVCenter);
    vl->addWidget(columns);

    list_ = new QListWidget(this);
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setAlternatingRowColors(false);
    list_->setUniformItemSizes(false);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setStyleSheet(QStringLiteral(
        "QListWidget{background:%1;border:none;color:%2;}"
        "QListWidget::item{border-bottom:1px solid %3;}"
        "QListWidget::item:selected{background:%4;color:%5;}"
        "QListWidget::item:hover{background:%6;}")
        .arg(window.name(QColor::HexRgb),
             text.name(QColor::HexRgb),
             border.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb),
             pal.color(QPalette::HighlightedText).name(QColor::HexRgb),
             hover.name(QColor::HexRgb)));
    vl->addWidget(list_, 1);

    auto *toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setOrientation(Qt::Horizontal);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toolbar->setStyleSheet(QStringLiteral(
        "QToolBar{background:%1;border-top:1px solid %2;spacing:2px;}"
        "QToolButton{color:%3;background:transparent;border:none;padding:3px;}"
        "QToolButton:hover{background:%4;border-radius:2px;}"
        "QToolButton:disabled{color:%5;}")
        .arg(window.name(QColor::HexRgb),
             border.name(QColor::HexRgb),
             button_text.name(QColor::HexRgb),
             hover.name(QColor::HexRgb),
             disabled_text.name(QColor::HexRgb)));

    auto make_layer_tool = [&](const QString &text, const QIcon &icon, const QString &tip) {
        auto *button = new QToolButton(toolbar);
        button->setText(text);
        button->setAccessibleName(text);
        button->setToolTip(tip);
        button->setIcon(icon);
        button->setIconSize(QSize(16, 16));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::StrongFocus);
        return button;
    };

    btn_add_ = make_layer_tool(obsgs_tr("OBSTitles.AddLayer"),
                               obs_icon("add.svg"),
                               obsgs_tr("OBSTitles.AddLayerTooltip"));
    auto *add_menu = new QMenu(btn_add_);
    add_menu->addAction(obs_icon("text.svg"),
                        obsgs_tr("OBSTitles.Text"), this, &LayerStack::on_add_text);
    add_menu->addAction(obs_icon("clock.svg"),
                        obsgs_tr("OBSTitles.Clock"), this, &LayerStack::on_add_clock);
    add_menu->addAction(obs_icon("text.svg"),
                        obsgs_tr("OBSTitles.Ticker"), this, &LayerStack::on_add_ticker);
    add_menu->addAction(obs_icon("shape.svg"),
                        obsgs_tr("OBSTitles.Shape"), this, &LayerStack::on_add_rect);
    add_menu->addAction(obs_icon("image.svg"),
                        obsgs_tr("OBSTitles.Image"), this, &LayerStack::on_add_image);
    btn_add_->setMenu(add_menu);
    btn_add_->setPopupMode(QToolButton::InstantPopup);
    btn_add_->setStyleSheet(QStringLiteral("QToolButton::menu-indicator{image:none;width:0px;}"));

    btn_move_up_ = make_layer_tool(obsgs_tr("OBSTitles.MoveLayerUp"),
                                   obs_icon("move-up.svg"),
                                   obsgs_tr("OBSTitles.MoveLayerUpTooltip"));
    btn_move_down_ = make_layer_tool(obsgs_tr("OBSTitles.MoveLayerDown"),
                                     obs_icon("move-down.svg"),
                                     obsgs_tr("OBSTitles.MoveLayerDownTooltip"));
    btn_del_ = make_layer_tool(obsgs_tr("OBSTitles.DeleteLayer"),
                               obs_icon("delete.svg"),
                               obsgs_tr("OBSTitles.DeleteLayerTooltip"));
    btn_move_up_->setEnabled(false);
    btn_move_down_->setEnabled(false);
    btn_del_->setEnabled(false);

    toolbar->addWidget(btn_add_);
    toolbar->addWidget(btn_move_up_);
    toolbar->addWidget(btn_move_down_);
    toolbar->addSeparator();
    toolbar->addWidget(btn_del_);
    vl->addWidget(toolbar);

    connect(btn_move_up_, &QToolButton::clicked, this, &LayerStack::on_move_up);
    connect(btn_move_down_, &QToolButton::clicked, this, &LayerStack::on_move_down);
    connect(btn_del_, &QToolButton::clicked, this, &LayerStack::on_delete);
    connect(list_, &QListWidget::itemSelectionChanged,
            this, &LayerStack::on_selection_changed);
    connect(list_->model(), &QAbstractItemModel::rowsMoved,
            this, [this]() { sync_order_from_list(); });
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QListWidget::customContextMenuRequested,
            this, &LayerStack::show_layer_context_menu);
    list_->viewport()->installEventFilter(this);
}

bool LayerStack::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == list_->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *mouse_event = static_cast<QMouseEvent *>(event);
        if (mouse_event->button() == Qt::LeftButton && !list_->itemAt(mouse_event->pos())) {
            QSignalBlocker blocker(list_);
            list_->clearSelection();
            list_->setCurrentItem(nullptr);
            emit layer_selected(std::string());
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LayerStack::set_title(std::shared_ptr<Title> t)
{
    title_ = t; populate();
}

void LayerStack::refresh() { populate(); }

void LayerStack::set_layer_clipboard_available(bool available)
{
    layer_clipboard_available_ = available;
}

QScrollBar *LayerStack::vertical_scroll_bar() const
{
    return list_ ? list_->verticalScrollBar() : nullptr;
}

void LayerStack::sync_order_from_list()
{
    if (!title_) return;

    std::vector<std::shared_ptr<Layer>> reordered;
    reordered.reserve(title_->layers.size());
    for (int i = list_->count() - 1; i >= 0; --i) {
        auto *item = list_->item(i);
        if (item->data(Qt::UserRole + 1).toString() == "property")
            continue;
        std::string id = item->data(Qt::UserRole).toString().toStdString();
        if (auto layer = title_->find_layer(id))
            reordered.push_back(layer);
    }
    if (reordered.size() == title_->layers.size()) {
        title_->layers = std::move(reordered);
        emit layer_order_changed();
    }
}

void LayerStack::populate()
{
    QString prev_id = list_->currentItem()
        ? list_->currentItem()->data(Qt::UserRole).toString()
        : QString();

    list_->blockSignals(true);
    list_->clear();
    if (!title_) { list_->blockSignals(false); return; }

    const QPalette pal = palette();
    const QColor text = pal.color(QPalette::WindowText);
    const QColor field_text = pal.color(QPalette::Text);
    const QColor button = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor disabled_text = pal.color(QPalette::Disabled, QPalette::WindowText);
    const QColor base = pal.color(QPalette::Base);
    const QColor border = pal.color(QPalette::Mid);
    const QColor dark = pal.color(QPalette::Dark);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor hover = button.lightness() < 128 ? button.lighter(125) : button.darker(108);
    const QString button_style = QStringLiteral(
        "QToolButton{color:%1;background:transparent;border:none;}"
        "QToolButton:hover{background:%2;border-radius:2px;}"
        "QToolButton:checked{color:%3;}")
        .arg(button_text.name(QColor::HexRgb),
             hover.name(QColor::HexRgb),
             text.name(QColor::HexRgb));
    const QString combo_style = QStringLiteral(
        "QComboBox{color:%1;background:%2;border:none;border-radius:3px;padding-left:4px;}"
        "QComboBox::drop-down{border:none;}"
        "QComboBox QAbstractItemView{background:%2;color:%1;selection-background-color:%3;selection-color:%4;}")
        .arg(field_text.name(QColor::HexRgb),
             base.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb),
             pal.color(QPalette::HighlightedText).name(QColor::HexRgb));
    const QString label_chip_style = QStringLiteral("color:%1;background:%2;border-radius:3px;padding-left:4px;")
                                         .arg(field_text.name(QColor::HexRgb),
                                              base.name(QColor::HexRgb));

    int row = 0;
    for (auto it = title_->layers.rbegin(); it != title_->layers.rend(); ++it, ++row) {
        auto &l = *it;
        auto *item = new QListWidgetItem();
        item->setData(Qt::UserRole, QString::fromStdString(l->id));
        item->setData(Qt::UserRole + 1, "layer");
        item->setFlags((item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                        Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled) & ~Qt::ItemIsUserCheckable);
        item->setSizeHint(QSize(0, 28));
        list_->addItem(item);

        QWidget *row_widget = new QWidget(list_);
        row_widget->setStyleSheet(QStringLiteral("background:transparent;color:%1;")
                                      .arg(text.name(QColor::HexRgb)));
        auto *hl = new QHBoxLayout(row_widget);
        hl->setContentsMargins(4, 0, 4, 0);
        hl->setSpacing(4);

        auto make_toggle = [&](const char *on_icon, const char *off_icon, bool checked,
                               const QString &tip) {
            auto *btn = new QToolButton(row_widget);
            btn->setCheckable(true);
            btn->setChecked(checked);
            btn->setIcon(obs_icon(checked ? on_icon : off_icon));
            btn->setToolTip(tip);
            btn->setFixedSize(20, 20);
            btn->setIconSize(QSize(14, 14));
            btn->setAutoRaise(true);
            btn->setStyleSheet(button_style);
            connect(btn, &QToolButton::toggled, btn, [btn, on_icon, off_icon](bool state) {
                btn->setIcon(obs_icon(state ? on_icon : off_icon));
            });
            hl->addWidget(btn);
            return btn;
        };

        QToolButton *vis = make_toggle("layer-visible.svg", "layer-hidden.svg", l->visible, obsgs_tr("OBSTitles.LayerVisibilityTooltip"));
        QToolButton *lock = make_toggle("layer-lock.svg", "layer-unlock.svg", l->locked, obsgs_tr("OBSTitles.LockLayerTooltip"));
        connect(vis, &QToolButton::toggled, this, [this, id = l->id, item](bool checked) {
            list_->setCurrentItem(item);
            emit layer_visibility_changed(id, checked);
        });
        connect(lock, &QToolButton::toggled, this, [this, id = l->id, item](bool checked) {
            list_->setCurrentItem(item);
            emit layer_lock_changed(id, checked);
        });

        QToolButton *expand = new QToolButton(row_widget);
        expand->setCheckable(true);
        expand->setChecked(l->properties_expanded);
        expand->setIcon(obs_icon(l->properties_expanded ? "keyframes-expand.svg" : "keyframes-collapse.svg"));
        expand->setToolTip(obsgs_tr("OBSTitles.ShowKeyframedPropertiesTooltip"));
        expand->setFixedSize(16, 20);
        expand->setIconSize(QSize(12, 12));
        expand->setAutoRaise(true);
        expand->setStyleSheet(button_style);
        connect(expand, &QToolButton::toggled, this, [this, expand, id = l->id](bool checked) {
            expand->setIcon(obs_icon(checked ? "keyframes-expand.svg" : "keyframes-collapse.svg"));
            emit layer_expand_changed(id, checked);
        });
        hl->addWidget(expand);

        QLabel *idx = new QLabel(QString::number(row + 1), row_widget);
        idx->setFixedWidth(24);
        idx->setAlignment(Qt::AlignCenter);
        idx->setStyleSheet(QStringLiteral("color:%1;font-weight:bold;")
                               .arg(disabled_text.name(QColor::HexRgb)));
        hl->addWidget(idx);

        QLabel *type = new QLabel(layer_type_short(l->type), row_widget);
        type->setFixedWidth(18);
        type->setAlignment(Qt::AlignCenter);
        type->setStyleSheet(QStringLiteral("background:%1;border:1px solid %2;color:%3;font-weight:bold;")
                                .arg(layer_color(*l, row).name(QColor::HexRgb),
                                     dark.name(QColor::HexRgb),
                                     pal.color(QPalette::HighlightedText).name(QColor::HexRgb)));
        hl->addWidget(type);

        QLineEdit *name = new QLineEdit(QString::fromStdString(l->name), row_widget);
        name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        name->setFrame(false);
        name->setReadOnly(l->locked);
        name->setToolTip(obsgs_tr("OBSTitles.RenameLayerTooltip"));
        name->setStyleSheet(l->locked
            ? QStringLiteral("QLineEdit{color:%1;background:transparent;border:none;}")
                  .arg(disabled_text.name(QColor::HexRgb))
            : QStringLiteral("QLineEdit{color:%1;background:transparent;border:none;padding:1px;} "
                             "QLineEdit:focus{background:%2;border:1px solid %3;border-radius:2px;}")
                  .arg(text.name(QColor::HexRgb),
                       base.name(QColor::HexRgb),
                       highlight.name(QColor::HexRgb)));
        connect(name, &QLineEdit::editingFinished, this, [this, id = l->id, name]() {
            emit layer_name_changed(id, name->text().trimmed().toStdString());
        });
        hl->addWidget(name, 1);

        QLabel *mode = new QLabel(obsgs_tr("OBSTitles.Normal"), row_widget);
        mode->setFixedWidth(54);
        mode->setStyleSheet(label_chip_style);
        hl->addWidget(mode);

        QComboBox *parent = new QComboBox(row_widget);
        parent->setFixedWidth(86);
        parent->setStyleSheet(combo_style);
        parent->addItem(obsgs_tr("OBSTitles.None"), "");
        for (const auto &candidate : title_->layers) {
            if (candidate->id == l->id) continue;
            parent->addItem(QString::fromStdString(candidate->name), QString::fromStdString(candidate->id));
        }
        int parent_idx = parent->findData(QString::fromStdString(l->parent_id));
        parent->setCurrentIndex(parent_idx >= 0 ? parent_idx : 0);
        connect(parent, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, id = l->id, parent](int index) {
                    emit layer_parent_changed(id, parent->itemData(index).toString().toStdString());
                });
        hl->addWidget(parent);

        QComboBox *mask = new QComboBox(row_widget);
        mask->setFixedWidth(112);
        mask->setStyleSheet(combo_style);
        mask->addItem("No Mask", QVariant(QStringLiteral("|0")));
        for (const auto &candidate : title_->layers) {
            if (candidate->id == l->id) continue;
            mask->addItem(QString::fromStdString(candidate->name + " α"),
                          QString::fromStdString(candidate->id + "|" + std::to_string((int)MaskMode::Alpha)));
            mask->addItem(QString::fromStdString(candidate->name + " -α"),
                          QString::fromStdString(candidate->id + "|" + std::to_string((int)MaskMode::InvertedAlpha)));
        }
        QString mask_value = QString::fromStdString(l->mask_source_id + "|" + std::to_string((int)l->mask_mode));
        int mask_idx = mask->findData(mask_value);
        mask->setCurrentIndex(mask_idx >= 0 ? mask_idx : 0);
        connect(mask, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, id = l->id, mask](int index) {
                    const QStringList parts = mask->itemData(index).toString().split('|');
                    const std::string source_id = parts.value(0).toStdString();
                    const MaskMode mode = (MaskMode)parts.value(1).toInt();
                    emit layer_mask_changed(id, source_id, mode);
                });
        hl->addWidget(mask);

        list_->setItemWidget(item, row_widget);
        if ((prev_id.isEmpty() && list_->currentItem() == nullptr) ||
            prev_id == item->data(Qt::UserRole).toString())
            list_->setCurrentItem(item);

        if (!l->properties_expanded) continue;

        std::set<std::string> seen;
        for (auto *prop : timeline_properties(*l)) {
            if (!prop->is_animated()) continue;
            QString label = property_label(prop->name);
            std::string key = label.toStdString();
            if (!seen.insert(key).second) continue;

            auto *prop_item = new QListWidgetItem();
            prop_item->setData(Qt::UserRole, QString::fromStdString(l->id));
            prop_item->setData(Qt::UserRole + 1, "property");
            prop_item->setData(Qt::UserRole + 2, label);
            prop_item->setFlags((prop_item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled) &
                                ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsUserCheckable));
            prop_item->setSizeHint(QSize(0, 28));
            list_->addItem(prop_item);

            QWidget *prop_widget = new QWidget(list_);
            auto *ph = new QHBoxLayout(prop_widget);
            ph->setContentsMargins(64, 0, 4, 0);
            ph->setSpacing(4);
            QLabel *diamond_indicator = new QLabel("◇", prop_widget);
            diamond_indicator->setFixedWidth(18);
            diamond_indicator->setAlignment(Qt::AlignCenter);
            diamond_indicator->setStyleSheet(QString("color:%1;").arg(layer_color(*l, row).name()));
            ph->addWidget(diamond_indicator);
            QLabel *prop_name = new QLabel(label, prop_widget);
            prop_name->setStyleSheet(QStringLiteral("color:%1;").arg(text.name(QColor::HexRgb)));
            ph->addWidget(prop_name, 1);
            QLabel *value = new QLabel(property_value_text(*prop, *l), prop_widget);
            value->setFixedWidth(95);
            value->setStyleSheet(QStringLiteral("color:%1;font-family:monospace;")
                                     .arg(highlight.name(QColor::HexRgb)));
            ph->addWidget(value);
            list_->setItemWidget(prop_item, prop_widget);
        }
    }
    list_->blockSignals(false);
    on_selection_changed();
}

void LayerStack::set_selected_layer(const std::string &layer_id)
{
    set_selected_layers(layer_id.empty() ? std::vector<std::string>()
                                         : std::vector<std::string>{layer_id});
}

void LayerStack::set_selected_layers(const std::vector<std::string> &layer_ids)
{
    QSignalBlocker blocker(list_);
    list_->clearSelection();
    if (layer_ids.empty()) {
        list_->setCurrentItem(nullptr);
        return;
    }

    std::set<QString> ids;
    for (const auto &id : layer_ids)
        ids.insert(QString::fromStdString(id));

    QListWidgetItem *current = nullptr;
    QString primary = QString::fromStdString(layer_ids.back());
    for (int i = 0; i < list_->count(); ++i) {
        auto *item = list_->item(i);
        if (item->data(Qt::UserRole + 1).toString() != "layer") continue;
        QString id = item->data(Qt::UserRole).toString();
        if (ids.find(id) != ids.end()) {
            item->setSelected(true);
            if (id == primary) current = item;
        }
    }
    if (current) list_->setCurrentItem(current, QItemSelectionModel::NoUpdate);
}

std::string LayerStack::selected_id() const
{
    auto *item = list_->currentItem();
    if (item && item->isSelected())
        return item->data(Qt::UserRole).toString().toStdString();
    auto selected = list_->selectedItems();
    return selected.isEmpty() ? std::string() : selected.back()->data(Qt::UserRole).toString().toStdString();
}

std::vector<std::string> LayerStack::selected_ids() const
{
    std::vector<std::string> ids;
    for (auto *item : list_->selectedItems()) {
        if (item->data(Qt::UserRole + 1).toString() != "layer") continue;
        ids.push_back(item->data(Qt::UserRole).toString().toStdString());
    }
    return ids;
}

void LayerStack::on_selection_changed()
{
    std::string id = selected_id();
    const bool has_layer = !id.empty() && title_ && title_->find_layer(id);
    if (btn_del_) btn_del_->setEnabled(has_layer);

    bool can_move_up = false;
    bool can_move_down = false;
    if (has_layer) {
        auto selected = selected_ids();
        if (selected.size() > 1)
            emit layers_selected(selected);
        auto it = std::find_if(title_->layers.begin(), title_->layers.end(),
                               [&](const auto &layer) { return layer && layer->id == id; });
        if (it != title_->layers.end()) {
            int idx = (int)std::distance(title_->layers.begin(), it);
            can_move_down = idx > 0;
            can_move_up = idx < (int)title_->layers.size() - 1;
        }
        if (selected.size() <= 1)
            emit layer_selected(id);
    }
    if (!has_layer)
        emit layer_selected(std::string());

    if (btn_move_up_) btn_move_up_->setEnabled(can_move_up);
    if (btn_move_down_) btn_move_down_->setEnabled(can_move_down);
}

void LayerStack::on_add_text() { emit add_layer_requested(LayerType::Text); }
void LayerStack::on_add_clock() { emit add_layer_requested(LayerType::Clock); }
void LayerStack::on_add_ticker() { emit add_layer_requested(LayerType::Ticker); }
void LayerStack::on_add_rect() { emit add_layer_requested(LayerType::Shape); }
void LayerStack::on_add_image() { emit add_layer_requested(LayerType::Image); }

void LayerStack::on_move_up()
{
    std::string id = selected_id();
    if (!title_ || id.empty()) return;
    auto layer = title_->find_layer(id);
    if (!layer) return;
    title_->move_layer(id, +1);
    emit layer_order_changed();
    set_selected_layer(id);
}

void LayerStack::on_move_down()
{
    std::string id = selected_id();
    if (!title_ || id.empty()) return;
    auto layer = title_->find_layer(id);
    if (!layer) return;
    title_->move_layer(id, -1);
    emit layer_order_changed();
    set_selected_layer(id);
}

void LayerStack::on_delete()
{
    std::string id = selected_id();
    if (!id.empty()) emit delete_layer_requested(id);
}

void LayerStack::show_layer_context_menu(const QPoint &pos)
{
    if (!title_) return;

    QListWidgetItem *item = list_->itemAt(pos);
    auto style_menu = [this](QMenu *menu) {
        if (!menu) return;
        const QPalette pal = palette();
        menu->setStyleSheet(QStringLiteral(
            "QMenu{color:%1;background:%2;border:1px solid %3;}"
            "QMenu::item{padding:5px 22px;}"
            "QMenu::item:selected{background:%4;color:%5;}"
            "QMenu::item:disabled{color:%6;}")
            .arg(pal.color(QPalette::Text).name(QColor::HexRgb),
                 pal.color(QPalette::Base).name(QColor::HexRgb),
                 pal.color(QPalette::Mid).name(QColor::HexRgb),
                 pal.color(QPalette::Highlight).name(QColor::HexRgb),
                 pal.color(QPalette::HighlightedText).name(QColor::HexRgb),
                 pal.color(QPalette::Disabled, QPalette::Text).name(QColor::HexRgb)));
    };
    if (!item) {
        if (btn_add_ && btn_add_->menu()) {
            style_menu(btn_add_->menu());
            btn_add_->menu()->exec(list_->viewport()->mapToGlobal(pos));
        }
        return;
    }

    std::string id = item ? item->data(Qt::UserRole).toString().toStdString() : selected_id();
    if (id.empty()) return;

    if (item && item->data(Qt::UserRole + 1).toString() == "layer" && !item->isSelected())
        list_->setCurrentItem(item);

    QMenu menu(this);
    style_menu(&menu);
    QAction *clone = menu.addAction(obsgs_tr("OBSTitles.CloneLayer"));
    QAction *copy = menu.addAction(obsgs_tr("OBSTitles.CopyLayer"));
    QAction *paste = menu.addAction(obsgs_tr("OBSTitles.PasteLayer"));
    paste->setEnabled(layer_clipboard_available_);
    menu.addSeparator();
    QAction *del = menu.addAction(obsgs_tr("OBSTitles.DeleteLayer"));

    QAction *chosen = menu.exec(list_->viewport()->mapToGlobal(pos));
    if (chosen == clone) emit clone_layer_requested(id);
    else if (chosen == copy) emit copy_layer_requested(id);
    else if (chosen == paste) emit paste_layer_requested(id);
    else if (chosen == del) emit delete_layer_requested(id);
}

void LayerStack::on_item_changed(QListWidgetItem *item)
{
    std::string id = item->data(Qt::UserRole).toString().toStdString();
    bool v = (item->checkState() == Qt::Checked);
    emit layer_visibility_changed(id, v);
}

/* ══════════════════════════════════════════════════════════════════
 *  TimelineWidget
 * ══════════════════════════════════════════════════════════════════ */
TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void TimelineWidget::set_title(std::shared_ptr<Title> t)
{
    const bool title_changed = t != title_;
    title_ = t;
    if (title_changed) {
        scroll_x_ = 0;
        fit_on_next_resize_ = true;
        selected_keyframes_.clear();
    } else {
        prune_keyframe_selection();
    }
    clamp_scroll();
    clamp_vertical_scroll();
    if (fit_on_next_resize_ && width() > 40) {
        fit_on_next_resize_ = false;
        fit_timeline();
        return;
    }
    update();
}

void TimelineWidget::set_selected_layer(const std::string &lid)
{
    sel_layer_id_ = lid; update();
}

void TimelineWidget::set_playhead(double t)
{
    playhead_ = snap_time(t);
    if (title_)
        keep_playhead_visible();
    update();
}

void TimelineWidget::set_vertical_scroll(int scroll_y)
{
    scroll_y_ = scroll_y;
    clamp_vertical_scroll();
    update();
}

void TimelineWidget::set_pixels_per_sec(double pixels_per_sec, double anchor_time, int anchor_x)
{
    pixels_per_sec_ = std::clamp(pixels_per_sec, 5.0, 1200.0);
    scroll_x_ = (int)std::round(anchor_time * pixels_per_sec_) - anchor_x;
    clamp_scroll();
    keep_playhead_visible();
    update();
    emit zoom_percent_changed(zoom_percent());
}

void TimelineWidget::set_zoom_percent(int percent)
{
    int clamped = std::clamp(percent, 5, 1200);
    double anchor_time = title_ ? std::clamp(playhead_, 0.0, title_->duration) : playhead_;
    int anchor_x = std::clamp(time_to_x(anchor_time), 24, std::max(24, width() - 24));
    set_pixels_per_sec((double)clamped, anchor_time, anchor_x);
}

int TimelineWidget::zoom_percent() const
{
    return (int)std::round(pixels_per_sec_);
}

void TimelineWidget::fit_timeline()
{
    double dur = title_ ? std::max(obs_frame_duration(), title_->duration) : 10.0;
    double fitted = (double)std::max(1, width() - 40) / dur;
    set_pixels_per_sec(fitted, 0.0, 0);
}

bool TimelineWidget::has_selected_keyframes() const
{
    return title_ && !selected_keyframes_.empty();
}

bool TimelineWidget::has_keyframe_clipboard() const
{
    return !keyframe_clipboard_.empty();
}

bool TimelineWidget::copy_keyframe_selection()
{
    return copy_selected_keyframes();
}

bool TimelineWidget::cut_keyframe_selection()
{
    if (!cut_selected_keyframes()) return false;
    emit keyframe_easing_changed();
    return true;
}

bool TimelineWidget::delete_keyframe_selection()
{
    if (!delete_selected_keyframes()) return false;
    emit keyframe_easing_changed();
    return true;
}

bool TimelineWidget::paste_keyframes_at_playhead()
{
    if (!paste_keyframes_at(playhead_)) return false;
    emit keyframe_easing_changed();
    return true;
}

bool TimelineWidget::keep_playhead_visible()
{
    if (!title_) return false;
    int phx = time_to_x(playhead_);
    int old_scroll = scroll_x_;
    if (phx < 24)
        scroll_x_ = std::max(0, (int)std::round(playhead_ * pixels_per_sec_) - 24);
    if (phx > width() - 24)
        scroll_x_ = std::max(0, (int)std::round(playhead_ * pixels_per_sec_) - width() + 24);
    clamp_scroll();
    return old_scroll != scroll_x_;
}

double TimelineWidget::x_to_time(int x) const
{
    return snap_time((x + scroll_x_) / pixels_per_sec_);
}

int TimelineWidget::time_to_x(double t) const
{
    return (int)std::round(t * pixels_per_sec_) - scroll_x_;
}

double TimelineWidget::snap_time(double t) const
{
    return snap_to_obs_frame(t);
}

void TimelineWidget::clamp_scroll()
{
    double dur = title_ ? title_->duration : 10.0;
    int max_scroll = std::max(0, (int)std::ceil(dur * pixels_per_sec_) - width() + 40);
    scroll_x_ = std::clamp(scroll_x_, 0, max_scroll);
}

int TimelineWidget::max_vertical_scroll() const
{
    int content_height = (int)timeline_rows(title_).size() * row_height();
    int viewport_height = std::max(0, height() - ruler_height());
    return std::max(0, content_height - viewport_height);
}

void TimelineWidget::clamp_vertical_scroll()
{
    scroll_y_ = std::clamp(scroll_y_, 0, max_vertical_scroll());
}


bool TimelineWidget::KeyframeRef::operator<(const KeyframeRef &other) const
{
    return std::tie(layer_id, prop_name, index) <
           std::tie(other.layer_id, other.prop_name, other.index);
}

AnimatedProperty *TimelineWidget::find_timeline_property(Layer &layer, const std::string &prop_name) const
{
    for (auto *prop : timeline_properties(layer)) {
        if (prop->name == prop_name)
            return prop;
    }
    return nullptr;
}

void TimelineWidget::clear_keyframe_selection()
{
    if (selected_keyframes_.empty()) return;
    selected_keyframes_.clear();
    update();
}

void TimelineWidget::prune_keyframe_selection()
{
    if (!title_) {
        selected_keyframes_.clear();
        return;
    }

    for (auto it = selected_keyframes_.begin(); it != selected_keyframes_.end();) {
        auto layer = title_->find_layer(it->layer_id);
        AnimatedProperty *prop = layer ? find_timeline_property(*layer, it->prop_name) : nullptr;
        if (!layer || !prop || it->index < 0 || it->index >= (int)prop->keyframes.size())
            it = selected_keyframes_.erase(it);
        else
            ++it;
    }
}

bool TimelineWidget::is_keyframe_selected(const std::string &layer_id, const std::string &prop_name, int kf_idx) const
{
    return selected_keyframes_.find({layer_id, prop_name, kf_idx}) != selected_keyframes_.end();
}

void TimelineWidget::select_keyframe(const std::string &layer_id, const std::string &prop_name,
                                     int kf_idx, bool additive, bool toggle)
{
    KeyframeRef ref{layer_id, prop_name, kf_idx};
    if (!additive)
        selected_keyframes_.clear();
    if (toggle && selected_keyframes_.find(ref) != selected_keyframes_.end())
        selected_keyframes_.erase(ref);
    else
        selected_keyframes_.insert(ref);
    update();
}

QRect TimelineWidget::marquee_rect() const
{
    return QRect(marquee_start_, marquee_current_).normalized()
        .intersected(QRect(0, ruler_height(), width(), std::max(0, height() - ruler_height())));
}

void TimelineWidget::select_keyframes_in_rect(const QRect &rect, bool additive)
{
    if (!title_) return;
    std::set<KeyframeRef> selection = additive ? selected_keyframes_ : std::set<KeyframeRef>{};
    auto rows = timeline_rows(title_);
    const QRect visible_timeline(0, ruler_height(), width(), std::max(0, height() - ruler_height()));
    QRect bounded = rect.normalized().intersected(visible_timeline);
    if (bounded.isEmpty()) {
        selected_keyframes_ = std::move(selection);
        update();
        return;
    }

    for (int row = 0; row < (int)rows.size(); ++row) {
        const auto &entry = rows[row];
        if (!entry.is_property || !entry.layer || !entry.prop) continue;
        if (!entry.layer->properties_expanded || entry.layer->locked) continue;
        int y = ruler_height() + row * row_height() - scroll_y_;
        int ky = y + row_height() / 2;
        if (ky < visible_timeline.top() || ky > visible_timeline.bottom()) continue;
        if (ky < bounded.top() || ky > bounded.bottom()) continue;
        for (int i = 0; i < (int)entry.prop->keyframes.size(); ++i) {
            const auto &kf = entry.prop->keyframes[i];
            int kx = time_to_x(entry.layer->in_time + kf.time);
            if (kx < visible_timeline.left() || kx > visible_timeline.right()) continue;
            if (bounded.contains(QPoint(kx, ky)))
                selection.insert({entry.layer->id, entry.prop->name, i});
        }
    }
    selected_keyframes_ = std::move(selection);
    update();
}

bool TimelineWidget::copy_selected_keyframes()
{
    if (!title_) return false;
    prune_keyframe_selection();
    if (selected_keyframes_.empty()) return false;

    struct PendingCopy {
        std::string layer_id;
        std::string prop_name;
        Keyframe keyframe;
        double timeline_time = 0.0;
    };
    std::vector<PendingCopy> pending;
    double origin = std::numeric_limits<double>::max();

    for (const auto &ref : selected_keyframes_) {
        auto layer = title_->find_layer(ref.layer_id);
        AnimatedProperty *prop = layer ? find_timeline_property(*layer, ref.prop_name) : nullptr;
        if (!layer || !prop || ref.index < 0 || ref.index >= (int)prop->keyframes.size()) continue;
        const Keyframe keyframe = prop->keyframes[ref.index];
        const double timeline_time = layer->in_time + keyframe.time;
        origin = std::min(origin, timeline_time);
        pending.push_back({ref.layer_id, ref.prop_name, keyframe, timeline_time});
    }

    if (pending.empty()) return false;
    std::sort(pending.begin(), pending.end(), [](const PendingCopy &a, const PendingCopy &b) {
        return std::tie(a.timeline_time, a.layer_id, a.prop_name) <
               std::tie(b.timeline_time, b.layer_id, b.prop_name);
    });

    keyframe_clipboard_.clear();
    keyframe_clipboard_.reserve(pending.size());
    for (const auto &entry : pending)
        keyframe_clipboard_.push_back({entry.layer_id, entry.prop_name, entry.keyframe, entry.timeline_time - origin});
    return true;
}

bool TimelineWidget::delete_selected_keyframes()
{
    if (!title_) return false;
    prune_keyframe_selection();
    if (selected_keyframes_.empty()) return false;

    std::map<std::pair<std::string, std::string>, std::vector<int>> grouped;
    for (const auto &ref : selected_keyframes_)
        grouped[{ref.layer_id, ref.prop_name}].push_back(ref.index);

    bool changed = false;
    for (auto &[prop_ref, indices] : grouped) {
        auto layer = title_->find_layer(prop_ref.first);
        if (!layer || layer->locked) continue;
        AnimatedProperty *prop = find_timeline_property(*layer, prop_ref.second);
        if (!prop) continue;
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        for (int index : indices) {
            if (index < 0 || index >= (int)prop->keyframes.size()) continue;
            prop->keyframes.erase(prop->keyframes.begin() + index);
            changed = true;
        }
    }

    if (changed) {
        selected_keyframes_.clear();
        update();
    }
    return changed;
}

bool TimelineWidget::cut_selected_keyframes()
{
    if (!copy_selected_keyframes()) return false;
    return delete_selected_keyframes();
}

bool TimelineWidget::paste_keyframes_at(double timeline_time)
{
    if (!title_ || keyframe_clipboard_.empty()) return false;

    std::map<std::pair<std::string, std::string>, std::vector<double>> inserted_times;
    bool changed = false;
    const double paste_origin = std::clamp(snap_time(timeline_time), 0.0, title_->duration);

    for (const auto &entry : keyframe_clipboard_) {
        auto layer = title_->find_layer(entry.layer_id);
        if (!layer || layer->locked) continue;
        AnimatedProperty *prop = find_timeline_property(*layer, entry.prop_name);
        if (!prop) continue;

        Keyframe pasted = entry.keyframe;
        const double target_time = paste_origin + entry.offset;
        pasted.time = std::clamp(snap_time(target_time - layer->in_time),
                                 0.0, std::max(0.0, layer->out_time - layer->in_time));
        prop->keyframes.push_back(pasted);
        inserted_times[{entry.layer_id, entry.prop_name}].push_back(pasted.time);
        changed = true;
    }

    if (!changed) return false;

    selected_keyframes_.clear();
    for (auto &[prop_ref, times] : inserted_times) {
        auto layer = title_->find_layer(prop_ref.first);
        AnimatedProperty *prop = layer ? find_timeline_property(*layer, prop_ref.second) : nullptr;
        if (!prop) continue;
        std::sort(prop->keyframes.begin(), prop->keyframes.end(),
                  [](const Keyframe &a, const Keyframe &b) { return a.time < b.time; });

        std::set<int> used;
        for (double inserted_time : times) {
            int best = -1;
            double best_distance = std::numeric_limits<double>::max();
            for (int i = 0; i < (int)prop->keyframes.size(); ++i) {
                if (used.count(i)) continue;
                const double distance = std::abs(prop->keyframes[i].time - inserted_time);
                if (distance < best_distance) {
                    best = i;
                    best_distance = distance;
                }
            }
            if (best >= 0) {
                used.insert(best);
                selected_keyframes_.insert({prop_ref.first, prop_ref.second, best});
            }
        }
    }

    update();
    return true;
}

void TimelineWidget::begin_keyframe_drag(const std::string &layer_id, const std::string &prop_name,
                                         int kf_idx, double start_time)
{
    drag_mode_ = DragMode::Keyframe;
    drag_layer_id_ = layer_id;
    drag_prop_name_ = prop_name;
    drag_keyframe_index_ = kf_idx;
    drag_start_time_ = start_time;
    dragged_keyframes_.clear();
    prune_keyframe_selection();
    if (!is_keyframe_selected(layer_id, prop_name, kf_idx))
        selected_keyframes_ = {{layer_id, prop_name, kf_idx}};

    for (const auto &ref : selected_keyframes_) {
        auto layer = title_ ? title_->find_layer(ref.layer_id) : nullptr;
        if (!layer || layer->locked) continue;
        AnimatedProperty *prop = find_timeline_property(*layer, ref.prop_name);
        if (!prop || ref.index < 0 || ref.index >= (int)prop->keyframes.size()) continue;
        dragged_keyframes_.push_back({ref, prop->keyframes[ref.index].time});
    }
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    int W = width(), H = height();
    int rh = ruler_height(), rowh = row_height();
    const QPalette pal = palette();
    auto with_alpha = [](QColor color, int alpha) {
        color.setAlpha(alpha);
        return color;
    };
    auto subtle = [](const QColor &color) {
        return color.lightness() < 128 ? color.lighter(108) : color.darker(104);
    };

    const QColor window = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor disabled_text = pal.color(QPalette::Disabled, QPalette::WindowText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor dark = pal.color(QPalette::Dark);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor ruler_bg = window.lightness() < 128 ? window.darker(116) : window.darker(106);
    const QColor property_bg = subtle(window);
    const QColor selected_row = with_alpha(highlight, window.lightness() < 128 ? 90 : 65);
    const QColor tick_major = with_alpha(text, 150);
    const QColor tick_minor = with_alpha(text, 80);
    const QColor label_text = with_alpha(text, 165);
    const QColor playhead_color = TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::Current);
    const QColor pause_color = TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::Pause);
    const QColor loop_color = TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::Loop);
    const QColor handle_color = with_alpha(text, 150);

    /* Background */
    p.fillRect(0, 0, W, H, window);

    /* Ruler */
    p.fillRect(0, 0, W, rh, ruler_bg);
    p.setPen(border);

    double dur = title_ ? title_->duration : 10.0;
    double fps = obs_frame_rate();
    double frame_step = obs_frame_duration();
    int first_frame = std::max(0, (int)std::floor(scroll_x_ / pixels_per_sec_ / frame_step) - 1);
    int last_frame = (int)std::ceil((scroll_x_ + W) / pixels_per_sec_ / frame_step) + 1;
    int label_every = std::max(1, (int)std::ceil(55.0 / (pixels_per_sec_ * frame_step)));

    for (int frame = first_frame; frame <= last_frame; ++frame) {
        double t = frame * frame_step;
        if (t > dur + frame_step) break;
        int x = time_to_x(t);
        if (x < 0 || x > W) continue;
        bool is_second = (frame % std::max(1, (int)std::round(fps)) == 0);
        bool label = (frame % label_every == 0) || is_second;
        p.setPen(is_second ? tick_major : tick_minor);
        p.drawLine(x, rh - (is_second ? 9 : label ? 6 : 3), x, rh);
        if (label) {
            p.setPen(label_text);
            int seconds = frame / std::max(1, (int)std::round(fps));
            int frame_in_second = frame % std::max(1, (int)std::round(fps));
            QString text = is_second
                ? QString("%1s").arg(seconds)
                : QString("+%1f").arg(frame_in_second, 2, 10, QChar('0'));
            p.drawText(x + 2, rh - 2, text);
        }
    }

    if (title_) {
        if (title_->playback_mode == 1) {
            int loop_x0 = time_to_x(std::clamp(title_->loop_start, 0.0, dur));
            int loop_x1 = time_to_x(std::clamp(title_->loop_end, title_->loop_start, dur));
            if (loop_x1 > loop_x0) {
                p.fillRect(loop_x0, 18, loop_x1 - loop_x0, rh - 18, with_alpha(loop_color, 45));
                p.setPen(QPen(loop_color, 2));
                p.drawLine(loop_x0, 18, loop_x0, H);
                p.drawLine(loop_x1, 18, loop_x1, H);
                p.setPen(loop_color.lightness() < 128 ? loop_color.lighter(170) : loop_color.darker(170));
                p.drawText(loop_x0 + 4, 20, 80, 16, Qt::AlignVCenter, obsgs_tr("OBSTitles.LoopIn"));
                p.drawText(loop_x1 + 4, 20, 80, 16, Qt::AlignVCenter, obsgs_tr("OBSTitles.LoopOut"));
            }
        }
        if (title_->playback_mode == 2) {
            int pause_x = time_to_x(std::clamp(title_->pause_time, 0.0, dur));
            p.setPen(QPen(pause_color, 2));
            p.drawLine(pause_x, 12, pause_x, rh);
            p.setBrush(pause_color);
            p.setPen(Qt::NoPen);
            QPolygon marker;
            marker << QPoint(pause_x - 6, 12) << QPoint(pause_x + 6, 12) << QPoint(pause_x, 22);
            p.drawPolygon(marker);
            p.setPen(pause_color.lightness() < 128 ? pause_color.lighter(170) : pause_color.darker(170));
            p.drawText(pause_x + 4, 22, 100, 16, Qt::AlignVCenter, obsgs_tr("OBSTitles.Pause"));
            p.setBrush(Qt::NoBrush);
        }
    }

    /* Layer/property rows.  This uses the same row model as LayerStack so
     * keyframed property rows stay vertically aligned with the layer list.
     */
    auto rows = timeline_rows(title_);
    for (int row = 0; row < (int)rows.size(); ++row) {
        auto &entry = rows[row];
        auto &layer = entry.layer;
        int y = rh + row * rowh - scroll_y_;
        if (y > H) break;
        if (y + rowh < rh) continue;
        bool sel = (layer->id == sel_layer_id_);

        p.fillRect(0, y, W, rowh,
                   entry.is_property ? property_bg :
                   sel ? selected_row : window);
        p.setPen(border);
        p.drawLine(0, y + rowh - 1, W, y + rowh - 1);

        int x0 = time_to_x(layer->in_time);
        int x1 = time_to_x(layer->out_time);
        if (!entry.is_property) {
            QRect strip_rect(std::min(x0, x1), y + 3, std::abs(x1 - x0), rowh - 6);
            QColor bar_col = layer_color(*layer, row);
            if (!layer->visible) {
                const int gray = qGray(bar_col.rgb());
                bar_col = QColor(gray, gray, gray).darker(135);
            }
            if (sel) bar_col = bar_col.lighter(125);
            p.fillRect(strip_rect, bar_col);
            if (layer->locked) {
                p.save();
                p.setClipRect(strip_rect);
                p.setPen(QPen(with_alpha(dark, 170), 2));
                for (int lx = strip_rect.left() - strip_rect.height(); lx < strip_rect.right() + strip_rect.height(); lx += 8)
                    p.drawLine(lx, strip_rect.bottom(), lx + strip_rect.height(), strip_rect.top());
                p.restore();
            }
            p.setBrush(Qt::NoBrush);
            p.setPen(dark);
            p.drawRect(strip_rect);

            /* Trim handles for mouse resizing of unlocked layer in/out. */
            if (!layer->locked) {
                p.fillRect(x0, y + 3, 4, rowh - 6, handle_color);
                p.fillRect(x1 - 4, y + 3, 4, rowh - 6, handle_color);
            }

            p.setPen(layer->visible ? text : disabled_text);
            p.drawText(std::max(strip_rect.left(), 0) + 6, y, std::max(1, strip_rect.width() - 12), rowh,
                       Qt::AlignVCenter, QString::fromStdString(layer->name));
        } else {
            p.fillRect(x0, y + rowh / 2 - 1, x1 - x0, 2, border);
            p.setPen(disabled_text.isValid() ? disabled_text : with_alpha(text, 150));
            p.drawText(6, y, 150, rowh, Qt::AlignVCenter, property_label(entry.prop->name));
        }

        auto draw_kf = [&](const AnimatedProperty &prop) {
            for (int i = 0; i < (int)prop.keyframes.size(); ++i) {
                const auto &kf = prop.keyframes[i];
                int kx = time_to_x(layer->in_time + kf.time);
                if (kx < 0 || kx > W) continue;
                int ky = y + rowh / 2;
                QColor kf_fill = keyframe_color(kf.easing);
                if (!layer->visible)
                    kf_fill = kf_fill.darker(160);
                const bool selected = is_keyframe_selected(layer->id, prop.name, i);
                if (selected) {
                    draw_keyframe_marker(p, QPointF(kx, ky), kf.easing, 8.0,
                                         with_alpha(highlighted_text, 45),
                                         highlighted_text, 2.0);
                }
                draw_keyframe_marker(p, QPointF(kx, ky), kf.easing, 5.0,
                                     selected ? kf_fill.lighter(125) : kf_fill,
                                     selected ? highlighted_text : border,
                                     selected ? 2.0 : 1.0);
            }
        };

        if (entry.is_property)
            draw_kf(*entry.prop);
        else if (!layer->properties_expanded)
            for (auto *prop : timeline_properties(*layer)) draw_kf(*prop);
    }

    /* Playhead */
    int phx = time_to_x(playhead_);
    p.setPen(QPen(playhead_color, 1.5));
    p.drawLine(phx, 0, phx, H);
    /* Playhead head triangle */
    p.setBrush(playhead_color);
    p.setPen(Qt::NoPen);
    QPolygon tri;
    tri << QPoint(phx - 6, 0)
        << QPoint(phx + 6, 0)
        << QPoint(phx,     10);
    p.drawPolygon(tri);

    QString tc = format_timecode(playhead_);
    QRect tc_rect(phx + 8, 2, 96, 18);
    if (tc_rect.right() > W) tc_rect.moveRight(phx - 8);
    p.fillRect(tc_rect, highlight);
    p.setPen(highlighted_text);
    p.drawText(tc_rect.adjusted(4, 0, -4, 0), Qt::AlignVCenter, tc);

    if (drag_mode_ == DragMode::Marquee && marquee_moved_) {
        QRect rect = marquee_rect();
        p.fillRect(rect, with_alpha(highlight, 35));
        p.setPen(QPen(highlight, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect.adjusted(0, 0, -1, -1));
    }
}

bool TimelineWidget::hit_keyframe(const QPoint &pos, std::shared_ptr<Layer> *hit_layer,
                                  AnimatedProperty **hit_prop, int *hit_kf_idx,
                                  int *hit_row_idx) const
{
    if (!title_ || pos.y() < ruler_height()) return false;
    auto rows = timeline_rows(title_);
    int row = (pos.y() - ruler_height() + scroll_y_) / row_height();
    if (row < 0 || row >= (int)rows.size()) return false;

    auto &entry = rows[row];
    constexpr int kHitRadius = 7;
    auto test_prop = [&](AnimatedProperty *prop) -> bool {
        for (int i = 0; i < (int)prop->keyframes.size(); ++i) {
            const auto &kf = prop->keyframes[i];
            int kx = time_to_x(entry.layer->in_time + kf.time);
            int ky = ruler_height() + row * row_height() - scroll_y_ + row_height() / 2;
            if (std::abs(pos.x() - kx) <= kHitRadius &&
                std::abs(pos.y() - ky) <= kHitRadius) {
                if (hit_layer) *hit_layer = entry.layer;
                if (hit_prop) *hit_prop = prop;
                if (hit_kf_idx) *hit_kf_idx = i;
                if (hit_row_idx) *hit_row_idx = row;
                return true;
            }
        }
        return false;
    };

    if (entry.is_property)
        return entry.prop && test_prop(entry.prop);
    if (!entry.layer->properties_expanded) {
        for (auto *prop : timeline_properties(*entry.layer)) {
            if (test_prop(prop)) return true;
        }
    }
    return false;
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent *ev)
{
    if (!title_) return;

    std::shared_ptr<Layer> layer;
    AnimatedProperty *hit_prop = nullptr;
    int hit_idx = -1;
    const bool has_hit = hit_keyframe(ev->pos(), &layer, &hit_prop, &hit_idx, nullptr);
    if (has_hit && layer && layer->locked) return;
    if (!has_hit && keyframe_clipboard_.empty()) return;

    if (has_hit && layer && hit_prop && !is_keyframe_selected(layer->id, hit_prop->name, hit_idx))
        select_keyframe(layer->id, hit_prop->name, hit_idx, false, false);
    prune_keyframe_selection();

    QMenu menu(this);
    menu.setTitle(has_hit ? obsgs_tr("OBSTitles.KeyframeEasing") : obsgs_tr("OBSTitles.Paste"));

    QAction *copy_action = menu.addAction(obsgs_tr("OBSTitles.Copy"));
    QAction *cut_action = menu.addAction(obsgs_tr("OBSTitles.Cut"));
    QAction *paste_action = menu.addAction(obsgs_tr("OBSTitles.Paste"));
    QAction *delete_action = menu.addAction(obsgs_tr("OBSTitles.Delete"));
    const bool has_selection = !selected_keyframes_.empty();
    copy_action->setEnabled(has_selection);
    cut_action->setEnabled(has_selection);
    paste_action->setEnabled(!keyframe_clipboard_.empty());
    delete_action->setEnabled(has_selection);

    struct EasingChoice {
        QAction *action = nullptr;
        AnimatedProperty *prop = nullptr;
        std::vector<int> target_indices;
        EasingType easing = EasingType::Linear;
    };
    std::vector<EasingChoice> choices;

    auto swatch_icon = [](EasingType easing) {
        QPixmap swatch(16, 16);
        swatch.fill(Qt::transparent);
        QPainter painter(&swatch);
        draw_keyframe_marker(painter, QPointF(8, 8), easing, 5.0,
                             keyframe_color(easing), QColor(0x7a, 0x5a, 0x00), 1.0);
        return QIcon(swatch);
    };

    auto add_easing_action = [&](QMenu *target_menu, const QString &label,
                                 AnimatedProperty *prop, EasingType easing,
                                 const std::vector<int> &indices) {
        QAction *action = target_menu->addAction(swatch_icon(easing), label);
        action->setToolTip(easing == EasingType::Hold
            ? obsgs_tr("OBSTitles.HoldEasingTooltip")
            : obsgs_tr("OBSTitles.EasingTooltip"));
        action->setCheckable(true);
        action->setChecked(prop && std::all_of(indices.begin(), indices.end(), [&](int idx) {
            return idx >= 0 && idx < (int)prop->keyframes.size() &&
                   prop->keyframes[idx].easing == easing;
        }));
        choices.push_back({action, prop, indices, easing});
        return action;
    };

    auto add_easing_group = [&](QMenu *target_menu, AnimatedProperty *prop, const std::vector<int> &indices) {
        auto *group = new QActionGroup(target_menu);
        group->setExclusive(true);
        for (auto [label, easing] : std::initializer_list<std::pair<QString, EasingType>>{
                 {obsgs_tr("OBSTitles.Linear"), EasingType::Linear},
                 {obsgs_tr("OBSTitles.EasyEase"), EasingType::EaseInOut},
                 {obsgs_tr("OBSTitles.EaseIn"), EasingType::EaseIn},
                 {obsgs_tr("OBSTitles.EaseOut"), EasingType::EaseOut},
                 {obsgs_tr("OBSTitles.Hold"), EasingType::Hold},
                 {obsgs_tr("OBSTitles.CustomBezier"), EasingType::Bezier},
             }) {
            add_easing_action(target_menu, label, prop, easing, indices)->setActionGroup(group);
        }
    };

    if (has_hit && layer && hit_prop) {
        menu.addSeparator();
        QMenu *easing_menu = menu.addMenu(obsgs_tr("OBSTitles.Easing"));
        QAction *header = easing_menu->addAction(QString("%1 · %2")
            .arg(QString::fromStdString(layer->name))
            .arg(property_label(hit_prop->name)));
        header->setEnabled(false);

        const bool has_previous_segment = hit_idx > 0;
        const bool has_next_segment = hit_idx + 1 < (int)hit_prop->keyframes.size();
        if (!has_previous_segment && !has_next_segment) {
            QAction *message = easing_menu->addAction(obsgs_tr("OBSTitles.AddKeyframeForEasing"));
            message->setEnabled(false);
        } else {
            QAction *scope = easing_menu->addAction(has_previous_segment && has_next_segment
                ? obsgs_tr("OBSTitles.EasingBothSegments")
                : has_next_segment ? obsgs_tr("OBSTitles.EasingNextSegment") : obsgs_tr("OBSTitles.EasingPreviousSegment"));
            scope->setEnabled(false);
            easing_menu->addSeparator();

            auto default_targets = [&]() {
                std::vector<int> indices;
                if (has_previous_segment && has_next_segment) {
                    indices = {hit_idx - 1, hit_idx};
                } else if (has_next_segment) {
                    indices = {hit_idx};
                } else {
                    indices = {hit_idx - 1};
                }
                return indices;
            };
            add_easing_group(easing_menu, hit_prop, default_targets());

            if (has_previous_segment && has_next_segment) {
                easing_menu->addSeparator();
                QMenu *advanced = easing_menu->addMenu(obsgs_tr("OBSTitles.ApplyOneSide"));
                QMenu *previous = advanced->addMenu(obsgs_tr("OBSTitles.PreviousSegment"));
                add_easing_group(previous, hit_prop, {hit_idx - 1});
                QMenu *next = advanced->addMenu(obsgs_tr("OBSTitles.NextSegment"));
                add_easing_group(next, hit_prop, {hit_idx});
            }
        }
    }

    QAction *chosen = menu.exec(ev->globalPos());
    if (!chosen) return;

    if (chosen == copy_action) {
        copy_selected_keyframes();
        return;
    }
    if (chosen == cut_action) {
        if (cut_selected_keyframes()) emit keyframe_easing_changed();
        return;
    }
    if (chosen == paste_action) {
        if (paste_keyframes_at(std::clamp(x_to_time(ev->pos().x()), 0.0, title_->duration)))
            emit keyframe_easing_changed();
        return;
    }
    if (chosen == delete_action) {
        if (delete_selected_keyframes()) emit keyframe_easing_changed();
        return;
    }

    auto choice = std::find_if(choices.begin(), choices.end(),
                               [&](const EasingChoice &candidate) { return candidate.action == chosen; });
    if (choice == choices.end() || !choice->prop) return;

    for (int idx : choice->target_indices) {
        if (idx >= 0 && idx < (int)choice->prop->keyframes.size())
            apply_easing_preset(choice->prop->keyframes[idx], choice->easing);
    }
    update();
    emit keyframe_easing_changed();
}

void TimelineWidget::wheelEvent(QWheelEvent *ev)
{
    if (!title_) return;

    const QPoint angle = ev->angleDelta();
    if (ev->modifiers() & Qt::ShiftModifier) {
        int delta = angle.x() != 0 ? angle.x() : angle.y();
        scroll_x_ -= delta;
        clamp_scroll();
        update();
        ev->accept();
        return;
    }

    if (ev->modifiers() & Qt::ControlModifier) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        int cursor_x = (int)std::round(ev->position().x());
#else
        int cursor_x = ev->pos().x();
#endif
        double anchor_time = (cursor_x + scroll_x_) / pixels_per_sec_;
        int delta = angle.y() != 0 ? angle.y() : angle.x();
        if (delta == 0) return;

        double factor = std::pow(1.0015, delta);
        set_pixels_per_sec(pixels_per_sec_ * factor, anchor_time, cursor_x);
        ev->accept();
        return;
    }

    int delta = angle.y() != 0 ? -angle.y() : -angle.x();
    if (delta == 0) return;
    emit vertical_scroll_delta_requested(delta);
    ev->accept();
}

void TimelineWidget::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    if (fit_on_next_resize_ && title_ && width() > 40) {
        fit_on_next_resize_ = false;
        fit_timeline();
        return;
    }
    clamp_scroll();
    clamp_vertical_scroll();
}

void TimelineWidget::keyPressEvent(QKeyEvent *ev)
{
    if (!title_) {
        QWidget::keyPressEvent(ev);
        return;
    }

    if (ev->matches(QKeySequence::Copy) && has_selected_keyframes()) {
        copy_keyframe_selection();
        ev->accept();
        return;
    }
    if (ev->matches(QKeySequence::Cut) && has_selected_keyframes()) {
        cut_keyframe_selection();
        ev->accept();
        return;
    }
    if (ev->matches(QKeySequence::Paste) && has_keyframe_clipboard()) {
        paste_keyframes_at_playhead();
        ev->accept();
        return;
    }
    if ((ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace) && has_selected_keyframes()) {
        delete_keyframe_selection();
        ev->accept();
        return;
    }

    QWidget::keyPressEvent(ev);
}

void TimelineWidget::mousePressEvent(QMouseEvent *ev)
{
    setFocus(Qt::MouseFocusReason);
    if (!title_) return;
    drag_mode_ = DragMode::None;
    drag_layer_id_.clear();
    drag_prop_name_.clear();
    drag_keyframe_index_ = -1;
    drag_start_time_ = 0.0;
    drag_start_in_ = 0.0;
    drag_start_out_ = 0.0;
    dragged_keyframes_.clear();
    marquee_moved_ = false;

    if (ev->pos().y() < ruler_height()) {
        if (title_->playback_mode == 2) {
            int pause_x = time_to_x(std::clamp(title_->pause_time, 0.0, title_->duration));
            if (std::abs(ev->pos().x() - pause_x) <= 8) {
                drag_mode_ = DragMode::PauseMarker;
                setCursor(Qt::SizeHorCursor);
                ev->accept();
                return;
            }
        }
        if (title_->playback_mode == 1) {
            int loop_x0 = time_to_x(std::clamp(title_->loop_start, 0.0, title_->duration));
            int loop_x1 = time_to_x(std::clamp(title_->loop_end, title_->loop_start, title_->duration));
            if (std::abs(ev->pos().x() - loop_x0) <= 8) {
                drag_mode_ = DragMode::LoopStart;
                setCursor(Qt::SizeHorCursor);
                ev->accept();
                return;
            }
            if (std::abs(ev->pos().x() - loop_x1) <= 8) {
                drag_mode_ = DragMode::LoopEnd;
                setCursor(Qt::SizeHorCursor);
                ev->accept();
                return;
            }
        }
        drag_mode_ = DragMode::Playhead;
        double t = std::clamp(x_to_time(ev->pos().x()), 0.0, title_->duration);
        emit playhead_changed(t);
        ev->accept();
        return;
    }

    std::shared_ptr<Layer> hit_layer;
    AnimatedProperty *hit_prop = nullptr;
    int hit_idx = -1;
    if (hit_keyframe(ev->pos(), &hit_layer, &hit_prop, &hit_idx, nullptr)) {
        if (hit_layer && hit_layer->locked) {
            ev->accept();
            return;
        }
        if (hit_layer) emit layer_selected(hit_layer->id);
        const bool shift = ev->modifiers() & Qt::ShiftModifier;
        if (shift) {
            select_keyframe(hit_layer->id, hit_prop->name, hit_idx, true, true);
            ev->accept();
            return;
        }
        if (!is_keyframe_selected(hit_layer->id, hit_prop->name, hit_idx))
            select_keyframe(hit_layer->id, hit_prop->name, hit_idx, false, false);
        begin_keyframe_drag(hit_layer->id, hit_prop->name, hit_idx,
                            std::clamp(x_to_time(ev->pos().x()), 0.0, title_->duration));
        setCursor(Qt::ClosedHandCursor);
        ev->accept();
        return;
    }

    auto rows = timeline_rows(title_);
    int row = (ev->pos().y() - ruler_height() + scroll_y_) / row_height();
    if (row >= 0 && row < (int)rows.size() && !rows[row].is_property) {
        auto layer = rows[row].layer;
        if (!layer) {
            ev->accept();
            return;
        }
        int x0 = time_to_x(layer->in_time);
        int x1 = time_to_x(layer->out_time);
        constexpr int kTrimHit = 7;
        const bool hit_strip = ev->pos().x() >= std::min(x0, x1) - kTrimHit &&
                               ev->pos().x() <= std::max(x0, x1) + kTrimHit;
        if (layer->locked && hit_strip) {
            ev->accept();
            return;
        }
        if (hit_strip) emit layer_selected(layer->id);
        if (std::abs(ev->pos().x() - x0) <= kTrimHit) {
            drag_mode_ = DragMode::TrimIn;
            drag_layer_id_ = layer->id;
            setCursor(Qt::SizeHorCursor);
            ev->accept();
            return;
        }
        if (std::abs(ev->pos().x() - x1) <= kTrimHit) {
            drag_mode_ = DragMode::TrimOut;
            drag_layer_id_ = layer->id;
            setCursor(Qt::SizeHorCursor);
            ev->accept();
            return;
        }
        if (ev->pos().x() >= std::min(x0, x1) && ev->pos().x() <= std::max(x0, x1)) {
            drag_mode_ = DragMode::Layer;
            drag_layer_id_ = layer->id;
            drag_start_time_ = x_to_time(ev->pos().x());
            drag_start_in_ = layer->in_time;
            drag_start_out_ = layer->out_time;
            setCursor(Qt::ClosedHandCursor);
            ev->accept();
            return;
        }
    }

    if (ev->button() == Qt::LeftButton && ev->pos().y() >= ruler_height()) {
        drag_mode_ = DragMode::Marquee;
        marquee_start_ = ev->pos();
        marquee_current_ = ev->pos();
        marquee_additive_ = ev->modifiers() & Qt::ShiftModifier;
        marquee_moved_ = false;
        if (!marquee_additive_)
            selected_keyframes_.clear();
        ev->accept();
        update();
        return;
    }

    emit layer_selected(std::string());
    ev->accept();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *ev)
{
    if (!title_) return;
    double t = std::clamp(x_to_time(ev->pos().x()), 0.0, title_->duration);

    if (drag_mode_ == DragMode::Playhead) {
        emit playhead_changed(t);
        return;
    }

    if (drag_mode_ == DragMode::PauseMarker) {
        title_->pause_time = t;
        update();
        return;
    }

    if (drag_mode_ == DragMode::LoopStart) {
        title_->loop_start = std::clamp(t, 0.0, title_->loop_end);
        update();
        return;
    }

    if (drag_mode_ == DragMode::LoopEnd) {
        title_->loop_end = std::clamp(t, title_->loop_start, title_->duration);
        update();
        return;
    }

    if (drag_mode_ == DragMode::Keyframe) {
        double delta = t - drag_start_time_;
        for (const auto &dragged : dragged_keyframes_) {
            auto layer = title_->find_layer(dragged.ref.layer_id);
            if (!layer || layer->locked) continue;
            AnimatedProperty *prop = find_timeline_property(*layer, dragged.ref.prop_name);
            if (!prop || dragged.ref.index < 0 || dragged.ref.index >= (int)prop->keyframes.size()) continue;
            prop->keyframes[dragged.ref.index].time =
                std::clamp(dragged.start_time + delta, 0.0, std::max(0.0, layer->out_time - layer->in_time));
        }
        update();
        return;
    }

    if (drag_mode_ == DragMode::Marquee) {
        marquee_current_ = ev->pos();
        if ((marquee_current_ - marquee_start_).manhattanLength() >= 3)
            marquee_moved_ = true;
        update();
        return;
    }

    if (drag_mode_ == DragMode::TrimIn || drag_mode_ == DragMode::TrimOut) {
        auto layer = title_->find_layer(drag_layer_id_);
        if (!layer || layer->locked) return;
        if (drag_mode_ == DragMode::TrimIn)
            layer->in_time = std::clamp(t, 0.0, std::max(0.0, layer->out_time - obs_frame_duration()));
        else
            layer->out_time = std::clamp(t, layer->in_time + obs_frame_duration(), title_->duration);
        update();
        return;
    }

    if (drag_mode_ == DragMode::Layer) {
        auto layer = title_->find_layer(drag_layer_id_);
        if (!layer || layer->locked) return;
        double duration = std::max(obs_frame_duration(), drag_start_out_ - drag_start_in_);
        double new_in = drag_start_in_ + (t - drag_start_time_);
        new_in = std::clamp(new_in, 0.0, std::max(0.0, title_->duration - duration));
        layer->in_time = new_in;
        layer->out_time = std::min(title_->duration, new_in + duration);
        update();
        return;
    }

    auto rows = timeline_rows(title_);
    int row = (ev->pos().y() - ruler_height() + scroll_y_) / row_height();
    if (row >= 0 && row < (int)rows.size() && !rows[row].is_property) {
        if (!rows[row].layer || rows[row].layer->locked) {
            unsetCursor();
            return;
        }
        int x0 = time_to_x(rows[row].layer->in_time);
        int x1 = time_to_x(rows[row].layer->out_time);
        if (std::abs(ev->pos().x() - x0) <= 7 || std::abs(ev->pos().x() - x1) <= 7)
            setCursor(Qt::SizeHorCursor);
        else if (ev->pos().x() >= std::min(x0, x1) && ev->pos().x() <= std::max(x0, x1))
            setCursor(Qt::OpenHandCursor);
        else
            unsetCursor();
    } else {
        unsetCursor();
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *)
{
    bool changed = drag_mode_ == DragMode::Keyframe ||
                   drag_mode_ == DragMode::TrimIn ||
                   drag_mode_ == DragMode::TrimOut ||
                   drag_mode_ == DragMode::Layer ||
                   drag_mode_ == DragMode::LoopStart ||
                   drag_mode_ == DragMode::LoopEnd ||
                   drag_mode_ == DragMode::PauseMarker;

    if (drag_mode_ == DragMode::Marquee) {
        if (marquee_moved_)
            select_keyframes_in_rect(marquee_rect(), marquee_additive_);
        else if (!marquee_additive_)
            clear_keyframe_selection();
    }

    if (drag_mode_ == DragMode::Keyframe && title_) {
        std::map<KeyframeRef, double> selected_times;
        for (const auto &ref : selected_keyframes_) {
            auto layer = title_->find_layer(ref.layer_id);
            AnimatedProperty *prop = layer ? find_timeline_property(*layer, ref.prop_name) : nullptr;
            if (prop && ref.index >= 0 && ref.index < (int)prop->keyframes.size())
                selected_times[ref] = prop->keyframes[ref.index].time;
        }

        std::set<std::pair<std::string, std::string>> props_to_sort;
        for (const auto &dragged : dragged_keyframes_)
            props_to_sort.insert({dragged.ref.layer_id, dragged.ref.prop_name});

        for (const auto &prop_ref : props_to_sort) {
            if (auto layer = title_->find_layer(prop_ref.first)) {
                if (layer->locked) continue;
                if (AnimatedProperty *prop = find_timeline_property(*layer, prop_ref.second)) {
                    std::sort(prop->keyframes.begin(), prop->keyframes.end(),
                              [](const Keyframe &a, const Keyframe &b) { return a.time < b.time; });
                }
            }
        }

        std::set<KeyframeRef> remapped;
        std::map<std::pair<std::string, std::string>, std::set<int>> used_indices;
        for (const auto &[ref, selected_time] : selected_times) {
            auto layer = title_->find_layer(ref.layer_id);
            AnimatedProperty *prop = layer ? find_timeline_property(*layer, ref.prop_name) : nullptr;
            if (!prop) continue;
            int best = -1;
            double best_distance = std::numeric_limits<double>::max();
            auto key = std::make_pair(ref.layer_id, ref.prop_name);
            for (int i = 0; i < (int)prop->keyframes.size(); ++i) {
                if (used_indices[key].count(i)) continue;
                double distance = std::abs(prop->keyframes[i].time - selected_time);
                if (distance < best_distance) {
                    best = i;
                    best_distance = distance;
                }
            }
            if (best >= 0) {
                used_indices[key].insert(best);
                remapped.insert({ref.layer_id, ref.prop_name, best});
            }
        }
        selected_keyframes_ = std::move(remapped);
    }

    drag_mode_ = DragMode::None;
    drag_layer_id_.clear();
    drag_prop_name_.clear();
    drag_keyframe_index_ = -1;
    drag_start_time_ = 0.0;
    drag_start_in_ = 0.0;
    drag_start_out_ = 0.0;
    dragged_keyframes_.clear();
    marquee_additive_ = false;
    marquee_moved_ = false;
    unsetCursor();
    update();
    if (changed) emit keyframe_easing_changed();
}

/* ══════════════════════════════════════════════════════════════════
 *  TitlePropertiesPanel
 * ══════════════════════════════════════════════════════════════════ */
TitlePropertiesPanel::TitlePropertiesPanel(QWidget *parent)
    : QGroupBox(parent)
{
    apply_theme_style();

    auto *fl = new QFormLayout(this);
    fl->setContentsMargins(14, 12, 14, 12);
    fl->setHorizontalSpacing(8);
    fl->setVerticalSpacing(6);
    fl->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fl->setFormAlignment(Qt::AlignTop);
    fl->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto add_form_row = [this](QFormLayout *form, const QString &label_text, QWidget *field) {
        auto *label = new NumericDragLabel(label_text, field, form->parentWidget(),
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = true;
                                               emit title_changed(true);
                                           },
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = false;
                                               emit title_changed(true);
                                           });
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->addRow(label, field);
    };

    spn_duration_ = new TimecodeSpinBox(this);
    spn_duration_->setRange(0.1, 3600.0);
    spn_duration_->setFixedHeight(22);
    add_form_row(fl, obsgs_tr("OBSTitles.LengthLabel"), spn_duration_);

    auto *playback_row = new QWidget(this);
    auto *playback_layout = new QHBoxLayout(playback_row);
    playback_layout->setContentsMargins(0, 0, 0, 0);
    playback_layout->setSpacing(3);
    grp_playback_mode_ = new QButtonGroup(playback_row);
    grp_playback_mode_->setExclusive(true);
    auto add_playback_button = [this, playback_row, playback_layout](int id, const char *icon_name,
                                                                     const QString &text) {
        auto *button = new QToolButton(playback_row);
        button->setCheckable(true);
        button->setIcon(obs_icon(icon_name));
        button->setIconSize(QSize(16, 16));
        button->setToolTip(text);
        button->setAccessibleName(text);
        button->setFixedSize(30, 24);
        grp_playback_mode_->addButton(button, id);
        playback_layout->addWidget(button);
    };
    add_playback_button(0, "play-once.svg", obsgs_tr("OBSTitles.PlayOnce"));
    add_playback_button(1, "restart-loop.svg", obsgs_tr("OBSTitles.RestartLoop"));
    add_playback_button(2, "ping-pong-loop.svg", obsgs_tr("OBSTitles.PingPongLoop"));
    add_playback_button(3, "pause-at-timeline-position.svg", obsgs_tr("OBSTitles.PauseAtTimelinePosition"));
    playback_layout->addStretch(1);
    add_form_row(fl, obsgs_tr("OBSTitles.PlaybackModeLabel"), playback_row);

    spn_pause_frame_ = new TimecodeSpinBox(this);
    spn_pause_frame_->setRange(0.0, 3600.0);
    spn_pause_frame_->setToolTip(obsgs_tr("OBSTitles.PauseFrameTooltip"));
    spn_pause_frame_->setFixedHeight(22);
    add_form_row(fl, obsgs_tr("OBSTitles.PauseFrameLabel"), spn_pause_frame_);


    spn_loop_start_ = new TimecodeSpinBox(this);
    spn_loop_start_->setRange(0.0, 3600.0);
    spn_loop_start_->setToolTip(obsgs_tr("OBSTitles.LoopStartTooltip"));
    spn_loop_start_->setFixedHeight(22);

    spn_loop_end_ = new TimecodeSpinBox(this);
    spn_loop_end_->setRange(0.0, 3600.0);
    spn_loop_end_->setToolTip(obsgs_tr("OBSTitles.LoopEndTooltip"));
    spn_loop_end_->setFixedHeight(22);

    loop_area_row_ = new QWidget(this);
    auto *loop_area_layout = new QHBoxLayout(loop_area_row_);
    loop_area_layout->setContentsMargins(0, 0, 0, 0);
    loop_area_layout->setSpacing(4);
    auto *loop_start_label = new QLabel(obsgs_tr("OBSTitles.StartLabel"), loop_area_row_);
    auto *loop_end_label = new QLabel(obsgs_tr("OBSTitles.EndLabel"), loop_area_row_);
    spn_loop_start_->setMinimumWidth(78);
    spn_loop_end_->setMinimumWidth(78);
    loop_area_layout->addWidget(loop_start_label);
    loop_area_layout->addWidget(spn_loop_start_, 1);
    loop_area_layout->addWidget(loop_end_label);
    loop_area_layout->addWidget(spn_loop_end_, 1);
    add_form_row(fl, obsgs_tr("OBSTitles.LoopAreaLabel"), loop_area_row_);

    connect(grp_playback_mode_, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this, [this](QAbstractButton *button) {
                if (!title_ || loading_values_ || !button) return;
                int selection = grp_playback_mode_->id(button);
                if (selection == 1 || selection == 2) {
                    title_->playback_mode = 1;
                    title_->loop_type = selection == 2 ? 1 : 0;
                } else {
                    title_->playback_mode = selection == 3 ? 2 : 0;
                }
                if (title_->playback_mode == 2 && title_->pause_time <= 0.0)
                    title_->pause_time = title_->duration;
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });

    connect(spn_pause_frame_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (!title_ || loading_values_) return;
                title_->pause_time = std::clamp(v, 0.0, title_->duration);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });


    connect(spn_duration_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (!title_ || loading_values_) return;
                double old_duration = title_->duration;
                title_->duration = v;
                for (auto &layer : title_->layers) {
                    if (std::abs(layer->out_time - old_duration) < 0.001 || layer->out_time > v)
                        layer->out_time = v;
                }
                title_->loop_start = std::clamp(title_->loop_start, 0.0, title_->duration);
                title_->loop_end = std::clamp(title_->loop_end, title_->loop_start, title_->duration);
                title_->pause_time = std::clamp(title_->pause_time, 0.0, title_->duration);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });

    connect(spn_loop_start_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (!title_ || loading_values_) return;
                title_->loop_start = std::clamp(v, 0.0, title_->duration);
                title_->loop_end = std::clamp(title_->loop_end, title_->loop_start, title_->duration);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });

    connect(spn_loop_end_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (!title_ || loading_values_) return;
                title_->loop_end = std::clamp(v, title_->loop_start, title_->duration);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });
}

bool TitlePropertiesPanel::event(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        apply_theme_style();
    return QGroupBox::event(event);
}

void TitlePropertiesPanel::apply_theme_style()
{
    if (applying_theme_style_)
        return;

    const QPalette pal = palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor base = pal.color(QPalette::Base);
    const QColor button = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor subtle_text = text.lightness() < 128 ? text.lighter(135) : text.darker(135);
    const QColor section_bg = window.lightness() < 128 ? window.lighter(112) : window.darker(104);
    const QColor hover = button.lightness() < 128 ? button.lighter(125) : button.darker(110);

    const QString theme_style = QStringLiteral(
        "QGroupBox{color:%1;background:%2;border:none;margin:0;padding:0;font-size:14px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}"
        "QDoubleSpinBox,QSpinBox,QComboBox{color:%4;background:%5;border:1px solid %3;"
        "border-radius:2px;padding:1px 3px;selection-background-color:%9;}"
        "QDoubleSpinBox:focus,QSpinBox:focus,QComboBox:focus{border-color:%9;}"
        "QToolButton{color:%6;background:%7;border:1px solid %3;"
        "border-radius:2px;padding:2px;}"
        "QToolButton:hover{background:%8;border-color:%3;}"
        "QToolButton:checked{background:%9;color:%10;border-color:%9;}"
        "QLabel{color:%11;font-size:10px;background:transparent;}")
        .arg(text.name(QColor::HexRgb),
             section_bg.name(QColor::HexRgb),
             border.name(QColor::HexRgb),
             pal.color(QPalette::Text).name(QColor::HexRgb),
             base.name(QColor::HexRgb),
             button_text.name(QColor::HexRgb),
             button.name(QColor::HexRgb),
             hover.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb),
             highlighted_text.name(QColor::HexRgb),
             subtle_text.name(QColor::HexRgb));

    if (styleSheet() == theme_style)
        return;

    applying_theme_style_ = true;
    setStyleSheet(theme_style);
    applying_theme_style_ = false;
}

void TitlePropertiesPanel::set_title(std::shared_ptr<Title> t)
{
    title_ = t;
    setTitle(QString());
    load_values();
}

void TitlePropertiesPanel::load_values()
{
    loading_values_ = true;
    double duration = title_ ? title_->duration : 5.0;
    double loop_start = title_ ? title_->loop_start : 1.0;
    double loop_end = title_ ? title_->loop_end : 4.0;
    int playback_mode = title_ ? std::clamp(title_->playback_mode, 0, 2) : 0;
    int loop_type = title_ ? std::clamp(title_->loop_type, 0, 1) : 0;
    int playback_selection = playback_mode == 1 ? (loop_type == 1 ? 2 : 1)
                                                : (playback_mode == 2 ? 3 : 0);
    double pause_time = title_ ? std::clamp(title_->pause_time, 0.0, duration) : 0.0;

    if (auto *button = grp_playback_mode_->button(playback_selection))
        button->setChecked(true);
    spn_duration_->setValue(duration);
    spn_loop_start_->setMaximum(duration);
    spn_loop_end_->setMaximum(duration);
    spn_loop_start_->setValue(std::clamp(loop_start, 0.0, duration));
    spn_loop_end_->setValue(std::clamp(loop_end, std::clamp(loop_start, 0.0, duration), duration));
    spn_pause_frame_->setMaximum(duration);
    spn_pause_frame_->setValue(pause_time);

    bool show_loop = playback_mode == 1;
    bool show_pause = playback_mode == 2;
    auto *form = qobject_cast<QFormLayout *>(layout());
    loop_area_row_->setVisible(show_loop);
    if (form) if (auto *label = qobject_cast<QWidget *>(form->labelForField(loop_area_row_))) label->setVisible(show_loop);
    spn_pause_frame_->setVisible(show_pause);
    if (form) if (auto *label = qobject_cast<QWidget *>(form->labelForField(spn_pause_frame_))) label->setVisible(show_pause);
    loading_values_ = false;
}

/* ══════════════════════════════════════════════════════════════════
 *  PropertiesPanel
 * ══════════════════════════════════════════════════════════════════ */
PropertiesPanel::PropertiesPanel(QWidget *parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const QPalette pal = qApp->palette();
    const QColor panel_bg = pal.color(QPalette::Window);
    const QColor panel_text = pal.color(QPalette::WindowText);
    const QColor control_bg = pal.color(QPalette::Base);
    const QColor control_text = pal.color(QPalette::Text);
    const QColor button_bg = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor subtle_text = panel_text.lightness() < 128 ? panel_text.lighter(135) : panel_text.darker(135);
    const QColor section_bg = panel_bg.lightness() < 128 ? panel_bg.lighter(112) : panel_bg.darker(104);
    const QColor hover_bg = button_bg.lightness() < 128 ? button_bg.lighter(122) : button_bg.darker(108);
    const QString panel_bg_name = panel_bg.name(QColor::HexRgb);
    const QString panel_text_name = panel_text.name(QColor::HexRgb);
    const QString control_bg_name = control_bg.name(QColor::HexRgb);
    const QString control_text_name = control_text.name(QColor::HexRgb);
    const QString button_bg_name = button_bg.name(QColor::HexRgb);
    const QString button_text_name = button_text.name(QColor::HexRgb);
    const QString border_name = border.name(QColor::HexRgb);
    const QString highlight_name = highlight.name(QColor::HexRgb);
    const QString highlighted_text_name = highlighted_text.name(QColor::HexRgb);
    const QString subtle_text_name = subtle_text.name(QColor::HexRgb);
    const QString section_bg_name = section_bg.name(QColor::HexRgb);
    const QString hover_bg_name = hover_bg.name(QColor::HexRgb);
    setStyleSheet(QStringLiteral("QScrollArea{background:%1;border:none;}").arg(panel_bg_name));

    auto *inner = new QWidget(this);
    inner->setStyleSheet(QStringLiteral("background:%1;").arg(panel_bg_name));
    auto *vl = new QVBoxLayout(inner);
    vl->setContentsMargins(4, 4, 4, 4);
    vl->setSpacing(3);

    const QString section_style =
        QStringLiteral("QGroupBox{color:%1;background:%2;border:1px solid %3;"
        "border-radius:2px;margin-top:16px;font-size:10px;font-weight:bold;}"
        "QGroupBox::title{subcontrol-origin:margin;left:6px;top:2px;padding:0 4px;}"
        "QGroupBox::indicator{width:10px;height:10px;margin-left:2px;}"
        "QLabel{color:%4;font-size:10px;}")
            .arg(panel_text_name, section_bg_name, border_name, subtle_text_name);
    const QString control_style =
        QStringLiteral("QDoubleSpinBox,QSpinBox,QComboBox,QLineEdit,QTextEdit{color:%1;background:%2;"
        "border:1px solid %3;border-radius:2px;padding:1px 3px;selection-background-color:%4;}"
        "QDoubleSpinBox:focus,QSpinBox:focus,QComboBox:focus,QLineEdit:focus,QTextEdit:focus{border-color:%4;}")
            .arg(control_text_name, control_bg_name, border_name, highlight_name);

    auto style_form = [](QFormLayout *form) {
        form->setContentsMargins(6, 5, 6, 6);
        form->setHorizontalSpacing(5);
        form->setVerticalSpacing(3);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignTop);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    };

    auto add_form_row = [this](QFormLayout *form, const QString &label_text, QWidget *field) {
        if (!form || label_text.isEmpty()) {
            if (form) form->addRow(label_text, field);
            return;
        }

        auto *label = new NumericDragLabel(label_text, field, form->parentWidget(),
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = true;
                                               emit property_changed(true);
                                           },
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = false;
                                               emit property_changed(true);
                                           });
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->addRow(label, field);
    };

    const QString checkbox_style =
        QStringLiteral("QCheckBox{color:%1;font-size:10px;spacing:5px;}"
        "QCheckBox::indicator{width:13px;height:13px;border:1px solid %2;"
        "background:%3;border-radius:2px;}"
        "QCheckBox::indicator:hover{border-color:%2;background:%4;}"
        "QCheckBox::indicator:checked{background:%5;border-color:%5;}")
            .arg(panel_text_name, border_name, button_bg_name, hover_bg_name, highlight_name);
    const QString push_button_style =
        QStringLiteral("QPushButton{color:%1;background:%2;border:1px solid %3;"
        "border-radius:2px;font-size:10px;padding:2px 8px;}"
        "QPushButton:hover{background:%4;border-color:%3;}"
        "QPushButton:pressed{background:%5;color:%6;border-color:%5;}")
            .arg(button_text_name, button_bg_name, border_name, hover_bg_name, highlight_name, highlighted_text_name);

    auto style_checkbox = [&](QCheckBox *box) {
        box->setFixedHeight(22);
        box->setStyleSheet(checkbox_style);
    };
    auto style_push_button = [&](QPushButton *button) {
        button->setFixedHeight(22);
        button->setStyleSheet(push_button_style);
    };

    auto mk_dspin = [&](double lo, double hi, double step) {
        auto *s = new QDoubleSpinBox(inner);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(1);
        s->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        s->setFixedHeight(22);
        s->setStyleSheet(control_style);
        return s;
    };

    auto mk_kf_button = [&](const QString &tip) {
        auto *b = new QPushButton(inner);
        b->setFixedSize(22, 22);
        b->setIconSize(QSize(16, 16));
        b->setIcon(keyframe_diamond_icon(false));
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setProperty("active", false);
        b->setProperty("outlined", false);
        b->setStyleSheet("QPushButton{background:transparent;border:none;border-radius:2px;padding:0;}"
                         "QPushButton:hover{background:#303030;}"
                         "QPushButton[outlined=\"true\"]{background:#201d12;}"
                         "QPushButton[active=\"true\"]{background:#2b2518;}");
        return b;
    };

    auto with_kf = [&](QWidget *field, QPushButton *button) {
        auto *row = new QWidget(inner);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(3);
        field->setSizePolicy(QSizePolicy::Expanding, field->sizePolicy().verticalPolicy());
        hl->addWidget(button);
        hl->addWidget(field, 1);
        return row;
    };

    auto make_collapsible = [this](QGroupBox *box) {
        box->setCheckable(true);
        box->setChecked(true);
        QObject::connect(box, &QGroupBox::toggled, box, [this, box](bool expanded) {
            const int scroll = verticalScrollBar() ? verticalScrollBar()->value() : 0;
            if (auto *form = qobject_cast<QFormLayout *>(box->layout())) {
                for (int row = 0; row < form->rowCount(); ++row) {
                    for (auto role : {QFormLayout::LabelRole, QFormLayout::FieldRole}) {
                        if (auto *item = form->itemAt(row, role)) {
                            if (auto *widget = item->widget()) widget->setVisible(expanded);
                            if (auto *child_layout = item->layout()) {
                                for (int j = 0; j < child_layout->count(); ++j)
                                    if (auto *child = child_layout->itemAt(j)->widget()) child->setVisible(expanded);
                            }
                        }
                    }
                }
            } else if (box->layout()) {
                for (int i = 0; i < box->layout()->count(); ++i)
                    if (auto *widget = box->layout()->itemAt(i)->widget()) widget->setVisible(expanded);
            }
            QTimer::singleShot(0, this, [this, scroll]() {
                if (verticalScrollBar()) verticalScrollBar()->setValue(scroll);
            });
        });
    };

    spn_px_      = mk_dspin(-9999, 9999, 1.0);
    spn_py_      = mk_dspin(-9999, 9999, 1.0);
    spn_scale_x_ = mk_dspin(-10000.0, 10000.0, 1.0);
    spn_scale_y_ = mk_dspin(-10000.0, 10000.0, 1.0);
    spn_scale_x_->setSuffix("%");
    spn_scale_y_->setSuffix("%");
    chk_scale_lock_ = new TransformLockCheckBox(inner);
    chk_scale_lock_->setChecked(true);
    spn_rot_     = mk_dspin(-9999,  9999,  0.5);
    spn_opacity_ = mk_dspin(0.0,   1.0,  0.01);
    chk_scene_mask_ = new QCheckBox(QStringLiteral("Use as Scene Mask"), inner);
    chk_scene_mask_->setToolTip(QStringLiteral("Render a configured OBS scene through this layer shape when the title source is used in OBS."));
    style_checkbox(chk_scene_mask_);
    spn_origin_x_ = mk_dspin(0.0, 1.0, 0.05);
    spn_origin_y_ = mk_dspin(0.0, 1.0, 0.05);
    spn_origin_x_->setDecimals(2);
    spn_origin_y_->setDecimals(2);
    spn_origin_x_->setToolTip(obsgs_tr("OBSTitles.OriginXTooltip"));
    spn_origin_y_->setToolTip(obsgs_tr("OBSTitles.OriginYTooltip"));
    cmb_anchor_ = new QComboBox(inner);
    for (const QString &label : QStringList{obsgs_tr("OBSTitles.TopLeft"), obsgs_tr("OBSTitles.TopCenter"), obsgs_tr("OBSTitles.TopRight"), obsgs_tr("OBSTitles.CenterLeft"), obsgs_tr("OBSTitles.Center"), obsgs_tr("OBSTitles.CenterRight"), obsgs_tr("OBSTitles.BottomLeft"), obsgs_tr("OBSTitles.BottomCenter"), obsgs_tr("OBSTitles.BottomRight")})
        cmb_anchor_->addItem(label);
    cmb_anchor_->setToolTip(obsgs_tr("OBSTitles.AnchorChangeTooltip"));
    cmb_anchor_->setFixedHeight(22);
    cmb_anchor_->setStyleSheet(control_style);

    btn_kf_pos_x_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleXKeyframe"));
    btn_kf_pos_y_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleYKeyframe"));
    btn_kf_scale_x_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleScaleXKeyframe"));
    btn_kf_scale_y_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleScaleYKeyframe"));
    btn_kf_rotation_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleRotationKeyframe"));
    btn_kf_opacity_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleOpacityKeyframe"));
    btn_kf_origin_x_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleOriginXKeyframe"));
    btn_kf_origin_y_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleOriginYKeyframe"));
    btn_kf_pos_y_->hide();
    btn_kf_scale_y_->hide();
    btn_kf_opacity_->hide();
    btn_kf_origin_y_->hide();
    spn_opacity_->hide();
    chk_scene_mask_->hide();

    /* ── Transform ── */
    transform_box_ = new QWidget(inner);
    transform_box_->setStyleSheet(QStringLiteral("background:%1;").arg(section_bg_name));
    auto *tform_layout = new QVBoxLayout(transform_box_);
    tform_layout->setContentsMargins(14, 0, 14, 12);
    tform_layout->setSpacing(8);

    auto *transform_header = new QWidget(transform_box_);
    transform_header->setStyleSheet("background:transparent;");
    auto *transform_header_layout = new QHBoxLayout(transform_header);
    transform_header_layout->setContentsMargins(0, 8, 0, 0);
    transform_header_layout->setSpacing(8);
    auto *transform_title = new QLabel(obsgs_tr("OBSTitles.Transform"), transform_header);
    transform_title->setStyleSheet(QStringLiteral("color:%1;font-size:14px;background:transparent;").arg(panel_text_name));
    transform_header_layout->addWidget(transform_title);
    transform_header_layout->addStretch();
    btn_transform_defaults_ = new QPushButton(QStringLiteral("Defaults"), transform_header);
    btn_transform_defaults_->setFixedHeight(22);
    btn_transform_defaults_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    btn_transform_defaults_->setIconSize(QSize(13, 13));
    btn_transform_defaults_->setToolTip(QStringLiteral("Restore Transform defaults"));
    btn_transform_defaults_->setStyleSheet(push_button_style);
    transform_header_layout->addWidget(btn_transform_defaults_);
    tform_layout->addWidget(transform_header);

    auto *transform_grid = new QGridLayout();
    transform_grid->setContentsMargins(0, 4, 0, 0);
    transform_grid->setHorizontalSpacing(8);
    transform_grid->setVerticalSpacing(8);
    transform_grid->setColumnMinimumWidth(0, 24);
    transform_grid->setColumnMinimumWidth(1, 82);
    transform_grid->setColumnMinimumWidth(2, 86);
    transform_grid->setColumnMinimumWidth(3, 22);
    transform_grid->setColumnMinimumWidth(4, 86);
    transform_grid->setColumnStretch(5, 1);

    auto style_transform_spin = [&](QDoubleSpinBox *spin) {
        spin->setFixedSize(78, 22);
        spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        spin->setPrefix(QString());
        spin->setStyleSheet(QStringLiteral("QDoubleSpinBox{color:%1;background:transparent;border:none;"
                            "padding:1px 2px;font-size:12px;selection-background-color:%2;}"
                            "QDoubleSpinBox::up-button,QDoubleSpinBox::down-button{width:12px;background:%3;"
                            "border-left:1px solid %4;}"
                            "QDoubleSpinBox::up-button:hover,QDoubleSpinBox::down-button:hover{background:%5;}")
                            .arg(control_text_name, highlight_name, button_bg_name, border_name, hover_bg_name));
    };
    style_transform_spin(spn_px_);
    style_transform_spin(spn_py_);
    style_transform_spin(spn_scale_x_);
    style_transform_spin(spn_scale_y_);
    style_transform_spin(spn_origin_x_);
    style_transform_spin(spn_origin_y_);
    style_transform_spin(spn_rot_);
    spn_rot_->setPrefix(QString());

    auto make_transform_field = [&](const QString &label, QDoubleSpinBox *spin) {
        auto *field = new QWidget(transform_box_);
        field->setObjectName(QStringLiteral("OBSTitlesTransformNumericField"));
        field->setStyleSheet(QStringLiteral("QWidget#OBSTitlesTransformNumericField{background:%1;border:1px solid %2;"
                             "border-radius:2px;}").arg(control_bg_name, border_name));
        auto *field_layout = new QHBoxLayout(field);
        field_layout->setContentsMargins(5, 0, 0, 0);
        field_layout->setSpacing(2);
        auto *field_label = new NumericDragLabel(label, spin, field,
                                                 [this]() {
                                                     if (loading_values_) return;
                                                     numeric_label_dragging_ = true;
                                                     emit property_changed(true);
                                                 },
                                                 [this]() {
                                                     if (loading_values_) return;
                                                     numeric_label_dragging_ = false;
                                                     emit property_changed(true);
                                                 });
        field_label->setFixedWidth(16);
        field_label->setAlignment(Qt::AlignCenter);
        field_label->setStyleSheet(QStringLiteral("QLabel{color:%1;background:transparent;font-size:11px;padding:0;}").arg(subtle_text_name));
        field_layout->addWidget(field_label);
        field_layout->addWidget(spin);
        field->setFixedSize(104, 24);
        return field;
    };

    QWidget *field_pos_x = make_transform_field(QStringLiteral("X"), spn_px_);
    QWidget *field_pos_y = make_transform_field(QStringLiteral("Y"), spn_py_);
    QWidget *field_scale_x = make_transform_field(QStringLiteral("W"), spn_scale_x_);
    QWidget *field_scale_y = make_transform_field(QStringLiteral("H"), spn_scale_y_);
    QWidget *field_origin_x = make_transform_field(QStringLiteral("X"), spn_origin_x_);
    QWidget *field_origin_y = make_transform_field(QStringLiteral("Y"), spn_origin_y_);
    QWidget *field_rotation = make_transform_field(QStringLiteral("R"), spn_rot_);

    btn_anchor_grid_ = new AnchorGridButton(transform_box_);
    btn_anchor_grid_->setFixedSize(36, 36);
    btn_anchor_grid_->setToolTip(obsgs_tr("OBSTitles.AnchorChangeTooltip"));
    if (auto *anchor_button = static_cast<AnchorGridButton *>(btn_anchor_grid_)) {
        anchor_button->anchor_selected = [this](int i) {
            if (cmb_anchor_) cmb_anchor_->setCurrentIndex(i);
        };
    }

    chk_scale_lock_->setText(QString());
    chk_scale_lock_->setToolTip(obsgs_tr("OBSTitles.ScaleLock"));
    chk_scale_lock_->setFixedSize(24, 24);

    const QString transform_label_style = QStringLiteral("color:%1;font-size:12px;background:transparent;").arg(panel_text_name);
    auto add_transform_row = [&](int row, QPushButton *kf, const QString &label, QWidget *drag_field, QWidget *first,
                                 QWidget *middle, QWidget *second) {
        transform_grid->addWidget(kf, row, 0, Qt::AlignCenter);
        auto *text = new NumericDragLabel(label, drag_field, transform_box_,
                                          [this]() {
                                              if (loading_values_) return;
                                              numeric_label_dragging_ = true;
                                              emit property_changed(true);
                                          },
                                          [this]() {
                                              if (loading_values_) return;
                                              numeric_label_dragging_ = false;
                                              emit property_changed(true);
                                          });
        text->setStyleSheet(transform_label_style);
        transform_grid->addWidget(text, row, 1, Qt::AlignVCenter);
        transform_grid->addWidget(first, row, 2, Qt::AlignLeft);
        if (middle) transform_grid->addWidget(middle, row, 3, Qt::AlignCenter);
        if (second) transform_grid->addWidget(second, row, 4, Qt::AlignLeft);
    };

    add_transform_row(0, btn_kf_pos_x_, QStringLiteral("Location"), field_pos_x, field_pos_x, nullptr, field_pos_y);
    add_transform_row(1, btn_kf_scale_x_, QStringLiteral("Scale"), field_scale_x, field_scale_x, chk_scale_lock_, field_scale_y);
    auto *anchor_label = new NumericDragLabel(QStringLiteral("Anchor"), field_origin_x, transform_box_,
                                              [this]() {
                                                  if (loading_values_) return;
                                                  numeric_label_dragging_ = true;
                                                  emit property_changed(true);
                                              },
                                              [this]() {
                                                  if (loading_values_) return;
                                                  numeric_label_dragging_ = false;
                                                  emit property_changed(true);
                                              });
    anchor_label->setStyleSheet(transform_label_style);
    transform_grid->addWidget(anchor_label, 2, 1, Qt::AlignVCenter);
    transform_grid->addWidget(btn_kf_origin_x_, 2, 0, Qt::AlignCenter);
    transform_grid->addWidget(field_origin_x, 2, 2, Qt::AlignLeft);
    transform_grid->addWidget(field_origin_y, 2, 4, Qt::AlignLeft);
    add_transform_row(3, btn_kf_rotation_, QStringLiteral("Rotation"), field_rotation, field_rotation, nullptr, btn_anchor_grid_);

    cmb_anchor_->hide();
    tform_layout->addLayout(transform_grid);
    vl->addWidget(transform_box_);

    /* ── Appearance ── */
    appearance_box_ = new QWidget(inner);
    appearance_box_->setStyleSheet(QStringLiteral("background:%1;").arg(section_bg_name));
    auto *appearance_layout = new QVBoxLayout(appearance_box_);
    appearance_layout->setContentsMargins(14, 2, 14, 12);
    appearance_layout->setSpacing(8);

    auto *appearance_title = new QLabel(QStringLiteral("Appearance"), appearance_box_);
    appearance_title->setStyleSheet(QStringLiteral("color:%1;font-size:14px;background:transparent;").arg(panel_text_name));
    appearance_layout->addWidget(appearance_title);

    auto *appearance_grid = new QGridLayout();
    appearance_grid->setContentsMargins(0, 4, 0, 0);
    appearance_grid->setHorizontalSpacing(8);
    appearance_grid->setVerticalSpacing(8);
    appearance_grid->setColumnMinimumWidth(0, 24);
    appearance_grid->setColumnMinimumWidth(1, 82);
    appearance_grid->setColumnMinimumWidth(2, 48);
    appearance_grid->setColumnMinimumWidth(3, 32);
    appearance_grid->setColumnMinimumWidth(4, 86);
    appearance_grid->setColumnStretch(5, 1);

    auto make_swatch = [&](const QString &tip) {
        auto *button = new QPushButton(appearance_box_);
        button->setFixedSize(32, 32);
        button->setToolTip(tip);
        button->setText(QString());
        return button;
    };
    btn_appearance_fill_color_ = make_swatch(obsgs_tr("OBSTitles.FillColorTooltip"));
    btn_appearance_stroke_color_ = make_swatch(obsgs_tr("OBSTitles.OutlineColorLabel"));
    spn_appearance_stroke_width_ = mk_dspin(0.0, 200.0, 1.0);
    spn_appearance_stroke_width_->setFixedSize(86, 24);
    spn_appearance_stroke_width_->setSuffix(QStringLiteral("px"));
    spn_appearance_stroke_width_->setDecimals(0);
    spn_appearance_stroke_width_->setStyleSheet(control_style);
    spn_appearance_opacity_ = mk_dspin(0.0, 100.0, 1.0);
    spn_appearance_opacity_->setFixedSize(86, 24);
    spn_appearance_opacity_->setSuffix(QStringLiteral("%"));
    spn_appearance_opacity_->setDecimals(0);
    spn_appearance_opacity_->setStyleSheet(control_style);
    chk_scene_mask_->setText(QString());
    chk_scene_mask_->show();
    chk_scene_mask_->setFixedSize(24, 24);
    chk_scene_mask_->setStyleSheet(checkbox_style);

    btn_kf_appearance_fill_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleFillColorKeyframe"));
    btn_kf_appearance_stroke_ = mk_kf_button(obsgs_tr("OBSTitles.OutlineColorLabel"));
    btn_kf_appearance_stroke_->setEnabled(false);
    btn_kf_appearance_opacity_ = btn_kf_opacity_;
    btn_kf_appearance_opacity_->show();
    auto *stroke_options_trigger = new QPushButton(appearance_box_);
    stroke_options_trigger->hide();
    btn_appearance_stroke_label_ = new StrokeOptionsLabel(
        QStringLiteral("Stroke"), spn_appearance_stroke_width_, appearance_box_,
        [stroke_options_trigger]() { stroke_options_trigger->click(); },
        [this]() {
            if (loading_values_) return;
            numeric_label_dragging_ = true;
            emit property_changed(true);
        },
        [this]() {
            if (loading_values_) return;
            numeric_label_dragging_ = false;
            emit property_changed(true);
        });
    btn_appearance_stroke_label_->setStyleSheet(
        QStringLiteral("QLabel{color:%1;background:transparent;text-decoration:underline;font-size:12px;padding:0;}"
        "QLabel:hover{color:%2;}").arg(panel_text_name, highlighted_text_name));

    auto add_appearance_row = [&](int row, QPushButton *kf, const QString &label, QWidget *primary,
                                  QWidget *secondary = nullptr, QWidget *label_widget = nullptr) {
        appearance_grid->addWidget(kf, row, 0, Qt::AlignCenter);
        QWidget *text = label_widget;
        if (!text) {
            text = new NumericDragLabel(label, secondary ? secondary : primary, appearance_box_,
                                        [this]() {
                                            if (loading_values_) return;
                                            numeric_label_dragging_ = true;
                                            emit property_changed(true);
                                        },
                                        [this]() {
                                            if (loading_values_) return;
                                            numeric_label_dragging_ = false;
                                            emit property_changed(true);
                                        });
            text->setStyleSheet(transform_label_style);
        }
        appearance_grid->addWidget(text, row, 1, Qt::AlignVCenter);
        if (primary)
            appearance_grid->addWidget(primary, row, 2, Qt::AlignLeft | Qt::AlignVCenter);
        if (secondary)
            appearance_grid->addWidget(secondary, row, 4, Qt::AlignLeft | Qt::AlignVCenter);
    };

    add_appearance_row(0, btn_kf_appearance_fill_, QStringLiteral("Fill"), btn_appearance_fill_color_);
    add_appearance_row(1, btn_kf_appearance_stroke_, QStringLiteral("Stroke"), btn_appearance_stroke_color_,
                       spn_appearance_stroke_width_, btn_appearance_stroke_label_);
    add_appearance_row(2, btn_kf_appearance_opacity_, QStringLiteral("Opacity"), nullptr,
                       spn_appearance_opacity_);
    auto *scene_mask_label = new QLabel(QStringLiteral("Set as Scene Mask"), appearance_box_);
    scene_mask_label->setStyleSheet(transform_label_style);
    appearance_grid->addWidget(scene_mask_label, 3, 1, 1, 3, Qt::AlignVCenter);
    appearance_grid->addWidget(chk_scene_mask_, 3, 4, Qt::AlignLeft | Qt::AlignVCenter);
    appearance_layout->addLayout(appearance_grid);
    vl->addWidget(appearance_box_);

    auto make_property_grid = [&](QWidget *parent_widget) {
        auto *grid = new QGridLayout(parent_widget);
        grid->setContentsMargins(6, 5, 6, 6);
        grid->setHorizontalSpacing(5);
        grid->setVerticalSpacing(3);
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);
        return grid;
    };
    auto grid_label = [&](const QString &text, QWidget *parent_widget, QWidget *field = nullptr) {
        auto *label = new NumericDragLabel(text, field, parent_widget,
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = true;
                                               emit property_changed(true);
                                           },
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = false;
                                               emit property_changed(true);
                                           });
        label->setStyleSheet(QStringLiteral("color:%1;font-size:10px;background:transparent;").arg(subtle_text_name));
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return label;
    };
    auto add_grid_field = [&](QGridLayout *grid, int row, int col, const QString &label_text, QWidget *field) {
        QWidget *parent_widget = grid->parentWidget();
        grid->addWidget(grid_label(label_text, parent_widget, field), row, col * 2);
        grid->addWidget(field, row, col * 2 + 1);
    };
    auto add_full_width_field = [&](QGridLayout *grid, int row, const QString &label_text, QWidget *field) {
        QWidget *parent_widget = grid->parentWidget();
        grid->addWidget(grid_label(label_text, parent_widget, field), row, 0);
        grid->addWidget(field, row, 1, 1, 3);
    };
    auto mk_combo = [&](const QStringList &labels, const QList<int> &values) {
        auto *combo = new QComboBox(inner);
        for (int i = 0; i < labels.size(); ++i)
            combo->addItem(labels[i], i < values.size() ? values[i] : i);
        combo->setFixedHeight(22);
        combo->setStyleSheet(control_style);
        return combo;
    };
    auto mk_type_button = [&](const QString &label, const QString &tip) {
        auto *button = new QToolButton(inner);
        button->setText(label);
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setFixedSize(28, 22);
        button->setAutoRaise(false);
        button->setStyleSheet(QStringLiteral(
            "QToolButton{color:%1;background:%2;border:1px solid %3;border-radius:2px;"
            "font-size:10px;font-weight:bold;padding:0;}"
            "QToolButton:hover{background:%4;border-color:%3;}"
            "QToolButton:checked{background:%5;color:%6;border-color:%5;}")
            .arg(button_text_name, button_bg_name, border_name, hover_bg_name,
                 highlight_name, highlighted_text_name));
        return button;
    };
    auto mk_paragraph_alignment_button = [&](const char *icon_name, const QString &tip) {
        auto *button = new QToolButton(inner);
        button->setIcon(obs_icon(icon_name));
        button->setIconSize(QSize(14, 14));
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        button->setCheckable(true);
        button->setFixedSize(30, 24);
        button->setAutoRaise(false);
        button->setStyleSheet(QStringLiteral(
            "QToolButton{color:%1;background:%2;border:1px solid %3;border-radius:2px;padding:2px;}"
            "QToolButton:hover{background:%4;border-color:%3;}"
            "QToolButton:checked{background:%5;color:%6;border-color:%5;}")
            .arg(button_text_name, button_bg_name, border_name, hover_bg_name,
                 highlight_name, highlighted_text_name));
        return button;
    };

    /* ── Character ── */
    text_box_ = new QGroupBox("Character", inner);
    text_box_->setStyleSheet(QStringLiteral(
        "QGroupBox{color:%1;background:%2;border:none;margin:0;padding:0;font-size:14px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}"
        "QLabel{color:%3;font-size:10px;background:transparent;}")
        .arg(panel_text_name, section_bg_name, subtle_text_name));
    auto *char_grid = make_property_grid(text_box_);
    char_grid->setContentsMargins(14, 28, 14, 12);
    char_grid->setHorizontalSpacing(8);
    char_grid->setVerticalSpacing(6);

    txt_content_ = new QTextEdit(inner);
    txt_content_->setAcceptRichText(false);
    txt_content_->setMinimumHeight(72);
    txt_content_->setMaximumHeight(92);
    txt_content_->setPlaceholderText(obsgs_tr("OBSTitles.EnterTextPlaceholder"));
    txt_content_->setStyleSheet(control_style);

    cmb_font_ = new QComboBox(inner);
    cmb_font_->setFixedHeight(22);
    cmb_font_->setEditable(true);
    cmb_font_->setInsertPolicy(QComboBox::NoInsert);
    cmb_font_->setMaxVisibleItems(24);
    cmb_font_->setStyleSheet(control_style);
    QFontDatabase fdb;
    for (auto &fam : fdb.families())
        cmb_font_->addItem(fam, fam);

    cmb_font_style_ = new QComboBox(inner);
    cmb_font_style_->setFixedHeight(22);
    cmb_font_style_->setStyleSheet(control_style);

    spn_size_ = new QSpinBox(inner);
    spn_size_->setRange(6, 500);
    spn_size_->setFixedHeight(22);
    spn_size_->setStyleSheet(control_style);

    cmb_kerning_mode_ = mk_combo({"Metrics", "Optical", "Manual"}, {0, 1, 2});
    spn_kerning_value_ = mk_dspin(-100.0, 500.0, 1.0);
    spn_kerning_value_->setSuffix(" px");
    spn_kerning_value_->setToolTip("Manual kerning adjustment added to tracking.");
    spn_text_leading_ = mk_dspin(-200.0, 500.0, 1.0);
    spn_text_leading_->setSuffix(" px");
    spn_text_leading_->setToolTip(obsgs_tr("OBSTitles.LeadingTooltip"));
    spn_char_tracking_ = mk_dspin(-100.0, 500.0, 1.0);
    spn_char_tracking_->setSuffix(" px");
    spn_char_tracking_->setToolTip(obsgs_tr("OBSTitles.TrackingTooltip"));
    spn_char_scale_x_ = mk_dspin(10.0, 500.0, 1.0);
    spn_char_scale_x_->setSuffix("%");
    spn_char_scale_y_ = mk_dspin(10.0, 500.0, 1.0);
    spn_char_scale_y_->setSuffix("%");
    spn_baseline_shift_ = mk_dspin(-500.0, 500.0, 1.0);
    spn_baseline_shift_->setSuffix(" px");
    cmb_language_ = mk_combo({"English", "Arabic", "Chinese", "French", "German", "Japanese", "Korean", "Portuguese", "Spanish"}, {});
    btn_text_color_ = new QPushButton(inner);
    btn_text_color_->setFixedHeight(22);
    btn_kf_text_color_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleTextColorKeyframe"));

    char_grid->addWidget(grid_label(obsgs_tr("OBSTitles.TextLabel"), text_box_), 0, 0);
    char_grid->addWidget(txt_content_, 0, 1, 1, 3);
    add_full_width_field(char_grid, 1, "Font", cmb_font_);
    add_full_width_field(char_grid, 2, "Style", cmb_font_style_);
    add_grid_field(char_grid, 3, 0, "Size", spn_size_);
    add_grid_field(char_grid, 3, 1, "Leading", spn_text_leading_);
    add_grid_field(char_grid, 4, 0, "Kerning", cmb_kerning_mode_);
    add_grid_field(char_grid, 4, 1, "Value", spn_kerning_value_);
    add_grid_field(char_grid, 5, 0, "H Scale", spn_char_scale_x_);
    add_grid_field(char_grid, 5, 1, "V Scale", spn_char_scale_y_);
    add_grid_field(char_grid, 6, 0, "Tracking", spn_char_tracking_);
    add_grid_field(char_grid, 6, 1, "Baseline", spn_baseline_shift_);
    row_text_color_ = with_kf(btn_text_color_, btn_kf_text_color_);
    add_full_width_field(char_grid, 7, "Fill Color", row_text_color_);
    add_grid_field(char_grid, 8, 0, "Language", cmb_language_);
    vl->addWidget(text_box_);

    /* ── Type Options ── */
    type_options_box_ = new QGroupBox("Type", inner);
    type_options_box_->setStyleSheet(QStringLiteral(
        "QGroupBox{color:%1;background:%2;border:none;margin:0;padding:0;font-size:14px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}")
        .arg(panel_text_name, section_bg_name));
    auto *type_grid = new QGridLayout(type_options_box_);
    type_grid->setContentsMargins(14, 28, 14, 12);
    type_grid->setHorizontalSpacing(4);
    type_grid->setVerticalSpacing(4);
    chk_bold_ = mk_type_button("B", obsgs_tr("OBSTitles.Bold"));
    chk_italic_ = mk_type_button("I", obsgs_tr("OBSTitles.Italic"));
    chk_font_kerning_ = mk_type_button("K", obsgs_tr("OBSTitles.Kerning"));
    chk_font_kerning_->setToolTip(obsgs_tr("OBSTitles.KerningTooltip"));
    btn_all_caps_ = mk_type_button("TT", "All Caps");
    btn_small_caps_ = mk_type_button("Tᴛ", "Small Caps");
    btn_superscript_ = mk_type_button("x²", "Superscript");
    btn_subscript_ = mk_type_button("x₂", "Subscript");
    btn_underline_ = mk_type_button("U", "Underline");
    btn_strikethrough_ = mk_type_button("S", "Strikethrough");
    btn_ligatures_ = mk_type_button("fi", "Ligatures");
    btn_stylistic_alternates_ = mk_type_button("Sw", "Stylistic Alternates");
    btn_fractions_ = mk_type_button("½", "Fractions");
    btn_opentype_features_ = mk_type_button("OT", "OpenType Features");
    QList<QToolButton *> type_buttons{chk_bold_, chk_italic_, btn_all_caps_, btn_small_caps_, btn_superscript_,
                                      btn_subscript_, btn_underline_, btn_strikethrough_, btn_ligatures_, btn_stylistic_alternates_,
                                      btn_fractions_, btn_opentype_features_, chk_font_kerning_};
    for (int i = 0; i < type_buttons.size(); ++i) type_grid->addWidget(type_buttons[i], i / 5, i % 5);
    type_grid->setColumnStretch(5, 1);
    vl->addWidget(type_options_box_);

    /* ── Paragraph ── */
    paragraph_box_ = new QGroupBox("Paragraph", inner);
    paragraph_box_->setStyleSheet(QStringLiteral(
        "QGroupBox{color:%1;background:%2;border:none;margin:0;padding:0;font-size:14px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}"
        "QLabel{color:%3;font-size:10px;background:transparent;}")
        .arg(panel_text_name, section_bg_name, subtle_text_name));
    auto *paragraph_layout = new QVBoxLayout(paragraph_box_);
    paragraph_layout->setContentsMargins(14, 28, 14, 12);
    paragraph_layout->setSpacing(7);

    auto add_paragraph_button = [&](QHBoxLayout *layout, QButtonGroup *group,
                                    const char *icon_name, const QString &tip, int id) {
        auto *button = mk_paragraph_alignment_button(icon_name, tip);
        group->addButton(button, id);
        layout->addWidget(button);
        return button;
    };
    auto add_paragraph_gap = [](QHBoxLayout *layout) {
        auto *gap = new QWidget();
        gap->setFixedWidth(12);
        layout->addWidget(gap);
    };
    auto mk_paragraph_spin = [&]() {
        auto *spin = mk_dspin(-10000.0, 10000.0, 1.0);
        spin->setSuffix(QStringLiteral(" pt"));
        spin->setDecimals(0);
        spin->setFixedWidth(94);
        return spin;
    };
    auto add_metric_control = [&](QGridLayout *grid, int row, int column,
                                  const char *icon_name, const QString &tip, QDoubleSpinBox *spin, QWidget *field) {
        auto *icon = new NumericDragLabel(QString(), field, paragraph_box_,
                                          [this]() {
                                              if (loading_values_) return;
                                              numeric_label_dragging_ = true;
                                              emit property_changed(true);
                                          },
                                          [this]() {
                                              if (loading_values_) return;
                                              numeric_label_dragging_ = false;
                                              emit property_changed(true);
                                          });
        icon->setPixmap(obs_icon(icon_name).pixmap(16, 16));
        icon->setToolTip(QStringLiteral("%1\n%2").arg(tip, obsgs_tr("OBSTitles.DragNumericLabelTooltip")));
        icon->setAccessibleName(tip);
        icon->setFixedWidth(20);
        icon->setAlignment(Qt::AlignCenter);
        spin->setToolTip(tip);
        grid->addWidget(icon, row, column * 2, Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(field, row, column * 2 + 1);
    };

    auto *horizontal_buttons = new QWidget(paragraph_box_);
    auto *horizontal_button_layout = new QHBoxLayout(horizontal_buttons);
    horizontal_button_layout->setContentsMargins(0, 0, 0, 0);
    horizontal_button_layout->setSpacing(4);
    grp_text_align_ = new QButtonGroup(horizontal_buttons);
    grp_text_align_->setExclusive(true);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "align-left.svg", obsgs_tr("OBSTitles.AlignLeft"), 0);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "align-center.svg", obsgs_tr("OBSTitles.AlignCenter"), 1);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "align-right.svg", obsgs_tr("OBSTitles.AlignRight"), 2);
    add_paragraph_gap(horizontal_button_layout);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "align-justify-left.svg", obsgs_tr("OBSTitles.JustifyLastLeft"), 3);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "align-justify-center.svg", obsgs_tr("OBSTitles.JustifyLastCenter"), 4);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "align-justify-right.svg", obsgs_tr("OBSTitles.JustifyLastRight"), 5);
    add_paragraph_gap(horizontal_button_layout);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "align-justify.svg", obsgs_tr("OBSTitles.JustifyAll"), 6);
    horizontal_button_layout->addStretch(1);
    paragraph_layout->addWidget(horizontal_buttons);

    auto *vertical_buttons = new QWidget(paragraph_box_);
    auto *vertical_button_layout = new QHBoxLayout(vertical_buttons);
    vertical_button_layout->setContentsMargins(0, 0, 0, 0);
    vertical_button_layout->setSpacing(4);
    grp_text_valign_ = new QButtonGroup(vertical_buttons);
    grp_text_valign_->setExclusive(true);
    add_paragraph_button(vertical_button_layout, grp_text_valign_, "align-top.svg", obsgs_tr("OBSTitles.AlignTop"), 0);
    add_paragraph_button(vertical_button_layout, grp_text_valign_, "align-vertical-center.svg", obsgs_tr("OBSTitles.AlignMiddle"), 1);
    add_paragraph_button(vertical_button_layout, grp_text_valign_, "align-bottom.svg", obsgs_tr("OBSTitles.AlignBottom"), 2);
    vertical_button_layout->addStretch(1);
    paragraph_layout->addWidget(vertical_buttons);

    spn_paragraph_indent_left_ = mk_paragraph_spin();
    spn_paragraph_indent_right_ = mk_paragraph_spin();
    spn_paragraph_indent_first_line_ = mk_paragraph_spin();
    btn_kf_paragraph_indent_left_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleParagraphIndentLeftKeyframe"));
    btn_kf_paragraph_indent_right_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleParagraphIndentRightKeyframe"));
    btn_kf_paragraph_indent_first_line_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleParagraphIndentFirstLineKeyframe"));
    auto *paragraph_indent_left_field = with_kf(spn_paragraph_indent_left_, btn_kf_paragraph_indent_left_);
    auto *paragraph_indent_right_field = with_kf(spn_paragraph_indent_right_, btn_kf_paragraph_indent_right_);
    auto *paragraph_indent_first_line_field = with_kf(spn_paragraph_indent_first_line_, btn_kf_paragraph_indent_first_line_);
    spn_paragraph_space_before_ = mk_paragraph_spin();
    spn_paragraph_space_after_ = mk_paragraph_spin();
    auto *metric_grid = new QGridLayout();
    metric_grid->setContentsMargins(0, 0, 0, 0);
    metric_grid->setHorizontalSpacing(8);
    metric_grid->setVerticalSpacing(4);
    metric_grid->setColumnStretch(1, 1);
    metric_grid->setColumnStretch(3, 1);
    add_metric_control(metric_grid, 0, 0, "paragraph-indent-left.svg", obsgs_tr("OBSTitles.ParagraphIndentLeft"), spn_paragraph_indent_left_, paragraph_indent_left_field);
    add_metric_control(metric_grid, 0, 1, "paragraph-indent-right.svg", obsgs_tr("OBSTitles.ParagraphIndentRight"), spn_paragraph_indent_right_, paragraph_indent_right_field);
    add_metric_control(metric_grid, 1, 0, "paragraph-indent-first-line.svg", obsgs_tr("OBSTitles.ParagraphIndentFirstLine"), spn_paragraph_indent_first_line_, paragraph_indent_first_line_field);
    add_metric_control(metric_grid, 2, 0, "paragraph-space-before.svg", obsgs_tr("OBSTitles.ParagraphSpaceBefore"), spn_paragraph_space_before_, spn_paragraph_space_before_);
    add_metric_control(metric_grid, 2, 1, "paragraph-space-after.svg", obsgs_tr("OBSTitles.ParagraphSpaceAfter"), spn_paragraph_space_after_, spn_paragraph_space_after_);
    paragraph_layout->addLayout(metric_grid);

    chk_paragraph_hyphenate_ = new QCheckBox(obsgs_tr("OBSTitles.Hyphenate"), paragraph_box_);
    style_checkbox(chk_paragraph_hyphenate_);
    paragraph_layout->addWidget(chk_paragraph_hyphenate_);

    vl->addWidget(paragraph_box_);

    /* ── Dynamic Text ── */
    dynamic_text_box_ = new QGroupBox("Text Flow", inner);
    dynamic_text_box_->setStyleSheet(QStringLiteral(
        "QGroupBox{color:%1;background:%2;border:none;margin:0;padding:0;font-size:14px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}"
        "QLabel{color:%3;font-size:10px;background:transparent;}")
        .arg(panel_text_name, section_bg_name, subtle_text_name));
    auto *dynamic_form = new QFormLayout(dynamic_text_box_);
    style_form(dynamic_form);
    dynamic_form->setContentsMargins(14, 28, 14, 12);
    dynamic_form->setHorizontalSpacing(8);
    dynamic_form->setVerticalSpacing(6);
    cmb_text_style_ = new QComboBox(inner);
    cmb_text_style_->addItem(obsgs_tr("OBSTitles.Normal"), 0);
    cmb_text_style_->addItem(obsgs_tr("OBSTitles.AllCaps"), 1);
    cmb_text_style_->addItem(obsgs_tr("OBSTitles.SmallCaps"), 2);
    cmb_text_style_->addItem(obsgs_tr("OBSTitles.Superscript"), 3);
    cmb_text_style_->addItem(obsgs_tr("OBSTitles.Subscript"), 4);
    cmb_text_style_->setToolTip(obsgs_tr("OBSTitles.TextStyleTooltip"));
    cmb_text_style_->setFixedHeight(22);
    cmb_text_style_->setStyleSheet(control_style);
    cmb_text_overflow_ = new QComboBox(inner);
    cmb_text_overflow_->addItem(obsgs_tr("OBSTitles.Wrap"), 0);
    cmb_text_overflow_->addItem(obsgs_tr("OBSTitles.Clip"), 1);
    cmb_text_overflow_->addItem(obsgs_tr("OBSTitles.HorizontalFit"), 2);
    cmb_text_overflow_->setToolTip(obsgs_tr("OBSTitles.TextOverflowTooltip"));
    cmb_text_overflow_->setFixedHeight(22);
    cmb_text_overflow_->setStyleSheet(control_style);
    spn_text_fit_min_scale_ = mk_dspin(0.05, 1.0, 0.05);
    spn_text_fit_min_scale_->setDecimals(2);
    spn_text_fit_min_scale_->setToolTip(obsgs_tr("OBSTitles.MinFitScaleTooltip"));
    lbl_text_fit_scale_ = new QLabel(obsgs_tr("OBSTitles.Scale100"), inner);
    lbl_text_fit_scale_->setStyleSheet(QStringLiteral("color:%1;font-size:10px;background:transparent;").arg(subtle_text_name));
    chk_expose_text_ = new QCheckBox(obsgs_tr("OBSTitles.ExposeInDock"), inner);
    chk_expose_text_->setToolTip(obsgs_tr("OBSTitles.ExposeInDockTooltip"));
    style_checkbox(chk_expose_text_);
    cmb_ticker_style_ = new QComboBox(inner);
    cmb_ticker_style_->addItem(obsgs_tr("OBSTitles.TickerHorizontal"), 0);
    cmb_ticker_style_->addItem(obsgs_tr("OBSTitles.TickerVerticalLine"), 1);
    cmb_ticker_style_->addItem(obsgs_tr("OBSTitles.TickerVerticalSmooth"), 2);
    cmb_ticker_style_->setFixedHeight(22);
    cmb_ticker_style_->setStyleSheet(control_style);
    spn_ticker_speed_ = mk_dspin(1.0, 5000.0, 1.0);
    spn_ticker_speed_->setSuffix(" px/s");
    spn_ticker_line_hold_ = new TimecodeSpinBox(inner);
    spn_ticker_line_hold_->setRange(0.1, 60.0);
    spn_ticker_line_hold_->setFixedHeight(22);
    spn_ticker_line_hold_->setStyleSheet(control_style);
    cmb_ticker_direction_ = new QComboBox(inner);
    cmb_ticker_direction_->setFixedHeight(22);
    cmb_ticker_direction_->setStyleSheet(control_style);
    add_form_row(dynamic_form, "Text Style", cmb_text_style_);
    add_form_row(dynamic_form, obsgs_tr("OBSTitles.OverflowLabel"), cmb_text_overflow_);
    add_form_row(dynamic_form, obsgs_tr("OBSTitles.MinFitScaleLabel"), spn_text_fit_min_scale_);
    add_form_row(dynamic_form, "", lbl_text_fit_scale_);
    add_form_row(dynamic_form, obsgs_tr("OBSTitles.LiveEditLabel"), chk_expose_text_);
    add_form_row(dynamic_form, obsgs_tr("OBSTitles.TickerStyleLabel"), cmb_ticker_style_);
    add_form_row(dynamic_form, obsgs_tr("OBSTitles.TickerSpeedLabel"), spn_ticker_speed_);
    add_form_row(dynamic_form, obsgs_tr("OBSTitles.TickerLineHoldLabel"), spn_ticker_line_hold_);
    add_form_row(dynamic_form, obsgs_tr("OBSTitles.DirectionLabel"), cmb_ticker_direction_);
    vl->addWidget(dynamic_text_box_);

    /* ── Bullets and Numbering ── */
    bullets_box_ = new QGroupBox("Bullets and Numbering", inner);
    bullets_box_->setStyleSheet(section_style);
    auto *bullets_layout = new QVBoxLayout(bullets_box_);
    bullets_layout->setContentsMargins(6, 5, 6, 6);
    auto *bullets_hint = new QLabel("Broadcast lower thirds typically use manual bullet glyphs; this group is ready for list presets.", inner);
    bullets_hint->setWordWrap(true);
    bullets_hint->setStyleSheet("color:#8f8f8f;font-size:10px;");
    bullets_layout->addWidget(bullets_hint);
    vl->addWidget(bullets_box_);
    bullets_box_->hide();

    /* ── Shape ── */
    rect_box_ = new QGroupBox(inner);
    rect_box_->setTitle(QString());
    rect_box_->setStyleSheet(QStringLiteral("QGroupBox{background:%1;border:none;margin:0;padding:0;}").arg(section_bg_name));
    auto *shape_layout = new QVBoxLayout(rect_box_);
    shape_layout->setContentsMargins(14, 0, 14, 12);
    shape_layout->setSpacing(8);

    auto *shape_header = new QWidget(rect_box_);
    shape_header->setStyleSheet("background:transparent;");
    auto *shape_header_layout = new QHBoxLayout(shape_header);
    shape_header_layout->setContentsMargins(0, 8, 0, 0);
    shape_header_layout->setSpacing(8);
    auto *shape_title = new QLabel(QStringLiteral("Shape"), shape_header);
    shape_title->setObjectName(QStringLiteral("OBSTitlesShapePanelTitle"));
    shape_title->setStyleSheet(QStringLiteral("color:%1;font-size:14px;background:transparent;").arg(panel_text_name));
    shape_header_layout->addWidget(shape_title);
    shape_header_layout->addStretch();
    btn_shape_defaults_ = new QPushButton(QStringLiteral("Defaults"), shape_header);
    btn_shape_defaults_->setFixedHeight(22);
    btn_shape_defaults_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    btn_shape_defaults_->setIconSize(QSize(13, 13));
    btn_shape_defaults_->setToolTip(QStringLiteral("Restore Shape defaults"));
    btn_shape_defaults_->setStyleSheet(push_button_style);
    shape_header_layout->addWidget(btn_shape_defaults_);
    shape_layout->addWidget(shape_header);

    auto *shape_types_row = new QWidget(rect_box_);
    shape_types_row->setObjectName(QStringLiteral("OBSTitlesShapeTypeButtonsRow"));
    shape_types_row->setStyleSheet("background:transparent;");
    auto *shape_types_layout = new QHBoxLayout(shape_types_row);
    shape_types_layout->setContentsMargins(0, 0, 0, 0);
    shape_types_layout->setSpacing(4);
    grp_shape_type_ = new QButtonGroup(shape_types_row);
    grp_shape_type_->setExclusive(true);
    auto add_shape_button = [&](ShapeType shape_type) {
        auto *button = new QToolButton(shape_types_row);
        button->setCheckable(true);
        button->setIcon(shape_tool_icon(shape_type));
        button->setIconSize(QSize(18, 18));
        button->setFixedSize(30, 28);
        button->setToolTip(shape_display_name(shape_type));
        button->setAccessibleName(shape_display_name(shape_type));
        button->setStyleSheet(QStringLiteral(
            "QToolButton{color:%1;background:%2;border:1px solid %3;border-radius:2px;padding:0;}"
            "QToolButton:hover{background:%4;border-color:%3;}"
            "QToolButton:checked{background:%5;color:%6;border-color:%5;}")
            .arg(button_text_name, button_bg_name, border_name, hover_bg_name, highlight_name, highlighted_text_name));
        grp_shape_type_->addButton(button, (int)shape_type);
        shape_types_layout->addWidget(button);
    };
    for (ShapeType shape_type : {ShapeType::Rectangle, ShapeType::RoundedRectangle, ShapeType::Ellipse,
                                 ShapeType::Triangle, ShapeType::Star, ShapeType::Polygon,
                                 ShapeType::Diamond, ShapeType::Line}) {
        add_shape_button(shape_type);
    }
    shape_types_layout->addStretch(1);
    shape_layout->addWidget(shape_types_row);

    auto *shape_grid = new QGridLayout();
    shape_grid->setContentsMargins(0, 0, 0, 0);
    shape_grid->setHorizontalSpacing(8);
    shape_grid->setVerticalSpacing(8);
    shape_grid->setColumnMinimumWidth(0, 24);
    shape_grid->setColumnMinimumWidth(1, 82);
    shape_grid->setColumnMinimumWidth(2, 86);
    shape_grid->setColumnMinimumWidth(3, 22);
    shape_grid->setColumnMinimumWidth(4, 86);
    shape_grid->setColumnStretch(5, 1);

    auto make_shape_field = [&](const QString &label, QDoubleSpinBox *spin, const QIcon &icon = QIcon()) {
        auto *field = new QWidget(rect_box_);
        field->setObjectName(QStringLiteral("OBSTitlesShapeNumericField"));
        field->setStyleSheet(QStringLiteral("QWidget#OBSTitlesShapeNumericField{background:%1;border:1px solid %2;"
                             "border-radius:2px;}").arg(control_bg_name, border_name));
        auto *field_layout = new QHBoxLayout(field);
        field_layout->setContentsMargins(5, 0, 0, 0);
        field_layout->setSpacing(2);
        auto *field_label = new NumericDragLabel(label, spin, field,
                                                 [this]() {
                                                     if (loading_values_) return;
                                                     numeric_label_dragging_ = true;
                                                     emit property_changed(true);
                                                 },
                                                 [this]() {
                                                     if (loading_values_) return;
                                                     numeric_label_dragging_ = false;
                                                     emit property_changed(true);
                                                 });
        field_label->setFixedWidth(icon.isNull() ? 16 : 18);
        field_label->setAlignment(icon.isNull() ? (Qt::AlignLeft | Qt::AlignVCenter) : Qt::AlignCenter);
        if (!icon.isNull()) {
            field_label->setText(QString());
            field_label->setPixmap(icon.pixmap(QSize(14, 14)));
            field_label->setAccessibleName(label);
        }
        field_label->setStyleSheet(QStringLiteral("color:%1;background:transparent;font-size:12px;").arg(control_text_name));
        field_layout->addWidget(field_label);
        field_layout->addWidget(spin, 1);
        field->setFixedSize(104, 24);
        return field;
    };

    spn_layer_w_ = mk_dspin(0.0, 9999.0, 10.0);
    spn_layer_h_ = mk_dspin(0.0, 9999.0, 10.0);
    chk_text_box_width_to_text_ = new QCheckBox(obsgs_tr("OBSTitles.TextBoxWidthToText"), inner);
    chk_text_box_height_to_text_ = new QCheckBox(obsgs_tr("OBSTitles.TextBoxHeightToText"), inner);
    style_checkbox(chk_text_box_width_to_text_);
    style_checkbox(chk_text_box_height_to_text_);
    spn_max_text_box_width_ = mk_dspin(1.0, 9999.0, 10.0);
    spn_max_text_box_height_ = mk_dspin(1.0, 9999.0, 10.0);
    spn_rect_corner_tl_ = mk_dspin(0.0, 1000.0, 1.0);
    spn_rect_corner_tr_ = mk_dspin(0.0, 1000.0, 1.0);
    spn_rect_corner_br_ = mk_dspin(0.0, 1000.0, 1.0);
    spn_rect_corner_bl_ = mk_dspin(0.0, 1000.0, 1.0);
    cmb_shape_type_ = new QComboBox(inner);
    cmb_shape_type_->addItem("Rectangle", (int)ShapeType::Rectangle);
    cmb_shape_type_->addItem("Rounded Rectangle", (int)ShapeType::RoundedRectangle);
    cmb_shape_type_->addItem("Ellipse", (int)ShapeType::Ellipse);
    cmb_shape_type_->addItem("Triangle", (int)ShapeType::Triangle);
    cmb_shape_type_->addItem("Star", (int)ShapeType::Star);
    cmb_shape_type_->addItem("Polygon", (int)ShapeType::Polygon);
    cmb_shape_type_->addItem("Diamond", (int)ShapeType::Diamond);
    cmb_shape_type_->addItem("Line", (int)ShapeType::Line);
    cmb_shape_type_->setFixedHeight(22);
    cmb_shape_type_->setStyleSheet(control_style);
    cmb_shape_type_->hide();
    spn_shape_points_ = new QSpinBox(inner); spn_shape_points_->setRange(3, 64); spn_shape_points_->setFixedHeight(22); spn_shape_points_->setStyleSheet(control_style);
    spn_shape_sides_ = new QSpinBox(inner); spn_shape_sides_->setRange(3, 64); spn_shape_sides_->setFixedHeight(22); spn_shape_sides_->setStyleSheet(control_style);
    spn_shape_inner_radius_ = mk_dspin(0.0, 1.0, 0.05); spn_shape_inner_radius_->setDecimals(2);
    spn_shape_outer_radius_ = mk_dspin(0.0, 1.0, 0.05); spn_shape_outer_radius_->setDecimals(2);
    spn_shape_roundness_ = mk_dspin(0.0, 1000.0, 1.0);
    btn_kf_width_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleWidthKeyframe"));
    btn_kf_width_->setToolTip(QStringLiteral("Toggle size keyframe"));

    style_transform_spin(spn_layer_w_);
    style_transform_spin(spn_layer_h_);
    style_transform_spin(spn_rect_corner_tl_);
    style_transform_spin(spn_rect_corner_tr_);
    style_transform_spin(spn_rect_corner_br_);
    style_transform_spin(spn_rect_corner_bl_);
    auto *field_width = make_shape_field(QStringLiteral("W"), spn_layer_w_);
    auto *field_height = make_shape_field(QStringLiteral("H"), spn_layer_h_);
    chk_size_lock_ = new TransformLockCheckBox(rect_box_);
    chk_size_lock_->setText(QString());
    chk_size_lock_->setToolTip(obsgs_tr("OBSTitles.LockAspectRatio"));
    chk_size_lock_->setFixedSize(24, 24);
    chk_size_lock_->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *size_label = new QLabel(QStringLiteral("Size"), rect_box_);
    size_label->setStyleSheet(QStringLiteral("color:%1;background:transparent;font-size:13px;").arg(panel_text_name));
    shape_grid->addWidget(btn_kf_width_, 0, 0, Qt::AlignCenter);
    shape_grid->addWidget(size_label, 0, 1);
    shape_grid->addWidget(field_width, 0, 2);
    shape_grid->addWidget(chk_size_lock_, 0, 3, Qt::AlignCenter);
    shape_grid->addWidget(field_height, 0, 4);
    shape_layout->addLayout(shape_grid);

    row_rect_corners_ = new QWidget(rect_box_);
    row_rect_corners_->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *corners_layout = new QGridLayout(row_rect_corners_);
    corners_layout->setContentsMargins(0, 0, 0, 0);
    corners_layout->setHorizontalSpacing(8);
    corners_layout->setVerticalSpacing(6);
    corners_layout->addWidget(make_shape_field(QStringLiteral("TL"), spn_rect_corner_tl_, obs_icon("corner-radius-tl.svg")), 0, 0);
    corners_layout->addWidget(make_shape_field(QStringLiteral("TR"), spn_rect_corner_tr_, obs_icon("corner-radius-tr.svg")), 0, 1);
    chk_corner_lock_ = new TransformLockCheckBox(row_rect_corners_);
    chk_corner_lock_->setText(QString());
    chk_corner_lock_->setToolTip(QStringLiteral("Sync all corner radii"));
    chk_corner_lock_->setFixedSize(24, 24);
    chk_corner_lock_->setStyleSheet(QStringLiteral("background:transparent;"));
    corners_layout->addWidget(chk_corner_lock_, 0, 2, 2, 1, Qt::AlignCenter);
    corners_layout->addWidget(make_shape_field(QStringLiteral("BL"), spn_rect_corner_bl_, obs_icon("corner-radius-bl.svg")), 1, 0);
    corners_layout->addWidget(make_shape_field(QStringLiteral("BR"), spn_rect_corner_br_, obs_icon("corner-radius-br.svg")), 1, 1);

    row_corner_type_ = new QWidget(rect_box_);
    row_corner_type_->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *corner_type_layout = new QHBoxLayout(row_corner_type_);
    corner_type_layout->setContentsMargins(0, 0, 0, 0);
    corner_type_layout->setSpacing(4);
    grp_corner_type_ = new QButtonGroup(row_corner_type_);
    grp_corner_type_->setExclusive(true);
    auto add_corner_type_button = [&](CornerType corner_type, const char *icon_name, const QString &tip) {
        auto *button = new QToolButton(row_corner_type_);
        button->setCheckable(true);
        button->setIcon(obs_icon(icon_name));
        button->setIconSize(QSize(16, 16));
        button->setFixedSize(28, 26);
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        button->setStyleSheet(QStringLiteral(
            "QToolButton{color:%1;background:%2;border:1px solid %3;border-radius:2px;padding:0;}"
            "QToolButton:hover{background:%4;border-color:%3;}"
            "QToolButton:checked{background:%5;color:%6;border-color:%5;}").
            arg(button_text_name, button_bg_name, border_name, hover_bg_name, highlight_name, highlighted_text_name));
        grp_corner_type_->addButton(button, (int)corner_type);
        corner_type_layout->addWidget(button);
    };
    add_corner_type_button(CornerType::Round, "corner-type-round.svg", QStringLiteral("Round"));
    add_corner_type_button(CornerType::Straight, "corner-type-straight.svg", QStringLiteral("Straight"));
    add_corner_type_button(CornerType::Concave, "corner-type-concave.svg", QStringLiteral("Concave"));
    add_corner_type_button(CornerType::Cutout, "corner-type-cutout.svg", QStringLiteral("Cutout"));
    corner_type_layout->addStretch(1);

    auto *shape_form_widget = new QWidget(rect_box_);
    shape_form_widget->setStyleSheet("background:transparent;");
    auto *rfl = new QFormLayout(shape_form_widget);
    style_form(rfl);
    add_form_row(rfl, "", chk_text_box_width_to_text_);
    add_form_row(rfl, obsgs_tr("OBSTitles.MaxTextBoxWidthLabel"), spn_max_text_box_width_);
    add_form_row(rfl, "", chk_text_box_height_to_text_);
    add_form_row(rfl, obsgs_tr("OBSTitles.MaxTextBoxHeightLabel"), spn_max_text_box_height_);
    add_form_row(rfl, obsgs_tr("OBSTitles.CornerLabel"), row_rect_corners_);
    add_form_row(rfl, QStringLiteral("Corner Type"), row_corner_type_);
    add_form_row(rfl, "Points", spn_shape_points_);
    add_form_row(rfl, "Sides", spn_shape_sides_);
    add_form_row(rfl, "Inner Radius", spn_shape_inner_radius_);
    add_form_row(rfl, "Outer Radius", spn_shape_outer_radius_);
    add_form_row(rfl, "Roundness", spn_shape_roundness_);
    cmb_fill_type_ = new QComboBox(inner);
    cmb_fill_type_->addItem(obsgs_tr("OBSTitles.Solid"), 0);
    cmb_fill_type_->addItem(obsgs_tr("OBSTitles.Gradient"), 1);
    cmb_fill_type_->setFixedHeight(22);
    cmb_fill_type_->setStyleSheet(control_style);
    row_fill_type_ = cmb_fill_type_;
    add_form_row(rfl, obsgs_tr("OBSTitles.FillTypeLabel"), row_fill_type_);
    btn_fill_color_ = new QPushButton(inner);
    btn_kf_fill_color_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleFillColorKeyframe"));
    row_fill_color_ = with_kf(btn_fill_color_, btn_kf_fill_color_);
    add_form_row(rfl, obsgs_tr("OBSTitles.ColorLabel"), row_fill_color_);
    shape_layout->addWidget(shape_form_widget);
    vl->addWidget(rect_box_);

    /* ── Gradient Properties ── */
    gradient_box_ = new QGroupBox(obsgs_tr("OBSTitles.GradientProperties"), inner);
    gradient_box_->setStyleSheet(section_style);
    auto *gfl = new QFormLayout(gradient_box_);
    style_form(gfl);
    cmb_gradient_type_ = new QComboBox(inner);
    cmb_gradient_type_->addItem(obsgs_tr("OBSTitles.LinearGradient"), 0);
    cmb_gradient_type_->addItem(obsgs_tr("OBSTitles.RadialGradient"), 1);
    cmb_gradient_type_->setFixedHeight(22);
    cmb_gradient_type_->setStyleSheet(control_style);
    btn_gradient_start_color_ = new QPushButton(inner);
    btn_gradient_end_color_ = new QPushButton(inner);
    spn_gradient_start_pos_ = mk_dspin(0.0, 1.0, 0.01);
    spn_gradient_end_pos_ = mk_dspin(0.0, 1.0, 0.01);
    spn_gradient_start_opacity_ = mk_dspin(0.0, 1.0, 0.01);
    spn_gradient_end_opacity_ = mk_dspin(0.0, 1.0, 0.01);
    spn_gradient_opacity_ = mk_dspin(0.0, 1.0, 0.01);
    spn_gradient_angle_ = mk_dspin(-360.0, 360.0, 1.0);
    spn_gradient_center_x_ = mk_dspin(0.0, 1.0, 0.01);
    spn_gradient_center_y_ = mk_dspin(0.0, 1.0, 0.01);
    spn_gradient_scale_ = mk_dspin(0.01, 10.0, 0.05);
    spn_gradient_focal_x_ = mk_dspin(0.0, 1.0, 0.01);
    spn_gradient_focal_y_ = mk_dspin(0.0, 1.0, 0.01);
    for (auto *spin : std::initializer_list<QDoubleSpinBox *>{spn_gradient_start_pos_, spn_gradient_end_pos_,
                                                               spn_gradient_start_opacity_, spn_gradient_end_opacity_,
                                                               spn_gradient_opacity_, spn_gradient_center_x_,
                                                               spn_gradient_center_y_, spn_gradient_scale_,
                                                               spn_gradient_focal_x_, spn_gradient_focal_y_})
        spin->setDecimals(2);
    spn_gradient_angle_->setSuffix("°");
    add_form_row(gfl, obsgs_tr("OBSTitles.GradientTypeLabel"), cmb_gradient_type_);
    add_form_row(gfl, obsgs_tr("OBSTitles.StartColorLabel"), btn_gradient_start_color_);
    add_form_row(gfl, obsgs_tr("OBSTitles.StartStopLabel"), spn_gradient_start_pos_);
    add_form_row(gfl, obsgs_tr("OBSTitles.StartOpacityLabel"), spn_gradient_start_opacity_);
    add_form_row(gfl, obsgs_tr("OBSTitles.EndColorLabel"), btn_gradient_end_color_);
    add_form_row(gfl, obsgs_tr("OBSTitles.EndStopLabel"), spn_gradient_end_pos_);
    add_form_row(gfl, obsgs_tr("OBSTitles.EndOpacityLabel"), spn_gradient_end_opacity_);
    add_form_row(gfl, obsgs_tr("OBSTitles.OpacityLabel"), spn_gradient_opacity_);
    add_form_row(gfl, obsgs_tr("OBSTitles.AngleLabel"), spn_gradient_angle_);
    add_form_row(gfl, obsgs_tr("OBSTitles.CenterXLabel"), spn_gradient_center_x_);
    add_form_row(gfl, obsgs_tr("OBSTitles.CenterYLabel"), spn_gradient_center_y_);
    add_form_row(gfl, obsgs_tr("OBSTitles.ScaleLabel"), spn_gradient_scale_);
    add_form_row(gfl, obsgs_tr("OBSTitles.FocalXLabel"), spn_gradient_focal_x_);
    add_form_row(gfl, obsgs_tr("OBSTitles.FocalYLabel"), spn_gradient_focal_y_);
    vl->addWidget(gradient_box_);
    make_collapsible(gradient_box_);

    /* ── Background Gradient Properties ── */
    background_gradient_box_ = new QGroupBox(obsgs_tr("OBSTitles.BackgroundGradientProperties"), inner);
    background_gradient_box_->setStyleSheet(section_style);
    auto *bgfl = new QFormLayout(background_gradient_box_);
    style_form(bgfl);
    cmb_background_gradient_type_ = new QComboBox(inner);
    cmb_background_gradient_type_->addItem(obsgs_tr("OBSTitles.LinearGradient"), 0);
    cmb_background_gradient_type_->addItem(obsgs_tr("OBSTitles.RadialGradient"), 1);
    cmb_background_gradient_type_->setFixedHeight(22);
    cmb_background_gradient_type_->setStyleSheet(control_style);
    btn_background_gradient_start_color_ = new QPushButton(inner);
    btn_background_gradient_end_color_ = new QPushButton(inner);
    spn_background_gradient_start_pos_ = mk_dspin(0.0, 1.0, 0.01);
    spn_background_gradient_end_pos_ = mk_dspin(0.0, 1.0, 0.01);
    spn_background_gradient_start_opacity_ = mk_dspin(0.0, 1.0, 0.01);
    spn_background_gradient_end_opacity_ = mk_dspin(0.0, 1.0, 0.01);
    spn_background_gradient_opacity_ = mk_dspin(0.0, 1.0, 0.01);
    spn_background_gradient_angle_ = mk_dspin(-360.0, 360.0, 1.0);
    spn_background_gradient_center_x_ = mk_dspin(0.0, 1.0, 0.01);
    spn_background_gradient_center_y_ = mk_dspin(0.0, 1.0, 0.01);
    spn_background_gradient_scale_ = mk_dspin(0.01, 10.0, 0.05);
    spn_background_gradient_focal_x_ = mk_dspin(0.0, 1.0, 0.01);
    spn_background_gradient_focal_y_ = mk_dspin(0.0, 1.0, 0.01);
    for (auto *spin : std::initializer_list<QDoubleSpinBox *>{spn_background_gradient_start_pos_, spn_background_gradient_end_pos_,
                                                               spn_background_gradient_start_opacity_, spn_background_gradient_end_opacity_,
                                                               spn_background_gradient_opacity_, spn_background_gradient_center_x_,
                                                               spn_background_gradient_center_y_, spn_background_gradient_scale_,
                                                               spn_background_gradient_focal_x_, spn_background_gradient_focal_y_})
        spin->setDecimals(2);
    spn_background_gradient_angle_->setSuffix("°");
    add_form_row(bgfl, obsgs_tr("OBSTitles.GradientTypeLabel"), cmb_background_gradient_type_);
    add_form_row(bgfl, obsgs_tr("OBSTitles.StartColorLabel"), btn_background_gradient_start_color_);
    add_form_row(bgfl, obsgs_tr("OBSTitles.StartStopLabel"), spn_background_gradient_start_pos_);
    add_form_row(bgfl, obsgs_tr("OBSTitles.StartOpacityLabel"), spn_background_gradient_start_opacity_);
    add_form_row(bgfl, obsgs_tr("OBSTitles.EndColorLabel"), btn_background_gradient_end_color_);
    add_form_row(bgfl, obsgs_tr("OBSTitles.EndStopLabel"), spn_background_gradient_end_pos_);
    add_form_row(bgfl, obsgs_tr("OBSTitles.EndOpacityLabel"), spn_background_gradient_end_opacity_);
    add_form_row(bgfl, obsgs_tr("OBSTitles.OpacityLabel"), spn_background_gradient_opacity_);
    add_form_row(bgfl, obsgs_tr("OBSTitles.AngleLabel"), spn_background_gradient_angle_);
    add_form_row(bgfl, obsgs_tr("OBSTitles.CenterXLabel"), spn_background_gradient_center_x_);
    add_form_row(bgfl, obsgs_tr("OBSTitles.CenterYLabel"), spn_background_gradient_center_y_);
    add_form_row(bgfl, obsgs_tr("OBSTitles.ScaleLabel"), spn_background_gradient_scale_);
    add_form_row(bgfl, obsgs_tr("OBSTitles.FocalXLabel"), spn_background_gradient_focal_x_);
    add_form_row(bgfl, obsgs_tr("OBSTitles.FocalYLabel"), spn_background_gradient_focal_y_);
    make_collapsible(background_gradient_box_);
    background_gradient_box_->setVisible(false);

    /* ── Outline ── */
    outline_box_ = new QGroupBox(obsgs_tr("OBSTitles.Outline"), inner);
    outline_box_->setStyleSheet(section_style);
    auto *outline_form = new QFormLayout(outline_box_);
    style_form(outline_form);
    chk_outline_enabled_ = new QCheckBox(obsgs_tr("OBSTitles.EnableOutline"), inner);
    style_checkbox(chk_outline_enabled_);
    cmb_stroke_fill_type_ = new QComboBox(inner);
    cmb_stroke_fill_type_->addItem(obsgs_tr("OBSTitles.None"), 0);
    cmb_stroke_fill_type_->addItem(obsgs_tr("OBSTitles.Color"), 1);
    cmb_stroke_fill_type_->addItem(obsgs_tr("OBSTitles.Gradient"), 2);
    cmb_stroke_fill_type_->setFixedHeight(22);
    cmb_stroke_fill_type_->setStyleSheet(control_style);
    spn_outline_width_ = mk_dspin(0.0, 200.0, 1.0);
    spn_outline_width_->setToolTip(obsgs_tr("OBSTitles.OutlineThicknessTooltip"));
    btn_outline_color_ = new QPushButton(inner);
    row_outline_color_ = btn_outline_color_;
    spn_outline_opacity_ = mk_dspin(0.0, 1.0, 0.05);
    spn_outline_opacity_->setDecimals(2);
    cmb_outline_join_ = new QComboBox(inner);
    cmb_outline_join_->addItem(obsgs_tr("OBSTitles.Miter"), 0);
    cmb_outline_join_->addItem(obsgs_tr("OBSTitles.Round"), 1);
    cmb_outline_join_->addItem(obsgs_tr("OBSTitles.Bevel"), 2);
    cmb_outline_join_->setFixedHeight(22);
    cmb_outline_join_->setStyleSheet(control_style);
    cmb_outline_position_ = new QComboBox(inner);
    cmb_outline_position_->addItem(obsgs_tr("OBSTitles.Back"), 0);
    cmb_outline_position_->addItem(obsgs_tr("OBSTitles.Front"), 1);
    cmb_outline_position_->setFixedHeight(22);
    cmb_outline_position_->setStyleSheet(control_style);
    chk_outline_antialias_ = new QCheckBox(obsgs_tr("OBSTitles.AntialiasOutline"), inner);
    style_checkbox(chk_outline_antialias_);
    cmb_stroke_gradient_type_ = new QComboBox(inner);
    cmb_stroke_gradient_type_->addItem(obsgs_tr("OBSTitles.LinearGradient"), 0);
    cmb_stroke_gradient_type_->addItem(obsgs_tr("OBSTitles.RadialGradient"), 1);
    cmb_stroke_gradient_type_->setFixedHeight(22);
    cmb_stroke_gradient_type_->setStyleSheet(control_style);
    btn_stroke_gradient_start_color_ = new QPushButton(inner);
    btn_stroke_gradient_end_color_ = new QPushButton(inner);
    spn_stroke_gradient_start_pos_ = mk_dspin(0.0, 1.0, 0.01);
    spn_stroke_gradient_end_pos_ = mk_dspin(0.0, 1.0, 0.01);
    spn_stroke_gradient_start_opacity_ = mk_dspin(0.0, 1.0, 0.01);
    spn_stroke_gradient_end_opacity_ = mk_dspin(0.0, 1.0, 0.01);
    spn_stroke_gradient_opacity_ = mk_dspin(0.0, 1.0, 0.01);
    spn_stroke_gradient_angle_ = mk_dspin(-360.0, 360.0, 1.0);
    spn_stroke_gradient_center_x_ = mk_dspin(0.0, 1.0, 0.01);
    spn_stroke_gradient_center_y_ = mk_dspin(0.0, 1.0, 0.01);
    spn_stroke_gradient_scale_ = mk_dspin(0.01, 10.0, 0.05);
    spn_stroke_gradient_focal_x_ = mk_dspin(0.0, 1.0, 0.01);
    spn_stroke_gradient_focal_y_ = mk_dspin(0.0, 1.0, 0.01);
    for (auto *spin : std::initializer_list<QDoubleSpinBox *>{spn_stroke_gradient_start_pos_, spn_stroke_gradient_end_pos_,
                                                               spn_stroke_gradient_start_opacity_, spn_stroke_gradient_end_opacity_,
                                                               spn_stroke_gradient_opacity_, spn_stroke_gradient_center_x_,
                                                               spn_stroke_gradient_center_y_, spn_stroke_gradient_scale_,
                                                               spn_stroke_gradient_focal_x_, spn_stroke_gradient_focal_y_})
        spin->setDecimals(2);
    spn_stroke_gradient_angle_->setSuffix("°");
    add_form_row(outline_form, "", chk_outline_enabled_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.StrokeFillLabel"), cmb_stroke_fill_type_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.ColorLabel"), row_outline_color_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.ThicknessLabel"), spn_outline_width_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.OpacityLabel"), spn_outline_opacity_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.JoinLabel"), cmb_outline_join_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.PositionLabelIndented"), cmb_outline_position_);
    add_form_row(outline_form, "", chk_outline_antialias_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.GradientTypeLabel"), cmb_stroke_gradient_type_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.StartColorLabel"), btn_stroke_gradient_start_color_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.StartStopLabel"), spn_stroke_gradient_start_pos_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.StartOpacityLabel"), spn_stroke_gradient_start_opacity_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.EndColorLabel"), btn_stroke_gradient_end_color_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.EndStopLabel"), spn_stroke_gradient_end_pos_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.EndOpacityLabel"), spn_stroke_gradient_end_opacity_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.OpacityLabel"), spn_stroke_gradient_opacity_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.AngleLabel"), spn_stroke_gradient_angle_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.CenterXLabel"), spn_stroke_gradient_center_x_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.CenterYLabel"), spn_stroke_gradient_center_y_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.ScaleLabel"), spn_stroke_gradient_scale_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.FocalXLabel"), spn_stroke_gradient_focal_x_);
    add_form_row(outline_form, obsgs_tr("OBSTitles.FocalYLabel"), spn_stroke_gradient_focal_y_);
    make_collapsible(outline_box_);
    outline_box_->setVisible(false);

    /* ── Image ── */
    image_box_ = new QWidget(inner);
    image_box_->setStyleSheet(QStringLiteral("background:%1;").arg(section_bg_name));
    auto *image_layout = new QVBoxLayout(image_box_);
    image_layout->setContentsMargins(14, 0, 14, 12);
    image_layout->setSpacing(8);

    auto *image_header = new QWidget(image_box_);
    image_header->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *image_header_layout = new QHBoxLayout(image_header);
    image_header_layout->setContentsMargins(0, 8, 0, 0);
    image_header_layout->setSpacing(8);
    auto *image_title = new QLabel(obsgs_tr("OBSTitles.Image"), image_header);
    image_title->setStyleSheet(QStringLiteral("color:%1;font-size:14px;background:transparent;").arg(panel_text_name));
    image_header_layout->addWidget(image_title);
    image_header_layout->addStretch(1);
    image_layout->addWidget(image_header);

    auto *image_content = new QWidget(image_box_);
    image_content->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *image_content_layout = new QHBoxLayout(image_content);
    image_content_layout->setContentsMargins(0, 0, 0, 0);
    image_content_layout->setSpacing(10);

    lbl_image_preview_ = new QLabel(image_content);
    lbl_image_preview_->setFixedSize(104, 104);
    lbl_image_preview_->setAlignment(Qt::AlignCenter);
    lbl_image_preview_->setStyleSheet(QStringLiteral("background:transparent;"));
    set_image_preview_label(lbl_image_preview_, QString());
    image_content_layout->addWidget(lbl_image_preview_, 0, Qt::AlignTop);

    auto *image_form_widget = new QWidget(image_content);
    image_form_widget->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *image_form = new QFormLayout(image_form_widget);
    style_form(image_form);
    edit_image_path_ = new QLineEdit(image_form_widget);
    edit_image_path_->setFixedHeight(22);
    edit_image_path_->setStyleSheet(control_style);
    btn_pick_image_ = new QPushButton(obsgs_tr("OBSTitles.Browse"), image_form_widget);
    style_push_button(btn_pick_image_);
    spn_layer_w_->setToolTip(obsgs_tr("OBSTitles.ImageWidthTooltip"));
    spn_layer_h_->setToolTip(obsgs_tr("OBSTitles.ImageHeightTooltip"));
    cmb_image_scale_filter_ = new QComboBox(image_form_widget);
    cmb_image_scale_filter_->setFixedHeight(22);
    cmb_image_scale_filter_->setStyleSheet(control_style);
    cmb_image_scale_filter_->addItem(obsgs_tr("OBSTitles.ScaleFilterDisable"), (int)ImageScaleFilter::Disable);
    cmb_image_scale_filter_->addItem(obsgs_tr("OBSTitles.ScaleFilterBilinear"), (int)ImageScaleFilter::Bilinear);
    cmb_image_scale_filter_->addItem(obsgs_tr("OBSTitles.ScaleFilterBicubic"), (int)ImageScaleFilter::Bicubic);
    cmb_image_scale_filter_->addItem(obsgs_tr("OBSTitles.ScaleFilterLanczos"), (int)ImageScaleFilter::Lanczos);
    cmb_image_scale_filter_->addItem(obsgs_tr("OBSTitles.ScaleFilterArea"), (int)ImageScaleFilter::Area);
    add_form_row(image_form, obsgs_tr("OBSTitles.PathLabel"), edit_image_path_);
    add_form_row(image_form, "", btn_pick_image_);
    add_form_row(image_form, obsgs_tr("OBSTitles.Filtering"), cmb_image_scale_filter_);
    image_content_layout->addWidget(image_form_widget, 1);
    image_layout->addWidget(image_content);
    const int image_size_index = vl->indexOf(rect_box_);
    if (image_size_index >= 0)
        vl->insertWidget(image_size_index, image_box_);
    else
        vl->addWidget(image_box_);

    shadow_box_ = new QGroupBox(obsgs_tr("OBSTitles.DropShadow"), inner);
    shadow_box_->setStyleSheet(section_style);
    auto *sfl = new QFormLayout(shadow_box_);
    style_form(sfl);
    chk_shadow_enabled_ = new QCheckBox(obsgs_tr("OBSTitles.EnableShadow"), inner);
    style_checkbox(chk_shadow_enabled_);
    cmb_shadow_preset_ = new QComboBox(inner);
    cmb_shadow_preset_->addItems({obsgs_tr("OBSTitles.Custom"), obsgs_tr("OBSTitles.Soft"), obsgs_tr("OBSTitles.Medium"), obsgs_tr("OBSTitles.Strong"), obsgs_tr("OBSTitles.Broadcast")});
    cmb_shadow_preset_->setFixedHeight(22);
    cmb_shadow_preset_->setStyleSheet(control_style);
    cmb_shadow_blur_type_ = new QComboBox(inner);
    add_shadow_blur_items(cmb_shadow_blur_type_);
    cmb_shadow_blur_type_->setFixedHeight(22);
    cmb_shadow_blur_type_->setStyleSheet(control_style);
    btn_shadow_color_ = new QPushButton(inner);
    spn_shadow_opacity_ = mk_dspin(0.0, 1.0, 0.05);
    spn_shadow_opacity_->setDecimals(2);
    spn_shadow_distance_ = mk_dspin(0.0, 200.0, 1.0);
    spn_shadow_angle_ = mk_dspin(-360.0, 360.0, 5.0);
    spn_shadow_blur_ = mk_dspin(0.0, 100.0, 1.0);
    spn_shadow_spread_ = mk_dspin(0.0, 100.0, 1.0);
    chk_long_shadow_enabled_ = new QCheckBox(obsgs_tr("OBSTitles.EnableLongShadow"), inner);
    style_checkbox(chk_long_shadow_enabled_);
    btn_long_shadow_color_ = new QPushButton(inner);
    spn_long_shadow_opacity_ = mk_dspin(0.0, 1.0, 0.05);
    spn_long_shadow_opacity_->setDecimals(2);
    spn_long_shadow_length_ = mk_dspin(0.0, 1000.0, 5.0);
    spn_long_shadow_angle_ = mk_dspin(-360.0, 360.0, 5.0);
    spn_long_shadow_falloff_ = mk_dspin(0.0, 4.0, 0.1);
    spn_long_shadow_falloff_->setDecimals(2);
    cmb_long_shadow_blur_type_ = new QComboBox(inner);
    cmb_long_shadow_blur_type_->addItem(obsgs_tr("OBSTitles.NoBlur"), (int)LongShadowBlurType::None);
    cmb_long_shadow_blur_type_->addItem(obsgs_tr("OBSTitles.BoxBlur"), (int)LongShadowBlurType::Box);
    cmb_long_shadow_blur_type_->addItem(obsgs_tr("OBSTitles.GaussianBlur"), (int)LongShadowBlurType::Gaussian);
    cmb_long_shadow_blur_type_->addItem(obsgs_tr("OBSTitles.StackFastBlur"), (int)LongShadowBlurType::StackFast);
    cmb_long_shadow_blur_type_->setFixedHeight(22);
    cmb_long_shadow_blur_type_->setStyleSheet(control_style);
    spn_long_shadow_blur_ = mk_dspin(0.0, 100.0, 1.0);
    btn_kf_shadow_enabled_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleShadowEnabledKeyframe"));
    btn_kf_shadow_color_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleShadowColorKeyframe"));
    btn_kf_shadow_opacity_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleShadowOpacityKeyframe"));
    btn_kf_shadow_distance_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleShadowDistanceKeyframe"));
    btn_kf_shadow_angle_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleShadowAngleKeyframe"));
    btn_kf_shadow_blur_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleShadowBlurKeyframe"));
    btn_kf_shadow_spread_ = mk_kf_button(obsgs_tr("OBSTitles.ToggleShadowSpreadKeyframe"));
    add_form_row(sfl, "", with_kf(chk_shadow_enabled_, btn_kf_shadow_enabled_));
    add_form_row(sfl, obsgs_tr("OBSTitles.PresetLabel"), cmb_shadow_preset_);
    add_form_row(sfl, obsgs_tr("OBSTitles.ColorLabel"), with_kf(btn_shadow_color_, btn_kf_shadow_color_));
    add_form_row(sfl, obsgs_tr("OBSTitles.OpacityLabel"), with_kf(spn_shadow_opacity_, btn_kf_shadow_opacity_));
    add_form_row(sfl, obsgs_tr("OBSTitles.DistanceLabel"), with_kf(spn_shadow_distance_, btn_kf_shadow_distance_));
    add_form_row(sfl, obsgs_tr("OBSTitles.AngleLabel"), with_kf(spn_shadow_angle_, btn_kf_shadow_angle_));
    add_form_row(sfl, obsgs_tr("OBSTitles.BlurTypeLabel"), cmb_shadow_blur_type_);
    add_form_row(sfl, obsgs_tr("OBSTitles.BlurLabel"), with_kf(spn_shadow_blur_, btn_kf_shadow_blur_));
    add_form_row(sfl, obsgs_tr("OBSTitles.SpreadLabel"), with_kf(spn_shadow_spread_, btn_kf_shadow_spread_));
    add_form_row(sfl, "", chk_long_shadow_enabled_);
    add_form_row(sfl, obsgs_tr("OBSTitles.LongShadowColor"), btn_long_shadow_color_);
    add_form_row(sfl, obsgs_tr("OBSTitles.LongShadowOpacity"), spn_long_shadow_opacity_);
    add_form_row(sfl, obsgs_tr("OBSTitles.LongShadowLength"), spn_long_shadow_length_);
    add_form_row(sfl, obsgs_tr("OBSTitles.LongShadowAngle"), spn_long_shadow_angle_);
    add_form_row(sfl, obsgs_tr("OBSTitles.LongShadowFalloff"), spn_long_shadow_falloff_);
    add_form_row(sfl, obsgs_tr("OBSTitles.LongShadowBlurType"), cmb_long_shadow_blur_type_);
    add_form_row(sfl, obsgs_tr("OBSTitles.LongShadowBlur"), spn_long_shadow_blur_);
    make_collapsible(shadow_box_);
    shadow_box_->setVisible(false);

    vl->addStretch();
    setWidget(inner);

    /* ── Connect signals → property_changed ── */
    auto emit_change = [this]() { if (!loading_values_) emit property_changed(!numeric_label_dragging_); };
    auto can_edit = [this]() { return layer_ && !loading_values_; };
    auto apply_text_char_format = [this](const RichTextCharFormat &format, uint32_t mask) {
        if (!layer_ || loading_values_) return;
        const bool active = active_text_edit_layer_id_ == layer_->id;
        apply_rich_text_format_to_layer_range(*layer_, format, mask, active);
        emit text_char_format_changed(layer_->id, format, mask);
    };
    auto apply_text_fill_format = [this, apply_text_char_format]() {
        if (!layer_ || loading_values_) return;
        RichTextCharFormat fmt = layer_char_format_for_editor(*layer_);
        apply_text_char_format(fmt, RichTextCharFillColor);
    };
    auto local_time = [this]() {
        return layer_ ? std::clamp(playhead_ - layer_->in_time, 0.0,
                                   std::max(0.0, layer_->out_time - layer_->in_time)) : 0.0;
    };
    auto update_text_box_auto_controls = [this]() {
        if (spn_max_text_box_width_)
            spn_max_text_box_width_->setEnabled(chk_text_box_width_to_text_ && chk_text_box_width_to_text_->isChecked());
        if (spn_max_text_box_height_)
            spn_max_text_box_height_->setEnabled(chk_text_box_height_to_text_ && chk_text_box_height_to_text_->isChecked());
    };
    auto install_delete_all_keyframes_menu =
        [this, can_edit, emit_change](QPushButton *button, auto props_for_layer) {
            if (!button) return;
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(button, &QPushButton::customContextMenuRequested,
                    this, [this, button, props_for_layer, can_edit, emit_change](const QPoint &pos) {
                        if (!layer_) return;
                        std::vector<AnimatedProperty *> props = props_for_layer();
                        bool has_keyframes = false;
                        for (auto *prop : props) {
                            if (prop && prop->is_animated()) {
                                has_keyframes = true;
                                break;
                            }
                        }

                        QMenu menu(button);
                        menu.setStyleSheet("QMenu{color:#ddd;background:#252525;border:1px solid #3a3a3a;}"
                                           "QMenu::item{padding:5px 22px;}"
                                           "QMenu::item:selected{background:#3b4f64;}"
                                           "QMenu::item:disabled{color:#666;}");
                        QAction *delete_all = menu.addAction(obsgs_tr("OBSTitles.DeleteAllKeyframes"));
                        delete_all->setEnabled(can_edit() && has_keyframes);
                        if (menu.exec(button->mapToGlobal(pos)) != delete_all || !can_edit()) return;

                        bool changed = false;
                        for (auto *prop : props) {
                            if (!prop || prop->keyframes.empty()) continue;
                            prop->keyframes.clear();
                            changed = true;
                        }
                        if (!changed) return;
                        load_values();
                        emit_change();
                    });
        };
    auto install_prop_delete_all = [&](QPushButton *button, AnimatedProperty Layer::*prop) {
        install_delete_all_keyframes_menu(button, [this, prop]() {
            return layer_ ? std::vector<AnimatedProperty *>{&(layer_.get()->*prop)}
                          : std::vector<AnimatedProperty *>{};
        });
    };
    auto install_group_delete_all = [&](QPushButton *button, std::initializer_list<AnimatedProperty Layer::*> props) {
        std::vector<AnimatedProperty Layer::*> prop_members(props);
        install_delete_all_keyframes_menu(button, [this, prop_members]() {
            std::vector<AnimatedProperty *> result;
            if (!layer_) return result;
            result.reserve(prop_members.size());
            for (auto prop : prop_members)
                result.push_back(&(layer_.get()->*prop));
            return result;
        });
    };

    install_prop_delete_all(btn_kf_pos_x_, &Layer::pos_x);
    install_prop_delete_all(btn_kf_pos_y_, &Layer::pos_y);
    install_prop_delete_all(btn_kf_scale_x_, &Layer::scale_x);
    install_prop_delete_all(btn_kf_scale_y_, &Layer::scale_y);
    install_prop_delete_all(btn_kf_rotation_, &Layer::rotation);
    install_prop_delete_all(btn_kf_opacity_, &Layer::opacity);
    install_prop_delete_all(btn_kf_origin_x_, &Layer::origin_x_prop);
    install_prop_delete_all(btn_kf_origin_y_, &Layer::origin_y_prop);
    install_prop_delete_all(btn_kf_paragraph_indent_left_, &Layer::paragraph_indent_left_prop);
    install_prop_delete_all(btn_kf_paragraph_indent_right_, &Layer::paragraph_indent_right_prop);
    install_prop_delete_all(btn_kf_paragraph_indent_first_line_, &Layer::paragraph_indent_first_line_prop);
    install_prop_delete_all(btn_kf_width_, &Layer::box_width);
    install_group_delete_all(btn_kf_text_color_, {&Layer::text_color_a, &Layer::text_color_r,
                                                  &Layer::text_color_g, &Layer::text_color_b});
    install_group_delete_all(btn_kf_fill_color_, {&Layer::fill_color_a, &Layer::fill_color_r,
                                                  &Layer::fill_color_g, &Layer::fill_color_b});
    install_prop_delete_all(btn_kf_shadow_enabled_, &Layer::shadow_enabled_prop);
    install_group_delete_all(btn_kf_shadow_color_, {&Layer::shadow_color_a, &Layer::shadow_color_r,
                                                    &Layer::shadow_color_g, &Layer::shadow_color_b});
    install_prop_delete_all(btn_kf_shadow_opacity_, &Layer::shadow_opacity_prop);
    install_prop_delete_all(btn_kf_shadow_distance_, &Layer::shadow_distance_prop);
    install_prop_delete_all(btn_kf_shadow_angle_, &Layer::shadow_angle_prop);
    install_prop_delete_all(btn_kf_shadow_blur_, &Layer::shadow_blur_prop);
    install_prop_delete_all(btn_kf_shadow_spread_, &Layer::shadow_spread_prop);

    connect(spn_px_,       QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v){
                if (can_edit()) { set_animated_value(layer_->pos_x, local_time(), v); emit_change(); }
            });
    connect(spn_py_,       QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v){
                if (can_edit()) { set_animated_value(layer_->pos_y, local_time(), v); emit_change(); }
            });
    connect(spn_scale_x_,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v){
                if (!can_edit()) return;
                const double scale = v / 100.0;
                const double t = local_time();
                set_animated_value(layer_->scale_x, t, scale);
                if (layer_->scale_lock) {
                    QSignalBlocker blocker(spn_scale_y_);
                    spn_scale_y_->setValue(v);
                    set_animated_value(layer_->scale_y, t, scale);
                }
                emit_change();
            });
    connect(spn_scale_y_,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v){
                if (!can_edit()) return;
                const double scale = v / 100.0;
                const double t = local_time();
                set_animated_value(layer_->scale_y, t, scale);
                if (layer_->scale_lock) {
                    QSignalBlocker blocker(spn_scale_x_);
                    spn_scale_x_->setValue(v);
                    set_animated_value(layer_->scale_x, t, scale);
                }
                emit_change();
            });
    connect(chk_scale_lock_, &QCheckBox::toggled,
            this, [this, can_edit, local_time, emit_change](bool locked) {
                if (!can_edit()) return;
                layer_->scale_lock = locked;
                if (locked) {
                    const double t = local_time();
                    const double scale = spn_scale_x_->value() / 100.0;
                    QSignalBlocker blocker(spn_scale_y_);
                    spn_scale_y_->setValue(spn_scale_x_->value());
                    set_animated_value(layer_->scale_y, t, scale);
                }
                emit_change();
            });
    connect(spn_rot_,      QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v){
                if (can_edit()) { set_animated_value(layer_->rotation, local_time(), v); emit_change(); }
            });
    connect(spn_opacity_,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v){
                if (can_edit()) { set_animated_value(layer_->opacity, local_time(), v); emit_change(); }
            });
    connect(spn_origin_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v){
                if (can_edit()) { layer_->origin_x = (float)v; set_animated_value(layer_->origin_x_prop, local_time(), v); emit_change(); }
            });
    connect(spn_origin_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v){
                if (can_edit()) { layer_->origin_y = (float)v; set_animated_value(layer_->origin_y_prop, local_time(), v); emit_change(); }
            });
    connect(cmb_anchor_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, local_time, emit_change](int idx) {
                if (!can_edit()) return;
                double t = local_time();
                QPointF next = anchor_point_from_index(idx);
                double w = eval_box_width(*layer_, t);
                double h = eval_box_height(*layer_, t);
                QPointF keep = rotated_scaled_delta((next.x() - layer_->origin_x) * w,
                                                    (next.y() - layer_->origin_y) * h,
                                                    layer_->rotation.evaluate(t),
                                                    layer_->scale_x.evaluate(t),
                                                    layer_->scale_y.evaluate(t));
                layer_->origin_x = (float)next.x();
                layer_->origin_y = (float)next.y();
                set_animated_value(layer_->origin_x_prop, t, next.x());
                set_animated_value(layer_->origin_y_prop, t, next.y());
                set_animated_value(layer_->pos_x, t, layer_->pos_x.evaluate(t) + keep.x());
                set_animated_value(layer_->pos_y, t, layer_->pos_y.evaluate(t) + keep.y());
                load_values();
                emit_change();
            });
    connect(btn_transform_defaults_, &QPushButton::clicked,
            this, [this, can_edit, local_time, emit_change]() {
                if (!can_edit()) return;
                const double t = local_time();
                set_animated_value(layer_->pos_x, t, 0.0);
                set_animated_value(layer_->pos_y, t, 0.0);
                set_animated_value(layer_->scale_x, t, 1.0);
                set_animated_value(layer_->scale_y, t, 1.0);
                set_animated_value(layer_->rotation, t, 0.0);
                set_animated_value(layer_->opacity, t, 1.0);
                layer_->origin_x = 0.5f;
                layer_->origin_y = 0.5f;
                set_animated_value(layer_->origin_x_prop, t, 0.5);
                set_animated_value(layer_->origin_y_prop, t, 0.5);
                layer_->scale_lock = true;
                load_values();
                emit_change();
            });
    connect(txt_content_, &QTextEdit::textChanged,
            this, [this, can_edit, emit_change]() {
                if (!can_edit()) return;
                std::string value = txt_content_->toPlainText().toStdString();
                if (layer_->type == LayerType::Clock) {
                    layer_->clock_format = value.empty() ? "H:i:s" : value;
                } else {
                    layer_->text_content = value;
                    if (layer_->rich_text.empty())
                        layer_->rich_text = rich_text_document_from_layer_defaults(*layer_);
                    layer_->rich_text.default_paragraph_format = layer_paragraph_format_for_editor(*layer_);
                    rich_text_document_replace_text(layer_->rich_text, value);
                    layer_->rich_text_html.clear();
                }
                emit_change();
            });
    connect(chk_text_box_width_to_text_, &QCheckBox::toggled,
            this, [this, can_edit, update_text_box_auto_controls, emit_change](bool v) {
                update_text_box_auto_controls();
                if (can_edit()) { layer_->text_box_width_to_text = v; emit_change(); }
            });
    connect(chk_text_box_height_to_text_, &QCheckBox::toggled,
            this, [this, can_edit, update_text_box_auto_controls, emit_change](bool v) {
                update_text_box_auto_controls();
                if (can_edit()) { layer_->text_box_height_to_text = v; emit_change(); }
            });
    connect(spn_max_text_box_width_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) {
                if (can_edit()) { layer_->max_text_box_width = (float)v; emit_change(); }
            });
    connect(spn_max_text_box_height_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) {
                if (can_edit()) { layer_->max_text_box_height = (float)v; emit_change(); }
            });
    connect(cmb_font_, &QComboBox::currentTextChanged,
            this, [this, can_edit, emit_change, apply_text_char_format](const QString &s){
                if (!can_edit()) return;
                populate_font_style_combo(cmb_font_style_, s, QString::fromStdString(layer_->font_style));
                layer_->font_family = s.toStdString();
                layer_->font_style = cmb_font_style_->currentText().toStdString();
                QFontDatabase fdb;
                layer_->font_bold = fdb.bold(s, cmb_font_style_->currentText());
                layer_->font_italic = fdb.italic(s, cmb_font_style_->currentText());
                RichTextCharFormat fmt = layer_char_format_for_editor(*layer_);
                apply_text_char_format(fmt, RichTextCharFontFamily | RichTextCharFontStyle |
                                       RichTextCharBold | RichTextCharItalic);
                emit_change();
            });
    connect(cmb_font_style_, &QComboBox::currentTextChanged,
            this, [this, can_edit, emit_change, apply_text_char_format](const QString &s){
                if (!can_edit()) return;
                layer_->font_style = s.toStdString();
                QFontDatabase fdb;
                const QString family = QString::fromStdString(layer_->font_family);
                layer_->font_bold = fdb.bold(family, s);
                layer_->font_italic = fdb.italic(family, s);
                RichTextCharFormat fmt = layer_char_format_for_editor(*layer_);
                apply_text_char_format(fmt, RichTextCharFontStyle | RichTextCharBold | RichTextCharItalic);
                emit_change();
            });
    connect(spn_size_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_char_format](int v){
                if (can_edit()) { layer_->font_size = v; RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharFontSize); emit_change(); }
            });
    connect(chk_bold_, &QToolButton::toggled,
            this, [this, can_edit, emit_change, apply_text_char_format](bool v){
                if (can_edit()) { layer_->font_bold = v; RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharBold); emit_change(); }
            });
    connect(chk_italic_, &QToolButton::toggled,
            this, [this, can_edit, emit_change, apply_text_char_format](bool v){
                if (can_edit()) { layer_->font_italic = v; RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharItalic); emit_change(); }
            });
    connect(chk_font_kerning_, &QToolButton::toggled,
            this, [this, can_edit, emit_change, apply_text_char_format](bool v){
                if (can_edit()) { layer_->font_kerning = v; RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharKerning); emit_change(); }
            });
    connect(cmb_kerning_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change, apply_text_char_format](int idx) {
                if (!can_edit()) return;
                layer_->kerning_mode = cmb_kerning_mode_->itemData(idx).toInt();
                layer_->font_kerning = layer_->kerning_mode != 2;
                if (chk_font_kerning_) chk_font_kerning_->setChecked(layer_->font_kerning);
                if (spn_kerning_value_) spn_kerning_value_->setEnabled(layer_->kerning_mode == 2);
                RichTextCharFormat fmt = layer_char_format_for_editor(*layer_);
                apply_text_char_format(fmt, RichTextCharKerning);
                emit_change();
            });
    connect(spn_kerning_value_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_char_format](double v) {
                if (can_edit()) { layer_->manual_kerning = (float)v; RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharKerning); emit_change(); }
            });
    connect(spn_text_leading_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v){
                if (can_edit()) { layer_->text_leading = (float)v; emit_change(); }
            });
    connect(spn_char_tracking_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_char_format](double v){
                if (can_edit()) { layer_->char_tracking = (float)v; RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharTracking); emit_change(); }
            });
    connect(spn_char_scale_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_char_format](double v){
                if (can_edit()) { layer_->char_scale_x = (float)(v / 100.0); RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharScaleX); emit_change(); }
            });
    connect(spn_char_scale_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_char_format](double v){
                if (can_edit()) { layer_->char_scale_y = (float)(v / 100.0); RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharScaleY); emit_change(); }
            });
    connect(spn_baseline_shift_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_char_format](double v){
                if (can_edit()) { layer_->baseline_shift = (float)v; RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharBaselineShift); emit_change(); }
            });
    connect(cmb_language_, &QComboBox::currentTextChanged,
            this, [this, can_edit, emit_change, apply_text_char_format](const QString &s){
                if (can_edit()) { layer_->text_language = s.toStdString(); RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharLanguage); emit_change(); }
            });
    auto set_exclusive_text_style = [this, can_edit, emit_change, apply_text_char_format](int style, bool checked) {
        if (!can_edit()) return;
        layer_->text_style = checked ? style : 0;
        RichTextCharFormat fmt = layer_char_format_for_editor(*layer_);
        apply_text_char_format(fmt, RichTextCharTextStyle);
        emit_change();
        load_values();
    };
    connect(btn_all_caps_, &QToolButton::toggled, this, [set_exclusive_text_style](bool v){ set_exclusive_text_style(1, v); });
    connect(btn_small_caps_, &QToolButton::toggled, this, [set_exclusive_text_style](bool v){ set_exclusive_text_style(2, v); });
    connect(btn_superscript_, &QToolButton::toggled, this, [set_exclusive_text_style](bool v){ set_exclusive_text_style(3, v); });
    connect(btn_subscript_, &QToolButton::toggled, this, [set_exclusive_text_style](bool v){ set_exclusive_text_style(4, v); });
    connect(btn_underline_, &QToolButton::toggled, this, [this, can_edit, emit_change, apply_text_char_format](bool v){ if (can_edit()) { layer_->text_underline = v; RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharUnderline); emit_change(); }});
    connect(btn_strikethrough_, &QToolButton::toggled, this, [this, can_edit, emit_change, apply_text_char_format](bool v){ if (can_edit()) { layer_->text_strikethrough = v; RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharStrikethrough); emit_change(); }});
    connect(btn_ligatures_, &QToolButton::toggled, this, [this, can_edit, emit_change, apply_text_char_format](bool v){ if (can_edit()) { layer_->text_ligatures = v; RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharLigatures); emit_change(); }});
    connect(btn_stylistic_alternates_, &QToolButton::toggled, this, [this, can_edit, emit_change, apply_text_char_format](bool v){ if (can_edit()) { layer_->text_stylistic_alternates = v; RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharStylisticAlternates); emit_change(); }});
    connect(btn_fractions_, &QToolButton::toggled, this, [this, can_edit, emit_change, apply_text_char_format](bool v){ if (can_edit()) { layer_->text_fractions = v; RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharFractions); emit_change(); }});
    connect(btn_opentype_features_, &QToolButton::toggled, this, [this, can_edit, emit_change, apply_text_char_format](bool v){ if (can_edit()) { layer_->text_opentype_features = v; RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharOpenTypeFeatures); emit_change(); }});
    connect(cmb_text_style_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change, apply_text_char_format](int idx) {
                if (can_edit()) { layer_->text_style = cmb_text_style_->itemData(idx).toInt(); RichTextCharFormat fmt = layer_char_format_for_editor(*layer_); apply_text_char_format(fmt, RichTextCharTextStyle); emit_change(); }
            });
    connect(cmb_text_overflow_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change](int idx) {
                if (can_edit()) { layer_->text_overflow_mode = cmb_text_overflow_->itemData(idx).toInt(); emit_change(); }
            });
    connect(spn_text_fit_min_scale_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) {
                if (can_edit()) { layer_->text_fit_min_scale = (float)v; emit_change(); }
            });
    connect(cmb_ticker_style_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change](int idx) {
                if (can_edit()) { layer_->ticker_style = cmb_ticker_style_->itemData(idx).toInt(); emit_change(); load_values(); }
            });
    connect(spn_ticker_speed_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) {
                if (can_edit()) { layer_->ticker_speed = v; emit_change(); }
            });
    connect(spn_ticker_line_hold_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) {
                if (can_edit()) { layer_->ticker_line_hold = v; emit_change(); }
            });
    connect(cmb_ticker_direction_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change](int idx) {
                if (can_edit()) { layer_->ticker_direction = cmb_ticker_direction_->itemData(idx).toInt(); emit_change(); }
            });
    connect(chk_expose_text_, &QCheckBox::toggled,
            this, [this, can_edit, emit_change](bool v){
                if (can_edit()) { layer_->expose_text = v; emit_change(); }
            });
    connect(chk_scene_mask_, &QCheckBox::toggled,
            this, [this, can_edit, emit_change](bool v){
                if (can_edit()) { layer_->use_as_scene_mask = v; load_values(); emit_change(); }
            });
    connect(stroke_options_trigger, &QPushButton::clicked,
            this, [this, can_edit, emit_change, control_style, checkbox_style]() {
                if (!can_edit()) return;

                QDialog popup(this, Qt::Popup | Qt::FramelessWindowHint);
                popup.setModal(true);
                popup.setStyleSheet(
                    "QDialog{background:#3c3c3c;border:1px solid #202020;}"
                    "QLabel{color:#d8d8d8;font-size:10px;}"
                    "QToolButton{color:#ddd;background:#4a4a4a;border:1px solid #5a5a5a;"
                    "border-radius:2px;padding:2px 6px;font-size:10px;}"
                    "QToolButton:hover{background:#565656;}"
                    "QToolButton:checked{background:#2d527f;border-color:#76a7df;color:#fff;}"
                    "QToolButton:disabled{color:#777;background:#444;border-color:#4a4a4a;}");
                auto *root = new QVBoxLayout(&popup);
                root->setContentsMargins(8, 8, 8, 8);
                root->setSpacing(6);

                auto *weight_row = new QWidget(&popup);
                auto *weight_layout = new QHBoxLayout(weight_row);
                weight_layout->setContentsMargins(0, 0, 0, 0);
                weight_layout->setSpacing(6);
                auto *weight_label = new QLabel(QStringLiteral("Weight:"), weight_row);
                auto *weight = new QDoubleSpinBox(weight_row);
                weight->setRange(0.0, 200.0);
                weight->setDecimals(0);
                weight->setSingleStep(1.0);
                weight->setSuffix(QStringLiteral(" px"));
                weight->setFixedWidth(82);
                weight->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
                weight->setStyleSheet(control_style);
                weight->setValue(layer_->stroke_width);
                weight_layout->addWidget(weight_label);
                weight_layout->addWidget(weight);
                weight_layout->addStretch();
                root->addWidget(weight_row);

                auto make_button = [](const QString &text, const QString &tip, QWidget *parent) {
                    auto *button = new QToolButton(parent);
                    button->setText(text);
                    button->setToolTip(tip);
                    button->setCheckable(true);
                    button->setFixedSize(28, 22);
                    return button;
                };
                auto add_button_group_row = [&](const QString &label_text, const QList<QToolButton *> &buttons,
                                                QWidget *extra = nullptr) {
                    auto *row = new QWidget(&popup);
                    auto *layout = new QHBoxLayout(row);
                    layout->setContentsMargins(0, 0, 0, 0);
                    layout->setSpacing(4);
                    auto *label = new QLabel(label_text, row);
                    label->setFixedWidth(46);
                    layout->addWidget(label);
                    for (auto *button : buttons)
                        layout->addWidget(button);
                    if (extra) {
                        layout->addSpacing(8);
                        layout->addWidget(extra);
                    }
                    layout->addStretch();
                    root->addWidget(row);
                };

                auto *cap_butt = make_button(QStringLiteral("Butt"), QStringLiteral("Cap style is not supported yet"), &popup);
                auto *cap_round = make_button(QStringLiteral("Rnd"), QStringLiteral("Cap style is not supported yet"), &popup);
                auto *cap_square = make_button(QStringLiteral("Sqr"), QStringLiteral("Cap style is not supported yet"), &popup);
                for (auto *button : {cap_butt, cap_round, cap_square})
                    button->setEnabled(false);
                add_button_group_row(QStringLiteral("Cap:"), {cap_butt, cap_round, cap_square});

                auto *corner_group = new QButtonGroup(&popup);
                corner_group->setExclusive(true);
                auto *corner_miter = make_button(QStringLiteral("M"), obsgs_tr("OBSTitles.Miter"), &popup);
                auto *corner_round = make_button(QStringLiteral("R"), obsgs_tr("OBSTitles.Round"), &popup);
                auto *corner_bevel = make_button(QStringLiteral("B"), obsgs_tr("OBSTitles.Bevel"), &popup);
                corner_group->addButton(corner_miter, 0);
                corner_group->addButton(corner_round, 1);
                corner_group->addButton(corner_bevel, 2);
                if (auto *button = corner_group->button(std::clamp(layer_->outline_join_style, 0, 2)))
                    button->setChecked(true);
                auto *limit = new QSpinBox(&popup);
                limit->setRange(1, 100);
                limit->setValue(10);
                limit->setFixedWidth(52);
                limit->setEnabled(false);
                limit->setStyleSheet(control_style);
                auto *limit_wrap = new QWidget(&popup);
                auto *limit_layout = new QHBoxLayout(limit_wrap);
                limit_layout->setContentsMargins(0, 0, 0, 0);
                limit_layout->setSpacing(4);
                limit_layout->addWidget(new QLabel(QStringLiteral("Limit:"), limit_wrap));
                limit_layout->addWidget(limit);
                add_button_group_row(QStringLiteral("Corner:"), {corner_miter, corner_round, corner_bevel}, limit_wrap);

                auto *align_group = new QButtonGroup(&popup);
                align_group->setExclusive(true);
                auto *align_back = make_button(QStringLiteral("Back"), obsgs_tr("OBSTitles.Back"), &popup);
                auto *align_front = make_button(QStringLiteral("Front"), obsgs_tr("OBSTitles.Front"), &popup);
                align_group->addButton(align_back, 0);
                align_group->addButton(align_front, 1);
                if (auto *button = align_group->button(layer_->outline_on_front ? 1 : 0))
                    button->setChecked(true);
                add_button_group_row(QStringLiteral("Align Stroke:"), {align_back, align_front});

                auto *dash_row = new QWidget(&popup);
                auto *dash_layout = new QHBoxLayout(dash_row);
                dash_layout->setContentsMargins(0, 0, 0, 0);
                dash_layout->setSpacing(4);
                auto *dashed = new QCheckBox(QStringLiteral("Dashed Line"), dash_row);
                dashed->setEnabled(false);
                dashed->setStyleSheet(checkbox_style);
                dashed->setToolTip(QStringLiteral("Dashed strokes are not supported yet"));
                dash_layout->addWidget(dashed);
                dash_layout->addStretch();
                root->addWidget(dash_row);

                auto *dash_values = new QWidget(&popup);
                auto *dash_values_layout = new QHBoxLayout(dash_values);
                dash_values_layout->setContentsMargins(0, 0, 0, 0);
                dash_values_layout->setSpacing(4);
                for (const QString &label : {QStringLiteral("dash"), QStringLiteral("gap"),
                                             QStringLiteral("dash"), QStringLiteral("gap"),
                                             QStringLiteral("dash"), QStringLiteral("gap")}) {
                    auto *field = new QSpinBox(dash_values);
                    field->setRange(0, 999);
                    field->setValue(label == QStringLiteral("dash") ? 12 : 0);
                    if (label == QStringLiteral("dash"))
                        field->setSuffix(QStringLiteral(" pt"));
                    field->setFixedWidth(48);
                    field->setEnabled(false);
                    field->setStyleSheet(control_style);
                    dash_values_layout->addWidget(field);
                }
                root->addWidget(dash_values);

                connect(weight, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                        &popup, [this, emit_change](double v) {
                            if (!layer_ || loading_values_) return;
                            layer_->stroke_width = (float)v;
                            layer_->outline_enabled = v > 0.0;
                            if (v > 0.0 && layer_->stroke_fill_type == 0)
                                layer_->stroke_fill_type = 1;
                            if (spn_appearance_stroke_width_) {
                                QSignalBlocker block(spn_appearance_stroke_width_);
                                spn_appearance_stroke_width_->setValue(v);
                            }
                            if (spn_outline_width_) {
                                QSignalBlocker block(spn_outline_width_);
                                spn_outline_width_->setValue(v);
                            }
                            emit_change();
                        });
                connect(corner_group, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
                        &popup, [this, corner_group, emit_change](QAbstractButton *button) {
                            if (!layer_ || loading_values_) return;
                            layer_->outline_join_style = corner_group->id(button);
                            if (cmb_outline_join_) {
                                QSignalBlocker block(cmb_outline_join_);
                                int idx = cmb_outline_join_->findData(layer_->outline_join_style);
                                cmb_outline_join_->setCurrentIndex(idx >= 0 ? idx : layer_->outline_join_style);
                            }
                            emit_change();
                        });
                connect(align_group, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
                        &popup, [this, align_group, emit_change](QAbstractButton *button) {
                            if (!layer_ || loading_values_) return;
                            layer_->outline_on_front = align_group->id(button) == 1;
                            if (cmb_outline_position_) {
                                QSignalBlocker block(cmb_outline_position_);
                                int idx = cmb_outline_position_->findData(layer_->outline_on_front ? 1 : 0);
                                cmb_outline_position_->setCurrentIndex(idx >= 0 ? idx : (layer_->outline_on_front ? 1 : 0));
                            }
                            emit_change();
                        });

                const QPoint popup_pos = btn_appearance_stroke_label_->mapToGlobal(
                    QPoint(0, btn_appearance_stroke_label_->height() + 2));
                popup.adjustSize();
                popup.move(clamp_popup_position_to_screen(popup_pos, popup.size(), btn_appearance_stroke_label_));
                popup.exec();
            });
    auto connect_alignment_group = [this, can_edit, emit_change](QButtonGroup *group, bool horizontal) {
        if (!group) return;
        for (auto *button : group->buttons()) {
            connect(button, &QAbstractButton::clicked, this, [this, can_edit, emit_change, group, horizontal, button]() {
                if (!can_edit()) return;
                int value = group->id(button);
                if (horizontal)
                    layer_->align_h = value;
                else
                    layer_->align_v = value;
                emit_change();
            });
        }
    };
    connect_alignment_group(grp_text_align_, true);
    connect_alignment_group(grp_text_valign_, false);
    auto connect_paragraph_spin = [this, can_edit, emit_change](QDoubleSpinBox *spin, float Layer::*field) {
        if (!spin) return;
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, can_edit, emit_change, field](double value) {
                    if (can_edit()) { layer_.get()->*field = (float)value; emit_change(); }
                });
    };
    auto connect_keyframed_paragraph_spin = [this, can_edit, local_time, emit_change](QDoubleSpinBox *spin, float Layer::*field, AnimatedProperty Layer::*prop) {
        if (!spin) return;
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, can_edit, local_time, emit_change, field, prop](double value) {
                    if (can_edit()) {
                        layer_.get()->*field = (float)value;
                        set_animated_value(layer_.get()->*prop, local_time(), value);
                        emit_change();
                    }
                });
    };
    connect_keyframed_paragraph_spin(spn_paragraph_indent_left_, &Layer::paragraph_indent_left, &Layer::paragraph_indent_left_prop);
    connect_keyframed_paragraph_spin(spn_paragraph_indent_right_, &Layer::paragraph_indent_right, &Layer::paragraph_indent_right_prop);
    connect_keyframed_paragraph_spin(spn_paragraph_indent_first_line_, &Layer::paragraph_indent_first_line, &Layer::paragraph_indent_first_line_prop);
    connect_paragraph_spin(spn_paragraph_space_before_, &Layer::paragraph_space_before);
    connect_paragraph_spin(spn_paragraph_space_after_, &Layer::paragraph_space_after);
    connect(chk_paragraph_hyphenate_, &QCheckBox::toggled,
            this, [this, can_edit, emit_change](bool v) {
                if (can_edit()) { layer_->paragraph_hyphenate = v; emit_change(); }
            });
    connect(btn_text_color_, &QPushButton::clicked,
            this, [this, can_edit, local_time, emit_change, apply_text_char_format]() {
                if (!can_edit()) return;
                QColor initial = color_from_argb(eval_text_color(*layer_, local_time()));
                QColor picked = QColorDialog::getColor(initial, this, obsgs_tr("OBSTitles.TextColor"),
                                                        QColorDialog::ShowAlphaChannel);
                if (!picked.isValid()) return;
                layer_->text_color = argb_from_color(picked);
                set_color_channels_at(*layer_, true, local_time(), layer_->text_color);
                RichTextCharFormat fmt = layer_char_format_for_editor(*layer_);
                apply_text_char_format(fmt, RichTextCharFillColor);
                style_color_button(btn_text_color_, layer_->text_color);
                emit_change();
            });
    auto open_color_selector = [this, can_edit, emit_change, apply_text_fill_format, local_time, control_style](bool stroke) {
        if (!can_edit()) return;
        const bool text_fill = !stroke && (layer_->type == LayerType::Text || layer_->type == LayerType::Clock ||
                                           layer_->type == LayerType::Ticker);

        QDialog popup(this, Qt::Popup | Qt::FramelessWindowHint);
        popup.setModal(true);
        popup.setMinimumWidth(880);
        popup.setStyleSheet(
            "QDialog{background:#3c3c3c;border:1px solid #202020;}"
            "QTabWidget::pane{border:1px solid #2b2b2b;background:#3c3c3c;}"
            "QTabBar::tab{color:#d8d8d8;background:#2f2f2f;border:1px solid #242424;"
            "padding:5px 10px;}"
            "QTabBar::tab:selected{background:#4a4a4a;color:#fff;}"
            "QTabBar::tab:disabled{color:#777;background:#303030;}"
            "QLabel{color:#d8d8d8;font-size:10px;}"
            "QPushButton{color:#ddd;background:#4a4a4a;border:1px solid #5a5a5a;"
            "border-radius:2px;padding:3px 8px;font-size:10px;}"
            "QPushButton:hover{background:#565656;}"
            "QSlider::groove:horizontal{height:12px;border:1px solid #242424;border-radius:2px;background:#2a2a2a;}"
            "QSlider::handle:horizontal{width:10px;margin:-3px 0;border:1px solid #202020;border-radius:2px;background:#d0d0d0;}"
            "QLineEdit,QSpinBox{color:#ddd;background:#252525;border:1px solid #363636;"
            "border-radius:2px;padding:2px 4px;selection-background-color:#4b6ea8;}"
            "QLineEdit:focus,QSpinBox:focus{border-color:#5a78ad;}");
        auto *root = new QVBoxLayout(&popup);
        root->setContentsMargins(8, 8, 8, 8);
        auto *tabs = new QTabWidget(&popup);
        root->addWidget(tabs);

        auto update_main_swatch = [this, stroke, text_fill]() {
            if (stroke) {
                if (layer_->stroke_fill_type == 2)
                    style_gradient_button(btn_appearance_stroke_color_,
                                          layer_->stroke_gradient_start_color,
                                          layer_->stroke_gradient_end_color,
                                          layer_->stroke_gradient_type);
                else
                    style_color_button(btn_appearance_stroke_color_, layer_->stroke_color);
                btn_appearance_stroke_color_->setText(QString());
                if (btn_outline_color_) style_color_button(btn_outline_color_, layer_->stroke_color);
            } else {
                const double t = std::clamp(playhead_ - layer_->in_time, 0.0,
                                            std::max(0.0, layer_->out_time - layer_->in_time));
                if (layer_->fill_type == 1)
                    style_gradient_button(btn_appearance_fill_color_,
                                          layer_->gradient_start_color,
                                          layer_->gradient_end_color,
                                          layer_->gradient_type);
                else
                    style_color_button(btn_appearance_fill_color_,
                                       text_fill ? eval_text_color(*layer_, t) : eval_fill_color(*layer_, t));
                btn_appearance_fill_color_->setText(QString());
                if (text_fill && btn_text_color_) style_color_button(btn_text_color_, eval_text_color(*layer_, t));
                if (!text_fill && btn_fill_color_) style_color_button(btn_fill_color_, eval_fill_color(*layer_, t));
            }
        };
        auto apply_solid_color = [this, stroke, text_fill, local_time, emit_change, apply_text_fill_format,
                                  update_main_swatch](const QColor &color) {
            if (!layer_ || loading_values_ || !color.isValid()) return;
            const uint32_t argb = argb_from_color(color);
            if (stroke) {
                layer_->outline_enabled = true;
                layer_->stroke_fill_type = 1;
                layer_->stroke_color = argb;
            } else {
                layer_->fill_type = 0;
                if (text_fill) {
                    layer_->text_color = argb;
                    set_color_channels_at(*layer_, true, local_time(), argb);
                    apply_text_fill_format();
                } else {
                    layer_->fill_color = argb;
                    set_color_channels_at(*layer_, false, local_time(), argb);
                }
            }
            update_main_swatch();
            emit_change();
        };

        auto *color_tab = new QWidget(tabs);
        auto *color_layout = new QHBoxLayout(color_tab);
        color_layout->setContentsMargins(8, 8, 8, 8);
        color_layout->setSpacing(12);
        QColor initial = stroke
            ? color_from_argb(layer_->stroke_color)
            : color_from_argb(text_fill ? eval_text_color(*layer_, local_time()) : eval_fill_color(*layer_, local_time()));
        QColor selected_color = initial;
        bool syncing_color_controls = false;

        auto swatch_style = [](const QColor &color, bool large = false) {
            return QStringLiteral(
                "QPushButton{background:%1;border:1px solid #202020;border-radius:2px;"
                "min-width:%2px;min-height:%3px;max-width:%2px;max-height:%3px;padding:0;}")
                .arg(color.name(QColor::HexArgb))
                .arg(large ? 86 : 30)
                .arg(large ? 154 : 30);
        };
        auto safe_color = [](QColor color) {
            color.setRed(std::clamp(color.red(), 16, 235));
            color.setGreen(std::clamp(color.green(), 16, 235));
            color.setBlue(std::clamp(color.blue(), 16, 235));
            return color;
        };
        auto color_hex = [](const QColor &color) {
            return color.alpha() < 255
                ? QStringLiteral("#%1%2%3%4")
                    .arg(color.red(), 2, 16, QLatin1Char('0'))
                    .arg(color.green(), 2, 16, QLatin1Char('0'))
                    .arg(color.blue(), 2, 16, QLatin1Char('0'))
                    .arg(color.alpha(), 2, 16, QLatin1Char('0'))
                    .toUpper()
                : QStringLiteral("#%1%2%3")
                    .arg(color.red(), 2, 16, QLatin1Char('0'))
                    .arg(color.green(), 2, 16, QLatin1Char('0'))
                    .arg(color.blue(), 2, 16, QLatin1Char('0'))
                    .toUpper();
        };
        auto parse_hex = [](QString text, QColor &color) {
            text = text.trimmed();
            if (text.startsWith(QStringLiteral("#"))) text.remove(0, 1);
            if (text.size() != 6 && text.size() != 8) return false;
            bool ok = false;
            const uint value = text.toUInt(&ok, 16);
            if (!ok) return false;
            if (text.size() == 6) {
                color = QColor((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF, 255);
            } else {
                color = QColor((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
            }
            return color.isValid();
        };

        auto *swatch_column = new QWidget(color_tab);
        auto *swatch_layout = new QVBoxLayout(swatch_column);
        swatch_layout->setContentsMargins(0, 0, 0, 0);
        swatch_layout->setSpacing(10);
        auto *current_swatch = new QPushButton(swatch_column);
        current_swatch->setEnabled(false);
        auto *safe_swatch = new QPushButton(swatch_column);
        safe_swatch->setEnabled(false);
        auto *current_label = new QLabel(QStringLiteral("Current"), swatch_column);
        auto *safe_label = new QLabel(QStringLiteral("Safe"), swatch_column);
        current_label->setAlignment(Qt::AlignCenter);
        safe_label->setAlignment(Qt::AlignCenter);
        swatch_layout->addWidget(current_swatch, 0, Qt::AlignCenter);
        swatch_layout->addWidget(current_label);
        swatch_layout->addWidget(safe_swatch, 0, Qt::AlignCenter);
        swatch_layout->addWidget(safe_label);
        swatch_layout->addStretch(1);
        color_layout->addWidget(swatch_column);

        auto *color_picker = new HsvColorPicker(color_tab);
        color_picker->set_color(initial);
        color_layout->addWidget(color_picker, 0, Qt::AlignTop);

        auto *slider_column = new QWidget(color_tab);
        auto *slider_layout = new QVBoxLayout(slider_column);
        slider_layout->setContentsMargins(0, 0, 0, 0);
        slider_layout->setSpacing(10);

        auto make_slider_group = [&](const QString &title) {
            auto *box = new QGroupBox(title, slider_column);
            box->setStyleSheet("QGroupBox{color:#d8d8d8;background:#343434;border:1px solid #292929;"
                               "border-radius:2px;margin-top:15px;padding-top:8px;font-size:10px;}"
                               "QGroupBox::title{subcontrol-origin:margin;left:6px;padding:0 3px;}");
            auto *layout = new QGridLayout(box);
            layout->setContentsMargins(8, 8, 8, 8);
            layout->setHorizontalSpacing(6);
            layout->setVerticalSpacing(5);
            return std::pair<QGroupBox *, QGridLayout *>(box, layout);
        };
        auto make_slider = [&](QGridLayout *grid, int row, const QString &label, int min, int max,
                               const QString &gradient) {
            auto *text = new QLabel(label, slider_column);
            auto *slider = new QSlider(Qt::Horizontal, slider_column);
            slider->setRange(min, max);
            slider->setFixedWidth(240);
            slider->setStyleSheet(QStringLiteral(
                "QSlider::groove:horizontal{height:12px;border:1px solid #242424;border-radius:2px;background:%1;}"
                "QSlider::handle:horizontal{width:10px;margin:-3px 0;border:1px solid #202020;"
                "border-radius:2px;background:#d0d0d0;}").arg(gradient));
            auto *spin = new QSpinBox(slider_column);
            spin->setRange(min, max);
            spin->setFixedWidth(56);
            grid->addWidget(text, row, 0);
            grid->addWidget(slider, row, 1);
            grid->addWidget(spin, row, 2);
            connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);
            return std::pair<QSlider *, QSpinBox *>(slider, spin);
        };
        auto [lab_box, lab_grid] = make_slider_group(QStringLiteral("Lab"));
        auto lab_l = make_slider(lab_grid, 0, QStringLiteral("L"), 0, 100,
                                 QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #111,stop:1 #eee)"));
        auto lab_a = make_slider(lab_grid, 1, QStringLiteral("a"), -128, 127,
                                 QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #1f8a47,stop:0.5 #777,stop:1 #b23a48)"));
        auto lab_b = make_slider(lab_grid, 2, QStringLiteral("b"), -128, 127,
                                 QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #2d5fb8,stop:0.5 #777,stop:1 #c9b236)"));
        for (auto slider : {lab_l.first, lab_a.first, lab_b.first}) slider->setEnabled(false);
        for (auto spin : {lab_l.second, lab_a.second, lab_b.second}) spin->setEnabled(false);
        auto [rgb_box, rgb_grid] = make_slider_group(QStringLiteral("RGB"));
        auto rgb_r = make_slider(rgb_grid, 0, QStringLiteral("R"), 0, 255,
                                 QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #000,stop:1 #f00)"));
        auto rgb_g = make_slider(rgb_grid, 1, QStringLiteral("G"), 0, 255,
                                 QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #000,stop:1 #0f0)"));
        auto rgb_b = make_slider(rgb_grid, 2, QStringLiteral("B"), 0, 255,
                                 QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #000,stop:1 #00f)"));
        auto rgb_a = make_slider(rgb_grid, 3, QStringLiteral("A"), 0, 255,
                                 QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 rgba(0,0,0,0),stop:1 #fff)"));
        slider_layout->addWidget(lab_box);
        slider_layout->addWidget(rgb_box);
        auto *hex_row = new QWidget(slider_column);
        auto *hex_layout = new QHBoxLayout(hex_row);
        hex_layout->setContentsMargins(0, 0, 0, 0);
        hex_layout->setSpacing(8);
        hex_layout->addWidget(new QLabel(QStringLiteral("hex:"), hex_row));
        auto *hex_edit = new QLineEdit(hex_row);
        hex_layout->addWidget(hex_edit, 1);
        slider_layout->addWidget(hex_row);
        color_layout->addWidget(slider_column, 1);

        auto *recent_column = new QWidget(color_tab);
        auto *recent_layout = new QVBoxLayout(recent_column);
        recent_layout->setContentsMargins(0, 0, 0, 0);
        recent_layout->setSpacing(8);
        auto *recent_title = new QLabel(QStringLiteral("Recent Swatches"), recent_column);
        recent_title->setStyleSheet("QLabel{font-size:14px;color:#f0f0f0;}");
        recent_layout->addWidget(recent_title);
        auto *recent_row = new QWidget(recent_column);
        auto *recent_row_layout = new QHBoxLayout(recent_row);
        recent_row_layout->setContentsMargins(0, 0, 0, 0);
        recent_row_layout->setSpacing(8);
        auto *recent_button = new QPushButton(recent_row);
        recent_button->setText(QString());
        auto *recent_hex = new QLabel(recent_row);
        recent_row_layout->addWidget(recent_button);
        recent_row_layout->addWidget(recent_hex);
        recent_layout->addWidget(recent_row);
        recent_layout->addStretch(1);
        color_layout->addWidget(recent_column);

        auto update_color_controls = [&]() {
            syncing_color_controls = true;
            current_swatch->setStyleSheet(swatch_style(selected_color, true));
            safe_swatch->setStyleSheet(swatch_style(safe_color(selected_color), true));
            recent_button->setStyleSheet(swatch_style(initial));
            recent_hex->setText(color_hex(initial));
            hex_edit->setText(color_hex(selected_color));
            rgb_r.first->setValue(selected_color.red());
            rgb_g.first->setValue(selected_color.green());
            rgb_b.first->setValue(selected_color.blue());
            rgb_a.first->setValue(selected_color.alpha());
            lab_l.first->setValue((int)std::round(selected_color.valueF() * 100.0));
            lab_a.first->setValue(selected_color.red() - selected_color.green());
            lab_b.first->setValue(selected_color.blue() - selected_color.green());
            syncing_color_controls = false;
        };
        auto apply_and_sync_color = [&](const QColor &color, bool update_picker) {
            if (!color.isValid()) return;
            selected_color = color;
            if (update_picker) color_picker->set_color(color);
            update_color_controls();
            apply_solid_color(color);
        };
        auto rgb_changed = [&]() {
            if (syncing_color_controls) return;
            apply_and_sync_color(QColor(rgb_r.first->value(), rgb_g.first->value(),
                                       rgb_b.first->value(), rgb_a.first->value()), true);
        };
        for (auto slider : {rgb_r.first, rgb_g.first, rgb_b.first, rgb_a.first})
            connect(slider, &QSlider::valueChanged, &popup, [=, &rgb_changed](int) { rgb_changed(); });
        connect(hex_edit, &QLineEdit::editingFinished, &popup, [=, &apply_and_sync_color, &parse_hex]() {
            QColor parsed;
            if (parse_hex(hex_edit->text(), parsed))
                apply_and_sync_color(parsed, true);
        });
        connect(recent_button, &QPushButton::clicked, &popup, [=, &apply_and_sync_color]() {
            apply_and_sync_color(initial, true);
        });
        update_color_controls();
        tabs->addTab(color_tab, QStringLiteral("Color"));

        auto *swatches_tab = new QWidget(tabs);
        auto *swatches_layout = new QVBoxLayout(swatches_tab);
        auto *swatches_label = new QLabel(QStringLiteral("Swatches are not implemented yet."), swatches_tab);
        swatches_layout->addWidget(swatches_label);
        tabs->addTab(swatches_tab, QStringLiteral("Swatches"));
        tabs->setTabEnabled(1, false);

        auto *gradient_tab = new QWidget(tabs);
        auto *gradient_layout = new QVBoxLayout(gradient_tab);
        gradient_layout->setContentsMargins(8, 8, 8, 8);
        gradient_layout->setSpacing(8);
        auto *gradient_main = new QWidget(gradient_tab);
        auto *gradient_main_layout = new QHBoxLayout(gradient_main);
        gradient_main_layout->setContentsMargins(0, 0, 0, 0);
        gradient_main_layout->setSpacing(12);
        auto *preview = new GradientEditorPreview(gradient_main);
        gradient_main_layout->addWidget(preview, 1);
        auto *properties = new QWidget(gradient_main);
        properties->setFixedWidth(360);
        auto *properties_layout = new QVBoxLayout(properties);
        properties_layout->setContentsMargins(0, 0, 0, 0);
        properties_layout->setSpacing(8);

        auto *type = new QComboBox(gradient_tab);
        type->addItem(obsgs_tr("OBSTitles.LinearGradient"), 0);
        type->addItem(obsgs_tr("OBSTitles.RadialGradient"), 1);
        type->addItem(QStringLiteral("Angle"), 2);
        type->addItem(QStringLiteral("Reflected"), 3);
        type->addItem(QStringLiteral("Diamond"), 4);
        type->hide();
        auto *type_box = new QGroupBox(QStringLiteral("Gradient Type"), properties);
        type_box->setStyleSheet("QGroupBox{color:#d8d8d8;background:#343434;border:1px solid #292929;"
                                "border-radius:2px;margin-top:15px;padding-top:8px;font-size:10px;}"
                                "QGroupBox::title{subcontrol-origin:margin;left:6px;padding:0 3px;}");
        auto *type_layout = new QGridLayout(type_box);
        type_layout->setContentsMargins(8, 8, 8, 8);
        type_layout->setHorizontalSpacing(6);
        type_layout->setVerticalSpacing(4);
        auto *type_group = new QButtonGroup(type_box);
        type_group->setExclusive(true);
        auto make_type_button = [&](int id, const QString &name) {
            auto *button = new QToolButton(type_box);
            button->setCheckable(true);
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setText(name);
            button->setIconSize(QSize(38, 28));
            button->setFixedSize(64, 60);
            QPixmap pix(38, 28);
            pix.fill(Qt::transparent);
            QPainter ip(&pix);
            ip.setRenderHint(QPainter::Antialiasing, true);
            QRectF r(4, 4, 30, 20);
            ip.setPen(QPen(QColor(210, 210, 210), 1));
            ip.setBrush(Qt::NoBrush);
            if (id == 1) {
                ip.drawEllipse(r);
                ip.drawLine(r.center(), r.bottomRight());
            } else if (id == 2) {
                ip.drawArc(r, 20 * 16, 250 * 16);
                ip.drawLine(r.center(), QPointF(r.right(), r.top()));
            } else if (id == 3) {
                ip.drawRect(r);
                ip.drawRect(r.adjusted(6, 4, -6, -4));
            } else if (id == 4) {
                QPolygonF diamond;
                diamond << QPointF(r.center().x(), r.top()) << QPointF(r.right(), r.center().y())
                        << QPointF(r.center().x(), r.bottom()) << QPointF(r.left(), r.center().y());
                ip.drawPolygon(diamond);
            } else {
                QLinearGradient g(r.topLeft(), r.topRight());
                g.setColorAt(0, QColor(210, 210, 210));
                g.setColorAt(1, QColor(70, 70, 70));
                ip.fillRect(r, g);
                ip.drawRect(r);
            }
            ip.end();
            button->setIcon(QIcon(pix));
            button->setStyleSheet("QToolButton{color:#ddd;background:#2f2f2f;border:1px solid #242424;"
                                  "border-radius:2px;padding:2px;font-size:9px;}"
                                  "QToolButton:hover{background:#3a3a3a;}"
                                  "QToolButton:checked{background:#4b6ea8;border-color:#7a9bd0;color:white;}");
            type_group->addButton(button, id);
            type_layout->addWidget(button, id / 3, id % 3);
            return button;
        };
        make_type_button(0, QStringLiteral("Linear"));
        make_type_button(1, QStringLiteral("Radial"));
        make_type_button(2, QStringLiteral("Angle"));
        make_type_button(3, QStringLiteral("Reflected"));
        make_type_button(4, QStringLiteral("Diamond"));
        properties_layout->addWidget(type_box);

        auto *param_box = new QGroupBox(QStringLiteral("Parameters"), properties);
        param_box->setStyleSheet(type_box->styleSheet());
        auto *form = new QFormLayout(param_box);
        form->setContentsMargins(8, 8, 8, 8);
        form->setHorizontalSpacing(8);
        form->setVerticalSpacing(5);
        auto *start_color = new QPushButton(gradient_tab);
        auto *end_color = new QPushButton(gradient_tab);
        auto make_spin = [&](double lo, double hi, double step, int decimals = 2) {
            auto *spin = new QDoubleSpinBox(gradient_tab);
            spin->setRange(lo, hi);
            spin->setSingleStep(step);
            spin->setDecimals(decimals);
            spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
            spin->setStyleSheet(control_style);
            return spin;
        };
        auto *start_pos = make_spin(0.0, 1.0, 0.01);
        auto *end_pos = make_spin(0.0, 1.0, 0.01);
        auto *start_opacity = make_spin(0.0, 1.0, 0.01);
        auto *end_opacity = make_spin(0.0, 1.0, 0.01);
        auto *gradient_opacity = make_spin(0.0, 1.0, 0.01);
        auto *angle = make_spin(-360.0, 360.0, 1.0, 0);
        angle->setSuffix(QStringLiteral("°"));
        auto *center_x = make_spin(0.0, 1.0, 0.01);
        auto *center_y = make_spin(0.0, 1.0, 0.01);
        auto *scale = make_spin(0.01, 10.0, 0.05);
        auto *focal_x = make_spin(0.0, 1.0, 0.01);
        auto *focal_y = make_spin(0.0, 1.0, 0.01);
        auto *reverse_gradient = new QCheckBox(QStringLiteral("Reverse Gradient"), param_box);
        auto *dither_gradient = new QCheckBox(QStringLiteral("Dither"), param_box);
        auto *repeat_mode = new QComboBox(param_box);
        repeat_mode->addItem(QStringLiteral("Clamp"), 0);
        repeat_mode->addItem(QStringLiteral("Repeat"), 1);
        repeat_mode->addItem(QStringLiteral("Mirror"), 2);
        repeat_mode->setStyleSheet(control_style);

        if (stroke) {
            type->setCurrentIndex(std::max(0, type->findData(std::clamp(layer_->stroke_gradient_type, 0, 4))));
            style_color_button(start_color, layer_->stroke_gradient_start_color);
            style_color_button(end_color, layer_->stroke_gradient_end_color);
            start_pos->setValue(layer_->stroke_gradient_start_pos);
            end_pos->setValue(layer_->stroke_gradient_end_pos);
            start_opacity->setValue(layer_->stroke_gradient_start_opacity);
            end_opacity->setValue(layer_->stroke_gradient_end_opacity);
            gradient_opacity->setValue(layer_->stroke_gradient_opacity);
            angle->setValue(layer_->stroke_gradient_angle);
            center_x->setValue(layer_->stroke_gradient_center_x);
            center_y->setValue(layer_->stroke_gradient_center_y);
            scale->setValue(layer_->stroke_gradient_scale);
            focal_x->setValue(layer_->stroke_gradient_focal_x);
            focal_y->setValue(layer_->stroke_gradient_focal_y);
        } else {
            type->setCurrentIndex(std::max(0, type->findData(std::clamp(layer_->gradient_type, 0, 4))));
            style_color_button(start_color, layer_->gradient_start_color);
            style_color_button(end_color, layer_->gradient_end_color);
            start_pos->setValue(layer_->gradient_start_pos);
            end_pos->setValue(layer_->gradient_end_pos);
            start_opacity->setValue(layer_->gradient_start_opacity);
            end_opacity->setValue(layer_->gradient_end_opacity);
            gradient_opacity->setValue(layer_->gradient_opacity);
            angle->setValue(layer_->gradient_angle);
            center_x->setValue(layer_->gradient_center_x);
            center_y->setValue(layer_->gradient_center_y);
            scale->setValue(layer_->gradient_scale);
            focal_x->setValue(layer_->gradient_focal_x);
            focal_y->setValue(layer_->gradient_focal_y);
        }
        start_color->setText(QString());
        end_color->setText(QString());
        if (auto *button = type_group->button(type->currentData().toInt()))
            button->setChecked(true);

        form->addRow(obsgs_tr("OBSTitles.AngleLabel"), angle);
        form->addRow(obsgs_tr("OBSTitles.CenterXLabel"), center_x);
        form->addRow(obsgs_tr("OBSTitles.CenterYLabel"), center_y);
        form->addRow(obsgs_tr("OBSTitles.ScaleLabel"), scale);
        form->addRow(obsgs_tr("OBSTitles.FocalXLabel"), focal_x);
        form->addRow(obsgs_tr("OBSTitles.FocalYLabel"), focal_y);
        form->addRow(QString(), reverse_gradient);
        form->addRow(QString(), dither_gradient);
        form->addRow(QStringLiteral("Repeat"), repeat_mode);
        properties_layout->addWidget(param_box);

        auto *stops_box = new QGroupBox(QStringLiteral("Gradient Stops"), properties);
        stops_box->setStyleSheet(type_box->styleSheet());
        auto *stops_layout = new QVBoxLayout(stops_box);
        stops_layout->setContentsMargins(8, 8, 8, 8);
        stops_layout->setSpacing(5);
        auto make_stop_row = [&](int stop_index, QPushButton *color_button, QDoubleSpinBox *pos_spin,
                                 QDoubleSpinBox *opacity_spin) {
            auto *row = new QWidget(stops_box);
            auto *layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(5);
            color_button->setFixedSize(36, 24);
            layout->addWidget(color_button);
            pos_spin->setSuffix(QStringLiteral("%"));
            pos_spin->setRange(0.0, 100.0);
            pos_spin->setDecimals(0);
            pos_spin->setFixedWidth(70);
            opacity_spin->setSuffix(QStringLiteral("%"));
            opacity_spin->setRange(0.0, 100.0);
            opacity_spin->setDecimals(0);
            opacity_spin->setFixedWidth(70);
            auto *delete_button = new QPushButton(QStringLiteral("×"), row);
            delete_button->setFixedSize(24, 24);
            delete_button->setEnabled(false);
            delete_button->setToolTip(QStringLiteral("The current title model stores two persistent gradient stops."));
            layout->addWidget(pos_spin);
            layout->addWidget(opacity_spin);
            layout->addWidget(delete_button);
            connect(row, &QWidget::destroyed, stops_box, [](){});
            row->setProperty("stop_index", stop_index);
            stops_layout->addWidget(row);
            return row;
        };
        auto *stop0_row = make_stop_row(0, start_color, start_pos, start_opacity);
        auto *stop1_row = make_stop_row(1, end_color, end_pos, end_opacity);
        start_pos->setValue(start_pos->value() * 100.0);
        end_pos->setValue(end_pos->value() * 100.0);
        start_opacity->setValue(start_opacity->value() * 100.0);
        end_opacity->setValue(end_opacity->value() * 100.0);
        auto *stop_actions = new QWidget(stops_box);
        auto *stop_actions_layout = new QHBoxLayout(stop_actions);
        stop_actions_layout->setContentsMargins(0, 0, 0, 0);
        stop_actions_layout->setSpacing(5);
        auto *add_stop = new QPushButton(QStringLiteral("+ Add Stop"), stop_actions);
        auto *duplicate_stop = new QPushButton(QStringLiteral("Duplicate"), stop_actions);
        auto *sort_stops = new QPushButton(QStringLiteral("Sort"), stop_actions);
        duplicate_stop->setToolTip(QStringLiteral("Duplicates the selected stop into the opposite persistent stop."));
        add_stop->setToolTip(QStringLiteral("Adds by moving the nearest persistent stop to the current preview position."));
        stop_actions_layout->addWidget(add_stop);
        stop_actions_layout->addWidget(duplicate_stop);
        stop_actions_layout->addWidget(sort_stops);
        stops_layout->addWidget(stop_actions);
        properties_layout->addWidget(stops_box);
        properties_layout->addStretch(1);
        gradient_main_layout->addWidget(properties);
        gradient_layout->addWidget(gradient_main, 1);

        auto *preset_box = new QGroupBox(QStringLiteral("Presets"), gradient_tab);
        preset_box->setStyleSheet(type_box->styleSheet());
        auto *preset_layout = new QHBoxLayout(preset_box);
        preset_layout->setContentsMargins(8, 8, 8, 8);
        preset_layout->setSpacing(8);
        auto make_preset = [&](uint32_t a, uint32_t b) {
            auto *button = new QPushButton(preset_box);
            button->setFixedSize(46, 32);
            style_gradient_button(button, a, b, 0);
            button->setText(QString());
            connect(button, &QPushButton::clicked, &popup, [=]() {
                style_color_button(start_color, a);
                style_color_button(end_color, b);
                if (stroke) {
                    layer_->stroke_gradient_start_color = a;
                    layer_->stroke_gradient_end_color = b;
                } else {
                    layer_->gradient_start_color = a;
                    layer_->gradient_end_color = b;
                }
                preview->set_gradient(type->currentData().toInt(), a, b, start_pos->value() / 100.0,
                                      end_pos->value() / 100.0, start_opacity->value() / 100.0,
                                      end_opacity->value() / 100.0, gradient_opacity->value(),
                                      angle->value(), center_x->value(), center_y->value(), scale->value());
            });
            preset_layout->addWidget(button);
        };
        make_preset(0xFFFFFFFF, 0xFF000000);
        make_preset(0xFF4B6EA8, 0xFF1B1B1B);
        make_preset(0xFFFF4D4D, 0xFFFFC857);
        make_preset(0xFF20C997, 0xFF4B6EA8);
        make_preset(0x00FFFFFF, 0xFFFFFFFF);
        preset_layout->addStretch(1);
        gradient_layout->addWidget(preset_box);
        tabs->addTab(gradient_tab, QStringLiteral("Gradient"));

        auto *pattern_tab = new QWidget(tabs);
        auto *pattern_layout = new QVBoxLayout(pattern_tab);
        pattern_layout->addWidget(new QLabel(QStringLiteral("Patterns are not implemented yet."), pattern_tab));
        tabs->addTab(pattern_tab, QStringLiteral("Pattern"));
        tabs->setTabEnabled(3, false);

        auto sync_stop_rows = [=]() {
            const bool start_selected = preview->selected_stop() == 0;
            stop0_row->setStyleSheet(start_selected ? QStringLiteral("background:#2e4662;border:1px solid #6b8fb5;")
                                                    : QStringLiteral("background:transparent;border:1px solid transparent;"));
            stop1_row->setStyleSheet(!start_selected ? QStringLiteral("background:#2e4662;border:1px solid #6b8fb5;")
                                                     : QStringLiteral("background:transparent;border:1px solid transparent;"));
        };
        auto update_preview = [=]() {
            const uint32_t start_argb = stroke ? layer_->stroke_gradient_start_color : layer_->gradient_start_color;
            const uint32_t end_argb = stroke ? layer_->stroke_gradient_end_color : layer_->gradient_end_color;
            preview->set_gradient(type->currentData().toInt(), start_argb, end_argb,
                                  start_pos->value() / 100.0, end_pos->value() / 100.0,
                                  start_opacity->value() / 100.0, end_opacity->value() / 100.0,
                                  gradient_opacity->value(), angle->value(),
                                  center_x->value(), center_y->value(), scale->value());
            sync_stop_rows();
        };
        auto apply_gradient = [=, &popup]() {
            if (!layer_ || loading_values_) return;
            if (stroke) {
                layer_->outline_enabled = true;
                layer_->stroke_fill_type = 2;
                layer_->stroke_gradient_type = type->currentData().toInt();
                layer_->stroke_gradient_start_pos = (float)(start_pos->value() / 100.0);
                layer_->stroke_gradient_end_pos = (float)(end_pos->value() / 100.0);
                layer_->stroke_gradient_start_opacity = (float)(start_opacity->value() / 100.0);
                layer_->stroke_gradient_end_opacity = (float)(end_opacity->value() / 100.0);
                layer_->stroke_gradient_opacity = (float)gradient_opacity->value();
                layer_->stroke_gradient_angle = (float)angle->value();
                layer_->stroke_gradient_center_x = (float)center_x->value();
                layer_->stroke_gradient_center_y = (float)center_y->value();
                layer_->stroke_gradient_scale = (float)scale->value();
                layer_->stroke_gradient_focal_x = (float)focal_x->value();
                layer_->stroke_gradient_focal_y = (float)focal_y->value();
            } else {
                layer_->fill_type = 1;
                layer_->gradient_type = type->currentData().toInt();
                layer_->gradient_start_pos = (float)(start_pos->value() / 100.0);
                layer_->gradient_end_pos = (float)(end_pos->value() / 100.0);
                layer_->gradient_start_opacity = (float)(start_opacity->value() / 100.0);
                layer_->gradient_end_opacity = (float)(end_opacity->value() / 100.0);
                layer_->gradient_opacity = (float)gradient_opacity->value();
                layer_->gradient_angle = (float)angle->value();
                layer_->gradient_center_x = (float)center_x->value();
                layer_->gradient_center_y = (float)center_y->value();
                layer_->gradient_scale = (float)scale->value();
                layer_->gradient_focal_x = (float)focal_x->value();
                layer_->gradient_focal_y = (float)focal_y->value();
                if (text_fill) apply_text_fill_format();
            }
            update_preview();
            update_main_swatch();
            emit_change();
        };
        auto pick_gradient_color = [&](bool start) {
            QColor initial = color_from_argb(stroke
                ? (start ? layer_->stroke_gradient_start_color : layer_->stroke_gradient_end_color)
                : (start ? layer_->gradient_start_color : layer_->gradient_end_color));
            QColor picked = QColorDialog::getColor(initial, &popup, QStringLiteral("Gradient Color"),
                                                    QColorDialog::ShowAlphaChannel);
            if (!picked.isValid()) return;
            if (stroke) {
                if (start) layer_->stroke_gradient_start_color = argb_from_color(picked);
                else layer_->stroke_gradient_end_color = argb_from_color(picked);
            } else {
                if (start) layer_->gradient_start_color = argb_from_color(picked);
                else layer_->gradient_end_color = argb_from_color(picked);
            }
            style_color_button(start ? start_color : end_color, argb_from_color(picked));
            (start ? start_color : end_color)->setText(QString());
            apply_gradient();
        };
        auto show_stop_color_popup = [=, &popup](int stop_index, const QPoint &global_pos) {
            auto *color_button = stop_index == 0 ? start_color : end_color;
            QColor initial = color_from_argb(stroke
                ? (stop_index == 0 ? layer_->stroke_gradient_start_color : layer_->stroke_gradient_end_color)
                : (stop_index == 0 ? layer_->gradient_start_color : layer_->gradient_end_color));
            initial.setAlphaF(stop_index == 0 ? start_opacity->value() / 100.0 : end_opacity->value() / 100.0);

            QDialog color_popup(&popup, Qt::Popup | Qt::FramelessWindowHint);
            color_popup.setStyleSheet(
                "QDialog{background:#3c3c3c;border:1px solid #202020;}"
                "QLabel{color:#ddd;font-size:10px;}"
                "QLineEdit,QSpinBox{color:#ddd;background:#252525;border:1px solid #363636;"
                "border-radius:2px;padding:2px 4px;selection-background-color:#4b6ea8;}"
                "QPushButton{color:#ddd;background:#4a4a4a;border:1px solid #5a5a5a;"
                "border-radius:2px;padding:3px 6px;font-size:10px;}"
                "QSlider::groove:horizontal{height:10px;border:1px solid #242424;border-radius:2px;background:#2a2a2a;}"
                "QSlider::handle:horizontal{width:10px;margin:-3px 0;border:1px solid #202020;border-radius:2px;background:#d0d0d0;}");
            auto *popup_layout = new QVBoxLayout(&color_popup);
            popup_layout->setContentsMargins(8, 8, 8, 8);
            popup_layout->setSpacing(6);
            auto *picker = new HsvColorPicker(&color_popup);
            picker->set_color(initial);
            popup_layout->addWidget(picker);
            auto *hex_row = new QWidget(&color_popup);
            auto *hex_layout = new QHBoxLayout(hex_row);
            hex_layout->setContentsMargins(0, 0, 0, 0);
            hex_layout->setSpacing(5);
            auto *swatch = new QPushButton(hex_row);
            swatch->setFixedSize(30, 24);
            auto *hex = new QLineEdit(hex_row);
            auto *eyedropper = new QPushButton(hex_row);
            eyedropper->setIcon(obs_icon("eyedropper.svg"));
            eyedropper->setFixedSize(28, 24);
            eyedropper->setToolTip(QStringLiteral("Use the toolbar eyedropper to sample from the canvas."));
            hex_layout->addWidget(swatch);
            hex_layout->addWidget(hex, 1);
            hex_layout->addWidget(eyedropper);
            popup_layout->addWidget(hex_row);
            auto *rgb_row = new QWidget(&color_popup);
            auto *rgb_layout = new QHBoxLayout(rgb_row);
            rgb_layout->setContentsMargins(0, 0, 0, 0);
            rgb_layout->setSpacing(5);
            auto make_rgb_spin = [&](const QString &label, int value) {
                auto *wrap = new QWidget(rgb_row);
                auto *layout = new QHBoxLayout(wrap);
                layout->setContentsMargins(0, 0, 0, 0);
                layout->setSpacing(3);
                layout->addWidget(new QLabel(label, wrap));
                auto *spin = new QSpinBox(wrap);
                spin->setRange(0, 255);
                spin->setValue(value);
                spin->setFixedWidth(54);
                layout->addWidget(spin);
                rgb_layout->addWidget(wrap);
                return spin;
            };
            auto *spin_r = make_rgb_spin(QStringLiteral("R"), initial.red());
            auto *spin_g = make_rgb_spin(QStringLiteral("G"), initial.green());
            auto *spin_b = make_rgb_spin(QStringLiteral("B"), initial.blue());
            auto *spin_a = make_rgb_spin(QStringLiteral("A"), initial.alpha());
            popup_layout->addWidget(rgb_row);

            bool syncing = false;
            auto apply_color = [&](const QColor &color, bool update_picker) {
                if (!color.isValid()) return;
                syncing = true;
                if (update_picker) picker->set_color(color);
                swatch->setStyleSheet(QStringLiteral("QPushButton{background:%1;border:1px solid #202020;border-radius:2px;padding:0;}")
                                      .arg(color.name(QColor::HexArgb)));
                hex->setText(gradient_editor_hex(color));
                spin_r->setValue(color.red());
                spin_g->setValue(color.green());
                spin_b->setValue(color.blue());
                spin_a->setValue(color.alpha());
                syncing = false;

                const uint32_t argb = argb_from_color(QColor(color.red(), color.green(), color.blue(), 255));
                if (stroke) {
                    if (stop_index == 0) layer_->stroke_gradient_start_color = argb;
                    else layer_->stroke_gradient_end_color = argb;
                } else {
                    if (stop_index == 0) layer_->gradient_start_color = argb;
                    else layer_->gradient_end_color = argb;
                }
                style_color_button(color_button, argb);
                color_button->setText(QString());
                if (stop_index == 0) start_opacity->setValue(color.alphaF() * 100.0);
                else end_opacity->setValue(color.alphaF() * 100.0);
                preview->set_stop_color(stop_index, color);
                apply_gradient();
            };
            picker->color_changed = [&](const QColor &color) { apply_color(color, false); };
            auto rgb_apply = [&]() {
                if (syncing) return;
                apply_color(QColor(spin_r->value(), spin_g->value(), spin_b->value(), spin_a->value()), true);
            };
            for (auto *spin : {spin_r, spin_g, spin_b, spin_a})
                connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), &color_popup, [=, &rgb_apply](int) { rgb_apply(); });
            connect(hex, &QLineEdit::editingFinished, &color_popup, [=, &apply_color]() {
                QColor parsed;
                if (gradient_editor_parse_hex(hex->text(), parsed))
                    apply_color(parsed, true);
            });
            apply_color(initial, false);
            color_popup.adjustSize();
            color_popup.move(clamp_popup_position_to_screen(global_pos, color_popup.size(), preview));
            color_popup.exec();
        };

        color_picker->color_changed = [&](const QColor &color) {
            apply_and_sync_color(color, false);
        };
        connect(tabs, &QTabWidget::currentChanged, &popup, [=](int idx) {
            if (idx == 0)
                apply_solid_color(color_picker->color());
            else if (idx == 2)
                apply_gradient();
        });
        connect(type, QOverload<int>::of(&QComboBox::currentIndexChanged), &popup, [=](int){ apply_gradient(); });
        connect(type_group, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
                &popup, [=](QAbstractButton *button) {
                    type->setCurrentIndex(std::max(0, type->findData(type_group->id(button))));
                });
        preview->selection_changed = [=](int) { sync_stop_rows(); };
        preview->gradient_changed = [=]() {
            {
                QSignalBlocker b0(start_pos), b1(end_pos), b2(start_opacity), b3(end_opacity);
                start_pos->setValue(preview->stop_position(0) * 100.0);
                end_pos->setValue(preview->stop_position(1) * 100.0);
                start_opacity->setValue(preview->stop_opacity(0) * 100.0);
                end_opacity->setValue(preview->stop_opacity(1) * 100.0);
            }
            if (stroke) {
                layer_->stroke_gradient_start_color = preview->stop_color_argb(0);
                layer_->stroke_gradient_end_color = preview->stop_color_argb(1);
            } else {
                layer_->gradient_start_color = preview->stop_color_argb(0);
                layer_->gradient_end_color = preview->stop_color_argb(1);
            }
            style_color_button(start_color, preview->stop_color_argb(0));
            style_color_button(end_color, preview->stop_color_argb(1));
            start_color->setText(QString());
            end_color->setText(QString());
            apply_gradient();
        };
        preview->color_popup_requested = [=, &show_stop_color_popup](int stop_index, const QPoint &global_pos) {
            show_stop_color_popup(stop_index, global_pos);
        };
        for (auto *spin : {start_pos, end_pos, start_opacity, end_opacity, gradient_opacity,
                           angle, center_x, center_y, scale, focal_x, focal_y}) {
            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &popup, [=](double){ apply_gradient(); });
        }
        connect(start_color, &QPushButton::clicked, &popup, [=, &show_stop_color_popup]() {
            preview->set_selected_stop(0);
            show_stop_color_popup(0, preview->stop_global_anchor(0));
        });
        connect(end_color, &QPushButton::clicked, &popup, [=, &show_stop_color_popup]() {
            preview->set_selected_stop(1);
            show_stop_color_popup(1, preview->stop_global_anchor(1));
        });
        connect(duplicate_stop, &QPushButton::clicked, &popup, [=]() {
            int src = preview->selected_stop();
            int dst = src == 0 ? 1 : 0;
            if (stroke) {
                if (dst == 0) layer_->stroke_gradient_start_color = preview->stop_color_argb(src);
                else layer_->stroke_gradient_end_color = preview->stop_color_argb(src);
            } else {
                if (dst == 0) layer_->gradient_start_color = preview->stop_color_argb(src);
                else layer_->gradient_end_color = preview->stop_color_argb(src);
            }
            (dst == 0 ? start_pos : end_pos)->setValue(std::clamp(preview->stop_position(src) * 100.0 + (src == 0 ? 12.0 : -12.0), 0.0, 100.0));
            (dst == 0 ? start_opacity : end_opacity)->setValue(preview->stop_opacity(src) * 100.0);
            apply_gradient();
        });
        connect(add_stop, &QPushButton::clicked, &popup, [=]() {
            int dst = preview->selected_stop() == 0 ? 1 : 0;
            (dst == 0 ? start_pos : end_pos)->setValue(50.0);
            preview->set_selected_stop(dst);
            apply_gradient();
            show_stop_color_popup(dst, preview->stop_global_anchor(dst));
        });
        connect(sort_stops, &QPushButton::clicked, &popup, [=]() {
            if (start_pos->value() <= end_pos->value()) return;
            const double p0 = start_pos->value(), p1 = end_pos->value();
            const double o0 = start_opacity->value(), o1 = end_opacity->value();
            const uint32_t c0 = stroke ? layer_->stroke_gradient_start_color : layer_->gradient_start_color;
            const uint32_t c1 = stroke ? layer_->stroke_gradient_end_color : layer_->gradient_end_color;
            start_pos->setValue(p1); end_pos->setValue(p0);
            start_opacity->setValue(o1); end_opacity->setValue(o0);
            if (stroke) {
                layer_->stroke_gradient_start_color = c1;
                layer_->stroke_gradient_end_color = c0;
            } else {
                layer_->gradient_start_color = c1;
                layer_->gradient_end_color = c0;
            }
            apply_gradient();
        });

        update_preview();
        tabs->setCurrentIndex((stroke ? layer_->stroke_fill_type == 2 : layer_->fill_type == 1) ? 2 : 0);
        auto *source_button = stroke ? btn_appearance_stroke_color_ : btn_appearance_fill_color_;
        popup.adjustSize();
        const QPoint button_center = source_button->mapToGlobal(QPoint(source_button->width() / 2,
                                                                        source_button->height() + 2));
        const QPoint desired_pos(button_center.x() - popup.width() / 2, button_center.y());
        popup.move(clamp_popup_position_to_screen(desired_pos, popup.size(), source_button));
        popup.exec();
    };
    connect(btn_appearance_fill_color_, &QPushButton::clicked,
            this, [open_color_selector]() { open_color_selector(false); });
    connect(btn_appearance_stroke_color_, &QPushButton::clicked,
            this, [open_color_selector]() { open_color_selector(true); });
    connect(spn_appearance_stroke_width_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) {
                if (!can_edit()) return;
                layer_->stroke_width = (float)v;
                layer_->outline_enabled = v > 0.0;
                if (v > 0.0 && layer_->stroke_fill_type == 0)
                    layer_->stroke_fill_type = 1;
                if (spn_outline_width_) {
                    QSignalBlocker block(spn_outline_width_);
                    spn_outline_width_->setValue(v);
                }
                emit_change();
            });
    connect(spn_appearance_opacity_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v) {
                if (!can_edit()) return;
                const double opacity = v / 100.0;
                set_animated_value(layer_->opacity, local_time(), opacity);
                if (spn_opacity_) {
                    QSignalBlocker block(spn_opacity_);
                    spn_opacity_->setValue(opacity);
                }
                emit_change();
            });
    connect(chk_shadow_enabled_, &QCheckBox::toggled, this, [this, can_edit, local_time, emit_change](bool v) {
        if (can_edit()) {
            layer_->shadow_enabled = v;
            set_animated_value(layer_->shadow_enabled_prop, local_time(), v ? 1.0 : 0.0);
            emit_change();
        }
    });
    connect(cmb_shadow_preset_, QOverload<int>::of(&QComboBox::activated), this, [this, can_edit, local_time, emit_change](int idx) {
        if (!can_edit() || idx <= 0) return;
        static const struct { float opacity, distance, blur, spread, angle; uint32_t color; } presets[] = {
            {0.35f, 5.0f, 8.0f, 1.0f, 135.0f, 0x99000000}, {0.55f, 8.0f, 5.0f, 2.0f, 135.0f, 0xAA000000},
            {0.75f, 12.0f, 3.0f, 3.0f, 135.0f, 0xCC000000}, {0.65f, 10.0f, 4.0f, 4.0f, 135.0f, 0xCC001428},
        };
        const auto &p = presets[std::clamp(idx - 1, 0, 3)];
        double t = local_time();
        layer_->shadow_enabled = true;
        layer_->shadow_opacity = p.opacity;
        layer_->shadow_distance = p.distance;
        layer_->shadow_blur = p.blur;
        layer_->shadow_spread = p.spread;
        layer_->shadow_angle = p.angle;
        layer_->shadow_color = p.color;
        set_animated_value(layer_->shadow_enabled_prop, t, 1.0);
        set_animated_value(layer_->shadow_opacity_prop, t, p.opacity);
        set_animated_value(layer_->shadow_distance_prop, t, p.distance);
        set_animated_value(layer_->shadow_blur_prop, t, p.blur);
        set_animated_value(layer_->shadow_spread_prop, t, p.spread);
        set_animated_value(layer_->shadow_angle_prop, t, p.angle);
        set_shadow_color_channels_at(*layer_, t, p.color);
        load_values(); emit_change();
    });
    connect(btn_shadow_color_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        QColor picked = QColorDialog::getColor(color_from_argb(eval_shadow_color(*layer_, local_time())), this, obsgs_tr("OBSTitles.ShadowColor"), QColorDialog::ShowAlphaChannel);
        if (!picked.isValid()) return;
        layer_->shadow_color = argb_from_color(picked);
        set_shadow_color_channels_at(*layer_, local_time(), layer_->shadow_color);
        style_color_button(btn_shadow_color_, layer_->shadow_color);
        emit_change();
    });
    connect(spn_shadow_opacity_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v) {
                if (!can_edit()) return;
                layer_->shadow_opacity = (float)v;
                set_animated_value(layer_->shadow_opacity_prop, local_time(), v);
                emit_change();
            });
    connect(spn_shadow_distance_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v) {
                if (!can_edit()) return;
                layer_->shadow_distance = (float)v;
                set_animated_value(layer_->shadow_distance_prop, local_time(), v);
                emit_change();
            });
    connect(spn_shadow_angle_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v) {
                if (!can_edit()) return;
                layer_->shadow_angle = (float)v;
                set_animated_value(layer_->shadow_angle_prop, local_time(), v);
                emit_change();
            });
    connect(spn_shadow_blur_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v) {
                if (!can_edit()) return;
                layer_->shadow_blur = (float)v;
                set_animated_value(layer_->shadow_blur_prop, local_time(), v);
                emit_change();
            });
    connect(spn_shadow_spread_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v) {
                if (!can_edit()) return;
                layer_->shadow_spread = (float)v;
                set_animated_value(layer_->shadow_spread_prop, local_time(), v);
                emit_change();
            });
    connect(cmb_shadow_blur_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change](int idx) {
                if (!can_edit()) return;
                layer_->shadow_blur_type = (ShadowBlurType)cmb_shadow_blur_type_->itemData(idx).toInt();
                emit_change();
            });
    connect(chk_long_shadow_enabled_, &QCheckBox::toggled, this, [this, can_edit, emit_change](bool v) {
        if (!can_edit()) return;
        layer_->long_shadow_enabled = v;
        if (v && layer_->long_shadow_length <= 0.0f) {
            layer_->long_shadow_length = 120.0f;
            if (spn_long_shadow_length_) spn_long_shadow_length_->setValue(layer_->long_shadow_length);
        }
        if (v && layer_->long_shadow_opacity <= 0.0f) {
            layer_->long_shadow_opacity = 0.45f;
            if (spn_long_shadow_opacity_) spn_long_shadow_opacity_->setValue(layer_->long_shadow_opacity);
        }
        emit_change();
    });
    connect(btn_long_shadow_color_, &QPushButton::clicked, this, [this, can_edit, emit_change]() {
        if (!can_edit()) return;
        QColor picked = QColorDialog::getColor(color_from_argb(layer_->long_shadow_color), this, obsgs_tr("OBSTitles.LongShadowColor"), QColorDialog::ShowAlphaChannel);
        if (!picked.isValid()) return;
        layer_->long_shadow_color = argb_from_color(picked);
        style_color_button(btn_long_shadow_color_, layer_->long_shadow_color);
        emit_change();
    });
    connect(spn_long_shadow_opacity_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->long_shadow_opacity = (float)v; emit_change(); } });
    connect(spn_long_shadow_length_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->long_shadow_length = (float)v; emit_change(); } });
    connect(spn_long_shadow_angle_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->long_shadow_angle = (float)v; emit_change(); } });
    connect(spn_long_shadow_falloff_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->long_shadow_falloff = (float)v; emit_change(); } });
    connect(cmb_long_shadow_blur_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change](int idx) {
                if (!can_edit()) return;
                layer_->long_shadow_blur_type = (LongShadowBlurType)cmb_long_shadow_blur_type_->itemData(idx).toInt();
                emit_change();
            });
    connect(spn_long_shadow_blur_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->long_shadow_blur = (float)v; emit_change(); } });

    connect(spn_layer_w_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v){
                if (!can_edit()) return;
                double t = local_time();
                double old_w = eval_box_width(*layer_, t);
                double old_h = eval_box_height(*layer_, t);
                layer_->rect_width = (float)v;
                set_animated_value(layer_->box_width, t, v);
                const bool lock_size = layer_->lock_aspect_ratio &&
                                       (layer_->type == LayerType::Image ||
                                        layer_->type == LayerType::Shape ||
                                        layer_->type == LayerType::SolidRect);
                if (lock_size && old_w > 0.0) {
                    layer_->rect_height = (float)(v * old_h / old_w);
                    set_animated_value(layer_->box_height, t, layer_->rect_height);
                    QSignalBlocker block(spn_layer_h_);
                    spn_layer_h_->setValue(layer_->rect_height);
                }
                emit_change();
            });
    connect(spn_layer_h_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, local_time, emit_change](double v){
                if (!can_edit()) return;
                double t = local_time();
                double old_w = eval_box_width(*layer_, t);
                double old_h = eval_box_height(*layer_, t);
                layer_->rect_height = (float)v;
                set_animated_value(layer_->box_height, t, v);
                const bool lock_size = layer_->lock_aspect_ratio &&
                                       (layer_->type == LayerType::Image ||
                                        layer_->type == LayerType::Shape ||
                                        layer_->type == LayerType::SolidRect);
                if (lock_size && old_h > 0.0) {
                    layer_->rect_width = (float)(v * old_w / old_h);
                    set_animated_value(layer_->box_width, t, layer_->rect_width);
                    QSignalBlocker block(spn_layer_w_);
                    spn_layer_w_->setValue(layer_->rect_width);
                }
                emit_change();
            });
    auto set_corner_spin_values = [this](double tl, double tr, double br, double bl, QDoubleSpinBox *except = nullptr) {
        auto set_spin = [except](QDoubleSpinBox *spin, double value) {
            if (!spin || spin == except) return;
            QSignalBlocker blocker(spin);
            spin->setValue(value);
        };
        set_spin(spn_rect_corner_tl_, tl);
        set_spin(spn_rect_corner_tr_, tr);
        set_spin(spn_rect_corner_br_, br);
        set_spin(spn_rect_corner_bl_, bl);
    };
    auto connect_corner_spin = [this, can_edit, emit_change, set_corner_spin_values](QDoubleSpinBox *spin, int corner_index) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, can_edit, emit_change, set_corner_spin_values, spin, corner_index](double v) {
                    if (!can_edit()) return;
                    if (chk_corner_lock_ && chk_corner_lock_->isChecked()) {
                        set_layer_all_corner_radii(*layer_, (float)v);
                        set_corner_spin_values(v, v, v, v, spin);
                    } else {
                        switch (corner_index) {
                        case 0: layer_->corner_radius_tl = (float)v; break;
                        case 1: layer_->corner_radius_tr = (float)v; break;
                        case 2: layer_->corner_radius_br = (float)v; break;
                        case 3: layer_->corner_radius_bl = (float)v; break;
                        default: break;
                        }
                        set_layer_corner_radii(*layer_, layer_->corner_radius_tl, layer_->corner_radius_tr,
                                               layer_->corner_radius_br, layer_->corner_radius_bl);
                    }
                    emit_change();
                });
    };
    connect_corner_spin(spn_rect_corner_tl_, 0);
    connect_corner_spin(spn_rect_corner_tr_, 1);
    connect_corner_spin(spn_rect_corner_br_, 2);
    connect_corner_spin(spn_rect_corner_bl_, 3);
    connect(chk_corner_lock_, &QCheckBox::toggled,
            this, [this, can_edit, emit_change, set_corner_spin_values](bool locked) {
                if (!can_edit()) return;
                layer_->corner_radius_locked = locked;
                if (locked) {
                    const double radius = spn_rect_corner_tl_ ? spn_rect_corner_tl_->value() : layer_->corner_radius_tl;
                    set_layer_all_corner_radii(*layer_, (float)radius);
                    set_corner_spin_values(radius, radius, radius, radius);
                }
                emit_change();
            });
    connect(grp_corner_type_, &QButtonGroup::idClicked,
            this, [this, can_edit, emit_change](int corner_type_id) {
                if (!can_edit()) return;
                layer_->corner_type = (CornerType)std::clamp(corner_type_id, 0, (int)CornerType::Cutout);
                emit_change();
            });
    connect(cmb_shape_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change](int idx){
                if (!can_edit()) return;
                const ShapeType previous_shape = layer_->shape_type;
                const ShapeType next_shape = (ShapeType)cmb_shape_type_->itemData(idx).toInt();
                layer_->shape_type = next_shape;
                if (next_shape == ShapeType::Rectangle) {
                    set_layer_all_corner_radii(*layer_, 0.0f);
                    layer_->corner_radius_locked = true;
                    layer_->shape_roundness = 0.0f;
                } else if (next_shape == ShapeType::RoundedRectangle &&
                           previous_shape != ShapeType::Rectangle &&
                           previous_shape != ShapeType::RoundedRectangle &&
                           layer_->corner_radius <= 0.0f) {
                    set_layer_all_corner_radii(*layer_, 18.0f);
                    layer_->corner_radius_locked = true;
                    layer_->shape_roundness = layer_->corner_radius;
                }
                load_values();
                emit_change();
            });
    connect(grp_shape_type_, &QButtonGroup::idClicked,
            this, [this](int shape_id) {
                if (!cmb_shape_type_ || loading_values_) return;
                const int idx = cmb_shape_type_->findData(shape_id);
                if (idx >= 0 && cmb_shape_type_->currentIndex() != idx)
                    cmb_shape_type_->setCurrentIndex(idx);
            });
    connect(chk_size_lock_, &QCheckBox::toggled,
            this, [this, can_edit, emit_change](bool locked) {
                if (can_edit()) {
                    layer_->lock_aspect_ratio = locked;
                    emit_change();
                }
            });
    connect(btn_shape_defaults_, &QPushButton::clicked,
            this, [this, can_edit, local_time, emit_change]() {
                if (!can_edit()) return;
                const double t = local_time();
                const double default_w = title_ ? std::max(1.0, title_->width * 0.5) : 960.0;
                const double default_h = 160.0;
                layer_->shape_type = ShapeType::Rectangle;
                layer_->rect_width = (float)default_w;
                layer_->rect_height = (float)default_h;
                set_animated_value(layer_->box_width, t, default_w);
                set_animated_value(layer_->box_height, t, default_h);
                set_layer_all_corner_radii(*layer_, 0.0f);
                layer_->corner_radius_locked = true;
                layer_->corner_type = CornerType::Round;
                layer_->shape_points = 5;
                layer_->shape_sides = 6;
                layer_->shape_inner_radius = 0.20f;
                layer_->shape_outer_radius = 0.5f;
                layer_->shape_roundness = 0.0f;
                layer_->lock_aspect_ratio = true;
                load_values();
                emit_change();
            });
    connect(spn_shape_points_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this, can_edit, emit_change](int v){ if (can_edit()) { layer_->shape_points = v; emit_change(); } });
    connect(spn_shape_sides_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this, can_edit, emit_change](int v){ if (can_edit()) { layer_->shape_sides = v; emit_change(); } });
    connect(spn_shape_inner_radius_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v){ if (can_edit()) { layer_->shape_inner_radius = (float)v; emit_change(); } });
    connect(spn_shape_outer_radius_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v){ if (can_edit()) { layer_->shape_outer_radius = (float)v; emit_change(); } });
    connect(spn_shape_roundness_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v){
                if (!can_edit()) return;
                const bool rectangle_shape = layer_->type == LayerType::SolidRect ||
                                             (layer_->type == LayerType::Shape &&
                                              (layer_->shape_type == ShapeType::Rectangle ||
                                               layer_->shape_type == ShapeType::RoundedRectangle));
                layer_->shape_roundness = (float)v;
                if (rectangle_shape)
                    set_layer_all_corner_radii(*layer_, (float)v);
                emit_change();
            });
    connect(btn_fill_color_, &QPushButton::clicked,
            this, [this, can_edit, local_time, emit_change]() {
                if (!can_edit()) return;
                QColor initial = color_from_argb(eval_fill_color(*layer_, local_time()));
                QColor picked = QColorDialog::getColor(initial, this, obsgs_tr("OBSTitles.FillColor"),
                                                        QColorDialog::ShowAlphaChannel);
                if (!picked.isValid()) return;
                layer_->fill_color = argb_from_color(picked);
                set_color_channels_at(*layer_, false, local_time(), layer_->fill_color);
                style_color_button(btn_fill_color_, layer_->fill_color);
                emit_change();
            });
    connect(cmb_fill_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change, apply_text_fill_format](int idx) {
                if (!can_edit()) return;
                layer_->fill_type = cmb_fill_type_->itemData(idx).toInt();
                apply_text_fill_format();
                load_values();
                emit_change();
            });
    connect(cmb_gradient_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change, apply_text_fill_format](int idx) {
                if (can_edit()) { layer_->gradient_type = cmb_gradient_type_->itemData(idx).toInt(); apply_text_fill_format(); emit_change(); }
            });
    auto connect_gradient_color = [this, can_edit, emit_change, apply_text_fill_format](QPushButton *button, uint32_t Layer::*member,
                                                                 const char *title_key, bool text_fill) {
        connect(button, &QPushButton::clicked, this, [this, can_edit, emit_change, apply_text_fill_format, button, member, title_key, text_fill]() {
            if (!can_edit()) return;
            QColor picked = QColorDialog::getColor(color_from_argb((*layer_).*member), this, obsgs_tr(title_key),
                                                    QColorDialog::ShowAlphaChannel);
            if (!picked.isValid()) return;
            (*layer_).*member = argb_from_color(picked);
            style_color_button(button, (*layer_).*member);
            if (text_fill) apply_text_fill_format();
            emit_change();
        });
    };
    connect_gradient_color(btn_gradient_start_color_, &Layer::gradient_start_color, "OBSTitles.StartColorLabel", true);
    connect_gradient_color(btn_gradient_end_color_, &Layer::gradient_end_color, "OBSTitles.EndColorLabel", true);
    connect_gradient_color(btn_background_gradient_start_color_, &Layer::background_gradient_start_color, "OBSTitles.StartColorLabel", false);
    connect_gradient_color(btn_background_gradient_end_color_, &Layer::background_gradient_end_color, "OBSTitles.EndColorLabel", false);
    connect(spn_gradient_start_pos_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_fill_format](double v) { if (can_edit()) { layer_->gradient_start_pos = (float)v; apply_text_fill_format(); emit_change(); } });
    connect(spn_gradient_end_pos_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_fill_format](double v) { if (can_edit()) { layer_->gradient_end_pos = (float)v; apply_text_fill_format(); emit_change(); } });
    connect(spn_gradient_start_opacity_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_fill_format](double v) { if (can_edit()) { layer_->gradient_start_opacity = (float)v; apply_text_fill_format(); emit_change(); } });
    connect(spn_gradient_end_opacity_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_fill_format](double v) { if (can_edit()) { layer_->gradient_end_opacity = (float)v; apply_text_fill_format(); emit_change(); } });
    connect(spn_gradient_opacity_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_fill_format](double v) { if (can_edit()) { layer_->gradient_opacity = (float)v; apply_text_fill_format(); emit_change(); } });
    connect(spn_gradient_angle_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_fill_format](double v) { if (can_edit()) { layer_->gradient_angle = (float)v; apply_text_fill_format(); emit_change(); } });
    connect(spn_gradient_center_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_fill_format](double v) { if (can_edit()) { layer_->gradient_center_x = (float)v; apply_text_fill_format(); emit_change(); } });
    connect(spn_gradient_center_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_fill_format](double v) { if (can_edit()) { layer_->gradient_center_y = (float)v; apply_text_fill_format(); emit_change(); } });
    connect(spn_gradient_scale_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_fill_format](double v) { if (can_edit()) { layer_->gradient_scale = (float)v; apply_text_fill_format(); emit_change(); } });
    connect(spn_gradient_focal_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_fill_format](double v) { if (can_edit()) { layer_->gradient_focal_x = (float)v; apply_text_fill_format(); emit_change(); } });
    connect(spn_gradient_focal_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change, apply_text_fill_format](double v) { if (can_edit()) { layer_->gradient_focal_y = (float)v; apply_text_fill_format(); emit_change(); } });
    connect(cmb_background_gradient_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change](int idx) {
                if (can_edit()) { layer_->background_gradient_type = cmb_background_gradient_type_->itemData(idx).toInt(); emit_change(); }
            });
    connect(spn_background_gradient_start_pos_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->background_gradient_start_pos = (float)v; emit_change(); } });
    connect(spn_background_gradient_end_pos_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->background_gradient_end_pos = (float)v; emit_change(); } });
    connect(spn_background_gradient_start_opacity_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->background_gradient_start_opacity = (float)v; emit_change(); } });
    connect(spn_background_gradient_end_opacity_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->background_gradient_end_opacity = (float)v; emit_change(); } });
    connect(spn_background_gradient_opacity_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->background_gradient_opacity = (float)v; emit_change(); } });
    connect(spn_background_gradient_angle_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->background_gradient_angle = (float)v; emit_change(); } });
    connect(spn_background_gradient_center_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->background_gradient_center_x = (float)v; emit_change(); } });
    connect(spn_background_gradient_center_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->background_gradient_center_y = (float)v; emit_change(); } });
    connect(spn_background_gradient_scale_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->background_gradient_scale = (float)v; emit_change(); } });
    connect(spn_background_gradient_focal_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->background_gradient_focal_x = (float)v; emit_change(); } });
    connect(spn_background_gradient_focal_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->background_gradient_focal_y = (float)v; emit_change(); } });
    connect(chk_outline_enabled_, &QCheckBox::toggled,
            this, [this, can_edit, emit_change](bool v) {
                if (can_edit()) { layer_->outline_enabled = v; load_values(); emit_change(); }
            });
    connect(cmb_stroke_fill_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change](int idx) {
                if (!can_edit() || idx < 0) return;
                layer_->stroke_fill_type = cmb_stroke_fill_type_->itemData(idx).toInt();
                layer_->outline_enabled = layer_->stroke_fill_type != 0;
                load_values();
                emit_change();
            });
    connect(spn_outline_width_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) {
                if (can_edit()) { layer_->stroke_width = (float)v; emit_change(); }
            });
    connect(spn_outline_opacity_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) {
                if (can_edit()) { layer_->outline_opacity = (float)v; emit_change(); }
            });
    connect(cmb_outline_join_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change](int idx) {
                if (can_edit()) { layer_->outline_join_style = cmb_outline_join_->itemData(idx).toInt(); emit_change(); }
            });
    connect(cmb_outline_position_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change](int idx) {
                if (can_edit()) { layer_->outline_on_front = cmb_outline_position_->itemData(idx).toInt() != 0; emit_change(); }
            });
    connect(chk_outline_antialias_, &QCheckBox::toggled,
            this, [this, can_edit, emit_change](bool v) {
                if (can_edit()) { layer_->outline_antialias = v; emit_change(); }
            });
    connect(btn_outline_color_, &QPushButton::clicked,
            this, [this, can_edit, emit_change]() {
                if (!can_edit()) return;
                QColor initial = color_from_argb(layer_->stroke_color);
                QColor picked = QColorDialog::getColor(initial, this, obsgs_tr("OBSTitles.OutlineColor"),
                                                        QColorDialog::ShowAlphaChannel);
                if (!picked.isValid()) return;
                layer_->stroke_color = argb_from_color(picked);
                style_color_button(btn_outline_color_, layer_->stroke_color);
                emit_change();
            });
    connect(cmb_stroke_gradient_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change](int idx) {
                if (can_edit() && idx >= 0) { layer_->stroke_gradient_type = cmb_stroke_gradient_type_->itemData(idx).toInt(); emit_change(); }
            });
    connect(btn_stroke_gradient_start_color_, &QPushButton::clicked,
            this, [this, can_edit, emit_change]() {
                if (!can_edit()) return;
                QColor picked = QColorDialog::getColor(color_from_argb(layer_->stroke_gradient_start_color), this,
                                                        obsgs_tr("OBSTitles.OutlineColor"), QColorDialog::ShowAlphaChannel);
                if (!picked.isValid()) return;
                layer_->stroke_gradient_start_color = argb_from_color(picked);
                style_color_button(btn_stroke_gradient_start_color_, layer_->stroke_gradient_start_color);
                emit_change();
            });
    connect(btn_stroke_gradient_end_color_, &QPushButton::clicked,
            this, [this, can_edit, emit_change]() {
                if (!can_edit()) return;
                QColor picked = QColorDialog::getColor(color_from_argb(layer_->stroke_gradient_end_color), this,
                                                        obsgs_tr("OBSTitles.OutlineColor"), QColorDialog::ShowAlphaChannel);
                if (!picked.isValid()) return;
                layer_->stroke_gradient_end_color = argb_from_color(picked);
                style_color_button(btn_stroke_gradient_end_color_, layer_->stroke_gradient_end_color);
                emit_change();
            });
    connect(spn_stroke_gradient_start_pos_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->stroke_gradient_start_pos = (float)v; emit_change(); } });
    connect(spn_stroke_gradient_end_pos_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->stroke_gradient_end_pos = (float)v; emit_change(); } });
    connect(spn_stroke_gradient_start_opacity_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->stroke_gradient_start_opacity = (float)v; emit_change(); } });
    connect(spn_stroke_gradient_end_opacity_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->stroke_gradient_end_opacity = (float)v; emit_change(); } });
    connect(spn_stroke_gradient_opacity_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->stroke_gradient_opacity = (float)v; emit_change(); } });
    connect(spn_stroke_gradient_angle_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->stroke_gradient_angle = (float)v; emit_change(); } });
    connect(spn_stroke_gradient_center_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->stroke_gradient_center_x = (float)v; emit_change(); } });
    connect(spn_stroke_gradient_center_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->stroke_gradient_center_y = (float)v; emit_change(); } });
    connect(spn_stroke_gradient_scale_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->stroke_gradient_scale = (float)v; emit_change(); } });
    connect(spn_stroke_gradient_focal_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->stroke_gradient_focal_x = (float)v; emit_change(); } });
    connect(spn_stroke_gradient_focal_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, can_edit, emit_change](double v) { if (can_edit()) { layer_->stroke_gradient_focal_y = (float)v; emit_change(); } });
    connect(edit_image_path_, &QLineEdit::textChanged,
            this, [this, can_edit, emit_change](const QString &path){
                set_image_preview_label(lbl_image_preview_, path);
                if (can_edit()) { layer_->image_path = path.toStdString(); emit_change(); }
            });
    connect(cmb_image_scale_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, can_edit, emit_change](int idx){
                if (!can_edit() || idx < 0) return;
                layer_->scale_filter = (ImageScaleFilter)cmb_image_scale_filter_->itemData(idx).toInt();
                emit_change();
            });
    connect(btn_pick_image_, &QPushButton::clicked,
            this, [this, can_edit, local_time, emit_change]() {
                if (!can_edit()) return;
                QString path = QFileDialog::getOpenFileName(
                    this, obsgs_tr("OBSTitles.ChooseImage"),
                    QString::fromStdString(layer_->image_path),
                    obsgs_tr("OBSTitles.ImageFileFilter"));
                if (path.isEmpty()) return;
                layer_->image_path = path.toStdString();
                QSize image_size = editor_image_intrinsic_size(path);
                if (image_size.isValid() && !image_size.isEmpty()) {
                    double t = local_time();
                    layer_->rect_width = (float)image_size.width();
                    layer_->rect_height = (float)image_size.height();
                    set_animated_value(layer_->box_width, t, layer_->rect_width);
                    set_animated_value(layer_->box_height, t, layer_->rect_height);
                }
                load_values();
                emit_change();
            });

    connect(btn_kf_pos_x_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->pos_x, local_time(), spn_px_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_pos_y_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->pos_y, local_time(), spn_py_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_scale_x_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        const double t = local_time();
        if (layer_->scale_lock) {
            const bool remove = keyframe_at_time(layer_->scale_x, t) || keyframe_at_time(layer_->scale_y, t);
            if (remove) {
                remove_keyframe_at(layer_->scale_x, t);
                remove_keyframe_at(layer_->scale_y, t);
            } else {
                add_or_replace_keyframe(layer_->scale_x, t, spn_scale_x_->value() / 100.0);
                add_or_replace_keyframe(layer_->scale_y, t, spn_scale_y_->value() / 100.0);
            }
        } else {
            toggle_keyframe(layer_->scale_x, t, spn_scale_x_->value() / 100.0);
        }
        load_values();
        emit_change();
    });
    connect(btn_kf_scale_y_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        const double t = local_time();
        if (layer_->scale_lock) {
            const bool remove = keyframe_at_time(layer_->scale_x, t) || keyframe_at_time(layer_->scale_y, t);
            if (remove) {
                remove_keyframe_at(layer_->scale_x, t);
                remove_keyframe_at(layer_->scale_y, t);
            } else {
                add_or_replace_keyframe(layer_->scale_x, t, spn_scale_x_->value() / 100.0);
                add_or_replace_keyframe(layer_->scale_y, t, spn_scale_y_->value() / 100.0);
            }
        } else {
            toggle_keyframe(layer_->scale_y, t, spn_scale_y_->value() / 100.0);
        }
        load_values();
        emit_change();
    });
    connect(btn_kf_rotation_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->rotation, local_time(), spn_rot_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_opacity_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->opacity, local_time(), spn_opacity_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_origin_x_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->origin_x_prop, local_time(), spn_origin_x_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_origin_y_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->origin_y_prop, local_time(), spn_origin_y_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_paragraph_indent_left_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->paragraph_indent_left_prop, local_time(), spn_paragraph_indent_left_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_paragraph_indent_right_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->paragraph_indent_right_prop, local_time(), spn_paragraph_indent_right_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_paragraph_indent_first_line_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->paragraph_indent_first_line_prop, local_time(), spn_paragraph_indent_first_line_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_width_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->box_width, local_time(), spn_layer_w_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_text_color_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        double t = local_time();
        uint32_t color = eval_text_color(*layer_, t);
        if (any_keyframe_at_time({&layer_->text_color_a, &layer_->text_color_r,
                                  &layer_->text_color_g, &layer_->text_color_b}, t)) {
            remove_keyframe_at(layer_->text_color_a, t);
            remove_keyframe_at(layer_->text_color_r, t);
            remove_keyframe_at(layer_->text_color_g, t);
            remove_keyframe_at(layer_->text_color_b, t);
        } else {
            add_or_replace_keyframe(layer_->text_color_a, t, (color >> 24) & 0xFF);
            add_or_replace_keyframe(layer_->text_color_r, t, (color >> 16) & 0xFF);
            add_or_replace_keyframe(layer_->text_color_g, t, (color >> 8) & 0xFF);
            add_or_replace_keyframe(layer_->text_color_b, t, color & 0xFF);
        }
        load_values();
        emit_change();
    });

    connect(btn_kf_shadow_enabled_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->shadow_enabled_prop, local_time(), chk_shadow_enabled_->isChecked() ? 1.0 : 0.0);
        load_values();
        emit_change();
    });
    connect(btn_kf_shadow_opacity_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->shadow_opacity_prop, local_time(), spn_shadow_opacity_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_shadow_distance_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->shadow_distance_prop, local_time(), spn_shadow_distance_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_shadow_angle_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->shadow_angle_prop, local_time(), spn_shadow_angle_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_shadow_blur_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->shadow_blur_prop, local_time(), spn_shadow_blur_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_shadow_spread_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        toggle_keyframe(layer_->shadow_spread_prop, local_time(), spn_shadow_spread_->value());
        load_values();
        emit_change();
    });
    connect(btn_kf_shadow_color_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        double t = local_time();
        uint32_t color = eval_shadow_color(*layer_, t);
        if (any_keyframe_at_time({&layer_->shadow_color_a, &layer_->shadow_color_r,
                                  &layer_->shadow_color_g, &layer_->shadow_color_b}, t)) {
            remove_keyframe_at(layer_->shadow_color_a, t);
            remove_keyframe_at(layer_->shadow_color_r, t);
            remove_keyframe_at(layer_->shadow_color_g, t);
            remove_keyframe_at(layer_->shadow_color_b, t);
        } else {
            add_or_replace_keyframe(layer_->shadow_color_a, t, (color >> 24) & 0xFF);
            add_or_replace_keyframe(layer_->shadow_color_r, t, (color >> 16) & 0xFF);
            add_or_replace_keyframe(layer_->shadow_color_g, t, (color >> 8) & 0xFF);
            add_or_replace_keyframe(layer_->shadow_color_b, t, color & 0xFF);
        }
        load_values();
        emit_change();
    });
    connect(btn_kf_appearance_fill_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        const bool text_fill = layer_->type == LayerType::Text || layer_->type == LayerType::Clock ||
                               layer_->type == LayerType::Ticker;
        double t = local_time();
        uint32_t color = text_fill ? eval_text_color(*layer_, t) : eval_fill_color(*layer_, t);
        auto &a = text_fill ? layer_->text_color_a : layer_->fill_color_a;
        auto &r = text_fill ? layer_->text_color_r : layer_->fill_color_r;
        auto &g = text_fill ? layer_->text_color_g : layer_->fill_color_g;
        auto &b = text_fill ? layer_->text_color_b : layer_->fill_color_b;
        if (any_keyframe_at_time({&a, &r, &g, &b}, t)) {
            remove_keyframe_at(a, t);
            remove_keyframe_at(r, t);
            remove_keyframe_at(g, t);
            remove_keyframe_at(b, t);
        } else {
            add_or_replace_keyframe(a, t, (color >> 24) & 0xFF);
            add_or_replace_keyframe(r, t, (color >> 16) & 0xFF);
            add_or_replace_keyframe(g, t, (color >> 8) & 0xFF);
            add_or_replace_keyframe(b, t, color & 0xFF);
        }
        load_values();
        emit_change();
    });
    connect(btn_kf_fill_color_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        double t = local_time();
        uint32_t color = eval_fill_color(*layer_, t);
        if (any_keyframe_at_time({&layer_->fill_color_a, &layer_->fill_color_r,
                                  &layer_->fill_color_g, &layer_->fill_color_b}, t)) {
            remove_keyframe_at(layer_->fill_color_a, t);
            remove_keyframe_at(layer_->fill_color_r, t);
            remove_keyframe_at(layer_->fill_color_g, t);
            remove_keyframe_at(layer_->fill_color_b, t);
        } else {
            add_or_replace_keyframe(layer_->fill_color_a, t, (color >> 24) & 0xFF);
            add_or_replace_keyframe(layer_->fill_color_r, t, (color >> 16) & 0xFF);
            add_or_replace_keyframe(layer_->fill_color_g, t, (color >> 8) & 0xFF);
            add_or_replace_keyframe(layer_->fill_color_b, t, color & 0xFF);
        }
        load_values();
        emit_change();
    });
}

void PropertiesPanel::set_title(std::shared_ptr<Title> t)
{
    title_ = t;
}

void PropertiesPanel::set_active_text_edit_layer(const std::string &layer_id)
{
    if (active_text_edit_layer_id_ == layer_id) return;
    active_text_edit_layer_id_ = layer_id;
    load_values();
}

void PropertiesPanel::set_layer(std::shared_ptr<Layer> layer, double t)
{
    layer_    = layer;
    playhead_ = t;
    load_values();
}

void PropertiesPanel::load_values()
{
    loading_values_ = true;
    if (!layer_) {
        if (transform_box_) transform_box_->setVisible(false);
        if (appearance_box_) appearance_box_->setVisible(false);
        if (btn_transform_defaults_) btn_transform_defaults_->setEnabled(false);
        if (btn_anchor_grid_) { btn_anchor_grid_->setProperty("active_index", 4); btn_anchor_grid_->update(); }
        text_box_->setVisible(false);
        if (type_options_box_) type_options_box_->setVisible(false);
        if (paragraph_box_) paragraph_box_->setVisible(false);
        if (dynamic_text_box_) dynamic_text_box_->setVisible(false);
        if (bullets_box_) bullets_box_->setVisible(false);
        rect_box_->setVisible(false);
        if (btn_shape_defaults_) btn_shape_defaults_->setVisible(false);
        if (chk_size_lock_) chk_size_lock_->setVisible(false);
        if (grp_shape_type_) {
            for (auto *button : grp_shape_type_->buttons())
                button->setVisible(false);
        }
        if (auto *shape_types_row = rect_box_ ? rect_box_->findChild<QWidget *>(QStringLiteral("OBSTitlesShapeTypeButtonsRow")) : nullptr)
            shape_types_row->setVisible(false);
        if (gradient_box_) gradient_box_->setVisible(false);
        image_box_->setVisible(false);
        if (outline_box_) outline_box_->setVisible(false);
        if (shadow_box_) shadow_box_->setVisible(false);
        spn_px_->setValue(0.0);
        spn_py_->setValue(0.0);
        spn_rot_->setValue(0.0);
        spn_opacity_->setValue(1.0);
        spn_origin_x_->setValue(0.5);
        spn_origin_y_->setValue(0.5);
        txt_content_->clear();
        edit_image_path_->clear();
        set_image_preview_label(lbl_image_preview_, QString());
        if (cmb_image_scale_filter_) {
            QSignalBlocker block(cmb_image_scale_filter_);
            cmb_image_scale_filter_->setCurrentIndex(1);
        }
        style_color_button(btn_text_color_, 0xFFFFFFFF);
        if (btn_text_color_) btn_text_color_->setEnabled(true);
        if (btn_kf_text_color_) btn_kf_text_color_->setEnabled(true);
        style_color_button(btn_fill_color_, 0xFF222222);
        if (cmb_fill_type_) cmb_fill_type_->setCurrentIndex(0);
        if (cmb_gradient_type_) cmb_gradient_type_->setCurrentIndex(0);
        if (btn_gradient_start_color_) style_color_button(btn_gradient_start_color_, 0xFF4B6EA8);
        if (btn_gradient_end_color_) style_color_button(btn_gradient_end_color_, 0xFF1B1B1B);
        if (btn_appearance_fill_color_) { style_color_button(btn_appearance_fill_color_, 0xFF222222); btn_appearance_fill_color_->setText(QString()); }
        if (btn_appearance_stroke_color_) { style_color_button(btn_appearance_stroke_color_, 0xFF000000); btn_appearance_stroke_color_->setText(QString()); }
        if (spn_appearance_stroke_width_) spn_appearance_stroke_width_->setValue(0.0);
        if (spn_appearance_opacity_) spn_appearance_opacity_->setValue(100.0);
        if (spn_gradient_start_pos_) spn_gradient_start_pos_->setValue(0.0);
        if (spn_gradient_end_pos_) spn_gradient_end_pos_->setValue(1.0);
        if (spn_gradient_start_opacity_) spn_gradient_start_opacity_->setValue(1.0);
        if (spn_gradient_end_opacity_) spn_gradient_end_opacity_->setValue(1.0);
        if (spn_gradient_opacity_) spn_gradient_opacity_->setValue(1.0);
        if (spn_gradient_angle_) spn_gradient_angle_->setValue(0.0);
        if (spn_gradient_center_x_) spn_gradient_center_x_->setValue(0.5);
        if (spn_gradient_center_y_) spn_gradient_center_y_->setValue(0.5);
        if (spn_gradient_scale_) spn_gradient_scale_->setValue(1.0);
        if (spn_gradient_focal_x_) spn_gradient_focal_x_->setValue(0.5);
        if (spn_gradient_focal_y_) spn_gradient_focal_y_->setValue(0.5);
        if (chk_outline_enabled_) chk_outline_enabled_->setChecked(false);
        if (cmb_stroke_fill_type_) cmb_stroke_fill_type_->setCurrentIndex(1);
        if (btn_outline_color_) style_color_button(btn_outline_color_, 0xFF000000);
        if (spn_outline_width_) spn_outline_width_->setValue(0.0);
        if (spn_outline_opacity_) spn_outline_opacity_->setValue(1.0);
        if (cmb_outline_join_) cmb_outline_join_->setCurrentIndex(1);
        if (cmb_outline_position_) cmb_outline_position_->setCurrentIndex(1);
        if (chk_outline_antialias_) chk_outline_antialias_->setChecked(true);
        if (cmb_stroke_gradient_type_) cmb_stroke_gradient_type_->setCurrentIndex(0);
        if (btn_stroke_gradient_start_color_) style_color_button(btn_stroke_gradient_start_color_, 0xFFFFFFFF);
        if (btn_stroke_gradient_end_color_) style_color_button(btn_stroke_gradient_end_color_, 0xFF000000);
        if (spn_stroke_gradient_start_pos_) spn_stroke_gradient_start_pos_->setValue(0.0);
        if (spn_stroke_gradient_end_pos_) spn_stroke_gradient_end_pos_->setValue(1.0);
        if (spn_stroke_gradient_start_opacity_) spn_stroke_gradient_start_opacity_->setValue(1.0);
        if (spn_stroke_gradient_end_opacity_) spn_stroke_gradient_end_opacity_->setValue(1.0);
        if (spn_stroke_gradient_opacity_) spn_stroke_gradient_opacity_->setValue(1.0);
        if (spn_stroke_gradient_angle_) spn_stroke_gradient_angle_->setValue(0.0);
        if (spn_stroke_gradient_center_x_) spn_stroke_gradient_center_x_->setValue(0.5);
        if (spn_stroke_gradient_center_y_) spn_stroke_gradient_center_y_->setValue(0.5);
        if (spn_stroke_gradient_scale_) spn_stroke_gradient_scale_->setValue(1.0);
        if (spn_stroke_gradient_focal_x_) spn_stroke_gradient_focal_x_->setValue(0.5);
        if (spn_stroke_gradient_focal_y_) spn_stroke_gradient_focal_y_->setValue(0.5);
        spn_layer_w_->setValue(0.0);
        spn_layer_h_->setValue(0.0);
        if (spn_rect_corner_tl_) spn_rect_corner_tl_->setValue(0.0);
        if (spn_rect_corner_tr_) spn_rect_corner_tr_->setValue(0.0);
        if (spn_rect_corner_br_) spn_rect_corner_br_->setValue(0.0);
        if (spn_rect_corner_bl_) spn_rect_corner_bl_->setValue(0.0);
        spn_size_->setValue(72);
        if (cmb_font_style_) populate_font_style_combo(cmb_font_style_, cmb_font_->currentText(), QStringLiteral("Regular"));
        chk_bold_->setChecked(false);
        chk_italic_->setChecked(false);
        if (chk_font_kerning_) chk_font_kerning_->setChecked(true);
        if (spn_text_leading_) spn_text_leading_->setValue(0.0);
        if (spn_char_tracking_) spn_char_tracking_->setValue(0.0);
        if (cmb_kerning_mode_) cmb_kerning_mode_->setCurrentIndex(0);
        if (spn_kerning_value_) spn_kerning_value_->setValue(0.0);
        if (spn_scale_x_) spn_scale_x_->setValue(100.0);
        if (spn_scale_y_) spn_scale_y_->setValue(100.0);
        if (chk_scale_lock_) chk_scale_lock_->setChecked(true);
        if (spn_char_scale_x_) spn_char_scale_x_->setValue(100.0);
        if (spn_char_scale_y_) spn_char_scale_y_->setValue(100.0);
        if (spn_baseline_shift_) spn_baseline_shift_->setValue(0.0);
        if (cmb_language_) cmb_language_->setCurrentIndex(0);
        for (auto *b : {btn_all_caps_, btn_small_caps_, btn_superscript_, btn_subscript_, btn_underline_,
                        btn_strikethrough_, btn_ligatures_, btn_stylistic_alternates_, btn_fractions_, btn_opentype_features_})
            if (b) b->setChecked(false);
        if (cmb_text_style_) cmb_text_style_->setCurrentIndex(0);
        if (cmb_text_overflow_) cmb_text_overflow_->setCurrentIndex(0);
        if (spn_text_fit_min_scale_) spn_text_fit_min_scale_->setValue(0.5);
        if (lbl_text_fit_scale_) lbl_text_fit_scale_->setText(obsgs_tr("OBSTitles.Scale100"));
        if (chk_text_box_width_to_text_) chk_text_box_width_to_text_->setChecked(false);
        if (chk_text_box_height_to_text_) chk_text_box_height_to_text_->setChecked(false);
        if (spn_max_text_box_width_) { spn_max_text_box_width_->setValue(1920.0); spn_max_text_box_width_->setEnabled(false); }
        if (spn_max_text_box_height_) { spn_max_text_box_height_->setValue(1080.0); spn_max_text_box_height_->setEnabled(false); }
        if (grp_text_align_ && grp_text_align_->button(1)) grp_text_align_->button(1)->setChecked(true);
        if (grp_text_valign_ && grp_text_valign_->button(1)) grp_text_valign_->button(1)->setChecked(true);
        if (spn_paragraph_indent_left_) spn_paragraph_indent_left_->setValue(0.0);
        if (spn_paragraph_indent_right_) spn_paragraph_indent_right_->setValue(0.0);
        if (spn_paragraph_indent_first_line_) spn_paragraph_indent_first_line_->setValue(0.0);
        if (spn_paragraph_space_before_) spn_paragraph_space_before_->setValue(0.0);
        if (spn_paragraph_space_after_) spn_paragraph_space_after_->setValue(0.0);
        if (chk_paragraph_hyphenate_) chk_paragraph_hyphenate_->setChecked(false);
        if (cmb_anchor_) cmb_anchor_->setCurrentIndex(4);
        if (btn_anchor_grid_) { btn_anchor_grid_->setProperty("active_index", 4); btn_anchor_grid_->update(); }
        if (chk_shadow_enabled_) chk_shadow_enabled_->setChecked(false);
        if (cmb_shadow_preset_) cmb_shadow_preset_->setCurrentIndex(0);
        if (cmb_shadow_blur_type_) cmb_shadow_blur_type_->setCurrentIndex(2);
        if (btn_shadow_color_) style_color_button(btn_shadow_color_, 0x99000000);
        if (spn_shadow_opacity_) spn_shadow_opacity_->setValue(0.6);
        if (spn_shadow_distance_) spn_shadow_distance_->setValue(8.0);
        if (spn_shadow_angle_) spn_shadow_angle_->setValue(135.0);
        if (spn_shadow_blur_) spn_shadow_blur_->setValue(4.0);
        if (spn_shadow_spread_) spn_shadow_spread_->setValue(0.0);
        if (chk_long_shadow_enabled_) chk_long_shadow_enabled_->setChecked(false);
        if (btn_long_shadow_color_) style_color_button(btn_long_shadow_color_, 0x99000000);
        if (spn_long_shadow_opacity_) spn_long_shadow_opacity_->setValue(0.45);
        if (spn_long_shadow_length_) spn_long_shadow_length_->setValue(0.0);
        if (spn_long_shadow_angle_) spn_long_shadow_angle_->setValue(135.0);
        if (spn_long_shadow_falloff_) spn_long_shadow_falloff_->setValue(1.0);
        if (cmb_long_shadow_blur_type_) cmb_long_shadow_blur_type_->setCurrentIndex(0);
        if (spn_long_shadow_blur_) spn_long_shadow_blur_->setValue(8.0);
        for (auto *b : {btn_kf_pos_x_, btn_kf_pos_y_, btn_kf_scale_x_, btn_kf_scale_y_,
                        btn_kf_rotation_, btn_kf_opacity_, btn_kf_origin_x_, btn_kf_origin_y_,
                        btn_kf_appearance_fill_, btn_kf_appearance_stroke_,
                        btn_kf_paragraph_indent_left_, btn_kf_paragraph_indent_right_, btn_kf_paragraph_indent_first_line_,
                        btn_kf_width_,
                        btn_kf_text_color_, btn_kf_fill_color_, btn_kf_shadow_enabled_,
                        btn_kf_shadow_opacity_, btn_kf_shadow_distance_, btn_kf_shadow_angle_,
                        btn_kf_shadow_blur_, btn_kf_shadow_spread_, btn_kf_shadow_color_}) {
            if (!b) continue;
            b->setIcon(keyframe_diamond_icon(false));
            b->setProperty("active", false);
            b->setProperty("outlined", false);
            b->style()->unpolish(b);
            b->style()->polish(b);
        }
        loading_values_ = false;
        return;
    }

    const bool is_text = layer_->type == LayerType::Text;
    const bool is_clock = layer_->type == LayerType::Clock;
    const bool is_ticker = layer_->type == LayerType::Ticker;
    const bool is_text_like = is_text || is_clock || is_ticker;
    const bool is_rect = layer_->type == LayerType::SolidRect || layer_->type == LayerType::Shape;
    const bool is_image = layer_->type == LayerType::Image;
    const bool is_scene_mask_layer = layer_->use_as_scene_mask;
    const bool supports_outline = is_text_like || is_rect;
    if (transform_box_) transform_box_->setVisible(true);
    if (appearance_box_) appearance_box_->setVisible(true);
    if (btn_transform_defaults_) btn_transform_defaults_->setEnabled(true);
    text_box_->setVisible(is_text_like);
    if (type_options_box_) type_options_box_->setVisible(is_text_like);
    if (paragraph_box_) paragraph_box_->setVisible(is_text_like);
    if (dynamic_text_box_) dynamic_text_box_->setVisible(is_text_like);
    if (bullets_box_) bullets_box_->setVisible(false);
    text_box_->setTitle("Character");
    if (row_text_color_) row_text_color_->setVisible(false);
    if (btn_kf_text_color_) btn_kf_text_color_->setVisible(false);
    if (auto *char_grid = qobject_cast<QGridLayout *>(text_box_->layout())) {
        const bool show_text_editor = !is_text;
        if (auto *label_item = char_grid->itemAtPosition(0, 0)) {
            if (auto *label = qobject_cast<QLabel *>(label_item->widget())) {
                label->setText(is_clock ? obsgs_tr("OBSTitles.DateTimeFormatLabel")
                                        : obsgs_tr("OBSTitles.TextLabel"));
                label->setVisible(show_text_editor);
            }
        }
        if (txt_content_) txt_content_->setVisible(show_text_editor);
        if (auto *label_item = char_grid->itemAtPosition(7, 0)) {
            if (auto *label = label_item->widget())
                label->setVisible(false);
        }
    }
    txt_content_->setPlaceholderText(is_clock ? "H:i:s" : obsgs_tr("OBSTitles.EnterTextPlaceholder"));
    if (spn_text_fit_min_scale_) spn_text_fit_min_scale_->setVisible(is_text_like && layer_->text_overflow_mode == 2 && !is_ticker);
    if (lbl_text_fit_scale_) lbl_text_fit_scale_->setVisible(is_text_like && layer_->text_overflow_mode == 2 && !is_ticker);
    if (auto *dynamic_form = qobject_cast<QFormLayout *>(dynamic_text_box_ ? dynamic_text_box_->layout() : nullptr)) {
        const bool show_ticker_fit = is_text_like && layer_->text_overflow_mode == 2 && !is_ticker;
        if (cmb_text_style_) {
            cmb_text_style_->setVisible(false);
            if (auto *label = dynamic_form->labelForField(cmb_text_style_)) label->setVisible(false);
        }
        if (auto *label = dynamic_form->labelForField(spn_text_fit_min_scale_))
            label->setVisible(show_ticker_fit);
        if (cmb_ticker_style_) {
            cmb_ticker_style_->setVisible(is_ticker);
            if (auto *label = dynamic_form->labelForField(cmb_ticker_style_)) label->setVisible(is_ticker);
        }
        if (spn_ticker_speed_) {
            spn_ticker_speed_->setVisible(is_ticker && layer_->ticker_style != 1);
            if (auto *label = dynamic_form->labelForField(spn_ticker_speed_)) label->setVisible(is_ticker && layer_->ticker_style != 1);
        }
        if (spn_ticker_line_hold_) {
            spn_ticker_line_hold_->setVisible(is_ticker && layer_->ticker_style == 1);
            if (auto *label = dynamic_form->labelForField(spn_ticker_line_hold_)) label->setVisible(is_ticker && layer_->ticker_style == 1);
        }
        if (cmb_ticker_direction_) {
            cmb_ticker_direction_->setVisible(is_ticker);
            if (auto *label = dynamic_form->labelForField(cmb_ticker_direction_)) label->setVisible(is_ticker);
        }
        if (chk_expose_text_) {
            chk_expose_text_->setVisible(is_text || is_ticker);
            if (auto *label = dynamic_form->labelForField(chk_expose_text_))
                label->setVisible(is_text || is_ticker);
        }
    }
    rect_box_->setVisible(is_text_like || is_rect || is_image);
    rect_box_->setTitle(QString());
    if (auto *shape_title = rect_box_->findChild<QLabel *>(QStringLiteral("OBSTitlesShapePanelTitle"))) {
        shape_title->setText(is_rect ? QStringLiteral("Shape")
                            : is_image ? obsgs_tr("OBSTitles.ImageSize")
                                       : (is_clock ? obsgs_tr("OBSTitles.ClockBox")
                                                   : (is_ticker ? obsgs_tr("OBSTitles.TickerBox")
                                                                : obsgs_tr("OBSTitles.TextBox"))));
    }
    const bool is_shape_layer = layer_->type == LayerType::Shape || layer_->type == LayerType::SolidRect;
    const ShapeType current_shape = layer_->type == LayerType::SolidRect ? ShapeType::RoundedRectangle : layer_->shape_type;
    const bool show_corner_radius = is_shape_layer &&
                                    (current_shape == ShapeType::Rectangle ||
                                     current_shape == ShapeType::RoundedRectangle);
    const bool show_star_controls = is_shape_layer && current_shape == ShapeType::Star;
    const bool show_polygon_controls = is_shape_layer && current_shape == ShapeType::Polygon;
    const bool show_roundness = false;
    if (row_rect_corners_) row_rect_corners_->setVisible(show_corner_radius);
    if (chk_corner_lock_) chk_corner_lock_->setVisible(show_corner_radius);
    if (row_corner_type_) row_corner_type_->setVisible(show_corner_radius);
    if (grp_shape_type_) {
        for (auto *button : grp_shape_type_->buttons())
            button->setVisible(is_shape_layer);
    }
    if (auto *shape_types_row = rect_box_->findChild<QWidget *>(QStringLiteral("OBSTitlesShapeTypeButtonsRow")))
        shape_types_row->setVisible(is_shape_layer);
    if (btn_shape_defaults_) btn_shape_defaults_->setVisible(is_shape_layer);
    if (chk_size_lock_) chk_size_lock_->setVisible(is_shape_layer || is_image);
    if (spn_shape_points_) spn_shape_points_->setVisible(show_star_controls);
    if (spn_shape_sides_) spn_shape_sides_->setVisible(show_polygon_controls);
    if (spn_shape_inner_radius_) spn_shape_inner_radius_->setVisible(show_star_controls);
    if (spn_shape_outer_radius_) spn_shape_outer_radius_->setVisible(show_star_controls);
    if (spn_shape_roundness_) spn_shape_roundness_->setVisible(show_roundness);
    const bool supports_fill_type = is_rect || is_text_like;
    const bool solid_fill_active = is_rect && layer_->fill_type == 0;
    if (cmb_fill_type_) cmb_fill_type_->setVisible(false);
    if (btn_fill_color_) btn_fill_color_->setVisible(false);
    if (gradient_box_) gradient_box_->setVisible(false);
    const bool supports_text_box_auto_size = is_text || is_clock;
    if (chk_text_box_width_to_text_) chk_text_box_width_to_text_->setVisible(supports_text_box_auto_size);
    if (chk_text_box_height_to_text_) chk_text_box_height_to_text_->setVisible(supports_text_box_auto_size);
    if (spn_max_text_box_width_) spn_max_text_box_width_->setVisible(supports_text_box_auto_size);
    if (spn_max_text_box_height_) spn_max_text_box_height_->setVisible(supports_text_box_auto_size);
    btn_kf_text_color_->setVisible(false);
    const bool gradient_text_active = is_text_like && layer_->fill_type == 1;
    if (btn_text_color_) btn_text_color_->setEnabled(!gradient_text_active);
    if (btn_kf_text_color_) btn_kf_text_color_->setEnabled(!gradient_text_active);
    btn_kf_fill_color_->setVisible(false);
    if (row_fill_type_) row_fill_type_->setVisible(false);
    if (row_fill_color_) row_fill_color_->setVisible(false);
    const bool fill_controls_enabled = !is_scene_mask_layer;
    if (cmb_fill_type_) cmb_fill_type_->setEnabled(fill_controls_enabled);
    if (row_fill_type_) row_fill_type_->setEnabled(fill_controls_enabled);
    if (btn_fill_color_) btn_fill_color_->setEnabled(fill_controls_enabled);
    if (row_fill_color_) row_fill_color_->setEnabled(fill_controls_enabled);
    if (gradient_box_) gradient_box_->setEnabled(fill_controls_enabled);
    if (btn_kf_fill_color_) btn_kf_fill_color_->setEnabled(fill_controls_enabled);
    if (background_gradient_box_) background_gradient_box_->setVisible(false);
    const bool stroke_enabled = supports_outline && layer_->outline_enabled && layer_->stroke_fill_type != 0;
    const bool stroke_color_active = stroke_enabled && layer_->stroke_fill_type == 1;
    const bool stroke_gradient_active = stroke_enabled && layer_->stroke_fill_type == 2;
    if (outline_box_) outline_box_->setVisible(false);
    if (auto *outline_form = qobject_cast<QFormLayout *>(outline_box_->layout())) {
        auto set_row_visible = [&](QWidget *field, bool visible) {
            if (!field) return;
            field->setVisible(visible);
            if (auto *label = outline_form->labelForField(field))
                label->setVisible(visible);
        };
        set_row_visible(chk_outline_enabled_, false);
        set_row_visible(cmb_stroke_fill_type_, false);
        set_row_visible(row_outline_color_, false);
        set_row_visible(spn_outline_width_, false);
        set_row_visible(spn_outline_opacity_, false);
        set_row_visible(cmb_outline_join_, false);
        set_row_visible(cmb_outline_position_, false);
        set_row_visible(chk_outline_antialias_, false);
        for (QWidget *field : std::initializer_list<QWidget *>{cmb_stroke_gradient_type_,
                                                               btn_stroke_gradient_start_color_,
                                                               spn_stroke_gradient_start_pos_,
                                                               spn_stroke_gradient_start_opacity_,
                                                               btn_stroke_gradient_end_color_,
                                                               spn_stroke_gradient_end_pos_,
                                                               spn_stroke_gradient_end_opacity_,
                                                               spn_stroke_gradient_opacity_,
                                                               spn_stroke_gradient_angle_,
                                                               spn_stroke_gradient_center_x_,
                                                               spn_stroke_gradient_center_y_,
                                                               spn_stroke_gradient_scale_,
                                                               spn_stroke_gradient_focal_x_,
                                                               spn_stroke_gradient_focal_y_}) {
            set_row_visible(field, false);
        }
    }
    if (auto *form = rect_box_->findChild<QFormLayout *>()) {
        if (auto *label = form->labelForField(row_rect_corners_)) label->setVisible(show_corner_radius);
        if (auto *label = form->labelForField(row_corner_type_)) label->setVisible(show_corner_radius);
        if (auto *label = form->labelForField(spn_shape_points_)) label->setVisible(show_star_controls);
        if (auto *label = form->labelForField(spn_shape_sides_)) label->setVisible(show_polygon_controls);
        if (auto *label = form->labelForField(spn_shape_inner_radius_)) label->setVisible(show_star_controls);
        if (auto *label = form->labelForField(spn_shape_outer_radius_)) label->setVisible(show_star_controls);
        if (auto *label = form->labelForField(spn_shape_roundness_)) label->setVisible(show_roundness);
        if (auto *label = form->labelForField(row_fill_type_))
            label->setVisible(false);
        if (auto *label = form->labelForField(row_fill_color_))
            label->setVisible(false);
        for (QWidget *field : std::initializer_list<QWidget *>{chk_text_box_width_to_text_, spn_max_text_box_width_,
                                                               chk_text_box_height_to_text_, spn_max_text_box_height_})
            if (auto *label = form->labelForField(field)) label->setVisible(supports_text_box_auto_size);
    }
    image_box_->setVisible(is_image);
    if (shadow_box_) shadow_box_->setVisible(false);

    double lt = std::clamp(playhead_ - layer_->in_time, 0.0,
                           std::max(0.0, layer_->out_time - layer_->in_time));
    spn_px_->setValue(layer_->pos_x.is_animated()
                      ? layer_->pos_x.evaluate(lt)
                      : layer_->pos_x.static_value);
    spn_py_->setValue(layer_->pos_y.is_animated()
                      ? layer_->pos_y.evaluate(lt)
                      : layer_->pos_y.static_value);
    spn_scale_x_->setValue((layer_->scale_x.is_animated()
                            ? layer_->scale_x.evaluate(lt)
                            : layer_->scale_x.static_value) * 100.0);
    spn_scale_y_->setValue((layer_->scale_y.is_animated()
                            ? layer_->scale_y.evaluate(lt)
                            : layer_->scale_y.static_value) * 100.0);
    if (chk_scale_lock_) chk_scale_lock_->setChecked(layer_->scale_lock);
    spn_rot_->setValue(layer_->rotation.is_animated()
                       ? layer_->rotation.evaluate(lt)
                       : layer_->rotation.static_value);
    spn_opacity_->setValue(layer_->opacity.is_animated()
                           ? layer_->opacity.evaluate(lt)
                           : layer_->opacity.static_value);
    if (spn_appearance_opacity_) spn_appearance_opacity_->setValue(spn_opacity_->value() * 100.0);
    const bool supports_fill_appearance = is_text_like || is_rect;
    if (btn_appearance_fill_color_) {
        if (layer_->fill_type == 1)
            style_gradient_button(btn_appearance_fill_color_,
                                  layer_->gradient_start_color,
                                  layer_->gradient_end_color,
                                  layer_->gradient_type);
        else
            style_color_button(btn_appearance_fill_color_,
                               is_text_like ? eval_text_color(*layer_, lt) : eval_fill_color(*layer_, lt));
        btn_appearance_fill_color_->setText(QString());
        btn_appearance_fill_color_->setEnabled(supports_fill_appearance && fill_controls_enabled);
    }
    if (btn_kf_appearance_fill_) btn_kf_appearance_fill_->setEnabled(supports_fill_appearance && fill_controls_enabled);
    if (btn_appearance_stroke_color_) {
        if (layer_->stroke_fill_type == 2)
            style_gradient_button(btn_appearance_stroke_color_,
                                  layer_->stroke_gradient_start_color,
                                  layer_->stroke_gradient_end_color,
                                  layer_->stroke_gradient_type);
        else
            style_color_button(btn_appearance_stroke_color_, eval_outline_color(*layer_, lt));
        btn_appearance_stroke_color_->setText(QString());
        btn_appearance_stroke_color_->setEnabled(supports_outline);
    }
    if (spn_appearance_stroke_width_) {
        spn_appearance_stroke_width_->setValue(eval_outline_width(*layer_, lt));
        spn_appearance_stroke_width_->setEnabled(supports_outline);
    }
    if (btn_kf_appearance_stroke_) btn_kf_appearance_stroke_->setEnabled(false);
    spn_origin_x_->setValue(eval_origin_x(*layer_, lt));
    spn_origin_y_->setValue(eval_origin_y(*layer_, lt));
    const int anchor_index = anchor_index_from_layer(*layer_);
    cmb_anchor_->setCurrentIndex(anchor_index);
    if (btn_anchor_grid_) { btn_anchor_grid_->setProperty("active_index", anchor_index); btn_anchor_grid_->update(); }

    spn_layer_w_->setValue(eval_box_width(*layer_, lt));
    spn_layer_h_->setValue(eval_box_height(*layer_, lt));
    if (chk_size_lock_) chk_size_lock_->setChecked(layer_->lock_aspect_ratio);
    if (chk_corner_lock_) chk_corner_lock_->setChecked(layer_->corner_radius_locked);
    if (spn_rect_corner_tl_) spn_rect_corner_tl_->setValue(layer_->corner_radius_tl);
    if (spn_rect_corner_tr_) spn_rect_corner_tr_->setValue(layer_->corner_radius_tr);
    if (spn_rect_corner_br_) spn_rect_corner_br_->setValue(layer_->corner_radius_br);
    if (spn_rect_corner_bl_) spn_rect_corner_bl_->setValue(layer_->corner_radius_bl);
    if (grp_corner_type_) {
        if (auto *button = grp_corner_type_->button((int)layer_->corner_type))
            button->setChecked(true);
    }
    if (cmb_shape_type_) {
        int shape_idx = cmb_shape_type_->findData((int)current_shape);
        cmb_shape_type_->setCurrentIndex(shape_idx >= 0 ? shape_idx : 0);
    }
    if (grp_shape_type_) {
        if (auto *button = grp_shape_type_->button((int)current_shape))
            button->setChecked(true);
    }
    if (spn_shape_points_) spn_shape_points_->setValue(layer_->shape_points);
    if (spn_shape_sides_) spn_shape_sides_->setValue(layer_->shape_sides);
    if (spn_shape_inner_radius_) spn_shape_inner_radius_->setValue(layer_->shape_inner_radius);
    if (spn_shape_outer_radius_) spn_shape_outer_radius_->setValue(layer_->shape_outer_radius);
    if (spn_shape_roundness_) {
        const bool rectangle_roundness = current_shape == ShapeType::Rectangle ||
                                         current_shape == ShapeType::RoundedRectangle;
        spn_shape_roundness_->setValue(rectangle_roundness ? layer_->corner_radius : layer_->shape_roundness);
    }
    edit_image_path_->setText(QString::fromStdString(layer_->image_path));
    set_image_preview_label(lbl_image_preview_, QString::fromStdString(layer_->image_path));
    if (cmb_image_scale_filter_) {
        QSignalBlocker block(cmb_image_scale_filter_);
        int filter_index = cmb_image_scale_filter_->findData((int)layer_->scale_filter);
        cmb_image_scale_filter_->setCurrentIndex(filter_index >= 0 ? filter_index : 1);
    }
    style_color_button(btn_text_color_, eval_text_color(*layer_, lt));
    style_color_button(btn_fill_color_, eval_fill_color(*layer_, lt));
    if (cmb_fill_type_) {
        int fill_idx = cmb_fill_type_->findData(layer_->fill_type);
        cmb_fill_type_->setCurrentIndex(fill_idx >= 0 ? fill_idx : 0);
    }
    if (cmb_gradient_type_) {
        int gradient_idx = cmb_gradient_type_->findData(layer_->gradient_type);
        cmb_gradient_type_->setCurrentIndex(gradient_idx >= 0 ? gradient_idx : 0);
    }
    if (btn_gradient_start_color_) style_color_button(btn_gradient_start_color_, layer_->gradient_start_color);
    if (btn_gradient_end_color_) style_color_button(btn_gradient_end_color_, layer_->gradient_end_color);
    if (spn_gradient_start_pos_) spn_gradient_start_pos_->setValue(layer_->gradient_start_pos);
    if (spn_gradient_end_pos_) spn_gradient_end_pos_->setValue(layer_->gradient_end_pos);
    if (spn_gradient_start_opacity_) spn_gradient_start_opacity_->setValue(layer_->gradient_start_opacity);
    if (spn_gradient_end_opacity_) spn_gradient_end_opacity_->setValue(layer_->gradient_end_opacity);
    if (spn_gradient_opacity_) spn_gradient_opacity_->setValue(layer_->gradient_opacity);
    if (spn_gradient_angle_) spn_gradient_angle_->setValue(layer_->gradient_angle);
    if (spn_gradient_center_x_) spn_gradient_center_x_->setValue(layer_->gradient_center_x);
    if (spn_gradient_center_y_) spn_gradient_center_y_->setValue(layer_->gradient_center_y);
    if (spn_gradient_scale_) spn_gradient_scale_->setValue(layer_->gradient_scale);
    if (spn_gradient_focal_x_) spn_gradient_focal_x_->setValue(layer_->gradient_focal_x);
    if (spn_gradient_focal_y_) spn_gradient_focal_y_->setValue(layer_->gradient_focal_y);
    if (chk_text_box_width_to_text_) chk_text_box_width_to_text_->setChecked(layer_->text_box_width_to_text);
    if (chk_text_box_height_to_text_) chk_text_box_height_to_text_->setChecked(layer_->text_box_height_to_text);
    if (spn_max_text_box_width_) { spn_max_text_box_width_->setValue(layer_->max_text_box_width); spn_max_text_box_width_->setEnabled(layer_->text_box_width_to_text); }
    if (spn_max_text_box_height_) { spn_max_text_box_height_->setValue(layer_->max_text_box_height); spn_max_text_box_height_->setEnabled(layer_->text_box_height_to_text); }
    if (cmb_background_gradient_type_) {
        int background_gradient_idx = cmb_background_gradient_type_->findData(layer_->background_gradient_type);
        cmb_background_gradient_type_->setCurrentIndex(background_gradient_idx >= 0 ? background_gradient_idx : 0);
    }
    if (btn_background_gradient_start_color_) style_color_button(btn_background_gradient_start_color_, layer_->background_gradient_start_color);
    if (btn_background_gradient_end_color_) style_color_button(btn_background_gradient_end_color_, layer_->background_gradient_end_color);
    if (spn_background_gradient_start_pos_) spn_background_gradient_start_pos_->setValue(layer_->background_gradient_start_pos);
    if (spn_background_gradient_end_pos_) spn_background_gradient_end_pos_->setValue(layer_->background_gradient_end_pos);
    if (spn_background_gradient_start_opacity_) spn_background_gradient_start_opacity_->setValue(layer_->background_gradient_start_opacity);
    if (spn_background_gradient_end_opacity_) spn_background_gradient_end_opacity_->setValue(layer_->background_gradient_end_opacity);
    if (spn_background_gradient_opacity_) spn_background_gradient_opacity_->setValue(layer_->background_gradient_opacity);
    if (spn_background_gradient_angle_) spn_background_gradient_angle_->setValue(layer_->background_gradient_angle);
    if (spn_background_gradient_center_x_) spn_background_gradient_center_x_->setValue(layer_->background_gradient_center_x);
    if (spn_background_gradient_center_y_) spn_background_gradient_center_y_->setValue(layer_->background_gradient_center_y);
    if (spn_background_gradient_scale_) spn_background_gradient_scale_->setValue(layer_->background_gradient_scale);
    if (spn_background_gradient_focal_x_) spn_background_gradient_focal_x_->setValue(layer_->background_gradient_focal_x);
    if (spn_background_gradient_focal_y_) spn_background_gradient_focal_y_->setValue(layer_->background_gradient_focal_y);
    if (chk_outline_enabled_) chk_outline_enabled_->setChecked(layer_->outline_enabled);
    if (cmb_stroke_fill_type_) {
        int stroke_fill_idx = cmb_stroke_fill_type_->findData(layer_->stroke_fill_type);
        cmb_stroke_fill_type_->setCurrentIndex(stroke_fill_idx >= 0 ? stroke_fill_idx : 1);
    }
    if (spn_outline_width_) spn_outline_width_->setValue(layer_->stroke_width);
    if (btn_outline_color_) style_color_button(btn_outline_color_, eval_outline_color(*layer_, lt));
    if (spn_outline_opacity_) spn_outline_opacity_->setValue(eval_outline_opacity(*layer_, lt));
    if (cmb_outline_join_) {
        int join_idx = cmb_outline_join_->findData(layer_->outline_join_style);
        cmb_outline_join_->setCurrentIndex(join_idx >= 0 ? join_idx : 1);
    }
    if (cmb_outline_position_) {
        int position_idx = cmb_outline_position_->findData(layer_->outline_on_front ? 1 : 0);
        cmb_outline_position_->setCurrentIndex(position_idx >= 0 ? position_idx : 1);
    }
    if (chk_outline_antialias_) chk_outline_antialias_->setChecked(layer_->outline_antialias);
    if (cmb_stroke_gradient_type_) {
        int stroke_gradient_idx = cmb_stroke_gradient_type_->findData(layer_->stroke_gradient_type);
        cmb_stroke_gradient_type_->setCurrentIndex(stroke_gradient_idx >= 0 ? stroke_gradient_idx : 0);
    }
    if (btn_stroke_gradient_start_color_) style_color_button(btn_stroke_gradient_start_color_, layer_->stroke_gradient_start_color);
    if (btn_stroke_gradient_end_color_) style_color_button(btn_stroke_gradient_end_color_, layer_->stroke_gradient_end_color);
    if (spn_stroke_gradient_start_pos_) spn_stroke_gradient_start_pos_->setValue(layer_->stroke_gradient_start_pos);
    if (spn_stroke_gradient_end_pos_) spn_stroke_gradient_end_pos_->setValue(layer_->stroke_gradient_end_pos);
    if (spn_stroke_gradient_start_opacity_) spn_stroke_gradient_start_opacity_->setValue(layer_->stroke_gradient_start_opacity);
    if (spn_stroke_gradient_end_opacity_) spn_stroke_gradient_end_opacity_->setValue(layer_->stroke_gradient_end_opacity);
    if (spn_stroke_gradient_opacity_) spn_stroke_gradient_opacity_->setValue(layer_->stroke_gradient_opacity);
    if (spn_stroke_gradient_angle_) spn_stroke_gradient_angle_->setValue(layer_->stroke_gradient_angle);
    if (spn_stroke_gradient_center_x_) spn_stroke_gradient_center_x_->setValue(layer_->stroke_gradient_center_x);
    if (spn_stroke_gradient_center_y_) spn_stroke_gradient_center_y_->setValue(layer_->stroke_gradient_center_y);
    if (spn_stroke_gradient_scale_) spn_stroke_gradient_scale_->setValue(layer_->stroke_gradient_scale);
    if (spn_stroke_gradient_focal_x_) spn_stroke_gradient_focal_x_->setValue(layer_->stroke_gradient_focal_x);
    if (spn_stroke_gradient_focal_y_) spn_stroke_gradient_focal_y_->setValue(layer_->stroke_gradient_focal_y);

    auto set_kf_icon = [](QPushButton *button, bool active, bool has_keyframes) {
        if (!button) return;
        const bool outlined = has_keyframes && !active;
        button->setIcon(keyframe_diamond_icon(active, outlined));
        button->setProperty("active", active);
        button->setProperty("outlined", outlined);
        button->style()->unpolish(button);
        button->style()->polish(button);
    };
    auto set_prop_kf_icon = [&](QPushButton *button, const AnimatedProperty &prop) {
        set_kf_icon(button, keyframe_at_time(prop, lt), prop.is_animated());
    };
    auto set_group_kf_icon = [&](QPushButton *button, std::initializer_list<const AnimatedProperty *> props) {
        set_kf_icon(button, any_keyframe_at_time(props, lt), any_keyframes(props));
    };
    set_prop_kf_icon(btn_kf_pos_x_, layer_->pos_x);
    set_prop_kf_icon(btn_kf_pos_y_, layer_->pos_y);
    set_prop_kf_icon(btn_kf_scale_x_, layer_->scale_x);
    set_prop_kf_icon(btn_kf_scale_y_, layer_->scale_y);
    set_prop_kf_icon(btn_kf_rotation_, layer_->rotation);
    set_prop_kf_icon(btn_kf_opacity_, layer_->opacity);
    set_prop_kf_icon(btn_kf_origin_x_, layer_->origin_x_prop);
    set_prop_kf_icon(btn_kf_origin_y_, layer_->origin_y_prop);
    set_prop_kf_icon(btn_kf_paragraph_indent_left_, layer_->paragraph_indent_left_prop);
    set_prop_kf_icon(btn_kf_paragraph_indent_right_, layer_->paragraph_indent_right_prop);
    set_prop_kf_icon(btn_kf_paragraph_indent_first_line_, layer_->paragraph_indent_first_line_prop);
    set_prop_kf_icon(btn_kf_width_, layer_->box_width);
    set_group_kf_icon(btn_kf_text_color_, {&layer_->text_color_a, &layer_->text_color_r,
                                           &layer_->text_color_g, &layer_->text_color_b});
    set_group_kf_icon(btn_kf_fill_color_, {&layer_->fill_color_a, &layer_->fill_color_r,
                                           &layer_->fill_color_g, &layer_->fill_color_b});
    if (is_text_like)
        set_group_kf_icon(btn_kf_appearance_fill_, {&layer_->text_color_a, &layer_->text_color_r,
                                                    &layer_->text_color_g, &layer_->text_color_b});
    else
        set_group_kf_icon(btn_kf_appearance_fill_, {&layer_->fill_color_a, &layer_->fill_color_r,
                                                    &layer_->fill_color_g, &layer_->fill_color_b});
    set_kf_icon(btn_kf_appearance_stroke_, false, false);
    set_prop_kf_icon(btn_kf_shadow_enabled_, layer_->shadow_enabled_prop);
    set_prop_kf_icon(btn_kf_shadow_opacity_, layer_->shadow_opacity_prop);
    set_prop_kf_icon(btn_kf_shadow_distance_, layer_->shadow_distance_prop);
    set_prop_kf_icon(btn_kf_shadow_angle_, layer_->shadow_angle_prop);
    set_prop_kf_icon(btn_kf_shadow_blur_, layer_->shadow_blur_prop);
    set_prop_kf_icon(btn_kf_shadow_spread_, layer_->shadow_spread_prop);
    set_group_kf_icon(btn_kf_shadow_color_, {&layer_->shadow_color_a, &layer_->shadow_color_r,
                                             &layer_->shadow_color_g, &layer_->shadow_color_b});

    const QString panel_text = is_clock
        ? QString::fromStdString(layer_->clock_format)
        : (!layer_->rich_text_html.empty() ? rich_text_plain_text(layer_->rich_text_html)
                                           : QString::fromStdString(layer_->text_content));
    txt_content_->setPlainText(panel_text);
    int ticker_style_idx = cmb_ticker_style_->findData(layer_->ticker_style);
    cmb_ticker_style_->setCurrentIndex(ticker_style_idx >= 0 ? ticker_style_idx : 0);
    spn_ticker_speed_->setValue(layer_->ticker_speed);
    spn_ticker_line_hold_->setValue(layer_->ticker_line_hold);
    cmb_ticker_direction_->clear();
    if (layer_->ticker_style == 0) {
        cmb_ticker_direction_->addItem(obsgs_tr("OBSTitles.LeftToRight"), 0);
        cmb_ticker_direction_->addItem(obsgs_tr("OBSTitles.RightToLeft"), 1);
    } else {
        cmb_ticker_direction_->addItem(obsgs_tr("OBSTitles.TopToBottom"), 0);
        cmb_ticker_direction_->addItem(obsgs_tr("OBSTitles.BottomToTop"), 1);
    }
    int ticker_direction_idx = cmb_ticker_direction_->findData(layer_->ticker_direction);
    cmb_ticker_direction_->setCurrentIndex(ticker_direction_idx >= 0 ? ticker_direction_idx : 0);
    int fi = cmb_font_->findText(QString::fromStdString(layer_->font_family));
    if (fi >= 0) cmb_font_->setCurrentIndex(fi);
    populate_font_style_combo(cmb_font_style_, QString::fromStdString(layer_->font_family), QString::fromStdString(layer_->font_style));
    spn_size_->setValue(layer_->font_size);
    chk_bold_->setChecked(layer_->font_bold);
    chk_italic_->setChecked(layer_->font_italic);
    if (chk_font_kerning_) chk_font_kerning_->setChecked(layer_->font_kerning);
    if (cmb_kerning_mode_) {
        int ki = cmb_kerning_mode_->findData(layer_->kerning_mode);
        cmb_kerning_mode_->setCurrentIndex(ki >= 0 ? ki : 0);
    }
    if (spn_kerning_value_) {
        spn_kerning_value_->setValue(layer_->manual_kerning);
        spn_kerning_value_->setEnabled(layer_->kerning_mode == 2);
    }
    if (spn_text_leading_) spn_text_leading_->setValue(layer_->text_leading);
    if (spn_char_tracking_) spn_char_tracking_->setValue(layer_->char_tracking);
    if (spn_char_scale_x_) spn_char_scale_x_->setValue(layer_->char_scale_x * 100.0);
    if (spn_char_scale_y_) spn_char_scale_y_->setValue(layer_->char_scale_y * 100.0);
    if (spn_baseline_shift_) spn_baseline_shift_->setValue(layer_->baseline_shift);
    if (cmb_language_) {
        int li = cmb_language_->findText(QString::fromStdString(layer_->text_language));
        cmb_language_->setCurrentIndex(li >= 0 ? li : 0);
    }
    if (btn_all_caps_) btn_all_caps_->setChecked(layer_->text_style == 1);
    if (btn_small_caps_) btn_small_caps_->setChecked(layer_->text_style == 2);
    if (btn_superscript_) btn_superscript_->setChecked(layer_->text_style == 3);
    if (btn_subscript_) btn_subscript_->setChecked(layer_->text_style == 4);
    if (btn_underline_) btn_underline_->setChecked(layer_->text_underline);
    const bool use_rich_char_summary = (layer_->type == LayerType::Text || layer_->type == LayerType::Ticker) && !is_clock;
    if (use_rich_char_summary) {
        const bool active = active_text_edit_layer_id_ == layer_->id;
        RichTextCharFormatSummary summary = summarize_rich_text_char_format(*layer_, active);
        const RichTextCharFormat &fmt = summary.format;
        int rich_fi = cmb_font_->findText(QString::fromStdString(fmt.font_family));
        if (rich_fi >= 0) cmb_font_->setCurrentIndex(rich_fi);
        populate_font_style_combo(cmb_font_style_, QString::fromStdString(fmt.font_family), QString::fromStdString(fmt.font_style));
        int rich_style_i = cmb_font_style_->findText(QString::fromStdString(fmt.font_style));
        if (rich_style_i >= 0) cmb_font_style_->setCurrentIndex(rich_style_i);
        spn_size_->setValue(fmt.font_size);
        chk_bold_->setChecked(fmt.bold);
        chk_italic_->setChecked(fmt.italic);
        if (chk_font_kerning_) chk_font_kerning_->setChecked(fmt.kerning);
        if (cmb_kerning_mode_) {
            int rich_kerning_i = cmb_kerning_mode_->findData(fmt.kerning_mode);
            cmb_kerning_mode_->setCurrentIndex(rich_kerning_i >= 0 ? rich_kerning_i : 0);
        }
        if (spn_kerning_value_) {
            spn_kerning_value_->setValue(fmt.manual_kerning);
            spn_kerning_value_->setEnabled(fmt.kerning_mode == 2);
        }
        if (spn_char_tracking_) spn_char_tracking_->setValue(fmt.tracking);
        if (spn_char_scale_x_) spn_char_scale_x_->setValue(fmt.scale_x * 100.0);
        if (spn_char_scale_y_) spn_char_scale_y_->setValue(fmt.scale_y * 100.0);
        if (spn_baseline_shift_) spn_baseline_shift_->setValue(fmt.baseline_shift);
        if (btn_underline_) btn_underline_->setChecked(fmt.underline);
        if (btn_strikethrough_) btn_strikethrough_->setChecked(fmt.strikethrough);
        if (cmb_language_) {
            int rich_language_i = cmb_language_->findText(QString::fromStdString(fmt.language));
            cmb_language_->setCurrentIndex(rich_language_i >= 0 ? rich_language_i : 0);
        }
        if (btn_all_caps_) btn_all_caps_->setChecked(fmt.text_style == 1);
        if (btn_small_caps_) btn_small_caps_->setChecked(fmt.text_style == 2);
        if (btn_superscript_) btn_superscript_->setChecked(fmt.text_style == 3);
        if (btn_subscript_) btn_subscript_->setChecked(fmt.text_style == 4);
        if (btn_ligatures_) btn_ligatures_->setChecked(fmt.ligatures);
        if (btn_stylistic_alternates_) btn_stylistic_alternates_->setChecked(fmt.stylistic_alternates);
        if (btn_fractions_) btn_fractions_->setChecked(fmt.fractions);
        if (btn_opentype_features_) btn_opentype_features_->setChecked(fmt.opentype_features);
        int rich_text_style_i = cmb_text_style_->findData(fmt.text_style);
        cmb_text_style_->setCurrentIndex(rich_text_style_i >= 0 ? rich_text_style_i : 0);
        if (summary.mixed & RichTextCharFillColor) {
            style_color_button_mixed(btn_text_color_);
            style_color_button_mixed(btn_gradient_start_color_);
            style_color_button_mixed(btn_gradient_end_color_);
        } else {
            style_color_button(btn_text_color_, fmt.fill.color);
            if (cmb_fill_type_) {
                int rich_fill_i = cmb_fill_type_->findData(fmt.fill.type);
                cmb_fill_type_->setCurrentIndex(rich_fill_i >= 0 ? rich_fill_i : 0);
            }
            if (cmb_gradient_type_) {
                int rich_gradient_i = cmb_gradient_type_->findData(fmt.fill.gradient_type);
                cmb_gradient_type_->setCurrentIndex(rich_gradient_i >= 0 ? rich_gradient_i : 0);
            }
            if (btn_gradient_start_color_) style_color_button(btn_gradient_start_color_, fmt.fill.gradient_start_color);
            if (btn_gradient_end_color_) style_color_button(btn_gradient_end_color_, fmt.fill.gradient_end_color);
            if (spn_gradient_start_pos_) spn_gradient_start_pos_->setValue(fmt.fill.gradient_start_pos);
            if (spn_gradient_end_pos_) spn_gradient_end_pos_->setValue(fmt.fill.gradient_end_pos);
            if (spn_gradient_start_opacity_) spn_gradient_start_opacity_->setValue(fmt.fill.gradient_start_opacity);
            if (spn_gradient_end_opacity_) spn_gradient_end_opacity_->setValue(fmt.fill.gradient_end_opacity);
            if (spn_gradient_opacity_) spn_gradient_opacity_->setValue(fmt.fill.gradient_opacity);
            if (spn_gradient_angle_) spn_gradient_angle_->setValue(fmt.fill.gradient_angle);
            if (spn_gradient_center_x_) spn_gradient_center_x_->setValue(fmt.fill.gradient_center_x);
            if (spn_gradient_center_y_) spn_gradient_center_y_->setValue(fmt.fill.gradient_center_y);
            if (spn_gradient_scale_) spn_gradient_scale_->setValue(fmt.fill.gradient_scale);
            if (spn_gradient_focal_x_) spn_gradient_focal_x_->setValue(fmt.fill.gradient_focal_x);
            if (spn_gradient_focal_y_) spn_gradient_focal_y_->setValue(fmt.fill.gradient_focal_y);
        }

        set_combo_mixed(cmb_font_, summary.mixed & RichTextCharFontFamily);
        set_combo_mixed(cmb_font_style_, summary.mixed & (RichTextCharFontFamily | RichTextCharFontStyle | RichTextCharBold | RichTextCharItalic));
        set_spin_mixed(spn_size_, summary.mixed & RichTextCharFontSize);
        set_button_mixed(chk_bold_, summary.mixed & RichTextCharBold);
        set_button_mixed(chk_italic_, summary.mixed & RichTextCharItalic);
        set_button_mixed(chk_font_kerning_, summary.mixed & RichTextCharKerning);
        set_combo_mixed(cmb_kerning_mode_, summary.mixed & RichTextCharKerning);
        set_spin_mixed(spn_kerning_value_, summary.mixed & RichTextCharKerning);
        set_spin_mixed(spn_char_tracking_, summary.mixed & RichTextCharTracking);
        set_spin_mixed(spn_char_scale_x_, summary.mixed & RichTextCharScaleX);
        set_spin_mixed(spn_char_scale_y_, summary.mixed & RichTextCharScaleY);
        set_spin_mixed(spn_baseline_shift_, summary.mixed & RichTextCharBaselineShift);
        set_button_mixed(btn_underline_, summary.mixed & RichTextCharUnderline);
        set_button_mixed(btn_strikethrough_, summary.mixed & RichTextCharStrikethrough);
        set_combo_mixed(cmb_language_, summary.mixed & RichTextCharLanguage);
        set_button_mixed(btn_all_caps_, summary.mixed & RichTextCharTextStyle);
        set_button_mixed(btn_small_caps_, summary.mixed & RichTextCharTextStyle);
        set_button_mixed(btn_superscript_, summary.mixed & RichTextCharTextStyle);
        set_button_mixed(btn_subscript_, summary.mixed & RichTextCharTextStyle);
        set_button_mixed(btn_ligatures_, summary.mixed & RichTextCharLigatures);
        set_button_mixed(btn_stylistic_alternates_, summary.mixed & RichTextCharStylisticAlternates);
        set_button_mixed(btn_fractions_, summary.mixed & RichTextCharFractions);
        set_button_mixed(btn_opentype_features_, summary.mixed & RichTextCharOpenTypeFeatures);
        set_combo_mixed(cmb_text_style_, summary.mixed & RichTextCharTextStyle);
        set_combo_mixed(cmb_fill_type_, summary.mixed & RichTextCharFillColor);
        set_combo_mixed(cmb_gradient_type_, summary.mixed & RichTextCharFillColor);
        set_spin_mixed(spn_gradient_start_pos_, summary.mixed & RichTextCharFillColor);
        set_spin_mixed(spn_gradient_end_pos_, summary.mixed & RichTextCharFillColor);
        set_spin_mixed(spn_gradient_start_opacity_, summary.mixed & RichTextCharFillColor);
        set_spin_mixed(spn_gradient_end_opacity_, summary.mixed & RichTextCharFillColor);
        set_spin_mixed(spn_gradient_opacity_, summary.mixed & RichTextCharFillColor);
        set_spin_mixed(spn_gradient_angle_, summary.mixed & RichTextCharFillColor);
        set_spin_mixed(spn_gradient_center_x_, summary.mixed & RichTextCharFillColor);
        set_spin_mixed(spn_gradient_center_y_, summary.mixed & RichTextCharFillColor);
        set_spin_mixed(spn_gradient_scale_, summary.mixed & RichTextCharFillColor);
        set_spin_mixed(spn_gradient_focal_x_, summary.mixed & RichTextCharFillColor);
        set_spin_mixed(spn_gradient_focal_y_, summary.mixed & RichTextCharFillColor);
    }
    if (!use_rich_char_summary) {
        if (btn_ligatures_) btn_ligatures_->setChecked(layer_->text_ligatures);
        if (btn_stylistic_alternates_) btn_stylistic_alternates_->setChecked(layer_->text_stylistic_alternates);
        if (btn_fractions_) btn_fractions_->setChecked(layer_->text_fractions);
        if (btn_opentype_features_) btn_opentype_features_->setChecked(layer_->text_opentype_features);
        int style_idx = cmb_text_style_->findData(layer_->text_style);
        cmb_text_style_->setCurrentIndex(style_idx >= 0 ? style_idx : 0);
    }
    int overflow_idx = cmb_text_overflow_->findData(layer_->text_overflow_mode);
    cmb_text_overflow_->setCurrentIndex(overflow_idx >= 0 ? overflow_idx : 0);
    spn_text_fit_min_scale_->setValue(layer_->text_fit_min_scale);
    bool is_fit = layer_->text_overflow_mode == 2 && !is_ticker;
    spn_text_fit_min_scale_->setVisible(is_fit);
    lbl_text_fit_scale_->setVisible(is_fit);
    if (auto *form = qobject_cast<QFormLayout *>(dynamic_text_box_ ? dynamic_text_box_->layout() : nullptr)) {
        if (auto *label = form->labelForField(spn_text_fit_min_scale_))
            label->setVisible(is_fit);
    }
    if (lbl_text_fit_scale_) {
        QFont preview_font = font_for_layer(*layer_);
        QRectF preview_rect(0, 0, eval_box_width(*layer_, lt), eval_box_height(*layer_, lt));
        double scale = horizontal_fit_scale(preview_font, preview_rect, display_text_for_style(*layer_), *layer_, lt);
        lbl_text_fit_scale_->setText(obsgs_tr("OBSTitles.ScalePercentFormat").arg((int)std::round(scale * 100.0)));
    }
    chk_expose_text_->setChecked(layer_->expose_text);
    if (chk_scene_mask_) chk_scene_mask_->setChecked(layer_->use_as_scene_mask);
    if (grp_text_align_) {
        QSignalBlocker block(grp_text_align_);
        if (auto *button = grp_text_align_->button(layer_->align_h))
            button->setChecked(true);
        else if (auto *fallback = grp_text_align_->button(1))
            fallback->setChecked(true);
    }
    if (grp_text_valign_) {
        QSignalBlocker block(grp_text_valign_);
        if (auto *button = grp_text_valign_->button(layer_->align_v))
            button->setChecked(true);
        else if (auto *fallback = grp_text_valign_->button(1))
            fallback->setChecked(true);
    }
    if (spn_paragraph_indent_left_) spn_paragraph_indent_left_->setValue(eval_paragraph_indent_left(*layer_, lt));
    if (spn_paragraph_indent_right_) spn_paragraph_indent_right_->setValue(eval_paragraph_indent_right(*layer_, lt));
    if (spn_paragraph_indent_first_line_) spn_paragraph_indent_first_line_->setValue(eval_paragraph_indent_first_line(*layer_, lt));
    if (spn_paragraph_space_before_) spn_paragraph_space_before_->setValue(layer_->paragraph_space_before);
    if (spn_paragraph_space_after_) spn_paragraph_space_after_->setValue(layer_->paragraph_space_after);
    if (chk_paragraph_hyphenate_) chk_paragraph_hyphenate_->setChecked(layer_->paragraph_hyphenate);

    chk_shadow_enabled_->setChecked(eval_shadow_enabled(*layer_, lt));
    cmb_shadow_preset_->setCurrentIndex(0);
    if (cmb_shadow_blur_type_) {
        int bi = cmb_shadow_blur_type_->findData((int)layer_->shadow_blur_type);
        cmb_shadow_blur_type_->setCurrentIndex(bi >= 0 ? bi : 2);
    }
    style_color_button(btn_shadow_color_, eval_shadow_color(*layer_, lt));
    spn_shadow_opacity_->setValue(eval_shadow_opacity(*layer_, lt));
    spn_shadow_distance_->setValue(eval_shadow_distance(*layer_, lt));
    spn_shadow_angle_->setValue(eval_shadow_angle(*layer_, lt));
    spn_shadow_blur_->setValue(eval_shadow_blur(*layer_, lt));
    spn_shadow_spread_->setValue(eval_shadow_spread(*layer_, lt));
    if (chk_long_shadow_enabled_) chk_long_shadow_enabled_->setChecked(layer_->long_shadow_enabled);
    if (btn_long_shadow_color_) style_color_button(btn_long_shadow_color_, layer_->long_shadow_color);
    if (spn_long_shadow_opacity_) spn_long_shadow_opacity_->setValue(layer_->long_shadow_opacity);
    if (spn_long_shadow_length_) spn_long_shadow_length_->setValue(layer_->long_shadow_length);
    if (spn_long_shadow_angle_) spn_long_shadow_angle_->setValue(layer_->long_shadow_angle);
    if (spn_long_shadow_falloff_) spn_long_shadow_falloff_->setValue(layer_->long_shadow_falloff);
    if (cmb_long_shadow_blur_type_) {
        int lbi = cmb_long_shadow_blur_type_->findData((int)layer_->long_shadow_blur_type);
        cmb_long_shadow_blur_type_->setCurrentIndex(lbi >= 0 ? lbi : 0);
    }
    if (spn_long_shadow_blur_) spn_long_shadow_blur_->setValue(layer_->long_shadow_blur);

    QFontDatabase fdb;
    cmb_font_->setToolTip(fdb.families().contains(QString::fromStdString(layer_->font_family))
        ? QString()
        : obsgs_tr("OBSTitles.FontMissingWarningFormat").arg(QString::fromStdString(layer_->font_family)));

    loading_values_ = false;
}
