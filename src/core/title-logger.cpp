#include "title-logger.h"

#include "title-preferences.h"

#include <obs-module.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

namespace {

QMutex g_log_mutex;
QString g_session_file_path;
QString g_session_stamp;
bool g_session_started = false;

const QVector<TitleLogCategory> &all_categories()
{
    static const QVector<TitleLogCategory> categories = {
        {QStringLiteral("General"), QStringLiteral("General messages"),
         QStringLiteral("Unclassified application messages."), QStringLiteral("Core and application"), true},
        {QStringLiteral("Plugin"), QStringLiteral("Plugin lifecycle"),
         QStringLiteral("Module load, unload and OBS frontend events."), QStringLiteral("Core and application"), true},
        {QStringLiteral("Preferences"), QStringLiteral("Preferences"),
         QStringLiteral("Application preference changes and configuration."), QStringLiteral("Core and application"), true},
        {QStringLiteral("TitleStore"), QStringLiteral("Titles and persistence"),
         QStringLiteral("Title loading, saving, revision and project storage."), QStringLiteral("Core and application"), true},
        {QStringLiteral("ImportExport"), QStringLiteral("Import and export"),
         QStringLiteral("Title/template import, append and export operations."), QStringLiteral("Core and application"), true},

        {QStringLiteral("Source"), QStringLiteral("Source lifecycle and visibility"),
         QStringLiteral("Source creation, update, activation, visibility, title binding and presentation resets."), QStringLiteral("OBS source"), true},
        {QStringLiteral("SourceTiming"), QStringLiteral("Source timing and animation"),
         QStringLiteral("OBS tick cadence, playhead progression, cue phases, animation frame selection and discontinuities."), QStringLiteral("OBS source"), true},
        {QStringLiteral("SourcePresentation"), QStringLiteral("Source frame presentation"),
         QStringLiteral("Tick-to-render handoff, dirty state, frame publication, skipped draws, stale-frame holds and generation mismatches."), QStringLiteral("OBS source"), true},
        {QStringLiteral("SourceFlicker"), QStringLiteral("Source flicker and frame consistency"),
         QStringLiteral("Detect transparent/missing frames, alternating publication state, repeated first-frame recovery and rapid visible-output changes."), QStringLiteral("OBS source"), false},
        {QStringLiteral("SourceMasks"), QStringLiteral("Source scene masks"),
         QStringLiteral("OBS scene-mask source acquisition, render passes, dimensions and failures."), QStringLiteral("OBS source"), true},
        {QStringLiteral("Audio"), QStringLiteral("Source and editor audio"),
         QStringLiteral("Audio decode, editor monitoring, packet scheduling, transport and underrun diagnostics."), QStringLiteral("OBS source"), true},

        {QStringLiteral("Editor"), QStringLiteral("Editor lifecycle"),
         QStringLiteral("Editor lifecycle, title changes and preview synchronization."), QStringLiteral("Editor and interface"), true},
        {QStringLiteral("Dock"), QStringLiteral("Docks and layout"),
         QStringLiteral("Dock lifecycle, layout restoration, title selection and cue controls."), QStringLiteral("Editor and interface"), true},
        {QStringLiteral("Canvas"), QStringLiteral("Canvas"),
         QStringLiteral("Canvas presentation, interactions and display rendering."), QStringLiteral("Editor and interface"), true},
        {QStringLiteral("Properties"), QStringLiteral("Properties panels"),
         QStringLiteral("Properties-panel edits and live visual updates."), QStringLiteral("Editor and interface"), true},
        {QStringLiteral("Timeline"), QStringLiteral("Timeline and transport"),
         QStringLiteral("Timeline edits, playback, work area and keyframes."), QStringLiteral("Editor and interface"), true},
        {QStringLiteral("LiveCueUI"), QStringLiteral("Live cue interface"),
         QStringLiteral("Live-cue table editing and user-interface state."), QStringLiteral("Editor and interface"), true},

        {QStringLiteral("Layers"), QStringLiteral("Layers"),
         QStringLiteral("Layer creation, deletion, ordering, visibility and parenting."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Grouping"), QStringLiteral("Grouping and parenting"),
         QStringLiteral("Group membership, reparent conversion, keyframe preservation and group-render diagnostics."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Coordinates"), QStringLiteral("Coordinate systems"),
         QStringLiteral("Local, parent and world-space conversion diagnostics."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Text"), QStringLiteral("Text model"),
         QStringLiteral("Rich-text layout, inline editing and text model changes."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Animation"), QStringLiteral("Animation evaluation"),
         QStringLiteral("Keyframe evaluation, interpolation, temporal sampling and animation transitions."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Transitions"), QStringLiteral("Transitions"),
         QStringLiteral("Layer and title transition setup and evaluation."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Ticker"), QStringLiteral("Ticker playback"),
         QStringLiteral("Ticker pause, resume, stop, cue gates and adaptive auto-pause."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("LiveCue"), QStringLiteral("Live text cues"),
         QStringLiteral("Live-cue state, payload and render lifecycle."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Playlist"), QStringLiteral("Playlist"),
         QStringLiteral("Playlist transitions, timing and cue selection."), QStringLiteral("Title model and animation"), true},

        {QStringLiteral("GpuPipeline"), QStringLiteral("GPU compositor"),
         QStringLiteral("GPU graph updates, frame publication and draw failures."), QStringLiteral("Rendering"), true},
        {QStringLiteral("RenderDiagnostics"), QStringLiteral("Correlated render diagnostics"),
         QStringLiteral("Editor transport, canvas scheduling, GPU-session state, depth-run and extrusion diagnostics."), QStringLiteral("Rendering"), true},
        {QStringLiteral("GpuText"), QStringLiteral("GPU text"),
         QStringLiteral("Glyph layout, atlas rendering and text-raster readiness."), QStringLiteral("Rendering"), true},
        {QStringLiteral("Effects"), QStringLiteral("Effects"),
         QStringLiteral("Effect compilation, passes and effect-stack changes."), QStringLiteral("Rendering"), true},
        {QStringLiteral("Extensions"), QStringLiteral("Effect extensions"),
         QStringLiteral("Extension discovery, validation and shader loading."), QStringLiteral("Rendering"), true},
        {QStringLiteral("Masks"), QStringLiteral("Layer masks and mattes"),
         QStringLiteral("Layer masks, track mattes and internal mask composition."), QStringLiteral("Rendering"), true},
        {QStringLiteral("Performance"), QStringLiteral("Performance (verbose)"),
         QStringLiteral("Render timing, resource and high-frequency diagnostics."), QStringLiteral("Rendering"), false},

        {QStringLiteral("Cache"), QStringLiteral("Cache state"),
         QStringLiteral("General frame-cache state, invalidation and diagnostics."), QStringLiteral("Cache and media"), true},
        {QStringLiteral("CacheQueue"), QStringLiteral("Cache queue"),
         QStringLiteral("Prerender queue scheduling, cancellation and retries."), QStringLiteral("Cache and media"), true},
        {QStringLiteral("CachePlayback"), QStringLiteral("Cache playback (verbose)"),
         QStringLiteral("Per-frame cache lookup and presentation decisions."), QStringLiteral("Cache and media"), false},
        {QStringLiteral("RamCache"), QStringLiteral("RAM/GPU cache"),
         QStringLiteral("GPU/RAM cache publication, eviction and memory use."), QStringLiteral("Cache and media"), true},
        {QStringLiteral("DiskCache"), QStringLiteral("Disk cache"),
         QStringLiteral("Disk-cache restore, compression, writes and failures."), QStringLiteral("Cache and media"), true},
        {QStringLiteral("Prerender"), QStringLiteral("Prerender pipeline"),
         QStringLiteral("GPU readback submission, resolution and frame publication."), QStringLiteral("Cache and media"), true},
        {QStringLiteral("Assets"), QStringLiteral("Assets and media"),
         QStringLiteral("Images, fonts, video, media decode and external asset loading."), QStringLiteral("Cache and media"), true},
        {QStringLiteral("ExternalData"), QStringLiteral("External data"),
         QStringLiteral("Provider lifecycle, refreshes, parsing, bindings, table mapping and render-queue diagnostics."), QStringLiteral("Cache and media"), true},
    };

    return categories;
}

bool category_default_enabled(const QString &key)
{
    for (const auto &category : all_categories()) {
        if (category.key.compare(key, Qt::CaseInsensitive) == 0)
            return category.default_enabled;
    }
    return true;
}

QString normalized_category(const char *category)
{
    const QString clean = QString::fromUtf8(category ? category : "General").trimmed();
    return clean.isEmpty() ? QStringLiteral("General") : clean;
}

QString ensure_session_path_locked()
{
    if (!g_session_started) {
        g_session_stamp = QDateTime::currentDateTime().toString(
            QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz"));
        g_session_started = true;
    }
    if (g_session_file_path.isEmpty()) {
        const QString directory = TitlePreferences::logging_directory();
        QDir().mkpath(directory);
        g_session_file_path = QDir(directory).filePath(
            QStringLiteral("broadcast-graphics-live_%1.log").arg(g_session_stamp));
    }
    return g_session_file_path;
}

int obs_level_for_title_level(TitleLogLevel level)
{
    switch (level) {
    case TitleLogLevel::Error:
        return LOG_ERROR;
    case TitleLogLevel::Warning:
        return LOG_WARNING;
    case TitleLogLevel::Info:
        return LOG_INFO;
    case TitleLogLevel::Debug:
    case TitleLogLevel::Trace:
        return LOG_DEBUG;
    case TitleLogLevel::Off:
    default:
        return LOG_INFO;
    }
}

void append_line_locked(const QString &path, const QString &line)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream stream(&file);
    stream << line << '\n';
}

} // namespace

namespace TitleLogger {

QString levelName(TitleLogLevel level)
{
    switch (level) {
    case TitleLogLevel::Error:
        return QStringLiteral("ERROR");
    case TitleLogLevel::Warning:
        return QStringLiteral("WARN");
    case TitleLogLevel::Info:
        return QStringLiteral("INFO");
    case TitleLogLevel::Debug:
        return QStringLiteral("DEBUG");
    case TitleLogLevel::Trace:
        return QStringLiteral("TRACE");
    case TitleLogLevel::Off:
    default:
        return QStringLiteral("OFF");
    }
}

void startSession()
{
    QMutexLocker lock(&g_log_mutex);
    if (g_session_started)
        return;
    ensure_session_path_locked();
    if (TitlePreferences::logging_enabled()) {
        append_line_locked(g_session_file_path,
            QStringLiteral("%1 [INFO] [Plugin] Logging session started: %2")
                .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                     g_session_file_path));
    }
}

void endSession()
{
    QMutexLocker lock(&g_log_mutex);
    if (!g_session_started)
        return;
    if (TitlePreferences::logging_enabled() && !g_session_file_path.isEmpty()) {
        append_line_locked(g_session_file_path,
            QStringLiteral("%1 [INFO] [Plugin] Logging session ended")
                .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)));
    }
    g_session_started = false;
    g_session_file_path.clear();
    g_session_stamp.clear();
}

QString currentSessionFilePath()
{
    QMutexLocker lock(&g_log_mutex);
    return ensure_session_path_locked();
}

bool relocateCurrentSession(const QString &directory)
{
    const QString requested_directory = directory.trimmed();
    if (requested_directory.isEmpty())
        return false;
    const QString clean_directory = QDir::cleanPath(requested_directory);

    QMutexLocker lock(&g_log_mutex);
    const QString old_path = ensure_session_path_locked();
    if (!QDir().mkpath(clean_directory))
        return false;
    const QString new_path = QDir(clean_directory).filePath(QFileInfo(old_path).fileName());
    if (QFileInfo(old_path).absoluteFilePath() == QFileInfo(new_path).absoluteFilePath())
        return true;

    bool moved = true;
    if (QFile::exists(old_path)) {
        QFile::remove(new_path);
        moved = QFile::rename(old_path, new_path);
        if (!moved) {
            moved = QFile::copy(old_path, new_path);
            if (moved)
                QFile::remove(old_path);
        }
    }
    if (moved)
        g_session_file_path = new_path;
    return moved;
}

QVector<TitleLogCategory> categories()
{
    return all_categories();
}

bool categoryEnabled(const QString &category)
{
    const QString clean = category.trimmed().isEmpty()
        ? QStringLiteral("General") : category.trimmed();
    return TitlePreferences::logging_category_enabled(
        clean, category_default_enabled(clean));
}

bool wouldLog(TitleLogLevel level, const char *category)
{
    if (level == TitleLogLevel::Off || !TitlePreferences::logging_enabled())
        return false;
    if ((int)level > (int)TitlePreferences::logging_level())
        return false;
    return categoryEnabled(normalized_category(category));
}

void log(TitleLogLevel level, const char *category, const QString &message)
{
    if (!wouldLog(level, category))
        return;

    const QString clean_category = normalized_category(category);

    const QString line = QStringLiteral("%1 [%2] [%3] %4")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  levelName(level), clean_category, message);

    {
        QMutexLocker lock(&g_log_mutex);
        append_line_locked(ensure_session_path_locked(), line);
    }

    if (TitlePreferences::logging_mirror_to_obs())
        blog(obs_level_for_title_level(level), "[Broadcast Graphics Live] %s", line.toUtf8().constData());
}

void error(const char *category, const QString &message)
{
    log(TitleLogLevel::Error, category, message);
}

void warning(const char *category, const QString &message)
{
    log(TitleLogLevel::Warning, category, message);
}

void info(const char *category, const QString &message)
{
    log(TitleLogLevel::Info, category, message);
}

void debug(const char *category, const QString &message)
{
    log(TitleLogLevel::Debug, category, message);
}

void trace(const char *category, const QString &message)
{
    log(TitleLogLevel::Trace, category, message);
}

} // namespace TitleLogger
