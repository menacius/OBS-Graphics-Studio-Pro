namespace {

bool editor_title_contains_audio_media_impl(const std::shared_ptr<Title> &title,
                                            std::set<std::string> &visited)
{
    if (!title || !visited.insert(title->id).second)
        return false;
    for (const auto &layer : title->layers) {
        if (!layer)
            continue;
        if (layer->type == LayerType::Audio)
            return true;
        if (layer->type == LayerType::Asset && !layer->asset_title_id.empty()) {
            auto asset = TitleDataStore::instance().get_title(layer->asset_title_id);
            if (editor_title_contains_audio_media_impl(asset, visited))
                return true;
        }
    }
    return false;
}

bool editor_title_contains_audio_media(const std::shared_ptr<Title> &title)
{
    std::set<std::string> visited;
    return editor_title_contains_audio_media_impl(title, visited);
}

}

class BglEditorAudioMeter : public QWidget {
public:
    explicit BglEditorAudioMeter(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("BroadcastGraphicsLiveEditorAudioMeter"));
        setMinimumSize(72, 180);
        setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
        setToolTip(QStringLiteral("Editor audio level"));
    }

    void setLevels(float left, float right, bool active)
    {
        const float target_l = active ? std::clamp(left, 0.0f, 1.0f) : 0.0f;
        const float target_r = active ? std::clamp(right, 0.0f, 1.0f) : 0.0f;
        const float attack = 0.85f;
        const float release = 0.74f;
        auto smooth = [&](float current, float target) {
            if (target > current)
                return current + (target - current) * attack;
            return std::max(target, current * release);
        };
        const float next_left = smooth(level_left_, target_l);
        const float next_right = smooth(level_right_, target_r);
        const float old_peak_left = peak_left_;
        const float old_peak_right = peak_right_;
        update_peak(next_left, peak_left_, peak_left_hold_);
        update_peak(next_right, peak_right_, peak_right_hold_);
        if (std::abs(next_left - level_left_) < 0.0001f &&
            std::abs(next_right - level_right_) < 0.0001f &&
            std::abs(old_peak_left - peak_left_) < 0.0001f &&
            std::abs(old_peak_right - peak_right_) < 0.0001f &&
            active_ == active)
            return;
        level_left_ = next_left;
        level_right_ = next_right;
        active_ = active;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        const QPalette pal = palette();
        painter.fillRect(rect(), pal.color(QPalette::Window));

        const int top = 8;
        const int bottom_margin = 19;
        const QRect meter_rect(4, top, 30,
                               std::max(48, height() - top - bottom_margin));
        const QRect scale_rect(meter_rect.right() + 6, meter_rect.top(),
                               std::max(24, width() - meter_rect.right() - 7),
                               meter_rect.height());

        const QColor frame = pal.color(QPalette::Mid);
        const QColor trough = pal.color(QPalette::Base);
        const bool dark_theme = pal.color(QPalette::Window).lightness() < 128;
        auto themed_meter_color = [&](QColor canonical) {
            /* Retain OBS' familiar meter hues while letting the active theme
             * influence brightness and a small amount of tint. Frames,
             * troughs, labels and disabled states remain palette-native. */
            const QColor accent = pal.color(QPalette::Highlight);
            canonical.setRed((canonical.red() * 9 + accent.red()) / 10);
            canonical.setGreen((canonical.green() * 9 + accent.green()) / 10);
            canonical.setBlue((canonical.blue() * 9 + accent.blue()) / 10);
            return dark_theme ? canonical.lighter(108)
                              : canonical.darker(106);
        };
        const QColor meter_green = themed_meter_color(QColor(66, 190, 92));
        const QColor meter_yellow = themed_meter_color(QColor(236, 183, 54));
        const QColor meter_red = themed_meter_color(QColor(226, 72, 72));

        painter.setPen(frame);
        painter.drawRect(meter_rect.adjusted(0, 0, -1, -1));
        painter.fillRect(meter_rect.adjusted(1, 1, -1, -1),
                         trough);

        const int clip_height = 11;
        const QRect left_clip(meter_rect.left() + 2, meter_rect.top() + 2,
                              12, clip_height);
        const QRect right_clip(meter_rect.left() + 16, meter_rect.top() + 2,
                               12, clip_height);
        painter.fillRect(left_clip,
                         peak_left_ >= 0.999f ? meter_red : trough);
        painter.fillRect(right_clip,
                         peak_right_ >= 0.999f ? meter_red : trough);
        painter.setPen(frame);
        painter.drawRect(left_clip.adjusted(0, 0, -1, -1));
        painter.drawRect(right_clip.adjusted(0, 0, -1, -1));

        const QRect level_rect(meter_rect.left() + 2,
                               meter_rect.top() + clip_height + 3,
                               meter_rect.width() - 4,
                               meter_rect.height() - clip_height - 5);

        auto y_for_db = [&](double db) -> int {
            db = std::clamp(db, -60.0, 0.0);
            const double t = (db + 60.0) / 60.0;
            return level_rect.bottom() - static_cast<int>(std::round(t * level_rect.height()));
        };
        auto db_for_level = [](float level) -> double {
            return level > 0.000001f ? 20.0 * std::log10(level) : -60.0;
        };

        QLinearGradient level_gradient(0, level_rect.top(), 0, level_rect.bottom());
        /* OBS-style zones: red above -9 dB, yellow to -20 dB and green below.
         * Close stops keep the thresholds legible without looking detached
         * from the surrounding themed dock. */
        level_gradient.setColorAt(0.0, meter_red.lighter(108));
        level_gradient.setColorAt(0.15, meter_red);
        level_gradient.setColorAt(0.17, meter_yellow.lighter(105));
        level_gradient.setColorAt(0.33, meter_yellow);
        level_gradient.setColorAt(0.35, meter_green.lighter(106));
        level_gradient.setColorAt(1.0, meter_green.darker(112));

        auto peak_color_for_db = [&](double db) {
            const QColor color = db >= -9.0 ? meter_red
                                : db >= -20.0 ? meter_yellow
                                              : meter_green;
            return dark_theme ? color.lighter(125) : color.darker(112);
        };

        auto draw_level_bar = [&](int x, float level, float peak) {
            const int bar_w = 12;
            const int y = y_for_db(db_for_level(level));
            const QRect channel(x, level_rect.top(), bar_w, level_rect.height());
            painter.fillRect(channel, trough);
            painter.setPen(frame);
            painter.drawRect(channel.adjusted(0, 0, -1, -1));
            const QRect fill(x + 1, y, bar_w - 2, level_rect.bottom() - y);
            if (fill.height() > 0)
                painter.fillRect(fill, level_gradient);
            if (peak > 0.000001f) {
                const double peak_db = db_for_level(peak);
                const int peak_y = y_for_db(peak_db);
                painter.fillRect(QRect(x + 1, peak_y, bar_w - 2, 2),
                                 peak_color_for_db(peak_db));
            }
        };
        draw_level_bar(level_rect.left(), level_left_, peak_left_);
        draw_level_bar(level_rect.left() + 14, level_right_, peak_right_);

        painter.setPen(QPen(frame, 1));
        for (int db = 0; db >= -51; db -= 3) {
            const int y = y_for_db(db);
            painter.drawLine(meter_rect.right() + 1, y, meter_rect.right() + 4, y);
            if (db % 6 == 0 || db == -3 || db == -51) {
                painter.setPen(pal.color(QPalette::Text));
                painter.drawText(scale_rect.left(), y - 7, scale_rect.width(), 14,
                                 Qt::AlignVCenter | Qt::AlignLeft,
                                 QString::number(db));
                painter.setPen(QPen(frame, 1));
            }
        }
        painter.setPen(pal.color(QPalette::Text));
        painter.drawText(scale_rect.left(), height() - 16, scale_rect.width(), 14,
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("dB"));
    }

private:
    static void update_peak(float level, float &peak, int &hold)
    {
        if (level >= peak) {
            peak = level;
            hold = 24;
        } else if (hold > 0) {
            --hold;
        } else {
            peak = std::max(level, peak * 0.955f);
        }
    }

    float level_left_ = 0.0f;
    float level_right_ = 0.0f;
    float peak_left_ = 0.0f;
    float peak_right_ = 0.0f;
    int peak_left_hold_ = 0;
    int peak_right_hold_ = 0;
    bool active_ = false;
};

class BglEditorAudioMonitorButton : public QToolButton {
public:
    explicit BglEditorAudioMonitorButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QToolButton::paintEvent(event);
        if (isChecked())
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF mark = QRectF(rect()).adjusted(9.0, 9.0, -9.0, -9.0);
        painter.setPen(QPen(QColor(220, 45, 45), 3.0,
                            Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(mark.topLeft(), mark.bottomRight());
        painter.drawLine(mark.topRight(), mark.bottomLeft());
    }
};

QWidget *TitleEditor::create_editor_audio_panel()
{
    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("BroadcastGraphicsLiveEditorAudioPanel"));
    panel->setMinimumWidth(78);
    panel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 6, 4, 6);
    layout->setSpacing(4);

    editor_audio_meter_ = new BglEditorAudioMeter(panel);
    layout->addWidget(editor_audio_meter_, 1, Qt::AlignHCenter);

    editor_audio_monitor_button_ = new BglEditorAudioMonitorButton(panel);
    editor_audio_monitor_button_->setObjectName(
        QStringLiteral("BroadcastGraphicsLiveEditorAudioMonitorButton"));
    editor_audio_monitor_button_->setIcon(obs_icon("headphones.svg"));
    editor_audio_monitor_button_->setText(QStringLiteral("🎧"));
    editor_audio_monitor_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    editor_audio_monitor_button_->setCheckable(true);
    editor_audio_monitor_button_->setAutoRaise(true);
    editor_audio_monitor_button_->setFixedSize(36, 36);
    editor_audio_monitor_button_->setIconSize(QSize(22, 22));
    editor_audio_monitor_button_->setStyleSheet(QStringLiteral(
        "QToolButton{border:none;background:transparent;padding:4px;}"
        "QToolButton:hover{background:palette(light);}"
        "QToolButton:checked{background:palette(highlight);"
        "color:palette(highlighted-text);}"
        "QToolButton:disabled{background:transparent;color:palette(mid);}"));
    connect(editor_audio_monitor_button_, &QToolButton::toggled, this,
            [this](bool enabled) {
                editor_audio_monitor_enabled_ = enabled;
                apply_editor_audio_monitoring();
                update_editor_audio_monitor_button();
            });

    layout->addWidget(editor_audio_monitor_button_, 0,
                      Qt::AlignHCenter | Qt::AlignBottom);

    editor_audio_meter_timer_ = new QTimer(panel);
    editor_audio_meter_timer_->setInterval(33);
    editor_audio_meter_timer_->setTimerType(Qt::CoarseTimer);
    connect(editor_audio_meter_timer_, &QTimer::timeout, this,
            &TitleEditor::update_editor_audio_meter);
    editor_audio_meter_timer_->start();
    connect(&CacheManager::instance(), &CacheManager::playbackSettingsChanged, this,
            [this]() {
                apply_editor_audio_monitoring();
                update_editor_audio_monitor_button();
                update_editor_audio_meter();
                if (editor_audio_preview_source_)
                    sync_editor_audio_preview(false);
            });

    update_editor_audio_monitor_button();
    update_editor_audio_meter();
    return panel;
}

void TitleEditor::apply_editor_audio_monitoring()
{
    if (!editor_audio_preview_source_)
        return;

    obs_source_set_monitoring_type(
        editor_audio_preview_source_,
        editor_audio_monitor_enabled_
            ? OBS_MONITORING_TYPE_MONITOR_ONLY
            : OBS_MONITORING_TYPE_NONE);
}

void TitleEditor::update_editor_audio_monitor_button()
{
    if (!editor_audio_monitor_button_)
        return;

    QSignalBlocker blocker(editor_audio_monitor_button_);
    const bool play_every_frame =
        CacheManager::instance().playbackSettings().play_every_frame;
    editor_audio_monitor_button_->setChecked(editor_audio_monitor_enabled_);
    editor_audio_monitor_button_->setEnabled(
        editor_audio_preview_source_ != nullptr ||
        editor_title_contains_audio_media(title_));
    editor_audio_monitor_button_->setToolTip(
        editor_audio_monitor_enabled_
            ? (play_every_frame
                   ? QStringLiteral("Editor audio monitoring is on; Play Every Frame varispeeds audio to visual cadence")
                   : QStringLiteral("Editor audio monitoring is on"))
            : QStringLiteral("Editor audio monitoring is muted"));
}

void TitleEditor::update_editor_audio_meter()
{
    if (!editor_audio_meter_)
        return;
    if (!editor_audio_meter_->isVisibleTo(this))
        return;

    float left = 0.0f;
    float right = 0.0f;
    const bool has_levels = editor_audio_preview_source_ &&
        title_source_get_audio_levels(editor_audio_preview_source_, &left, &right);
    editor_audio_meter_->setLevels(left, right,
                                   has_levels && playing_ && title_ != nullptr);
}

/* Editor-only monitored preview source. It uses the same BGL source/audio
 * runtime as program output, but is private and monitor-only, preventing a
 * second scene/program output while still following the editor transport. */
void TitleEditor::ensure_editor_audio_preview()
{
    if (editor_audio_preview_source_ || !title_ ||
        !editor_title_contains_audio_media(title_))
        return;

    BGL_LOG_INFO(
        "Audio",
        QStringLiteral("[editor-preview-create-request] title=%1 playhead=%2")
            .arg(QString::fromStdString(title_->id))
            .arg(playhead_, 0, 'f', 3));

    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "title_id", title_->id.c_str());
    obs_data_set_bool(settings, "editor_audio_preview", true);
    obs_data_set_bool(settings, "editor_transport_controlled", true);
    editor_audio_preview_source_ = obs_source_create_private(
        "broadcast_graphics_live_source", "BGL Editor Audio Preview", settings);
    obs_data_release(settings);

    if (!editor_audio_preview_source_) {
        BGL_LOG_ERROR(
            "Audio",
            QStringLiteral("[editor-preview-create-failed] title=%1")
                .arg(QString::fromStdString(title_->id)));
        return;
    }

    obs_source_inc_active(editor_audio_preview_source_);
    obs_source_inc_showing(editor_audio_preview_source_);
    apply_editor_audio_monitoring();
    update_editor_audio_monitor_button();

    BGL_LOG_INFO(
        "Audio",
        QStringLiteral(
            "[editor-preview-created] title=%1 source=%2 active=%3 "
            "showing=%4 monitoring_type=%5")
            .arg(QString::fromStdString(title_->id))
            .arg(QString::fromUtf8(
                obs_source_get_name(editor_audio_preview_source_)))
            .arg(obs_source_active(editor_audio_preview_source_) ? 1 : 0)
            .arg(obs_source_showing(editor_audio_preview_source_) ? 1 : 0)
            .arg(static_cast<int>(obs_source_get_monitoring_type(
                editor_audio_preview_source_))));

    obs_source_media_set_time(
        editor_audio_preview_source_,
        static_cast<int64_t>(std::llround(playhead_ * 1000.0)));
    obs_source_media_play_pause(editor_audio_preview_source_, true);
    editor_audio_snapshot_dirty_ = true;
    editor_audio_play_state_known_ = true;
    editor_audio_last_playing_ = false;
}

void TitleEditor::release_editor_audio_preview()
{
    if (!editor_audio_preview_source_)
        return;

    BGL_LOG_INFO(
        "Audio",
        QStringLiteral(
            "[editor-preview-release] source=%1 playhead=%2 state=%3")
            .arg(QString::fromUtf8(
                obs_source_get_name(editor_audio_preview_source_)))
            .arg(playhead_, 0, 'f', 3)
            .arg(static_cast<int>(
                obs_source_media_get_state(editor_audio_preview_source_))));

    obs_source_media_stop(editor_audio_preview_source_);
    obs_source_dec_showing(editor_audio_preview_source_);
    obs_source_dec_active(editor_audio_preview_source_);
    obs_source_release(editor_audio_preview_source_);
    editor_audio_preview_source_ = nullptr;
    editor_audio_snapshot_dirty_ = true;
    editor_audio_play_state_known_ = false;
    editor_audio_last_playing_ = false;
    update_editor_audio_monitor_button();
}

void TitleEditor::sync_editor_audio_preview(bool discontinuity)
{
    if (!title_)
        return;

    if (!editor_title_contains_audio_media(title_)) {
        release_editor_audio_preview();
        return;
    }

    ensure_editor_audio_preview();
    if (!editor_audio_preview_source_)
        return;

    if (discontinuity || editor_audio_snapshot_dirty_) {
        title_source_set_editor_title_snapshot(editor_audio_preview_source_, title_);
        editor_audio_snapshot_dirty_ = false;
    }
    const CachePlaybackSettings cache_settings =
        CacheManager::instance().playbackSettings();
    double editor_audio_play_every_frame_speed_factor = 1.0;
    if (cache_settings.play_every_frame && playing_ && !discontinuity) {
        const qint64 elapsed_ms = editor_audio_speed_clock_.isValid()
            ? editor_audio_speed_clock_.elapsed()
            : 0;
        const double wall_seconds = static_cast<double>(elapsed_ms) / 1000.0;
        const double timeline_seconds = std::abs(
            playhead_ - editor_audio_speed_last_playhead_);
        if (wall_seconds > 0.001 && timeline_seconds > 0.0) {
            const double measured = std::clamp(timeline_seconds / wall_seconds,
                                               0.02, 4.0);
            editor_audio_speed_factor_ =
                std::clamp(editor_audio_speed_factor_ * 0.65 +
                               measured * 0.35,
                           0.02, 4.0);
        }
        editor_audio_play_every_frame_speed_factor = editor_audio_speed_factor_;
    } else {
        editor_audio_speed_factor_ = 1.0;
    }
    editor_audio_speed_last_playhead_ = playhead_;
    editor_audio_speed_clock_.restart();
    title_source_set_editor_transport(
        editor_audio_preview_source_, playhead_, playback_reverse_,
        editor_audio_play_every_frame_speed_factor);

    if (discontinuity) {
        BGL_LOG_INFO(
            "Audio",
            QStringLiteral(
                "[editor-preview-seek] source=%1 playhead=%2 playing=%3 reverse=%4")
                .arg(QString::fromUtf8(
                    obs_source_get_name(editor_audio_preview_source_)))
                .arg(playhead_, 0, 'f', 3)
                .arg(playing_ ? 1 : 0)
                .arg(playback_reverse_ ? 1 : 0));
        editor_audio_preview_seeking_ = true;
        obs_source_media_set_time(
            editor_audio_preview_source_,
            static_cast<int64_t>(std::llround(playhead_ * 1000.0)));
        editor_audio_preview_seeking_ = false;
    }

    const bool play_state_changed = !editor_audio_play_state_known_ ||
        editor_audio_last_playing_ != playing_;
    if (play_state_changed || discontinuity) {
        obs_source_media_play_pause(editor_audio_preview_source_, !playing_);
        editor_audio_play_state_known_ = true;
        editor_audio_last_playing_ = playing_;
    }

    if (discontinuity || play_state_changed) {
        BGL_LOG_DEBUG(
            "Audio",
            QStringLiteral(
                "[editor-preview-sync] source=%1 playhead=%2 playing=%3 "
                "media_state=%4 discontinuity=%5 reverse=%6 audioSpeed=%7")
                .arg(QString::fromUtf8(
                    obs_source_get_name(editor_audio_preview_source_)))
                .arg(playhead_, 0, 'f', 3)
                .arg(playing_ ? 1 : 0)
                .arg(static_cast<int>(
                    obs_source_media_get_state(editor_audio_preview_source_)))
                .arg(discontinuity ? 1 : 0)
                .arg(playback_reverse_ ? 1 : 0)
                .arg(editor_audio_play_every_frame_speed_factor, 0, 'f', 3));
    }
}


void TitleEditor::publish_editor_audio_runtime_state()
{
    if (!title_) return;
    editor_audio_snapshot_dirty_ = true;
    auto stored = TitleDataStore::instance().get_title(title_->id);
    if (!stored) return;
    for (const auto &src : title_->layers) {
        if (!src) continue;
        auto dst = stored->find_layer(src->id);
        if (!dst) continue;
        if (src->type == LayerType::Audio || src->type == LayerType::Video || layer_type_is_container(src->type)) {
            dst->audio_volume = src->audio_volume;
            dst->audio_pan = src->audio_pan;
            dst->audio_volume_prop = src->audio_volume_prop;
            dst->audio_pan_prop = src->audio_pan_prop;
            dst->audio_muted = src->audio_muted;
            dst->audio_solo = src->audio_solo;
            dst->audio_fade_in = src->audio_fade_in;
            dst->audio_fade_out = src->audio_fade_out;
            dst->audio_fade_curve = src->audio_fade_curve;
            dst->audio_loop = src->audio_loop;
            dst->audio_independent = src->audio_independent;
            dst->audio_playback_mode = src->audio_playback_mode;
            dst->video_playback_mode = src->video_playback_mode;
            dst->audio_in_point = src->audio_in_point;
            dst->audio_out_point = src->audio_out_point;
            dst->audio_effects = src->audio_effects;
        }
    }
    TitleDataStore::instance().touch_runtime_change();
}

void TitleEditor::update_footer_diagnostics()
{
    if (!status_diagnostics_label_ ||
        !status_diagnostics_label_->isVisibleTo(this))
        return;
    int total = 0, ready = 0;
    int active_waveform_percent = 100;
    QString active_waveform_label;
    if (title_) {
        auto stored = TitleDataStore::instance().get_title(title_->id);
        for (const auto &layer : title_->layers) {
            if (!layer || layer->type != LayerType::Audio) continue;
            ++total;
            bool has = !layer->audio_waveform.empty();
            if (stored) {
                auto runtime_layer = stored->find_layer(layer->id);
                if (runtime_layer) {
                    if (!runtime_layer->audio_waveform.empty()) {
                        layer->audio_waveform = runtime_layer->audio_waveform;
                        layer->audio_waveform_duration = runtime_layer->audio_waveform_duration;
                        has = true;
                    }
                    layer->audio_waveform_progress_percent =
                        runtime_layer->audio_waveform_progress_percent;
                    layer->audio_waveform_generating =
                        runtime_layer->audio_waveform_generating;
                    layer->audio_waveform_progress_label =
                        runtime_layer->audio_waveform_progress_label;
                }
            }
            if (has) {
                ++ready;
            } else if (layer->audio_waveform_generating) {
                active_waveform_percent = std::clamp(
                    layer->audio_waveform_progress_percent, 0, 99);
                active_waveform_label = layer->audio_waveform_progress_label.empty()
                    ? QString::fromStdString(layer->name)
                    : QString::fromStdString(layer->audio_waveform_progress_label);
            }
        }
    }
    const int waveform_percent = total == 0 ? 100 :
        std::max(0, std::min(100, (ready * 100 + active_waveform_percent) /
                                  std::max(1, total)));
    const CanvasPreview::DiagnosticsSample diagnostics = canvas_
        ? canvas_->take_diagnostics_sample()
        : CanvasPreview::DiagnosticsSample{};
    const double render_ms = diagnostics.average_render_ms;
    const double live_fps = playing_ ? diagnostics.playback_fps : 0.0;
    const double target_fps = std::max(1.0, obs_frame_rate());
    const double frame_budget_ms = 1000.0 / target_fps;
    const QString normal_color = status_diagnostics_label_->palette().color(QPalette::Text).name();
    QString render_color = normal_color;
    if (render_ms > frame_budget_ms) render_color = QStringLiteral("#e53935");
    else if (render_ms >= frame_budget_ms * 0.80) render_color = QStringLiteral("#f9a825");
    const double fps_measurement_tolerance = std::max(0.5, target_fps * 0.01);
    const QString fps_color =
        (playing_ && live_fps + fps_measurement_tolerance < target_fps)
        ? QStringLiteral("#e53935") : normal_color;

    QStringList parts;
    if (waveform_percent < 100) {
        if (!active_waveform_label.isEmpty())
            parts << QStringLiteral("Waveform %1% — %2")
                         .arg(waveform_percent)
                         .arg(active_waveform_label.toHtmlEscaped());
        else
            parts << QStringLiteral("Waveform %1%").arg(waveform_percent);
    }
    parts << QStringLiteral("Average time to render frame: <span style=\"color:%1\">%2 ms</span>")
                 .arg(render_color).arg(render_ms, 0, 'f', 1);
    parts << QStringLiteral("FPS: <span style=\"color:%1\">%2</span>")
                 .arg(fps_color).arg(live_fps, 0, 'f', 1);
    const QString diagnostics_text = parts.join(QStringLiteral("   |   "));
    if (status_diagnostics_label_->text() != diagnostics_text)
        status_diagnostics_label_->setText(diagnostics_text);
    if (waveform_percent < 100 && timeline_)
        timeline_->update();
}
