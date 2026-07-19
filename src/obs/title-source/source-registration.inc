    title_gpu_render_session_update_range(
        session, title, time, model_revision, first_dynamic_layer,
        title.layers.size());

    const qint64 image_key = 0;
    std::lock_guard<std::mutex> lock(session->mutex);
    const bool needs_upload = !session->base_frame_texture ||
        session->base_frame_image_key != image_key ||
        session->base_frame_rect != destination;
    session->use_submitted_final = false;
    session->use_gpu_cached_final = false;
    session->submitted_gpu_cache_key.clear();
    session->pending_submitted_final = QImage();
    session->submitted_final_pending = false;
    session->use_base_frame = true;
    session->use_gpu_cached_base = false;
    session->base_gpu_cache_key.clear();
    session->base_frame_rect = destination;
    session->base_frame_image_key = image_key;
    if (needs_upload) {
        session->pending_base_frame = cached_prefix;
        session->base_frame_pending = true;
    }
    session->frame_dirty = true;
    return true;
}


static uint32_t argb_with_alpha_byte(uint32_t argb, uint8_t alpha)
{
    return (argb & 0x00FFFFFFu) | (static_cast<uint32_t>(alpha) << 24);
}

static void make_rich_text_strokes_enabled(RichTextDocument &doc, bool enabled)
{
    doc.default_format.stroke.enabled = enabled;
    if (!enabled) {
        doc.default_format.stroke.width = 0.0f;
        doc.default_format.stroke.opacity = 0.0f;
    }
    if (doc.has_typing_format) {
        doc.typing_format.stroke.enabled = enabled;
        if (!enabled) {
            doc.typing_format.stroke.width = 0.0f;
            doc.typing_format.stroke.opacity = 0.0f;
        }
    }
    for (RichTextRange &range : doc.ranges) {
        range.format.stroke.enabled = enabled;
        if (!enabled) {
            range.format.stroke.width = 0.0f;
            range.format.stroke.opacity = 0.0f;
        }
    }
}

static void make_rich_text_fill_transparent(RichTextDocument &doc)
{
    auto clear_fill = [](RichTextFill &fill) {
        fill.type = 0;
        fill.color = argb_with_alpha_byte(fill.color, 0);
        fill.gradient_start_color = argb_with_alpha_byte(fill.gradient_start_color, 0);
        fill.gradient_end_color = argb_with_alpha_byte(fill.gradient_end_color, 0);
        fill.gradient_start_opacity = 0.0f;
        fill.gradient_end_opacity = 0.0f;
        fill.gradient_opacity = 0.0f;
    };
    clear_fill(doc.default_format.fill);
    if (doc.has_typing_format)
        clear_fill(doc.typing_format.fill);
    for (RichTextRange &range : doc.ranges)
        clear_fill(range.format.fill);
}

static Layer scene_mask_fill_matte_layer(const Layer &layer)
{
    Layer fill = layer;
    fill.outline_enabled = false;
    fill.stroke_width = 0.0f;
    fill.outline_opacity = 0.0f;
    fill.stroke_fill_type = 0;
    fill.background_stroke_width = 0.0f;
    fill.background_stroke_opacity = 0.0f;
    fill.background_stroke_width_prop = AnimatedProperty("background_stroke_width", 0.0);
    fill.background_stroke_opacity_prop = AnimatedProperty("background_stroke_opacity", 0.0);
    make_rich_text_strokes_enabled(fill.rich_text, false);
    return fill;
}

static Layer scene_mask_stroke_composite_layer(const Layer &layer)
{
    Layer stroke = layer;
    stroke.use_as_scene_mask = false;
    stroke.effects.clear();
    stroke.fill_type = 0;
    stroke.fill_color = argb_with_alpha_byte(stroke.fill_color, 0);
    stroke.text_color = argb_with_alpha_byte(stroke.text_color, 0);
    stroke.gradient_start_color = argb_with_alpha_byte(stroke.gradient_start_color, 0);
    stroke.gradient_end_color = argb_with_alpha_byte(stroke.gradient_end_color, 0);
    stroke.gradient_start_opacity = 0.0f;
    stroke.gradient_end_opacity = 0.0f;
    stroke.gradient_opacity = 0.0f;
    stroke.background_enabled = false;
    stroke.background_opacity = 0.0f;
    stroke.background_enabled_prop = AnimatedProperty("background_enabled", 0.0);
    stroke.background_opacity_prop = AnimatedProperty("background_opacity", 0.0);
    make_rich_text_fill_transparent(stroke.rich_text);
    return stroke;
}

static void title_gpu_render_session_prepare_auxiliary_layers(
    TitleGpuRenderSession *session, const Title &title, double time,
    uint64_t model_revision, const std::vector<std::string> &layer_ids)
{
    if (!session || layer_ids.empty())
        return;

    std::unordered_set<std::string> requested_ids;
    requested_ids.reserve(layer_ids.size());
    for (const std::string &id : layer_ids) {
        if (!id.empty())
            requested_ids.insert(id);
    }
    if (requested_ids.empty())
        return;

    /* A scene-mask layer may itself use a track matte.  Prepare the complete
     * dependency chain as auxiliary GPU inputs so cached title playback never
     * falls back to a CPU mask surface. */
    bool dependency_added = true;
    while (dependency_added) {
        dependency_added = false;
        std::vector<std::string> snapshot(requested_ids.begin(),
                                          requested_ids.end());
        for (const std::string &id : snapshot) {
            const Layer *layer = find_layer_by_id(title, id);
            if (!layer || layer->mask_mode == MaskMode::None ||
                layer->mask_source_id.empty())
                continue;
            dependency_added = requested_ids.insert(
                layer->mask_source_id).second || dependency_added;
        }
    }

    std::lock_guard<std::mutex> lock(session->mutex);
    const bool model_changed = !session->has_title ||
        session->model_revision != model_revision || session->title.id != title.id;
    if (model_changed) {
        session->title = clone_title_snapshot(title);
        session->has_title = true;
        session->model_revision = model_revision;
    }
    session->time = std::clamp(time, 0.0,
        std::max(0.0, session->title.duration));

    std::unordered_set<std::string> requested_raster_ids;
    requested_raster_ids.reserve(requested_ids.size());
    for (const std::string &id : requested_ids)
        requested_raster_ids.insert(gpu_scene_mask_raster_id(id));
    for (auto it = session->layers.begin(); it != session->layers.end();) {
        if (!is_gpu_scene_mask_raster_id(it->first) ||
            requested_raster_ids.find(it->first) != requested_raster_ids.end()) {
            ++it;
            continue;
        }
        if (it->second.texture || it->second.text_layer) {
            it->second.pending_image = QImage();
            it->second.gpu_text = false;
            it->second.gpu_primitive = false;
            it->second.pending_upload = true;
            it->second.key.clear();
            ++it;
        } else {
            if (it->second.transform_grid)
                gs_vertexbuffer_destroy(it->second.transform_grid);
            it = session->layers.erase(it);
        }
    }

    const qint64 clock_second = QDateTime::currentSecsSinceEpoch();
    for (const std::string &id : requested_ids) {
        const Layer *layer = find_layer_by_id(session->title, id);
        if (!layer)
            continue;
        const double layer_time = gpu_layer_render_time(
            session->title, *layer, session->time);
        std::string key = effect_layer_cache_key(
            nullptr, session->title, *layer, layer_time,
            std::max(1, session->title.width),
            std::max(1, session->title.height), false, true) +
            "|gpu-scene-mask-fill-only-v259";
        if (layer->type == LayerType::Clock)
            key += "|clock=" + std::to_string(clock_second);
        else if (layer->type == LayerType::Ticker)
            key += "|ticker=" + std::to_string(QDateTime::currentMSecsSinceEpoch());
        else if (layer->type == LayerType::Video) {
            key += "|" + bgl::video::FrameRuntime::instance()
                .frame_cache_key_for_layer(
                    *layer, layer_time,
                    1.0 / std::max(1e-6, source_frame_duration()));
        }

        auto &entry = session->layers[gpu_scene_mask_raster_id(id)];
        if (entry.key == key)
            continue;
        /* Build only the layer's reusable visual source.  This may be an exact
         * compatibility raster for unsupported source semantics, but it is
         * never converted into a CPU alpha/luma mask; all mask extraction and
         * composition happen after upload in the GPU mask graph. */
        const Layer fill_matte_layer = scene_mask_fill_matte_layer(*layer);
        const MotionBaseRaster raster = render_gpu_layer_base_raster(
            session->title, fill_matte_layer, layer_time);
        entry.key = std::move(key);
        entry.gpu_text = false;
        entry.gpu_primitive = false;
        entry.pending_image = raster.image;
        entry.origin = raster.origin;
        entry.logical_width = raster.logical_width > 0.0
            ? raster.logical_width : raster.image.width();
        entry.logical_height = raster.logical_height > 0.0
            ? raster.logical_height : raster.image.height();
        entry.base_box_width = std::max(0.0001, eval_box_width(*layer, layer_time));
        entry.base_box_height = std::max(0.0001, eval_box_height(*layer, layer_time));
        entry.pending_upload = true;
    }
}

static gs_texture_t *title_gpu_render_session_render_auxiliary_layer(
    TitleGpuRenderSession *session, const std::string &layer_id,
    double title_time)
{
    if (!session || layer_id.empty())
        return nullptr;

    std::lock_guard<std::mutex> lock(session->mutex);
    if (!session->has_title)
        return nullptr;
    session->width = clamped_source_dimension(session->title.width);
    session->height = clamped_source_dimension(session->title.height);
    if (!ensure_gpu_session_objects(session, session->width, session->height))
        return nullptr;

    const Layer *layer = find_layer_by_id(session->title, layer_id);
    auto found = session->layers.find(gpu_scene_mask_raster_id(layer_id));
    if (!layer || found == session->layers.end())
        return nullptr;

    /* Auxiliary scene masks use the same cached GPU matte graph as track
     * mattes.  This preserves parent transforms, nested mattes and mask-layer
     * effects without creating a CPU alpha surface. */
    ScopedGpuCompositorState state;
    if (!upload_gpu_layer_raster(session, found->second) ||
        !found->second.texture)
        return nullptr;
    // A scene-mask layer's effects belong to the final masked scene, not to
    // the alpha/luma matte itself. Build a geometry/track-matte-only mask here
    // and apply the layer effect stack after the OBS scene has been clipped.
    Layer matte_layer = scene_mask_fill_matte_layer(*layer);
    matte_layer.effects.clear();
    return render_gpu_mask_graph_texture(
        session, session->title, matte_layer, title_time,
        gpu_scene_mask_raster_id(layer_id), matte_layer.parent_id);
}

static bool prepare_external_composite_dimensions_locked(
    TitleGpuRenderSession *session)
{
    if (!session || !session->has_title)
        return false;
    const double render_scale = session->editor_draft
        ? std::clamp(static_cast<double>(session->preview_quality_scale),
                     0.25, 1.0)
        : 1.0;
    session->width = clamped_source_dimension(std::max(
        1, static_cast<int>(std::ceil(session->title.width * render_scale))));
    session->height = clamped_source_dimension(std::max(
        1, static_cast<int>(std::ceil(session->title.height * render_scale))));
    return ensure_gpu_session_objects(session, session->width,
                                      session->height);
}

static bool present_gpu_session_texture_locked(
    TitleGpuRenderSession *session, gs_texture_t *texture,
    uint32_t output_width, uint32_t output_height,
    bool replace_destination)
{
    if (!session || !texture)
        return false;
    if (!replace_destination &&
        (!gpu_session_has_published_frame_for_current_title(session) ||
         texture != session->final_texture)) {
        session->last_error =
            "Refused to present a texture outside the Published generation.";
        BGL_LOG_WARNING("RenderDiagnostics", QStringLiteral(
            "stage=presentation-invariant action=reject-unpublished texture=%1 final=%2 target=%3 generation=%4")
            .arg(reinterpret_cast<quintptr>(texture), 0, 16)
            .arg(reinterpret_cast<quintptr>(session->final_texture), 0, 16)
            .arg(session->active_presentation_target)
            .arg(session->published_presentation_generation));
        return false;
    }
    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_eparam_t *image = effect
        ? bgl_effect_param(effect, "image")
        : nullptr;
    if (!effect || !image)
        return false;
    gs_effect_set_texture(image, texture);
    const uint64_t draw_serial = ++session->draw_serial;
    if (TitlePreferences::cache_playback_logging_enabled()) {
        BGL_LOG_INFO("CachePlayback", QStringLiteral(
            "stage=gpu-draw title=%1 session=%2 drawSerial=%3 submittedSerial=%4 uploadedSerial=%5 publishedSerial=%6 texture=%7 output=%8x%9 dirty=%10 useSubmitted=%11 useGpuRam=%12 destinationAware=%13")
            .arg(session->has_title
                     ? QString::fromStdString(session->title.id)
                     : QStringLiteral("<none>"))
            .arg(reinterpret_cast<quintptr>(session), 0, 16)
            .arg(draw_serial)
            .arg(session->submitted_final_serial)
            .arg(session->uploaded_final_serial)
            .arg(session->published_final_serial)
            .arg(reinterpret_cast<quintptr>(texture), 0, 16)
            .arg(output_width).arg(output_height)
            .arg(session->frame_dirty ? 1 : 0)
            .arg(session->use_submitted_final ? 1 : 0)
            .arg(session->use_gpu_cached_final ? 1 : 0)
            .arg(replace_destination ? 1 : 0));
    }
    gs_blend_state_push();
    if (replace_destination) {
        /* The texture already contains the captured destination plus the
         * complete title stack. Replace the destination rectangle rather than
         * compositing that destination a second time. */
        gs_enable_blending(false);
    } else {
        gs_enable_blending(true);
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
    }
    while (gs_effect_loop(effect, "Draw"))
        gs_draw_sprite(texture, 0, output_width, output_height);
    gs_blend_state_pop();
    return true;
}

bool title_gpu_render_session_draw(TitleGpuRenderSession *session,
                                   uint32_t output_width,
                                   uint32_t output_height)
{
    if (!session || session->destroying.load())
        return false;
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load())
        return false;
    gs_texture_t *texture = render_gpu_session_locked(session);
    if (!texture && gpu_session_has_published_frame_for_current_title(session))
        texture = session->final_texture;
    return present_gpu_session_texture_locked(
        session, texture, output_width, output_height, false);
}

bool title_gpu_render_session_draw_transition_inputs(
    TitleGpuRenderSession *session, gs_texture_t *scene_a, gs_texture_t *scene_b,
    uint32_t input_width, uint32_t input_height,
    uint32_t output_width, uint32_t output_height)
{
    if (!session || !scene_a || !scene_b || session->destroying.load())
        return false;
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load())
        return false;
    session->transition_input_textures[0] = scene_a;
    session->transition_input_textures[1] = scene_b;
    session->transition_input_width = input_width;
    session->transition_input_height = input_height;
    ++session->transition_input_generation;
    session->frame_dirty = true;
    gs_texture_t *texture = render_gpu_session_locked(session);
    /* Manual Stinger composition already contains Scene A, Scene B and all
     * authored artwork. Replace the transition callback target so its alpha is
     * preserved exactly and the scenes are never blended a second time. */
    const bool presented = present_gpu_session_texture_locked(
        session, texture, output_width, output_height, true);
    session->transition_input_textures[0] = nullptr;
    session->transition_input_textures[1] = nullptr;
    session->transition_input_width = 0;
    session->transition_input_height = 0;
    return presented;
}

bool title_gpu_render_session_draw_over_background_rect(
    TitleGpuRenderSession *session, gs_texture_t *background,
    uint32_t background_width, uint32_t background_height,
    float background_x, float background_y, float background_width_px,
    float background_height_px, uint32_t output_width,
    uint32_t output_height,
    bool isolate_editor_background_from_effects)
{
    if (!session || !background || session->destroying.load())
        return false;
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load() ||
        !prepare_external_composite_dimensions_locked(session))
        return false;

    gs_texture_t *local_background = nullptr;
    {
        ScopedGpuCompositorState state;
        local_background = render_external_background_local_locked(
            session, background, background_width, background_height,
            ExternalBackgroundMapping::Rectangle, nullptr,
            background_x, background_y, background_width_px,
            background_height_px, output_width, output_height);
    }
    gs_texture_t *texture = local_background
        ? render_gpu_session_locked(
              session, local_background,
              isolate_editor_background_from_effects)
        : nullptr;
    if (!texture) {
        /* Keep the source/editor visible on unsupported graphics backends.
         * This fallback loses destination-dependent blending but never loses
         * the title itself. */
        texture = render_gpu_session_locked(session);
        return present_gpu_session_texture_locked(
            session, texture, output_width, output_height, false);
    }
    return present_gpu_session_texture_locked(
        session, texture, output_width, output_height, true);
}

bool title_gpu_render_session_draw_over_current_target(
    TitleGpuRenderSession *session, uint32_t output_width,
    uint32_t output_height)
{
    if (!session || session->destroying.load())
        return false;

    gs_texture_t *current_target = gs_get_render_target();
    if (!current_target)
        return title_gpu_render_session_draw(session, output_width,
                                             output_height);
    const uint32_t background_width = gs_texture_get_width(current_target);
    const uint32_t background_height = gs_texture_get_height(current_target);
    const enum gs_color_format background_format =
        gs_texture_get_color_format(current_target);
    if (background_width == 0 || background_height == 0 ||
        background_format == GS_UNKNOWN)
        return title_gpu_render_session_draw(session, output_width,
                                             output_height);

    struct matrix4 local_to_background;
    gs_matrix_get(&local_to_background);

    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load() ||
        !prepare_external_composite_dimensions_locked(session))
        return false;
    if (!ensure_external_background_snapshot(
            session, background_width, background_height,
            background_format)) {
        gs_texture_t *fallback = render_gpu_session_locked(session);
        return present_gpu_session_texture_locked(
            session, fallback, output_width, output_height, false);
    }

    /* Never sample the render target while writing to it. The snapshot is the
     * exact scene state after lower OBS items and before this BGL source. */
    gs_copy_texture(session->external_background_snapshot, current_target);

    gs_texture_t *local_background = nullptr;
    {
        ScopedGpuCompositorState state;
        local_background = render_external_background_local_locked(
            session, session->external_background_snapshot,
            background_width, background_height,
            ExternalBackgroundMapping::CurrentTransform,
            &local_to_background, 0.0f, 0.0f,
            static_cast<float>(background_width),
            static_cast<float>(background_height),
            output_width, output_height);
    }
    gs_texture_t *texture = local_background
        ? render_gpu_session_locked(session, local_background)
        : nullptr;
    if (!texture) {
        texture = render_gpu_session_locked(session);
        return present_gpu_session_texture_locked(
            session, texture, output_width, output_height, false);
    }
    return present_gpu_session_texture_locked(
        session, texture, output_width, output_height, true);
}

std::string title_gpu_render_session_last_error(TitleGpuRenderSession *session)
{
    if (!session)
        return "GPU render session is null.";
    std::lock_guard<std::mutex> lock(session->mutex);
    return session->last_error ? std::string(session->last_error) : std::string();
}

bool title_gpu_render_session_last_draw_deferred(
    TitleGpuRenderSession *session)
{
    if (!session)
        return false;
    std::lock_guard<std::mutex> lock(session->mutex);
    return session->last_draw_deferred;
}

bool title_gpu_render_session_shader_compile_status(
    TitleGpuRenderSession *session, TitleGpuShaderCompileStatus &status)
{
    status = {};
    status.progress_percent = 100;
    if (!session)
        return false;
    std::lock_guard<std::mutex> lock(session->shader_compile_mutex);
    status.active = session->shader_compile_active;
    status.queued = !session->shader_compile_queue.empty();
    status.completed_jobs = session->shader_compile_completed_jobs;
    status.total_jobs = session->shader_compile_total_jobs;
    status.progress_percent = std::clamp(
        session->shader_compile_progress_percent, 0, 100);
    status.label = session->shader_compile_active
        ? session->shader_compile_label
        : (!session->shader_compile_queue.empty()
               ? session->shader_compile_queue.front().label
               : std::string());
    return status.active || status.queued || status.progress_percent < 100;
}

bool title_gpu_render_session_get_diagnostics(
    TitleGpuRenderSession *session, TitleGpuRenderDiagnostics &diagnostics)
{
    diagnostics = {};
    if (!session || session->destroying.load())
        return false;
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load())
        return false;

    diagnostics.valid = true;
    diagnostics.session_time = session->time;
    diagnostics.last_published_time = session->diagnostics_last_published_time;
    diagnostics.update_serial = session->diagnostics_update_serial;
    diagnostics.render_serial = session->diagnostics_render_serial;
    diagnostics.publish_serial = session->diagnostics_publish_serial;
    diagnostics.presentation_generation =
        session->published_presentation_generation;
    diagnostics.model_revision = session->model_revision;
    diagnostics.published_model_revision = session->published_model_revision;
    diagnostics.frame_dirty = session->frame_dirty;
    diagnostics.last_draw_deferred = session->last_draw_deferred;
    diagnostics.state_transaction_pending =
        session->state_transaction_pending.load(std::memory_order_acquire);
    diagnostics.has_published_frame =
        gpu_session_has_published_frame_for_current_title(session);
    diagnostics.force_compatibility_raster_rebuild =
        session->force_compatibility_raster_rebuild;
    diagnostics.use_submitted_final = session->use_submitted_final;
    diagnostics.use_gpu_cached_final = session->use_gpu_cached_final;
    diagnostics.use_base_frame = session->use_base_frame;
    diagnostics.raster_count = session->layers.size();
    diagnostics.hardware_depth_run_count =
        session->diagnostics_hardware_depth_run_count;
    diagnostics.hardware_depth_layer_count =
        session->diagnostics_hardware_depth_layer_count;
    diagnostics.extrusion_pass_count =
        session->diagnostics_extrusion_pass_count;

    for (const auto &pair : session->layers) {
        const auto &entry = pair.second;
        if (entry.pending_upload)
            ++diagnostics.pending_raster_count;
        if (entry.gpu_text)
            ++diagnostics.gpu_text_raster_count;
        if (entry.gpu_primitive)
            ++diagnostics.gpu_primitive_raster_count;
    }
    if (session->has_title) {
        for (const auto &layer : session->title.layers) {
            if (!layer)
                continue;
            if (layer->type == LayerType::Light && layer->visible)
                ++diagnostics.visible_light_layer_count;
        }
    }
    return true;
}

static bool title_gpu_render_session_submit_async_readback(
    TitleGpuRenderSession *session, const std::string &cache_key,
    const QRect &requested_region, TitleGpuReadbackTicket &ticket)
{
    ticket = {};
    if (!session || session->destroying.load())
        return false;

    obs_enter_graphics();
    std::unique_lock<std::mutex> lock(session->mutex);
    if (session->destroying.load()) {
        lock.unlock();
        obs_leave_graphics();
        return false;
    }

    gs_texture_t *texture = render_gpu_session_locked(session);
    /* A readback is valid only when the texture was published from the exact
     * current model and the graph is clean. render_gpu_session_locked() may
     * deliberately return the previous same-title frame while a replacement
     * text/shape raster is pending; caching that fallback under the new key
     * would permanently associate stale or transparent pixels with 100% cache
     * progress. */
    if (!texture || session->frame_dirty ||
        !gpu_session_final_matches_model(session)) {
        BGL_LOG_DEBUG("Prerender", QStringLiteral(
            "stage=readback-submit action=reject-incomplete title=%1 revision=%2 publishedRevision=%3 dirty=%4 texture=%5 error=%6")
            .arg(session->has_title
                     ? QString::fromStdString(session->title.id)
                     : QStringLiteral("<none>"))
            .arg(session->model_revision)
            .arg(session->published_model_revision)
            .arg(session->frame_dirty ? 1 : 0)
            .arg(texture ? 1 : 0)
            .arg(session->last_error ? QString::fromUtf8(session->last_error)
                                     : QStringLiteral("none")));
        lock.unlock();
        obs_leave_graphics();
        return false;
    }

    /* The RAM cache is published only after the final readback has been
     * resolved and converted to sparse aligned tiles. Keeping a full-canvas
     * texrender here was the source of the excessive per-frame RAM usage. */
    (void)cache_key;

    std::size_t slot_index = session->readback_slots.size();
    for (std::size_t offset = 0; offset < session->readback_slots.size(); ++offset) {
        const std::size_t candidate =
            (session->next_readback_slot + offset) % session->readback_slots.size();
        if (!session->readback_slots[candidate].pending) {
            slot_index = candidate;
            break;
        }
    }
    if (slot_index >= session->readback_slots.size()) {
        lock.unlock();
        obs_leave_graphics();
        return false;
    }

    const QRect canvas_rect(0, 0, static_cast<int>(session->width),
                            static_cast<int>(session->height));
    QRect region = requested_region.isEmpty()
        ? canvas_rect : requested_region.intersected(canvas_rect);
    if (region.isEmpty())
        region = canvas_rect;

    auto &slot = session->readback_slots[slot_index];
    gs_texture_t *readback_texture = texture;
    const bool cropped = region != canvas_rect;
    if (cropped) {
        ScopedGpuCompositorState crop_state;
        if (!slot.crop_target)
            slot.crop_target = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
        struct vec4 clear;
        vec4_zero(&clear);
        if (!slot.crop_target ||
            !begin_gpu_target(slot.crop_target,
                              static_cast<uint32_t>(region.width()),
                              static_cast<uint32_t>(region.height()), clear)) {
            lock.unlock();
            obs_leave_graphics();
            return false;
        }
        gs_eparam_t *image = session->blit_effect
            ? bgl_effect_param(session->blit_effect, "image")
            : nullptr;
        if (!image) {
            gs_texrender_end(slot.crop_target);
            lock.unlock();
            obs_leave_graphics();
            return false;
        }
        gs_effect_set_texture(image, texture);
        gs_enable_blending(false);
        gs_matrix_push();
        gs_matrix_identity();
        gs_matrix_translate3f(static_cast<float>(-region.x()),
                              static_cast<float>(-region.y()), 0.0f);
        while (gs_effect_loop(session->blit_effect, "Draw"))
            gs_draw_sprite(texture, 0, session->width, session->height);
        gs_matrix_pop();
        gs_texrender_end(slot.crop_target);
        readback_texture = gs_texrender_get_texture(slot.crop_target);
    }

    const uint32_t readback_width = static_cast<uint32_t>(region.width());
    const uint32_t readback_height = static_cast<uint32_t>(region.height());
    if (!slot.stage || slot.width != readback_width ||
        slot.height != readback_height) {
        if (slot.stage)
            gs_stagesurface_destroy(slot.stage);
        slot.stage = gs_stagesurface_create(readback_width, readback_height,
                                            GS_BGRA);
        slot.width = slot.stage ? readback_width : 0;
        slot.height = slot.stage ? readback_height : 0;
    }
    if (!readback_texture || !slot.stage) {
        lock.unlock();
        obs_leave_graphics();
        return false;
    }

    gs_stage_texture(slot.stage, readback_texture);
    slot.region = region;
    slot.serial = ++session->next_readback_serial;
    slot.pending = true;
    session->next_readback_slot =
        (slot_index + 1) % session->readback_slots.size();

    ticket.session = session;
    ticket.serial = slot.serial;
    ticket.region = region;
    ticket.canvas_width = session->width;
    ticket.canvas_height = session->height;
    lock.unlock();
    obs_leave_graphics();
    return true;
}

bool title_gpu_render_session_resolve_readback(
    const TitleGpuReadbackTicket &ticket, QImage &image)
{
    image = QImage();
    if (!ticket.valid() || ticket.session->destroying.load())
        return false;

    obs_enter_graphics();
    std::unique_lock<std::mutex> lock(ticket.session->mutex);
    TitleGpuRenderSession::AsyncReadbackSlot *slot = nullptr;
    for (auto &candidate : ticket.session->readback_slots) {
        if (candidate.pending && candidate.serial == ticket.serial) {
            slot = &candidate;
            break;
        }
    }
    if (!slot || !slot->stage) {
        lock.unlock();
        obs_leave_graphics();
        return false;
    }

    uint8_t *mapped = nullptr;
    uint32_t linesize = 0;
    const bool mapped_ok = gs_stagesurface_map(slot->stage, &mapped, &linesize);
    if (mapped_ok && mapped && linesize >= slot->width * 4) {
        QImage result(static_cast<int>(slot->width),
                      static_cast<int>(slot->height),
                      QImage::Format_ARGB32_Premultiplied);
        if (!result.isNull()) {
            for (uint32_t y = 0; y < slot->height; ++y) {
                std::memcpy(result.scanLine(static_cast<int>(y)),
                            mapped + static_cast<size_t>(y) * linesize,
                            static_cast<size_t>(slot->width) * 4);
            }
            bgs::cache_frame_payload::set_placement(
                result, slot->region.x(), slot->region.y(),
                static_cast<int>(ticket.canvas_width),
                static_cast<int>(ticket.canvas_height));
            image = std::move(result);
        }
        gs_stagesurface_unmap(slot->stage);
    }
    slot->pending = false;
    slot->serial = 0;
    slot->region = QRect();
    lock.unlock();
    obs_leave_graphics();
    return !image.isNull();
}

void title_gpu_render_session_discard_readback(
    const TitleGpuReadbackTicket &ticket)
{
    QImage discarded;
    title_gpu_render_session_resolve_readback(ticket, discarded);
}

void title_gpu_render_session_cancel_readback(
    const TitleGpuReadbackTicket &ticket)
{
    if (!ticket.valid() || ticket.session->destroying.load())
        return;
    obs_enter_graphics();
    {
        std::lock_guard<std::mutex> lock(ticket.session->mutex);
        for (auto &slot : ticket.session->readback_slots) {
            if (!slot.pending || slot.serial != ticket.serial)
                continue;
            slot.pending = false;
            slot.serial = 0;
            slot.region = QRect();
            break;
        }
    }
    obs_leave_graphics();
}

bool render_title_gpu_cache_submit_readback(
    const Title &title, double time, uint64_t model_revision,
    const std::string &cache_key, const QRect &region,
    TitleGpuReadbackTicket &ticket)
{
    /* This guard covers model preparation as well as graph submission. Legacy
     * compatibility raster helpers contain optional GPU surface passes that
     * map back into Cairo/QImage; the prerender path must never enter them.
     * The only permitted map is title_gpu_render_session_resolve_readback(),
     * after the completed unified frame has reached its staging slot. */
    ScopedGpuReadbackContract final_frame_only(
        GpuReadbackContract::FinalFrameOnly);
    const TitleDynamicLayerAnalysis analysis =
        analyze_title_dynamic_layers(title);
    if (analysis.has_dynamic_layers && !analysis.has_cacheable_prefix)
        return false;
    const std::size_t cache_end = analysis.has_dynamic_layers
        ? analysis.first_dynamic_layer : title.layers.size();
    const auto acquired = acquire_gpu_readback_session(model_revision);
    title_gpu_render_session_update_range(
        acquired.first, title, time, acquired.second, 0, cache_end);
    return title_gpu_render_session_submit_async_readback(
        acquired.first, cache_key, region, ticket);
}

QImage title_gpu_render_session_readback(TitleGpuRenderSession *session)
{
    if (!session || session->destroying.load())
        return QImage();
    QImage result;
    /* Match the graphics -> session ordering used by OBS display callbacks and
     * session destruction. The previous inverse order could deadlock a cache
     * worker against Preview/Program while one side waited for the graphics
     * lock and the other waited for this mutex. */
    obs_enter_graphics();
    std::unique_lock<std::mutex> lock(session->mutex);
    if (session->destroying.load()) {
        lock.unlock();
        obs_leave_graphics();
        return QImage();
    }
    gs_texture_t *texture = render_gpu_session_locked(session);
    const bool complete_current_frame = texture && !session->frame_dirty &&
        gpu_session_final_matches_model(session);
    if (!complete_current_frame) {
        BGL_LOG_DEBUG("Prerender", QStringLiteral(
            "stage=sync-readback action=reject-incomplete title=%1 revision=%2 publishedRevision=%3 dirty=%4 texture=%5")
            .arg(session->has_title
                     ? QString::fromStdString(session->title.id)
                     : QStringLiteral("<none>"))
            .arg(session->model_revision)
            .arg(session->published_model_revision)
            .arg(session->frame_dirty ? 1 : 0)
            .arg(texture ? 1 : 0));
        texture = nullptr;
    }
    if (texture) {
        if (!session->stage || session->stage_width != session->width ||
            session->stage_height != session->height) {
            if (session->stage)
                gs_stagesurface_destroy(session->stage);
            session->stage = gs_stagesurface_create(session->width, session->height, GS_BGRA);
            session->stage_width = session->stage ? session->width : 0;
            session->stage_height = session->stage ? session->height : 0;
        }
    }
    if (texture && session->stage) {
        gs_stage_texture(session->stage, texture);
        uint8_t *mapped = nullptr;
        uint32_t linesize = 0;
        if (gs_stagesurface_map(session->stage, &mapped, &linesize) && mapped &&
            linesize >= session->width * 4) {
            result = QImage((int)session->width, (int)session->height,
                            QImage::Format_ARGB32_Premultiplied);
            for (uint32_t y = 0; y < session->height; ++y)
                std::memcpy(result.scanLine((int)y), mapped + (size_t)y * linesize,
                            (size_t)session->width * 4);
            gs_stagesurface_unmap(session->stage);
        }
    }
    lock.unlock();
    obs_leave_graphics();
    return result;
}

QImage render_title_gpu_frame_readback(const Title &title, double time,
                                           uint64_t model_revision)
{
    const auto acquired = acquire_gpu_readback_session(model_revision);
    title_gpu_render_session_update(acquired.first, title, time,
                                    acquired.second);
    return title_gpu_render_session_readback(acquired.first);
}

static const TitleSourceData::SceneMaskConfig *scene_mask_config_for_layer(
    const TitleSourceData &data, const std::string &layer_id)
{
    for (const auto &cfg : data.scene_masks) {
        if (cfg.layer_id == layer_id)
            return &cfg;
    }
    return nullptr;
}

static obs_source_t *active_scene_mask_source_for_name(const TitleSourceData &data, const std::string &name)
{
    for (const auto &active : data.active_scene_mask_scenes) {
        if (active.name == name && active.source)
            return active.source;
    }
    return nullptr;
}

struct HiddenSceneMaskItem {
    obs_sceneitem_t *item = nullptr;
    bool visible = false;
};

struct HideSceneMaskSourceContext {
    obs_source_t *source = nullptr;
    std::vector<HiddenSceneMaskItem> hidden_items;
};

static bool hide_scene_mask_source_item(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
    auto *ctx = static_cast<HideSceneMaskSourceContext *>(param);
    if (!ctx || !ctx->source || !item)
        return true;

    if (obs_sceneitem_is_group(item))
        obs_sceneitem_group_enum_items(item, hide_scene_mask_source_item, param);

    obs_source_t *item_source = obs_sceneitem_get_source(item);
    if (item_source == ctx->source && obs_sceneitem_visible(item)) {
        obs_sceneitem_addref(item);
        ctx->hidden_items.push_back({item, true});
        obs_sceneitem_set_visible(item, false);
    }

    return true;
}

static HideSceneMaskSourceContext hide_scene_mask_source_items(obs_source_t *scene_source, obs_source_t *source)
{
    HideSceneMaskSourceContext ctx;
    ctx.source = source;
    if (!scene_source || !source)
        return ctx;

    if (obs_scene_t *scene = obs_scene_from_source(scene_source))
        obs_scene_enum_items(scene, hide_scene_mask_source_item, &ctx);

    return ctx;
}

static void restore_scene_mask_source_items(HideSceneMaskSourceContext &ctx)
{
    for (auto &hidden : ctx.hidden_items) {
        if (hidden.item) {
            obs_sceneitem_set_visible(hidden.item, hidden.visible);
            obs_sceneitem_release(hidden.item);
        }
    }
    ctx.hidden_items.clear();
}

static constexpr const char *kSceneMaskEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform texture2d mask;

sampler_state textureSampler {
    Filter   = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};

struct VertDataIn {
    float4 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

struct VertDataOut {
    float4 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

VertDataOut VSDefault(VertDataIn v_in)
{
    VertDataOut vert_out;
    vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    vert_out.uv = v_in.uv;
    return vert_out;
}

float4 PSMaskedScene(VertDataOut v_in) : TARGET
{
    float4 rgba = image.Sample(textureSampler, v_in.uv);
    float mask_alpha = mask.Sample(textureSampler, v_in.uv).a;
    rgba.rgb *= (rgba.a > 0.0) ? (1.0 / rgba.a) : 0.0;
    rgba.a *= mask_alpha;
    rgba.rgb *= rgba.a;
    return rgba;
}

technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v_in);
        pixel_shader = PSMaskedScene(v_in);
    }
}
)";

static gs_effect_t *scene_mask_effect_for_data(TitleSourceData *data,
                                                TitleGpuRenderSession *session = nullptr)
{
    if (!data)
        return nullptr;
    if (data->scene_mask_effect)
        return data->scene_mask_effect;

    if (session) {
        ensure_session_shader_effect(session, data->scene_mask_effect,
                                     "core:scene-mask", "Scene mask matte",
                                     kSceneMaskEffect, "obs-bgs-scene-mask.effect",
                                     &data->scene_mask_effect);
        return nullptr;
    }

    data->scene_mask_effect =
        gs_effect_create(kSceneMaskEffect, "obs-bgs-scene-mask.effect", nullptr);
    return data->scene_mask_effect;
}

static bool draw_gpu_layer_range_over_current_target(
    TitleGpuRenderSession *session, std::size_t first_layer,
    std::size_t last_layer, uint32_t output_width, uint32_t output_height)
{
    if (!session || session->destroying.load())
        return false;

    gs_texture_t *current_target = gs_get_render_target();
    if (!current_target)
        return false;
    const uint32_t background_width = gs_texture_get_width(current_target);
    const uint32_t background_height = gs_texture_get_height(current_target);
    const enum gs_color_format background_format =
        gs_texture_get_color_format(current_target);
    if (background_width == 0 || background_height == 0 ||
        background_format == GS_UNKNOWN)
        return false;

    struct matrix4 local_to_background;
    gs_matrix_get(&local_to_background);

    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load() ||
        !prepare_external_composite_dimensions_locked(session))
        return false;
    if (!ensure_external_background_snapshot(
            session, background_width, background_height,
            background_format))
        return false;

    gs_copy_texture(session->external_background_snapshot, current_target);
    gs_texture_t *local_background = nullptr;
    {
        ScopedGpuCompositorState state;
        local_background = render_external_background_local_locked(
            session, session->external_background_snapshot,
            background_width, background_height,
            ExternalBackgroundMapping::CurrentTransform,
            &local_to_background, 0.0f, 0.0f,
            static_cast<float>(background_width),
            static_cast<float>(background_height),
            output_width, output_height);
    }
    if (!local_background)
        return false;

    const std::size_t saved_first = session->first_layer;
    const std::size_t saved_last = session->last_layer;
    session->first_layer = std::min(first_layer, session->title.layers.size());
    session->last_layer = std::max(
        session->first_layer,
        std::min(last_layer, session->title.layers.size()));
    gs_texture_t *texture = render_gpu_session_locked(session, local_background);
    session->first_layer = saved_first;
    session->last_layer = saved_last;
    return present_gpu_session_texture_locked(
        session, texture, output_width, output_height, true);
}

static bool scene_mask_layer_has_backdrop_effects(const Layer &layer,
                                                  double layer_time)
{
    const double local_time = std::max(0.0, layer_time - layer.in_time);
    return std::any_of(
        layer.effects.begin(), layer.effects.end(),
        [local_time](const LayerEffect &effect) {
            return effect.affect_layers_behind &&
                   eval_effect_enabled(effect, local_time);
        });
}

/* Scene-mask layers are inserted after the ordinary GPU title pass, directly
 * over the current OBS destination. Their affect-layers-behind effects must
 * therefore process the framebuffer that already contains every lower title
 * layer and every lower scene-mask scene. Treating those effects as ordinary
 * foreground effects either skipped them or applied them only to the selected
 * scene texture, so Blur/Color/Glow could no longer affect lower scenes.
 *
 * Snapshot and map the current destination into title-local coordinates, run
 * each backdrop effect independently, bound it by the untouched scene mask,
 * and replace the destination before the scene artwork itself is composited.
 * This mirrors the root/group bounded-adjustment contract. Failure is
 * deliberately fail-open: the scene remains visible even if an optional
 * backdrop shader is unavailable. */
static bool apply_scene_mask_backdrop_effects_over_current_target(
    TitleSourceData *data, const Title &title, const Layer &layer,
    double title_time, gs_texture_t *coverage,
    uint32_t output_width, uint32_t output_height)
{
    if (!data || !data->gpu_render_session || !coverage)
        return false;

    TitleGpuRenderSession *session = data->gpu_render_session;
    const double layer_time = gpu_layer_render_time(title, layer, title_time);
    if (!scene_mask_layer_has_backdrop_effects(layer, layer_time))
        return true;

    gs_texture_t *current_target = gs_get_render_target();
    if (!current_target)
        return false;
    const uint32_t background_width = gs_texture_get_width(current_target);
    const uint32_t background_height = gs_texture_get_height(current_target);
    const enum gs_color_format background_format =
        gs_texture_get_color_format(current_target);
    if (background_width == 0 || background_height == 0 ||
        background_format == GS_UNKNOWN)
        return false;

    struct matrix4 local_to_background;
    gs_matrix_get(&local_to_background);

    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load() ||
        !prepare_external_composite_dimensions_locked(session) ||
        !ensure_external_background_snapshot(
            session, background_width, background_height,
            background_format))
        return false;

    gs_copy_texture(session->external_background_snapshot, current_target);
    gs_texture_t *frame = nullptr;
    {
        ScopedGpuCompositorState state;
        frame = render_external_background_local_locked(
            session, session->external_background_snapshot,
            background_width, background_height,
            ExternalBackgroundMapping::CurrentTransform,
            &local_to_background, 0.0f, 0.0f,
            static_cast<float>(background_width),
            static_cast<float>(background_height),
            output_width, output_height);
    }
    if (!frame)
        return false;

    bool next_is_a = true;
    bool changed = false;
    const double local_time = std::max(0.0, layer_time - layer.in_time);
    for (const LayerEffect &backdrop_effect : layer.effects) {
        if (!backdrop_effect.affect_layers_behind ||
            !eval_effect_enabled(backdrop_effect, local_time))
            continue;

        Layer adjustment_layer = layer;
        adjustment_layer.effects.clear();
        adjustment_layer.effects.push_back(backdrop_effect);
        gs_texture_t *adjusted = apply_gpu_layer_effect_stack(
            session, adjustment_layer, layer_time, frame,
            session->width, session->height, nullptr, true, frame, true);
        if (!adjusted || adjusted == frame)
            continue;

        gs_texrender_t *target = next_is_a
            ? session->frame_a : session->frame_b;
        gs_texture_t *mixed = mix_gpu_adjustment_layer(
            session, title, adjustment_layer, title_time,
            frame, adjusted, coverage, target, true);
        if (mixed && mixed != frame) {
            frame = mixed;
            next_is_a = !next_is_a;
            changed = true;
        }
    }

    return !changed || present_gpu_session_texture_locked(
        session, frame, output_width, output_height, true);
}


static void render_scene_mask_stroke_over_current_target(
    TitleSourceData *data, const Title &title, const Layer &mask_layer,
    double title_time, bool apply_chain_opacity = true)
{
    if (!data || !data->gpu_render_session)
        return;
    const double layer_time = gpu_layer_render_time(title, mask_layer, title_time);
    const double local_time = std::max(0.0, layer_time - mask_layer.in_time);
    const bool has_outline = eval_outline_width(mask_layer, local_time) > 0.0 &&
        (mask_layer.stroke_fill_type == 2 ||
         ((eval_outline_color(mask_layer, local_time) >> 24) & 0xFF) > 0);
    const bool rich_text_like = mask_layer.type == LayerType::Text ||
        mask_layer.type == LayerType::Clock || mask_layer.type == LayerType::Ticker;
    const bool has_rich_stroke = rich_text_like &&
        max_rich_text_stroke_width(mask_layer, local_time) > 0.0;
    const bool has_background_stroke = eval_background_stroke_width(mask_layer, local_time) > 0.0;
    if (!has_outline && !has_rich_stroke && !has_background_stroke)
        return;

    Layer stroke_layer = scene_mask_stroke_composite_layer(mask_layer);
    MotionBaseRaster raster = render_gpu_layer_base_raster(
        title, stroke_layer, layer_time, 1.0,
        false, bgl::video::VideoDecodeClient::LiveOutput);
    if (raster.image.isNull())
        return;

    TitleGpuRenderSession::LayerRaster entry;
    entry.pending_image = raster.image;
    entry.origin = raster.origin;
    entry.logical_width = raster.logical_width > 0.0
        ? raster.logical_width : raster.image.width();
    entry.logical_height = raster.logical_height > 0.0
        ? raster.logical_height : raster.image.height();
    entry.base_box_width = std::max(0.0001, eval_box_width(stroke_layer, local_time));
    entry.base_box_height = std::max(0.0001, eval_box_height(stroke_layer, local_time));
    entry.layer_box_rect = raster.layer_box_rect;
    entry.image_clip_rect = raster.image_clip_rect;
    entry.pending_upload = true;

    if (!upload_gpu_layer_raster(data->gpu_render_session, entry) || !entry.texture)
        return;

    const float opacity = apply_chain_opacity
        ? static_cast<float>(std::clamp(
              gpu_layer_chain_opacity(title, mask_layer, title_time), 0.0, 1.0))
        : 1.0f;
    gs_blend_state_push();
    gs_enable_blending(true);
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
    draw_gpu_layer_texture(data->gpu_render_session, &entry, title, stroke_layer,
                           entry.texture, entry.origin, entry.width, entry.height,
                           entry.logical_width, entry.logical_height,
                           entry.image_clip_rect, entry.base_box_width,
                           entry.base_box_height, title_time, opacity);
    gs_blend_state_pop();
    if (entry.texture)
        gs_texture_destroy(entry.texture);
    if (entry.transform_grid)
        gs_vertexbuffer_destroy(entry.transform_grid);
}

static void render_scene_masks_gpu(TitleSourceData *data, const Title &title, double title_time)
{
    if (!data || data->scene_masks.empty() || data->active_scene_mask_scenes.empty())
        return;

    if (!data->scene_mask_scene_texrender)
        data->scene_mask_scene_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    if (!data->scene_mask_matted_texrender)
        data->scene_mask_matted_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    if (!data->scene_mask_scene_texrender || !data->scene_mask_matted_texrender ||
        !scene_mask_effect_for_data(data, data->gpu_render_session))
        return;

    for (std::size_t layer_index = 0; layer_index < title.layers.size(); ++layer_index) {
        const auto &layer_ptr = title.layers[layer_index];
        const Layer *layer = layer_ptr.get();
        if (!layer || !layer->use_as_scene_mask || !layer_type_can_be_scene_mask(layer->type) ||
            !gpu_layer_chain_visible(title, *layer, title_time))
            continue;
        const auto *cfg = scene_mask_config_for_layer(*data, layer->id);
        if (!cfg || cfg->scene_name.empty())
            continue;

        obs_source_t *scene = active_scene_mask_source_for_name(*data, cfg->scene_name);
        if (!scene)
            continue;

        const double lt = std::max(0.0, title_time - layer->in_time);
        const double w = eval_box_width(*layer, lt);
        const double h = eval_box_height(*layer, lt);
        if (w <= 0.0 || h <= 0.0) {
            continue;
        }
        const double zoom = std::clamp(cfg->zoom, 0.01, 100.0);

        gs_texture_t *mask_texture =
            title_gpu_render_session_render_auxiliary_layer(
                data->gpu_render_session, layer->id, title_time);
        if (!mask_texture)
            continue;

        /* Backdrop effects belong to the already-composited lower stack. Run
         * them before drawing this scene's foreground, bounded by the original
         * scene-mask silhouette. A failure intentionally leaves the lower
         * stack unchanged and continues with the scene artwork. */
        apply_scene_mask_backdrop_effects_over_current_target(
            data, title, *layer, title_time, mask_texture,
            data->tex_w, data->tex_h);

        gs_viewport_push();
        gs_projection_push();
        gs_blend_state_push();
        struct vec4 clear;
        vec4_zero(&clear);
        if (!begin_gpu_target(data->scene_mask_scene_texrender,
                              data->tex_w, data->tex_h, clear)) {
            gs_blend_state_pop();
            gs_projection_pop();
            gs_viewport_pop();
            continue;
        }
        gs_enable_blending(true);
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

        gs_matrix_push();
        gs_matrix_identity();
        if (cfg->move_with_mask) {
            apply_layer_world_transform_gs(title, *layer, title_time);
            gs_matrix_translate3f((float)(-eval_origin_x(*layer, lt) * w + cfg->x),
                                  (float)(-eval_origin_y(*layer, lt) * h + cfg->y), 0.0f);
        } else {
            gs_matrix_translate3f((float)cfg->x, (float)cfg->y, 0.0f);
        }
        gs_matrix_scale3f((float)zoom, (float)zoom, 1.0f);
        auto hidden_items = hide_scene_mask_source_items(scene, data->source);
        obs_source_video_render(scene);
        restore_scene_mask_source_items(hidden_items);
        gs_matrix_pop();

        gs_texrender_end(data->scene_mask_scene_texrender);
        gs_blend_state_pop();
        gs_projection_pop();
        gs_viewport_pop();

        gs_texture_t *scene_texture = gs_texrender_get_texture(data->scene_mask_scene_texrender);
        gs_effect_t *effect = scene_mask_effect_for_data(data, data->gpu_render_session);
        if (scene_texture && effect) {
            gs_eparam_t *image = bgl_effect_param(effect, "image");
            gs_eparam_t *mask = bgl_effect_param(effect, "mask");
            if (image && mask) {
                gs_viewport_push();
                gs_projection_push();
                struct vec4 transparent;
                vec4_zero(&transparent);
                if (!begin_gpu_target(data->scene_mask_matted_texrender,
                                      data->tex_w, data->tex_h,
                                      transparent)) {
                    gs_projection_pop();
                    gs_viewport_pop();
                    continue;
                }
                gs_matrix_push();
                gs_matrix_identity();
                gs_effect_set_texture(image, scene_texture);
                gs_effect_set_texture(mask, mask_texture);
                gs_enable_blending(false);
                while (gs_effect_loop(effect, "Draw"))
                    gs_draw_sprite(scene_texture, 0, data->tex_w, data->tex_h);
                gs_matrix_pop();

                /* Composite the mask object's stroke into the matted scene before
                 * the layer effect stack runs. This keeps effects visually uniform
                 * across the filled scene artwork and the authored stroke. Opacity
                 * is applied later to the combined texture, so avoid applying the
                 * layer chain opacity twice here. */
                render_scene_mask_stroke_over_current_target(
                    data, title, *layer, title_time, false);

                gs_texrender_end(data->scene_mask_matted_texrender);
                gs_projection_pop();
                gs_viewport_pop();

                gs_texture_t *composited = gs_texrender_get_texture(data->scene_mask_matted_texrender);
                const double layer_time = gpu_layer_render_time(title, *layer, title_time);
                if (composited && !layer->effects.empty())
                    composited = apply_gpu_layer_effect_stack(
                        data->gpu_render_session, *layer, layer_time, composited,
                        data->tex_w, data->tex_h, nullptr, false, nullptr, true);
                if (!composited)
                    continue;

                gs_effect_t *present = obs_get_base_effect(OBS_EFFECT_DEFAULT);
                gs_eparam_t *present_image = present
                    ? bgl_effect_param(present, "image") : nullptr;
                if (!present || !present_image)
                    continue;
                gs_effect_set_texture(present_image, composited);
                gs_blend_state_push();
                gs_enable_blending(true);
                gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
                const float opacity = static_cast<float>(std::clamp(
                    gpu_layer_chain_opacity(title, *layer, title_time), 0.0, 1.0));
                struct vec4 tint;
                vec4_set(&tint, opacity, opacity, opacity, opacity);
                gs_eparam_t *color = bgl_effect_param(present, "color");
                if (color) gs_effect_set_vec4(color, &tint);
                gs_matrix_push();
                gs_matrix_identity();
                while (gs_effect_loop(present, "Draw"))
                    gs_draw_sprite(composited, 0, data->tex_w, data->tex_h);
                gs_matrix_pop();
                gs_blend_state_pop();

                /* Composite the ordinary layers between this scene mask and
                 * the next scene mask. This keeps each scene at its real stack
                 * position without drawing upper layers repeatedly. */
                std::size_t next_scene_mask = title.layers.size();
                for (std::size_t next = layer_index + 1; next < title.layers.size(); ++next) {
                    if (title.layers[next] && title.layers[next]->use_as_scene_mask &&
                        layer_type_can_be_scene_mask(title.layers[next]->type) &&
                        gpu_layer_chain_visible(title, *title.layers[next], title_time)) {
                        next_scene_mask = next;
                        break;
                    }
                }
                if (layer_index + 1 < next_scene_mask) {
                    draw_gpu_layer_range_over_current_target(
                        data->gpu_render_session, layer_index + 1,
                        next_scene_mask, data->tex_w, data->tex_h);
                }
            }
        }
    }
}

static void source_video_render(void *priv, gs_effect_t * /*effect*/)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    if (!data)
        return;

    const uint64_t source_render_serial = ++data->diagnostic_render_serial;
    const auto render_now = std::chrono::steady_clock::now();
    auto record_skipped_render = [&](const char *reason) {
        ++data->diagnostic_skipped_render_count;
        BGL_LOG_DEBUG("SourcePresentation", QStringLiteral(
            "event=render-skipped source=%1 title=%2 render=%3 reason=%4 playhead=%5 dirty=%6 firstPending=%7 shown=%8 outputVisible=%9 requestedGeneration=%10 appliedGeneration=%11")
            .arg(data->source ? QString::fromUtf8(obs_source_get_name(data->source)) : QStringLiteral("<null>"))
            .arg(QString::fromStdString(data->title_id))
            .arg(source_render_serial)
            .arg(QString::fromUtf8(reason))
            .arg(data->playhead, 0, 'f', 6)
            .arg(data->dirty)
            .arg(data->first_frame_pending)
            .arg(data->shown_on_display.load(std::memory_order_acquire))
            .arg(data->output_visible.load(std::memory_order_acquire))
            .arg(data->requested_presentation_generation.load(std::memory_order_acquire))
            .arg(data->applied_presentation_generation.load(std::memory_order_acquire)));
        if (data->diagnostic_last_render_succeeded &&
            (!data->diagnostic_last_flicker_warning.time_since_epoch().count() ||
             render_now - data->diagnostic_last_flicker_warning >= std::chrono::milliseconds(250))) {
            BGL_LOG_WARNING("SourceFlicker", QStringLiteral(
                "event=visible-frame-gap source=%1 title=%2 render=%3 reason=%4 playhead=%5 previousSuccessfulRender=%6 skippedTotal=%7")
                .arg(data->source ? QString::fromUtf8(obs_source_get_name(data->source)) : QStringLiteral("<null>"))
                .arg(QString::fromStdString(data->title_id))
                .arg(source_render_serial)
                .arg(QString::fromUtf8(reason))
                .arg(data->playhead, 0, 'f', 6)
                .arg(data->diagnostic_successful_render_serial)
                .arg(data->diagnostic_skipped_render_count));
            data->diagnostic_last_flicker_warning = render_now;
        }
        data->diagnostic_last_render_was_skipped = true;
    };

    if (!data->shown_on_display.load(std::memory_order_acquire)) {
        record_skipped_render("source-hidden");
        return;
    }
    if (!data->output_visible.load(std::memory_order_acquire)) {
        record_skipped_render("output-hidden");
        return;
    }
    if (!data->gpu_render_session) {
        record_skipped_render("missing-gpu-session");
        return;
    }
    if (g_source_scene_collection_transition.load(std::memory_order_acquire)) {
        record_skipped_render("scene-collection-transition");
        return;
    }
    /* Frontend/source callbacks may invalidate a presentation between video
     * ticks. Never draw the previous generation while waiting for the tick to
     * rebuild the current title graph. */
    if (!source_presentation_generation_is_current(data)) {
        record_skipped_render("presentation-generation-mismatch");
        return;
    }

    auto title = data->editor_transport_controlled
        ? editor_title_snapshot_for_source(data)
        : TitleDataStore::instance().get_title(data->title_id);
    if (!title) {
        record_skipped_render("title-missing");
        return;
    }

    const uint32_t width = clamped_source_dimension(title->width);
    const uint32_t height = clamped_source_dimension(title->height);
    bool rendered = false;
    std::size_t first_scene_mask = title->layers.size();
    for (std::size_t i = 0; i < title->layers.size(); ++i) {
        if (title->layers[i] && title->layers[i]->use_as_scene_mask &&
            layer_type_can_be_scene_mask(title->layers[i]->type) &&
            gpu_layer_chain_visible(*title, *title->layers[i], data->playhead)) {
            first_scene_mask = i;
            break;
        }
    }
    if (first_scene_mask < title->layers.size()) {
        /* Render only the ordinary stack below the first scene mask. The scene
         * masks and the ranges between them are then inserted sequentially by
         * render_scene_masks_gpu(), preserving the real layer order. */
        rendered = first_scene_mask == 0 || draw_gpu_layer_range_over_current_target(
            data->gpu_render_session, 0, first_scene_mask, width, height);
    } else {
        rendered = title_requires_destination_compositing(*title)
            ? title_gpu_render_session_draw_over_current_target(
                  data->gpu_render_session, width, height)
            : title_gpu_render_session_draw(data->gpu_render_session,
                                            width, height);
    }
    const std::string gpu_error = rendered
        ? std::string()
        : title_gpu_render_session_last_error(data->gpu_render_session);
    TitlePreferences::set_gpu_available(
        rendered, rendered ? nullptr
                           : (gpu_error.empty()
                                  ? "Unified GPU compositor could not render the title."
                                  : gpu_error.c_str()));
    if (!rendered) {
        BGL_LOG_WARNING("GpuPipeline", QStringLiteral(
            "consumer=source action=draw-failed title=%1 failures=%2 error=%3")
            .arg(QString::fromStdString(title->id))
            .arg(data->consecutive_draw_failures + 1)
            .arg(gpu_error.empty()
                     ? QStringLiteral("unknown GPU compositor error")
                     : QString::fromStdString(gpu_error)));
        ++data->consecutive_draw_failures;
        data->dirty = true;
        data->first_frame_pending = true;
        /* Reconcile the full model/cache contract on the next tick rather than
         * waiting for an unrelated cue or edit to bump a revision. */
        if (data->consecutive_draw_failures >= 2) {
            data->seen_store_revision = std::numeric_limits<uint64_t>::max();
            data->seen_external_data_revision =
                std::numeric_limits<uint64_t>::max();
            data->cache_hash_revision = std::numeric_limits<uint64_t>::max();
            data->cached_content_hash.clear();
        }
        return;
    }

    data->consecutive_draw_failures = 0;
    data->first_frame_pending = false;
    data->dirty = false;
    data->diagnostic_last_render_succeeded = true;
    data->diagnostic_last_render_was_skipped = false;
    data->diagnostic_successful_render_serial = source_render_serial;
    data->diagnostic_last_render_time = render_now;

    TitleGpuRenderDiagnostics source_gpu_diagnostics;
    const bool have_source_gpu_diagnostics =
        title_gpu_render_session_get_diagnostics(data->gpu_render_session,
                                                 source_gpu_diagnostics);
    BGL_LOG_DEBUG("SourcePresentation", QStringLiteral(
        "event=render-presented source=%1 title=%2 render=%3 playhead=%4 size=%5x%6 dirty=%7 firstPending=%8 gpuDiagnostics=%9 gpuSessionTime=%10 lastPublishedTime=%11 updateSerial=%12 gpuRenderSerial=%13 publishSerial=%14 modelRevision=%15 publishedRevision=%16 frameDirty=%17 deferred=%18 hasFrame=%19 cachedFinal=%20 baseFrame=%21 rasters=%22 pendingRasters=%23")
        .arg(data->source ? QString::fromUtf8(obs_source_get_name(data->source)) : QStringLiteral("<null>"))
        .arg(QString::fromStdString(title->id))
        .arg(source_render_serial)
        .arg(data->playhead, 0, 'f', 6)
        .arg(width).arg(height)
        .arg(data->dirty).arg(data->first_frame_pending)
        .arg(have_source_gpu_diagnostics)
        .arg(source_gpu_diagnostics.session_time, 0, 'f', 6)
        .arg(source_gpu_diagnostics.last_published_time, 0, 'f', 6)
        .arg(source_gpu_diagnostics.update_serial)
        .arg(source_gpu_diagnostics.render_serial)
        .arg(source_gpu_diagnostics.publish_serial)
        .arg(source_gpu_diagnostics.model_revision)
        .arg(source_gpu_diagnostics.published_model_revision)
        .arg(source_gpu_diagnostics.frame_dirty)
        .arg(source_gpu_diagnostics.last_draw_deferred)
        .arg(source_gpu_diagnostics.has_published_frame)
        .arg(source_gpu_diagnostics.use_gpu_cached_final)
        .arg(source_gpu_diagnostics.use_base_frame)
        .arg(source_gpu_diagnostics.raster_count)
        .arg(source_gpu_diagnostics.pending_raster_count));
    if (have_source_gpu_diagnostics &&
        (!source_gpu_diagnostics.has_published_frame ||
         source_gpu_diagnostics.last_draw_deferred ||
         source_gpu_diagnostics.published_model_revision !=
             source_gpu_diagnostics.model_revision)) {
        BGL_LOG_WARNING("SourceFlicker", QStringLiteral(
            "event=frame-consistency-anomaly source=%1 title=%2 render=%3 playhead=%4 hasFrame=%5 deferred=%6 modelRevision=%7 publishedRevision=%8 frameDirty=%9 publishSerial=%10")
            .arg(data->source ? QString::fromUtf8(obs_source_get_name(data->source)) : QStringLiteral("<null>"))
            .arg(QString::fromStdString(title->id))
            .arg(source_render_serial)
            .arg(data->playhead, 0, 'f', 6)
            .arg(source_gpu_diagnostics.has_published_frame)
            .arg(source_gpu_diagnostics.last_draw_deferred)
            .arg(source_gpu_diagnostics.model_revision)
            .arg(source_gpu_diagnostics.published_model_revision)
            .arg(source_gpu_diagnostics.frame_dirty)
            .arg(source_gpu_diagnostics.publish_serial));
    }
    render_scene_masks_gpu(data, *title, data->playhead);
}


static void add_scene_list_items(obs_property_t *property)
{
    obs_property_list_add_string(property, "", "");
    obs_frontend_source_list scenes = {};
    obs_frontend_get_scenes(&scenes);
    for (size_t i = 0; i < scenes.sources.num; ++i) {
        obs_source_t *scene = scenes.sources.array[i];
        const char *name = obs_source_get_name(scene);
        if (name && *name)
            obs_property_list_add_string(property, name, name);
    }
    obs_frontend_source_list_free(&scenes);
}

static void add_scene_mask_properties_for_title(obs_properties_t *props, const std::string &title_id)
{
    obs_properties_remove_by_name(props, PROP_SCENE_MASKS_GROUP);

    auto title = TitleDataStore::instance().get_title(title_id);
    auto masks = scene_mask_layers(title);
    if (masks.empty())
        return;

    obs_properties_t *group = obs_properties_create();
    for (const auto &layer : masks) {
        if (!layer) continue;
        obs_properties_t *layer_group = obs_properties_create();
        obs_property_t *scene = obs_properties_add_list(
            layer_group, scene_mask_key(layer->id, "scene").c_str(), "Target OBS scene",
            OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        add_scene_list_items(scene);
        obs_properties_add_float_slider(layer_group, scene_mask_key(layer->id, "zoom_percent").c_str(),
            "Scene Zoom Level (%)", 1.0, 800.0, 1.0);
        obs_properties_add_float_slider(layer_group, scene_mask_key(layer->id, "x").c_str(),
            "Horizontal Position", -9999.0, 9999.0, 1.0);
        obs_properties_add_float_slider(layer_group, scene_mask_key(layer->id, "y").c_str(),
            "Vertical Position", -9999.0, 9999.0, 1.0);
        obs_properties_add_bool(layer_group, scene_mask_key(layer->id, "move_with_mask").c_str(),
            "Move Scene With Mask");
        obs_properties_add_group(group, (std::string("scene_mask_group_") + layer->id).c_str(),
            layer->name.c_str(), OBS_GROUP_NORMAL, layer_group);
    }
    obs_properties_add_group(props, PROP_SCENE_MASKS_GROUP, "Scene Masks", OBS_GROUP_NORMAL, group);
}

static bool source_title_property_modified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
    const std::string title_id = obs_data_get_string(settings, PROP_TITLE_ID);
    set_scene_mask_property_defaults(settings, title_id);
    add_scene_mask_properties_for_title(props, title_id);
    return true;
}

/* ── Properties panel ─────────────────────────────────────────────── */
static obs_properties_t *source_get_properties(void *priv)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    obs_properties_t *props = obs_properties_create();

    /* Title selector */
    obs_property_t *p = obs_properties_add_list(
        props, PROP_TITLE_ID, bgl_tr_c("OBSTitles.TitleID"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

    obs_property_list_add_string(p, bgl_tr_c("OBSTitles.NoTitle"), "");
    for (auto &t : TitleDataStore::instance().titles()) {
        if (!t || t->is_asset)
            continue;
        obs_property_list_add_string(p, t->name.c_str(), t->id.c_str());
    }
    obs_property_set_modified_callback(p, source_title_property_modified);

    obs_properties_add_bool(
        props, PROP_CUE_FIRST_ROW_WHEN_ACTIVE,
        bgl_tr_c("OBSTitles.CueFirstRowWhenActive"));

    add_scene_mask_properties_for_title(props, data ? data->title_id : std::string());

    return props;
}

static void source_get_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, PROP_TITLE_ID, "");
    obs_data_set_default_bool(settings, PROP_CUE_FIRST_ROW_WHEN_ACTIVE, false);
}

static void sync_media_transport_cue_state(TitleSourceData *data, bool cue)
{
    if (!data)
        return;

    auto title = data->editor_transport_controlled
        ? editor_title_snapshot_for_source(data)
        : TitleDataStore::instance().get_title(data->title_id);
    if (!title)
        return;

    const auto exposed = exposed_text_layers(title);
    if (cue) {
        if (!exposed.empty()) {
            bgs::live_text::normalize_live_text_rows(title, exposed);
            if (!title->live_text_rows.empty()) {
                int row = title->current_cue_row;
                if (row < 0 || row >= static_cast<int>(title->live_text_rows.size()))
                    row = 0;
                apply_live_text_row(title, row);
                title->current_cue_row = row;
            }
        }
        title->pending_cue_row = -1;
        title->cue_uncue_requested = false;
        title->cue_persistence_transition = false;
        title->cue_persistent_text_columns.clear();
        data->waiting_for_cue = false;
    } else {
        title->pending_cue_row = -1;
        title->cue_uncue_requested = false;
        title->current_cue_row = -1;
        title->cue_persistence_transition = false;
        title->cue_persistent_text_columns.clear();
        data->active_cue_row = -1;
        data->waiting_for_cue = true;
    }

    ++title->cue_revision;
    TitleDataStore::instance().touch_runtime_change();
    data->force_cue_state_sync = true;
    data->dirty = true;
    data->first_frame_pending = true;
}

static void source_media_play_pause(void *priv, bool pause)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    if (!data) return;
    sync_editor_transport_override(data);
    const bool was_playing = data->playing;
    data->playing = !pause;
    data->playback_state.store(
        data->preview_ready.load(std::memory_order_acquire)
            ? TitlePlaybackState::PreviewReady
                            : (pause ? TitlePlaybackState::Paused
                                     : TitlePlaybackState::Playing),
        std::memory_order_release);
    if (!data->editor_audio_preview && !pause)
        sync_media_transport_cue_state(data, true);
    else if (data->editor_audio_preview)
        data->waiting_for_cue = false;
    if (data->audio_runtime)
        data->audio_runtime->transport(data->playhead, data->playing, true,
                                       was_playing != data->playing,
                                       data->editor_transport_controlled
                                           ? data->editor_playback_reverse
                                           : data->playback_reverse);
}

static void source_media_restart(void *priv)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    if (!data) return;
    sync_editor_transport_override(data);
    data->playhead = 0.0;
    data->playback_reverse = false;
    if (!data->editor_transport_controlled)
        data->editor_playback_reverse = false;
    data->playing = true;
    data->preview_ready.store(false, std::memory_order_release);
    data->playback_state.store(TitlePlaybackState::Playing,
                               std::memory_order_release);
    if (!data->editor_audio_preview)
        sync_media_transport_cue_state(data, true);
    else
        data->waiting_for_cue = false;
    data->dirty = true;
    if (data->audio_runtime)
        data->audio_runtime->transport(
            0.0, true, true, true,
            data->editor_transport_controlled
                ? data->editor_playback_reverse
                : data->playback_reverse);
}

static void source_media_stop(void *priv)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    if (!data) return;
    data->playing = false;
    data->preview_ready.store(false, std::memory_order_release);
    data->playback_state.store(TitlePlaybackState::Stopped,
                               std::memory_order_release);
    data->playhead = 0.0;
    if (!data->editor_audio_preview)
        sync_media_transport_cue_state(data, false);
    else
        data->waiting_for_cue = true;
    data->dirty = true;
    if (data->audio_runtime) data->audio_runtime->stop(true);
}

static int64_t source_media_get_duration(void *priv)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    if (!data) return 0;
    if (data->audio_runtime) return data->audio_runtime->duration_ms();
    auto title = data->editor_transport_controlled
        ? editor_title_snapshot_for_source(data)
        : TitleDataStore::instance().get_title(data->title_id);
    return title ? static_cast<int64_t>(std::llround(title->duration * 1000.0)) : 0;
}

static int64_t source_media_get_time(void *priv)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    return data && data->audio_runtime
        ? data->audio_runtime->time_ms()
        : (data ? static_cast<int64_t>(std::llround(data->playhead * 1000.0)) : 0);
}

static void source_media_set_time(void *priv, int64_t milliseconds)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    if (!data) return;
    sync_editor_transport_override(data);
    auto title = data->editor_transport_controlled
        ? editor_title_snapshot_for_source(data)
        : TitleDataStore::instance().get_title(data->title_id);
    const double duration = title ? std::max(0.0, title->duration) : 0.0;
    data->playhead = std::clamp(milliseconds / 1000.0, 0.0, duration);
    data->dirty = true;
    data->first_frame_pending = true;
    if (data->audio_runtime)
        data->audio_runtime->transport(
            data->playhead, data->playing, true, true,
            data->editor_transport_controlled
                ? data->editor_playback_reverse
                : data->playback_reverse);
}

static enum obs_media_state source_media_get_state(void *priv)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    return data && data->audio_runtime
        ? data->audio_runtime->media_state()
        : OBS_MEDIA_STATE_STOPPED;
}

/* ══════════════════════════════════════════════════════════════════
 *  Registration
 * ══════════════════════════════════════════════════════════════════ */
void title_source_register()
{
    static obs_source_info si = {};
    si.id             = "broadcast_graphics_live_source";
    si.type           = OBS_SOURCE_TYPE_INPUT;
    si.output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO |
                        OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_CONTROLLABLE_MEDIA;
    si.get_name       = source_get_name;
    si.create         = source_create;
    si.destroy        = source_destroy;
    si.update         = source_update;
    si.get_width      = source_get_width;
    si.get_height     = source_get_height;
    si.video_tick     = source_video_tick;
    si.video_render   = source_video_render;
    si.activate       = source_activate;
    si.deactivate     = source_deactivate;
    si.show           = source_show;
    si.hide           = source_hide;
    si.get_properties = source_get_properties;
    si.get_defaults   = source_get_defaults;
    si.media_play_pause = source_media_play_pause;
    si.media_restart    = source_media_restart;
    si.media_stop       = source_media_stop;
    si.media_get_duration = source_media_get_duration;
    si.media_get_time     = source_media_get_time;
    si.media_set_time     = source_media_set_time;
    si.media_get_state    = source_media_get_state;

    obs_register_source(&si);
    blog(LOG_INFO, "[Broadcast Graphics Live] Source type registered.");
}
