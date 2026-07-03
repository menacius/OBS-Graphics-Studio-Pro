/* Editor-only monitored preview source. It uses the same BGL source/audio
 * runtime as program output, but is private and monitor-only, preventing a
 * second scene/program output while still following the editor transport. */
void TitleEditor::ensure_editor_audio_preview()
{
    if (editor_audio_preview_source_ || !title_)
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
    obs_source_set_monitoring_type(
        editor_audio_preview_source_, OBS_MONITORING_TYPE_MONITOR_ONLY);

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
}

void TitleEditor::sync_editor_audio_preview(bool discontinuity)
{
    if (!title_)
        return;

    ensure_editor_audio_preview();
    if (!editor_audio_preview_source_)
        return;

    title_source_set_editor_transport(editor_audio_preview_source_, playhead_,
                                      playback_reverse_);

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

    obs_source_media_play_pause(editor_audio_preview_source_, !playing_);

    BGL_LOG_DEBUG(
        "Audio",
        QStringLiteral(
            "[editor-preview-sync] source=%1 playhead=%2 playing=%3 "
            "media_state=%4 discontinuity=%5 reverse=%6")
            .arg(QString::fromUtf8(
                obs_source_get_name(editor_audio_preview_source_)))
            .arg(playhead_, 0, 'f', 3)
            .arg(playing_ ? 1 : 0)
            .arg(static_cast<int>(
                obs_source_media_get_state(editor_audio_preview_source_)))
            .arg(discontinuity ? 1 : 0)
            .arg(playback_reverse_ ? 1 : 0));
}


void TitleEditor::publish_editor_audio_runtime_state()
{
    if (!title_) return;
    auto stored = TitleDataStore::instance().get_title(title_->id);
    if (!stored) return;
    for (const auto &src : title_->layers) {
        if (!src) continue;
        auto dst = stored->find_layer(src->id);
        if (!dst) continue;
        if (src->type == LayerType::Audio || layer_type_is_container(src->type)) {
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
            dst->audio_in_point = src->audio_in_point;
            dst->audio_out_point = src->audio_out_point;
            dst->audio_effects = src->audio_effects;
        }
    }
    TitleDataStore::instance().touch_runtime_change();
}

void TitleEditor::update_footer_diagnostics()
{
    if (!status_diagnostics_label_) return;
    int total = 0, ready = 0;
    if (title_) {
        auto stored = TitleDataStore::instance().get_title(title_->id);
        for (const auto &layer : title_->layers) {
            if (!layer || layer->type != LayerType::Audio) continue;
            ++total;
            bool has = !layer->audio_waveform.empty();
            if (!has && stored) {
                auto runtime_layer = stored->find_layer(layer->id);
                if (runtime_layer && !runtime_layer->audio_waveform.empty()) {
                    layer->audio_waveform = runtime_layer->audio_waveform;
                    layer->audio_waveform_duration = runtime_layer->audio_waveform_duration;
                    has = true;
                }
            }
            if (has) ++ready;
        }
    }
    const int waveform_percent = total == 0 ? 100 : (ready * 100 / total);
    const double render_ms = canvas_ ? canvas_->average_render_cost_ms() : 0.0;
    const double live_fps = playing_ && canvas_ ? canvas_->live_playback_fps() : 0.0;
    const double target_fps = std::max(1.0, obs_frame_rate());
    const double frame_budget_ms = 1000.0 / target_fps;
    const QString normal_color = status_diagnostics_label_->palette().color(QPalette::Text).name();
    QString render_color = normal_color;
    if (render_ms > frame_budget_ms) render_color = QStringLiteral("#e53935");
    else if (render_ms >= frame_budget_ms * 0.80) render_color = QStringLiteral("#f9a825");
    const QString fps_color = (playing_ && live_fps + 0.05 < target_fps)
        ? QStringLiteral("#e53935") : normal_color;

    QStringList parts;
    if (waveform_percent < 100)
        parts << QStringLiteral("Waveform %1%").arg(waveform_percent);
    parts << QStringLiteral("Average time to render frame: <span style=\"color:%1\">%2 ms</span>")
                 .arg(render_color).arg(render_ms, 0, 'f', 1);
    parts << QStringLiteral("FPS: <span style=\"color:%1\">%2</span>")
                 .arg(fps_color).arg(live_fps, 0, 'f', 1);
    status_diagnostics_label_->setText(parts.join(QStringLiteral("   |   ")));
    if (timeline_) timeline_->update();
}
