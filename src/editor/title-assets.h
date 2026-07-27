#pragma once

#include <obs-module.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QHash>
#include <QIcon>
#include <QIODevice>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QString>
#include <QSvgRenderer>
#include <QWidget>

static inline QColor bgl_icon_color()
{
    const QPalette palette = qApp ? qApp->palette() : QPalette();
    const QColor button_text = palette.color(QPalette::Active, QPalette::ButtonText);
    return button_text.isValid() ? button_text : QColor(0x20, 0x20, 0x20);
}

static inline double bgl_linear_color_channel(double channel)
{
    channel = std::clamp(channel, 0.0, 1.0);
    return channel <= 0.04045
        ? channel / 12.92
        : std::pow((channel + 0.055) / 1.055, 2.4);
}

static inline double bgl_relative_luminance(const QColor &color)
{
    return 0.2126 * bgl_linear_color_channel(color.redF()) +
           0.7152 * bgl_linear_color_channel(color.greenF()) +
           0.0722 * bgl_linear_color_channel(color.blueF());
}

static inline double bgl_contrast_ratio(const QColor &a, const QColor &b)
{
    const double la = bgl_relative_luminance(a);
    const double lb = bgl_relative_luminance(b);
    const double lighter = std::max(la, lb);
    const double darker = std::min(la, lb);
    return (lighter + 0.05) / (darker + 0.05);
}

static inline QColor bgl_composite_over(const QColor &foreground,
                                        const QColor &background)
{
    const double alpha = std::clamp<double>(static_cast<double>(foreground.alphaF()), 0.0, 1.0);
    const double inverse = 1.0 - alpha;
    return QColor::fromRgbF(
        foreground.redF() * alpha + background.redF() * inverse,
        foreground.greenF() * alpha + background.greenF() * inverse,
        foreground.blueF() * alpha + background.blueF() * inverse,
        1.0);
}

static inline QColor bgl_mix_colors(const QColor &foreground,
                                    const QColor &background,
                                    double foreground_weight)
{
    foreground_weight = std::clamp(foreground_weight, 0.0, 1.0);
    const double background_weight = 1.0 - foreground_weight;
    return QColor::fromRgbF(
        foreground.redF() * foreground_weight +
            background.redF() * background_weight,
        foreground.greenF() * foreground_weight +
            background.greenF() * background_weight,
        foreground.blueF() * foreground_weight +
            background.blueF() * background_weight,
        1.0);
}

/* Resolve the darkest and lightest semantic foregrounds supplied by the
 * current OBS palette.  Keeping both candidates lets the layer UI use the
 * host theme while applying a deliberately conservative brightness switch. */
static inline std::array<QColor, 2> bgl_semantic_foreground_extremes(
    const QPalette &palette)
{
    const std::array<QColor, 6> candidates = {
        palette.color(QPalette::Active, QPalette::WindowText),
        palette.color(QPalette::Active, QPalette::Text),
        palette.color(QPalette::Active, QPalette::ButtonText),
        palette.color(QPalette::Active, QPalette::BrightText),
        palette.color(QPalette::Active, QPalette::HighlightedText),
        palette.color(QPalette::Active, QPalette::Shadow),
    };

    QColor darkest;
    QColor lightest;
    double darkest_luminance = 2.0;
    double lightest_luminance = -1.0;
    for (const QColor &candidate : candidates) {
        if (!candidate.isValid())
            continue;
        const double luminance = bgl_relative_luminance(candidate);
        if (luminance < darkest_luminance) {
            darkest = candidate;
            darkest_luminance = luminance;
        }
        if (luminance > lightest_luminance) {
            lightest = candidate;
            lightest_luminance = luminance;
        }
    }
    if (!darkest.isValid())
        darkest = QColor(24, 24, 24);
    if (!lightest.isValid())
        lightest = QColor(245, 245, 245);
    darkest.setAlpha(255);
    lightest.setAlpha(255);
    return {darkest, lightest};
}

/* The former maximum-contrast comparison switched to dark text on many
 * medium-saturation colors.  Use dark UI content only on genuinely bright
 * surfaces; everything below this relative-luminance threshold stays light. */
static inline bool bgl_background_prefers_dark_foreground(
    const QColor &background)
{
    constexpr double kDarkForegroundLuminanceThreshold = 0.42;
    return bgl_relative_luminance(background) >=
           kDarkForegroundLuminanceThreshold;
}

static inline QColor bgl_background_aware_foreground(
    const QColor &background, const QPalette &palette)
{
    const auto extremes = bgl_semantic_foreground_extremes(palette);
    return bgl_background_prefers_dark_foreground(background)
        ? extremes[0]
        : extremes[1];
}

/* Waveforms intentionally use the opposite semantic polarity from the
 * background-aware strip label/icon so they remain visually distinct. */
static inline QColor bgl_background_aware_opposite_foreground(
    const QColor &background, const QPalette &palette)
{
    const auto extremes = bgl_semantic_foreground_extremes(palette);
    return bgl_background_prefers_dark_foreground(background)
        ? extremes[1]
        : extremes[0];
}

static inline QColor bgl_background_aware_opposite_foreground(
    const QColor &background)
{
    return bgl_background_aware_opposite_foreground(
        background, qApp ? qApp->palette() : QPalette());
}

static inline QColor bgl_background_aware_foreground(
    const QColor &background)
{
    return bgl_background_aware_foreground(
        background, qApp ? qApp->palette() : QPalette());
}

static inline QColor bgl_background_aware_muted_foreground(
    const QColor &background, const QPalette &palette)
{
    return bgl_mix_colors(
        bgl_background_aware_foreground(background, palette), background, 0.62);
}

static inline QIcon bgl_icon(const char *file_name, const QColor &color)
{
    QString rel = QStringLiteral("icons/") + QString::fromUtf8(file_name);
    char *path = obs_module_file(rel.toUtf8().constData());
    if (!path)
        return QIcon();

    const QString icon_path = QString::fromUtf8(path);
    bfree(path);

    QFile file(icon_path);
    if (!file.open(QIODevice::ReadOnly))
        return QIcon(icon_path);

    QByteArray svg = file.readAll();
    svg.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QSvgRenderer renderer(svg);
    if (!renderer.isValid())
        return QIcon(icon_path);

    QIcon icon;
    const int sizes[] = {16, 20, 24, 32};
    for (int size : sizes) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), color);
        icon.addPixmap(pixmap);
    }
    return icon;
}

static inline QIcon bgl_icon(const char *file_name)
{
    return bgl_icon(file_name, bgl_icon_color());
}

static inline QIcon bgl_brand_icon()
{
    char *path = obs_module_file("icons/broadcast-graphics-live-app-icon.png");
    if (path) {
        const QString icon_path = QString::fromUtf8(path);
        bfree(path);

        QPixmap source(icon_path);
        if (!source.isNull()) {
            QIcon icon;
            const int sizes[] = {16, 20, 24, 32, 48, 64, 96, 128, 256};
            for (int size : sizes)
                icon.addPixmap(source.scaled(size, size, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation));
            return icon;
        }

        return QIcon(icon_path);
    }

    path = obs_module_file("icons/broadcast-graphics-live-app-icon.svg");
    if (!path)
        return QIcon();

    const QString icon_path = QString::fromUtf8(path);
    bfree(path);

    QFile file(icon_path);
    if (!file.open(QIODevice::ReadOnly))
        return QIcon(icon_path);

    const QByteArray svg = file.readAll();
    QSvgRenderer renderer(svg);
    if (!renderer.isValid())
        return QIcon(icon_path);

    QIcon icon;
    const int sizes[] = {16, 20, 24, 32, 48, 64, 96, 128, 256};
    for (int size : sizes) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        renderer.render(&painter, pixmap.rect());
        icon.addPixmap(pixmap);
    }
    return icon;
}

static inline void bgl_apply_brand_icon(QWidget *window)
{
    if (window)
        window->setWindowIcon(bgl_brand_icon());
}

