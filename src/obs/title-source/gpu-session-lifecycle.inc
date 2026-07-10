static bool layer_should_render_as_visible_content_for_gpu_session(
    const TitleGpuRenderSession *session, const Title &title, const Layer &layer)
{
    /* Scene-mask layers stay matte controls in OBS output, but the editor
     * previews them as real placeholder artwork so their own effects, masks
     * and blend modes can be evaluated. This mirrors the A/B TransitionInput
     * placeholder contract for Stingers without leaking the placeholder to
     * live output. */
    if (session && session->scene_mask_placeholder_preview_enabled &&
        layer.use_as_scene_mask)
        return true;
    return layer_should_render_as_visible_content(title, layer);
}

static std::string gpu_mask_texture_key(
    const TitleGpuRenderSession *session, const Layer &layer,
    double title_time, const std::string &raster_id,
    const TitleGpuRenderSession::LayerRaster &raster)
{
    std::ostringstream key;
    key << "phase13-mask-v1|title=" << session->title.id
        << "|model=" << session->model_revision
        << "|layer=" << layer.id
        << "|raster=" << raster_id
        << "|raster-key=" << raster.key
        << "|time=" << std::fixed << std::setprecision(9) << title_time
        << "|surface=" << session->width << 'x' << session->height
        << "|draft=" << (session->editor_draft ? 1 : 0)
        << "|scale=" << std::setprecision(6)
        << session->preview_quality_scale;
    return key.str();
}
static void destroy_gpu_mask_cache_entry(
    TitleGpuRenderSession::MaskTextureCacheEntry &entry)
{
    for (gs_texrender_t *&target : entry.targets) {
        if (target)
            gs_texrender_destroy(target);
        target = nullptr;
    }
    entry.active_target = -1;
}
static void prune_gpu_mask_texture_cache(TitleGpuRenderSession *session)
{
    constexpr std::size_t kMaximumMaskTextures = 64;
    if (!session || session->mask_texture_cache.size() <= kMaximumMaskTextures)
        return;
    const uint64_t keep_after = session->mask_texture_cache_tick > 120
        ? session->mask_texture_cache_tick - 120 : 0;
    for (auto it = session->mask_texture_cache.begin();
         it != session->mask_texture_cache.end() &&
         session->mask_texture_cache.size() > kMaximumMaskTextures;) {
        if (it->second.last_used >= keep_after) {
            ++it;
            continue;
        }
        destroy_gpu_mask_cache_entry(it->second);
        it = session->mask_texture_cache.erase(it);
    }
    /* A project can legitimately use more than 64 masks in the same frame, so
     * age-based pruning alone is not a hard bound.  Evict the least-recently
     * used entries until the per-session GPU mask cache is bounded. */
    while (session->mask_texture_cache.size() > kMaximumMaskTextures) {
        auto victim = session->mask_texture_cache.end();
        for (auto it = session->mask_texture_cache.begin();
             it != session->mask_texture_cache.end(); ++it) {
            if (victim == session->mask_texture_cache.end() ||
                it->second.last_used < victim->second.last_used)
                victim = it;
        }
        if (victim == session->mask_texture_cache.end())
            break;
        destroy_gpu_mask_cache_entry(victim->second);
        session->mask_texture_cache.erase(victim);
    }
}
static gs_texture_t *render_gpu_group_graph_texture(
    TitleGpuRenderSession *session, const Title &title, const Layer &group,
    double title_time, std::unordered_set<std::string> &visiting,
    gs_texture_t *composition_background = nullptr);
static std::string gpu_hardware_depth_camera_id(const Title &title,
                                                const Layer &layer,
                                                double title_time);
static gs_texture_t *render_gpu_hardware_depth_run(
    TitleGpuRenderSession *session, const Title &title,
    const std::vector<const Layer *> &layers, double title_time,
    gs_texture_t *background,
    const std::string &compositor_group_id = std::string());
static bool composite_gpu_frame_layer(TitleGpuRenderSession *session,
                                      gs_texture_t *background,
                                      gs_texture_t *foreground,
                                      EffectBlendMode mode,
                                      double opacity,
                                      gs_texrender_t *target);
static const char *gpu_mask_graph_purpose_key(GpuMaskGraphPurpose purpose)
{
    if (purpose == GpuMaskGraphPurpose::ClippingShape)
        return "clipping-shape";
    if (purpose == GpuMaskGraphPurpose::EffectSource)
        return "effect-source";
    return "matte-artwork";
}
static std::string gpu_mask_cache_slot(
    const std::string &raster_id, const std::string &compositor_group_id,
    GpuMaskGraphPurpose purpose = GpuMaskGraphPurpose::MatteArtwork)
{
    return std::string("matte|") + raster_id + "|scope=" +
        (compositor_group_id.empty() ? std::string("<root>")
                                     : compositor_group_id) +
        "|purpose=" + gpu_mask_graph_purpose_key(purpose);
}
static gs_texture_t *render_gpu_mask_graph_texture(
    TitleGpuRenderSession *session, const Title &title, const Layer &layer,
    double title_time, const std::string &raster_id,
    const std::string &compositor_group_id,
    std::unordered_set<std::string> &visiting,
    GpuMaskGraphPurpose purpose)
{
    if (!session)
        return nullptr;
    const std::string resolved_raster_id = raster_id.empty()
        ? gpu_session_layer_id(layer) : raster_id;
    const std::string cache_slot = gpu_mask_cache_slot(
        resolved_raster_id, compositor_group_id, purpose);
    const bool clipping_shape =
        purpose == GpuMaskGraphPurpose::ClippingShape;
    const bool effect_source =
        purpose == GpuMaskGraphPurpose::EffectSource;
    auto cached_texture = [&](const std::string &desired_key) -> gs_texture_t * {
        auto existing = session->mask_texture_cache.find(cache_slot);
        if (existing == session->mask_texture_cache.end() ||
            existing->second.key != desired_key ||
            existing->second.active_target < 0 ||
            existing->second.width != session->width ||
            existing->second.height != session->height)
            return nullptr;
        existing->second.last_used = ++session->mask_texture_cache_tick;
        return gs_texrender_get_texture(
            existing->second.targets[existing->second.active_target]);
    };
    auto publish_texture = [&](gs_texture_t *texture,
                               const std::string &desired_key,
                               double opacity = 1.0) -> gs_texture_t * {
        auto &cache = session->mask_texture_cache[cache_slot];
        const int render_index = cache.active_target == 0 ? 1 : 0;
        if (!cache.targets[render_index])
            cache.targets[render_index] = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
        if (!cache.targets[render_index])
            return nullptr;
        bool published = false;
        const double resolved_opacity = std::clamp(opacity, 0.0, 1.0);
        if (!texture || resolved_opacity <= 0.000001) {
            struct vec4 clear;
            vec4_zero(&clear);
            if (begin_gpu_target(cache.targets[render_index], session->width,
                                 session->height, clear)) {
                gs_texrender_end(cache.targets[render_index]);
                published = true;
            }
        } else if (resolved_opacity >= 0.999999) {
            published = copy_full_canvas_gpu_texture(
                session, texture, cache.targets[render_index]);
        } else {
            gs_texrender_t *transparent = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
            struct vec4 clear;
            vec4_zero(&clear);
            if (transparent && begin_gpu_target(transparent, session->width,
                                                session->height, clear)) {
                gs_texrender_end(transparent);
                gs_texture_t *background = gs_texrender_get_texture(transparent);
                published = background && composite_gpu_frame_layer(
                    session, background, texture, EffectBlendMode::Normal,
                    resolved_opacity, cache.targets[render_index]);
            }
            if (transparent)
                gs_texrender_destroy(transparent);
        }
        if (!published)
            return nullptr;
        cache.active_target = render_index;
        cache.width = session->width;
        cache.height = session->height;
        cache.key = desired_key;
        cache.last_used = ++session->mask_texture_cache_tick;
        return gs_texrender_get_texture(cache.targets[cache.active_target]);
    };
    const double resolved_time = gpu_layer_render_time(title, layer, title_time);
    /* A Group matte is the group's fully composited child result, including the
     * Group's own effects and nested matte, with opacity evaluated in the same
     * compositor scope as an ordinary layer. */
    if (layer_type_is_container(layer.type)) {
        std::ostringstream key;
        key << "group-matte-v3|title=" << title.id
            << "|model=" << session->model_revision
            << "|layer=" << layer.id
            << "|time=" << std::fixed << std::setprecision(9) << resolved_time
            << "|scope=" << compositor_group_id
            << "|purpose=" << gpu_mask_graph_purpose_key(purpose)
            << "|surface=" << session->width << 'x' << session->height
            << "|draft=" << (session->editor_draft ? 1 : 0);
        /* Clipping consumes the Group's composited shape, not its opacity.
         * Keep the ordinary matte/artwork path unchanged so a visible clipping
         * base still renders with the Group's real animated opacity. */
        const double opacity = (clipping_shape || effect_source) ? 1.0 :
            gpu_layer_compositor_opacity(
                title, layer, title_time, compositor_group_id);
        key << "|opacity=" << std::setprecision(9) << opacity;
        const std::string desired_key = key.str();
        if (gs_texture_t *cached = cached_texture(desired_key))
            return cached;
        gs_texture_t *group_texture = render_gpu_group_graph_texture(
            session, title, layer, title_time, visiting);
        return publish_texture(group_texture, desired_key, opacity);
    }
    auto raster_found = session->layers.find(resolved_raster_id);
    if (raster_found == session->layers.end())
        return nullptr;
    const bool runtime_transition_input =
        layer.type == LayerType::TransitionInput &&
        layer.transition_input_slot >= 0 && layer.transition_input_slot <= 1 &&
        session->transition_input_textures[layer.transition_input_slot];
    if (!runtime_transition_input &&
        (!upload_gpu_layer_raster(session, raster_found->second) ||
         !raster_found->second.texture))
        return nullptr;
    const std::string visit_id = layer.id + '|' + resolved_raster_id;
    if (!visiting.insert(visit_id).second) {
        session->last_error = "GPU mask graph contains a cyclic track-matte dependency.";
        return nullptr;
    }
    const bool has_nested_mask = layer.mask_mode != MaskMode::None &&
                                 !layer.mask_source_id.empty();
    const bool effects_after_nested_mask = has_nested_mask &&
                                           layer.effect_stack_respects_masks;
    gs_texture_t *nested_mask_texture = nullptr;
    bool nested_mask_available = false;
    std::string nested_cache_slot;
    if (has_nested_mask) {
        const Layer *nested = find_layer_by_id(title, layer.mask_source_id);
        if (nested && gpu_layer_chain_active_as_matte(title, *nested, title_time)) {
            const std::string nested_raster_id =
                is_gpu_scene_mask_raster_id(resolved_raster_id)
                    ? gpu_scene_mask_raster_id(nested->id)
                    : gpu_session_layer_id(*nested);
            /* The nested matte is evaluated in the target layer's own group
             * compositor, not in the outer consumer's scope. */
            const std::string nested_scope = layer.parent_id;
            const GpuMaskGraphPurpose nested_purpose =
                mask_mode_is_clipping(layer.mask_mode)
                    ? GpuMaskGraphPurpose::ClippingShape
                    : GpuMaskGraphPurpose::MatteArtwork;
            nested_cache_slot = gpu_mask_cache_slot(
                nested_raster_id, nested_scope, nested_purpose);
            nested_mask_texture = render_gpu_mask_graph_texture(
                session, title, *nested, title_time, nested_raster_id,
                nested_scope, visiting, nested_purpose);
            nested_mask_available = nested_mask_texture != nullptr;
        }
    }
    std::string desired_key = gpu_mask_texture_key(
        session, layer, resolved_time, resolved_raster_id,
        raster_found->second);
    desired_key += "|scope=" + compositor_group_id;
    desired_key += "|purpose=" +
        std::string(gpu_mask_graph_purpose_key(purpose));
    desired_key += "|mode=" + std::to_string(static_cast<int>(layer.mask_mode));
    const double graph_opacity = (clipping_shape || effect_source) ? 1.0 :
        gpu_layer_compositor_opacity(title, layer, title_time,
                                     compositor_group_id);
    desired_key += "|opacity=" + std::to_string(graph_opacity);
    if (layer.type == LayerType::TransitionInput)
        desired_key += "|runtime-input=" +
            std::to_string(session->transition_input_generation);
    if (has_nested_mask) {
        const auto nested_cache = session->mask_texture_cache.find(
            nested_cache_slot);
        desired_key += "|nested=";
        if (nested_cache != session->mask_texture_cache.end())
            desired_key += nested_cache->second.key;
        else
            desired_key += nested_mask_available ? "uncached" : "missing";
    }
    if (gs_texture_t *cached = cached_texture(desired_key)) {
        visiting.erase(visit_id);
        return cached;
    }
    gs_texture_t *matte_texture = nullptr;
    const bool nested_hides_layer = has_nested_mask &&
        !nested_mask_available && !mask_mode_is_inverted(layer.mask_mode);
    if (!nested_hides_layer) {
        /* Alpha/Luma mattes preserve source opacity. ClippingShape instead
         * renders the same artwork at full compositor opacity, so animated
         * opacity (including 0%) cannot shrink or fade the clipping region.
         * Intrinsic artwork alpha and anti-aliased edge coverage are retained,
         * which keeps image transparency and smooth text/shape edges intact. */
        const double matte_opacity = (clipping_shape || effect_source) ? 1.0 :
            gpu_layer_compositor_opacity(title, layer, title_time,
                                         compositor_group_id);
        if (!render_gpu_layer_to_target(session, title, layer, title_time,
                                        session->mask_target,
                                        !effects_after_nested_mask,
                                        resolved_raster_id, matte_opacity,
                                        compositor_group_id, effect_source)) {
            visiting.erase(visit_id);
            return nullptr;
        }
        matte_texture = gs_texrender_get_texture(session->mask_target);
        if (has_nested_mask && nested_mask_available) {
            matte_texture = apply_gpu_mask(session, matte_texture,
                                            nested_mask_texture,
                                            layer.mask_mode);
        }
        if (effects_after_nested_mask && matte_texture) {
            matte_texture = apply_gpu_layer_effect_stack(
                session, layer, resolved_time, matte_texture,
                session->width, session->height, nullptr, false, nullptr, true);
        }
    }
    gs_texture_t *published = publish_texture(matte_texture, desired_key);
    visiting.erase(visit_id);
    return published;
}
static gs_texture_t *render_gpu_mask_graph_texture(
    TitleGpuRenderSession *session, const Title &title, const Layer &layer,
    double title_time, const std::string &raster_id,
    const std::string &compositor_group_id, GpuMaskGraphPurpose purpose)
{
    std::unordered_set<std::string> visiting;
    return render_gpu_mask_graph_texture(session, title, layer, title_time,
                                         raster_id, compositor_group_id,
                                         visiting, purpose);
}
/* Group matte rendering recursively composites children and therefore reuses
 * shared per-session layer/effect targets.  A consumer foreground can live in
 * one of those same targets.  Snapshot the consumer before entering the matte
 * graph so producing a Group matte cannot overwrite the layer that is about to
 * be masked.  Single-layer mattes keep their existing zero-copy path. */
static gs_texrender_t *snapshot_gpu_track_matte_target(
    TitleGpuRenderSession *session, gs_texture_t *texture,
    gs_texture_t *&protected_texture)
{
    protected_texture = texture;
    if (!session || !texture)
        return nullptr;
    gs_texrender_t *snapshot = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    if (!snapshot)
        return nullptr;
    if (!copy_full_canvas_gpu_texture(session, texture, snapshot)) {
        gs_texrender_destroy(snapshot);
        return nullptr;
    }
    protected_texture = gs_texrender_get_texture(snapshot);
    if (!protected_texture) {
        gs_texrender_destroy(snapshot);
        return nullptr;
    }
    return snapshot;
}
static gs_texture_t *apply_gpu_track_matte_texture(
    TitleGpuRenderSession *session, const Title &title,
    gs_texture_t *target_texture, const Layer &mask_layer,
    double title_time, const std::string &raster_id,
    const std::string &compositor_group_id, MaskMode mode,
    std::unordered_set<std::string> &visiting)
{
    if (!session || !target_texture || mode == MaskMode::None)
        return target_texture;

    gs_texrender_t *target_snapshot = nullptr;
    gs_texture_t *protected_target = target_texture;
    if (layer_type_is_container(mask_layer.type)) {
        target_snapshot = snapshot_gpu_track_matte_target(
            session, target_texture, protected_target);
        if (!target_snapshot) {
            session->last_error =
                "Could not preserve target texture before Group track-matte rendering.";
            return nullptr;
        }
    }

    const GpuMaskGraphPurpose mask_purpose = mask_mode_is_clipping(mode)
        ? GpuMaskGraphPurpose::ClippingShape
        : GpuMaskGraphPurpose::MatteArtwork;
    gs_texture_t *mask_texture = render_gpu_mask_graph_texture(
        session, title, mask_layer, title_time, raster_id,
        compositor_group_id, visiting, mask_purpose);
    gs_texture_t *result = nullptr;
    if (mask_texture) {
        result = apply_gpu_mask(session, protected_target, mask_texture, mode);
    } else if (mask_mode_is_inverted(mode)) {
        if (target_snapshot) {
            if (copy_full_canvas_gpu_texture(session, protected_target,
                                             session->masked_target))
                result = gs_texrender_get_texture(session->masked_target);
        } else {
            result = protected_target;
        }
    }

    if (target_snapshot)
        gs_texrender_destroy(target_snapshot);
    return result;
}

static gs_texture_t *apply_gpu_track_matte_texture(
    TitleGpuRenderSession *session, const Title &title,
    gs_texture_t *target_texture, const Layer &mask_layer,
    double title_time, const std::string &raster_id,
    const std::string &compositor_group_id, MaskMode mode)
{
    std::unordered_set<std::string> visiting;
    return apply_gpu_track_matte_texture(
        session, title, target_texture, mask_layer, title_time, raster_id,
        compositor_group_id, mode, visiting);
}

static bool composite_gpu_frame_layer(TitleGpuRenderSession *session,
                                      gs_texture_t *background,
                                      gs_texture_t *foreground,
                                      EffectBlendMode mode,
                                      double opacity,
                                      gs_texrender_t *target)
{
    if (!session || !background || !foreground || !target)
        return false;
    if (!session->blend_effect)
        session->blend_effect = gs_effect_create(
            kGpuFrameBlendEffect, "obs-bgs-gpu-frame-blend.effect", nullptr);
    if (!session->blend_effect) {
        session->last_error = "Could not compile GPU frame-blend shader.";
        return false;
    }
    struct vec4 clear;
    vec4_zero(&clear);
    if (!begin_gpu_target(target, session->width, session->height, clear))
        return false;
    gs_eparam_t *bg = bgl_effect_param(session->blend_effect, "background");
    gs_eparam_t *fg = bgl_effect_param(session->blend_effect, "foreground");
    gs_eparam_t *blend_mode = bgl_effect_param(session->blend_effect, "blendMode");
    gs_eparam_t *opacity_param = bgl_effect_param(session->blend_effect, "opacity");
    if (!bg || !fg || !blend_mode || !opacity_param) {
        gs_texrender_end(target);
        return false;
    }
    gs_effect_set_texture(bg, background);
    gs_effect_set_texture(fg, foreground);
    gs_effect_set_int(blend_mode, (int)mode);
    gs_effect_set_float(opacity_param, static_cast<float>(std::clamp(opacity, 0.0, 1.0)));
    gs_enable_blending(false);
    while (gs_effect_loop(session->blend_effect, "Draw"))
        gs_draw_sprite(background, 0, session->width, session->height);
    gs_texrender_end(target);
    return gs_texrender_get_texture(target) != nullptr;
}

static double gpu_layer_local_opacity(const Layer &layer, double title_time)
{
    const double local_time = std::max(0.0, title_time - layer.in_time);
    const LayerTransitionVisualState transition = evaluate_layer_general_transitions(
        layer.transitions, layer.in_time, layer.out_time, title_time);
    return std::clamp(layer.opacity.evaluate(local_time) * transition.opacity,
                      0.0, 1.0);
}

static std::size_t gpu_layer_transform_keyframe_count(const Layer &layer)
{
    return layer.position.keyframes.size() +
        layer.position_3d.keyframes.size() +
        layer.position_z.keyframes.size() + layer.scale.keyframes.size() +
        layer.scale_z.keyframes.size() + layer.rotation.keyframes.size() +
        layer.rotation_x.keyframes.size() + layer.rotation_y.keyframes.size() +
        layer.orientation_x.keyframes.size() +
        layer.orientation_y.keyframes.size() +
        layer.orientation_z.keyframes.size() +
        layer.origin_prop.keyframes.size() + layer.anchor_z.keyframes.size();
}

static void gpu_note_group_silhouette_pass(
    TitleGpuRenderSession *session, const std::string &group_id)
{
    if (!session || group_id.empty())
        return;
    auto &diagnostics = session->group_render_diagnostics[group_id];
    if (!diagnostics.initialized) {
        diagnostics.window_started = std::chrono::steady_clock::now();
        diagnostics.initialized = true;
    }
    ++diagnostics.silhouette_passes;
}

static void gpu_record_group_render_diagnostics(
    TitleGpuRenderSession *session, const Layer &group,
    uint64_t elapsed_us, std::size_t child_count,
    uint64_t target_creations, const Layer *largest_keyframed_child)
{
    if (!session)
        return;
    const auto now = std::chrono::steady_clock::now();
    auto &diagnostics = session->group_render_diagnostics[group.id];
    if (!diagnostics.initialized) {
        diagnostics.window_started = now;
        diagnostics.initialized = true;
    }
    ++diagnostics.calls;
    diagnostics.total_us += elapsed_us;
    diagnostics.max_us = std::max(diagnostics.max_us, elapsed_us);
    diagnostics.child_samples += static_cast<uint64_t>(child_count);
    diagnostics.target_creations += target_creations;
    if (elapsed_us >= 8000)
        ++diagnostics.slow_frames;
    if (largest_keyframed_child) {
        const std::size_t child_keyframes =
            gpu_layer_transform_keyframe_count(*largest_keyframed_child);
        if (child_keyframes > diagnostics.max_child_transform_keyframes) {
            diagnostics.max_child_transform_keyframes = child_keyframes;
            diagnostics.max_child_id = largest_keyframed_child->id;
            diagnostics.max_child_name = largest_keyframed_child->name;
        }
    }

    const auto window_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - diagnostics.window_started).count();
    if (window_ms < 1000 || diagnostics.calls == 0)
        return;

    const double average_ms =
        static_cast<double>(diagnostics.total_us) /
        static_cast<double>(diagnostics.calls) / 1000.0;
    const double average_children =
        static_cast<double>(diagnostics.child_samples) /
        static_cast<double>(diagnostics.calls);
    const std::size_t transform_keyframes =
        group.position.keyframes.size() + group.position_3d.keyframes.size() +
        group.position_z.keyframes.size() + group.scale.keyframes.size() +
        group.scale_z.keyframes.size() + group.rotation.keyframes.size() +
        group.rotation_x.keyframes.size() + group.rotation_y.keyframes.size() +
        group.orientation_x.keyframes.size() +
        group.orientation_y.keyframes.size() +
        group.orientation_z.keyframes.size();
    BGL_LOG_DEBUG("Grouping", QStringLiteral(
        "GPU group render group=%1 name=\"%2\" calls=%3 avgMs=%4 maxMs=%5 "
        "avgChildren=%6 targetCreates=%7 silhouettePasses=%8 surface=%9x%10 "
        "effects=%11 mask=%12 transformKeyframes=%13 cacheEntries=%14 "
        "slowFrames=%15 maxChildTransformKeys=%16 maxChild=%17 maxChildName=\"%18\"")
        .arg(QString::fromStdString(group.id),
             QString::fromStdString(group.name))
        .arg(static_cast<qulonglong>(diagnostics.calls))
        .arg(average_ms, 0, 'f', 3)
        .arg(static_cast<double>(diagnostics.max_us) / 1000.0, 0, 'f', 3)
        .arg(average_children, 0, 'f', 2)
        .arg(static_cast<qulonglong>(diagnostics.target_creations))
        .arg(static_cast<qulonglong>(diagnostics.silhouette_passes))
        .arg(session->width)
        .arg(session->height)
        .arg(static_cast<qulonglong>(group.effects.size()))
        .arg(group.mask_mode != MaskMode::None &&
             !group.mask_source_id.empty() ? 1 : 0)
        .arg(static_cast<qulonglong>(transform_keyframes))
        .arg(static_cast<qulonglong>(session->mask_texture_cache.size()))
        .arg(static_cast<qulonglong>(diagnostics.slow_frames))
        .arg(static_cast<qulonglong>(diagnostics.max_child_transform_keyframes))
        .arg(QString::fromStdString(diagnostics.max_child_id))
        .arg(QString::fromStdString(diagnostics.max_child_name)));

    diagnostics.calls = 0;
    diagnostics.total_us = 0;
    diagnostics.max_us = 0;
    diagnostics.child_samples = 0;
    diagnostics.target_creations = 0;
    diagnostics.silhouette_passes = 0;
    diagnostics.slow_frames = 0;
    diagnostics.max_child_transform_keyframes = 0;
    diagnostics.max_child_id.clear();
    diagnostics.max_child_name.clear();
    diagnostics.window_started = now;
}

static gs_texture_t *render_gpu_group_graph_texture(
    TitleGpuRenderSession *session, const Title &title, const Layer &group,
    double title_time, std::unordered_set<std::string> &visiting,
    gs_texture_t *composition_background)
{
    if (!session || !layer_type_is_container(group.type))
        return nullptr;
    const auto render_started = std::chrono::steady_clock::now();
    uint64_t target_creations = 0;
    std::size_t diagnostic_child_count = 0;
    const std::string visit_id = std::string("group|") + group.id;
    if (!visiting.insert(visit_id).second) {
        session->last_error = "GPU group graph contains a cyclic dependency.";
        BGL_LOG_WARNING("Grouping", QStringLiteral(
            "Rejected cyclic GPU group graph group=%1 name=\"%2\"")
            .arg(QString::fromStdString(group.id),
                 QString::fromStdString(group.name)));
        return nullptr;
    }

    const std::string result_cache_slot =
        std::string("group-result|") + group.id;
    auto &result_cache = session->mask_texture_cache[result_cache_slot];
    if (result_cache.width != session->width ||
        result_cache.height != session->height) {
        destroy_gpu_mask_cache_entry(result_cache);
        result_cache.width = session->width;
        result_cache.height = session->height;
    }
    for (gs_texrender_t *&target : result_cache.targets) {
        if (!target) {
            target = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
            if (target)
                ++target_creations;
        }
    }
    if (!result_cache.targets[0] || !result_cache.targets[1]) {
        BGL_LOG_WARNING("Grouping", QStringLiteral(
            "Unable to allocate persistent GPU group targets group=%1 surface=%2x%3")
            .arg(QString::fromStdString(group.id))
            .arg(session->width)
            .arg(session->height));
        visiting.erase(visit_id);
        return nullptr;
    }

    const int ping_index = result_cache.active_target == 0 ? 1 : 0;
    const int pong_index = ping_index == 0 ? 1 : 0;
    gs_texrender_t *ping = result_cache.targets[ping_index];
    gs_texrender_t *pong = result_cache.targets[pong_index];

    struct vec4 clear; vec4_zero(&clear);
    if (!begin_gpu_target(ping, session->width, session->height, clear)) {
        BGL_LOG_WARNING("Grouping", QStringLiteral(
            "Unable to begin GPU group target group=%1 surface=%2x%3")
            .arg(QString::fromStdString(group.id))
            .arg(session->width)
            .arg(session->height));
        visiting.erase(visit_id);
        return nullptr;
    }
    gs_texrender_end(ping);
    gs_texture_t *frame = gs_texrender_get_texture(ping);
    bool current_ping = true;

    /* Compose the group exactly like an ordinary layer target.  Its children
     * are rendered first without inheriting the group's own matte; the group's
     * matte is then evaluated once against the complete group result.  This
     * uses the same mask graph that already makes a group reliable as a matte
     * source and avoids losing sibling artwork through shared per-child mask
     * targets. */
    const bool group_has_mask = group.mask_mode != MaskMode::None &&
                                !group.mask_source_id.empty();

    /* A clipping matte is a two-layer composite: the matte source remains the
     * visible base and the linked target is constrained by that source alpha.
     * Emit each base once inside this compositor, immediately before the first
     * linked target, so layer order cannot place the base above and hide the
     * clipped result. */
    std::unordered_set<std::string> emitted_clipping_bases;
    auto composite_clipping_base = [&](const Layer &target) -> bool {
        if (!mask_mode_is_clipping(target.mask_mode) ||
            target.mask_source_id.empty())
            return true;
        if (emitted_clipping_bases.find(target.mask_source_id) !=
            emitted_clipping_bases.end())
            return true;
        const Layer *base_layer = find_layer_by_id(title, target.mask_source_id);
        if (!base_layer)
            return true;
        if (!gpu_layer_chain_active_as_matte(title, *base_layer, title_time))
            return true;
        /* MatteOnly keeps the source active for clipping but suppresses its
         * artwork.  Only VisibleAndMatte publishes the clipping base into the
         * composed frame; the target consumes a separate opacity-independent
         * shape texture. */
        if (base_layer->matte_visibility_mode !=
            MatteVisibilityMode::VisibleAndMatte) {
            emitted_clipping_bases.insert(base_layer->id);
            return true;
        }
        gs_texture_t *base_texture = render_gpu_mask_graph_texture(
            session, title, *base_layer, title_time,
            gpu_session_layer_id(*base_layer), group.id, visiting,
            GpuMaskGraphPurpose::MatteArtwork);
        if (!base_texture)
            return false;
        gs_texrender_t *next = current_ping ? pong : ping;
        if (!composite_gpu_frame_layer(session, frame, base_texture,
                                       base_layer->blend_mode, 1.0, next))
            return false;
        frame = gs_texrender_get_texture(next);
        current_ping = !current_ping;
        emitted_clipping_bases.insert(base_layer->id);
        return true;
    };

    const std::vector<const Layer *> ordered_children =
        bgs::transform3d::ordered_group_children(title, group.id, title_time);
    diagnostic_child_count = ordered_children.size();
    const Layer *largest_keyframed_child = nullptr;
    std::size_t largest_child_transform_keyframes = 0;
    for (const Layer *child : ordered_children) {
        if (!child)
            continue;
        const std::size_t keyframes = gpu_layer_transform_keyframe_count(*child);
        if (keyframes > largest_child_transform_keyframes) {
            largest_child_transform_keyframes = keyframes;
            largest_keyframed_child = child;
        }
    }

    /* Hardware depth is valid inside a group because child world matrices
     * already include the complete nested group chain. Keep runs local to the
     * current compositor boundary: group masks/effects still flatten the
     * completed result exactly once, while simple sibling planes can occlude
     * each other per pixel before that flattening step. */
    struct GroupHardwareDepthRun {
        std::vector<const Layer *> layers;
        std::size_t writer_count = 0;
        std::size_t transparent_count = 0;
    };
    std::unordered_map<const Layer *, GroupHardwareDepthRun>
        group_hardware_depth_runs;
    std::size_t depth_cursor = 0;
    while (depth_cursor < ordered_children.size()) {
        const Layer *layer = ordered_children[depth_cursor];
        const bool visible_candidate = layer &&
            gpu_layer_visible_in_group_compositor(
                title, *layer, title_time, group.id) &&
            layer_should_render_as_visible_content_for_gpu_session(session, title, *layer) &&
            bgs::transform3d::layer_passes_backface_culling(
                title, *layer, title_time) &&
            bgs::transform3d::hardware_depth_candidate(
                title, *layer, title_time);
        const auto raster = layer
            ? session->layers.find(gpu_session_layer_id(*layer))
            : session->layers.end();
        if (!visible_candidate || raster == session->layers.end() ||
            !raster->second.texture) {
            ++depth_cursor;
            continue;
        }

        GroupHardwareDepthRun run;
        const std::string camera_id = gpu_hardware_depth_camera_id(
            title, *layer, title_time);
        std::size_t run_cursor = depth_cursor;
        while (run_cursor < ordered_children.size()) {
            const Layer *candidate = ordered_children[run_cursor];
            if (!candidate) {
                ++run_cursor;
                continue;
            }
            const bool candidate_visible =
                gpu_layer_visible_in_group_compositor(
                    title, *candidate, title_time, group.id) &&
                layer_should_render_as_visible_content_for_gpu_session(session, title, *candidate) &&
                bgs::transform3d::layer_passes_backface_culling(
                    title, *candidate, title_time);
            if (!candidate_visible) {
                ++run_cursor;
                continue;
            }
            if (!bgs::transform3d::hardware_depth_candidate(
                    title, *candidate, title_time) ||
                gpu_hardware_depth_camera_id(title, *candidate, title_time) != camera_id)
                break;
            const auto candidate_raster = session->layers.find(
                gpu_session_layer_id(*candidate));
            if (candidate_raster == session->layers.end() ||
                !candidate_raster->second.texture)
                break;
            run.layers.push_back(candidate);
            if (bgs::transform3d::hardware_depth_writer(
                    title, *candidate, title_time))
                ++run.writer_count;
            if (bgs::transform3d::hardware_depth_transparent_candidate(
                    title, *candidate, title_time))
                ++run.transparent_count;
            ++run_cursor;
        }
        if (run.layers.size() >= 2 &&
            (run.writer_count >= 1 || run.transparent_count >= 2))
            group_hardware_depth_runs.emplace(run.layers.front(),
                                               std::move(run));
        depth_cursor = std::max(depth_cursor + 1, run_cursor);
    }

    std::unordered_set<const Layer *> rendered_group_depth_layers;
    for (const Layer *child_ptr : ordered_children) {
        if (!child_ptr ||
            rendered_group_depth_layers.find(child_ptr) !=
                rendered_group_depth_layers.end())
            continue;
        const auto depth_run = group_hardware_depth_runs.find(child_ptr);
        if (depth_run != group_hardware_depth_runs.end()) {
            if (gs_texture_t *depth_frame = render_gpu_hardware_depth_run(
                    session, title, depth_run->second.layers, title_time,
                    frame, group.id)) {
                frame = depth_frame;
                rendered_group_depth_layers.insert(
                    depth_run->second.layers.begin(),
                    depth_run->second.layers.end());
                continue;
            }
        }
        const Layer &child = *child_ptr;
        const double child_time = gpu_layer_render_time(title, child, title_time);
        if (!gpu_layer_visible_in_group_compositor(title, child, title_time,
                                                group.id) ||
            !layer_should_render_as_visible_content_for_gpu_session(session, title, child) ||
            !bgs::transform3d::layer_passes_backface_culling(
                title, child, title_time))
            continue;
        if (!composite_clipping_base(child))
            continue;

        const bool has_mask = child.mask_mode != MaskMode::None &&
                              !child.mask_source_id.empty();
        const bool effects_after_mask = has_mask &&
                                        child.effect_stack_respects_masks;
        const double child_local_time = std::max(0.0, child_time - child.in_time);
        const bool has_backdrop_effect = std::any_of(
            child.effects.begin(), child.effects.end(),
            [child_local_time](const LayerEffect &effect) {
                return eval_effect_enabled(effect, child_local_time) &&
                       effect.affect_layers_behind;
            });

        gs_texture_t *foreground = nullptr;
        gs_texrender_t *child_silhouette_target = nullptr;
        gs_texture_t *child_silhouette = nullptr;
        gs_texrender_t *child_foreground_target = nullptr;
        bool backdrop_foreground_safe = true;

        auto destroy_child_targets = [&]() {
            if (child_silhouette_target)
                gs_texrender_destroy(child_silhouette_target);
            if (child_foreground_target)
                gs_texrender_destroy(child_foreground_target);
            child_silhouette_target = nullptr;
            child_foreground_target = nullptr;
        };

        auto retain_child_silhouette = [&](gs_texture_t *texture) {
            if (!texture || child_silhouette_target)
                return;
            child_silhouette_target = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
            if (child_silhouette_target &&
                copy_full_canvas_gpu_texture(session, texture,
                                             child_silhouette_target))
                child_silhouette = gs_texrender_get_texture(
                    child_silhouette_target);
        };

        auto apply_child_mask = [&](gs_texture_t *texture) -> gs_texture_t * {
            if (!texture || !has_mask)
                return texture;
            const Layer *mask_layer = find_layer_by_id(title,
                                                       child.mask_source_id);
            if (mask_layer &&
                gpu_layer_chain_active_as_matte(title, *mask_layer, title_time)) {
                return apply_gpu_track_matte_texture(
                    session, title, texture, *mask_layer, title_time,
                    gpu_session_layer_id(*mask_layer), group.id,
                    child.mask_mode, visiting);
            }
            return mask_mode_is_inverted(child.mask_mode) ? texture : nullptr;
        };

        if (layer_type_is_container(child.type)) {
            if (has_backdrop_effect) {
                Layer silhouette_group = child;
                silhouette_group.effects.clear();
                retain_child_silhouette(render_gpu_group_graph_texture(
                    session, title, silhouette_group, title_time, visiting,
                    frame));
            }
            foreground = render_gpu_group_graph_texture(
                session, title, child, title_time, visiting, frame);
        } else if (child.type == LayerType::Adjustment) {
            gs_texture_t *coverage = render_gpu_adjustment_coverage(
                session, title, child, title_time);
            gs_texture_t *adjusted = apply_gpu_layer_effect_stack(
                session, child, child_time, frame, session->width,
                session->height, nullptr, false, frame, true);
            gs_texrender_t *next = current_ping ? pong : ping;
            gs_texture_t *mixed = coverage && adjusted
                ? mix_gpu_adjustment_layer(
                      session, title, child, title_time, frame, adjusted,
                      coverage, next, false,
                      gpu_layer_local_opacity(child, child_time))
                : nullptr;
            if (mixed && mixed != frame) {
                frame = mixed;
                current_ping = !current_ping;
            }
            continue;
        } else {
            if (has_backdrop_effect &&
                render_gpu_layer_to_target(
                    session, title, child, title_time,
                    session->adjustment_coverage_target, false,
                    std::string(), 1.0, group.id, false, frame)) {
                retain_child_silhouette(apply_child_mask(
                    gs_texrender_get_texture(
                        session->adjustment_coverage_target)));
            }

            /* A group owns the child opacity boundary.  Render the child's
             * complete effect stack at full alpha, then apply its local opacity
             * exactly once during sibling compositing.  Baking chain opacity
             * here and multiplying it again below made shadows/glows and other
             * child effects fade or disappear after grouping. */
            if (!render_gpu_layer_to_target(
                    session, title, child, title_time,
                    session->layer_target, !effects_after_mask,
                    std::string(), 1.0, group.id, false, frame)) {
                destroy_child_targets();
                continue;
            }
            foreground = gs_texrender_get_texture(session->layer_target);
        }

        if (!foreground) {
            destroy_child_targets();
            continue;
        }

        /* Normal layer masks/effects are handled here. Recursive group renders
         * already apply their own mask and mask-respecting stack order. */
        if (!layer_type_is_container(child.type)) {
            foreground = apply_child_mask(foreground);
            if (foreground && effects_after_mask)
                foreground = apply_gpu_layer_effect_stack(
                    session, child, child_time, foreground,
                    session->width, session->height, nullptr, false, frame, true);
        }
        if (!foreground) {
            destroy_child_targets();
            continue;
        }

        /* Mask-after-effects and other full-frame child passes can leave the
         * foreground in a shared session effect target. Preserve it before an
         * affect-behind pass reuses those buffers, otherwise grouped child
         * artwork can be replaced by the adjusted background. */
        if (has_backdrop_effect) {
            child_foreground_target = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
            if (child_foreground_target &&
                copy_full_canvas_gpu_texture(session, foreground,
                                             child_foreground_target)) {
                foreground = gs_texrender_get_texture(child_foreground_target);
            } else {
                if (child_foreground_target)
                    gs_texrender_destroy(child_foreground_target);
                child_foreground_target = nullptr;
                backdrop_foreground_safe = false;
            }
        }

        /* Affect-behind effects on a child operate on the already-composited
         * lower siblings inside this group, bounded by the untouched child
         * silhouette.  They must not escape to the title root or be silently
         * skipped merely because the layer is grouped. */
        if (has_backdrop_effect && child_silhouette &&
            backdrop_foreground_safe) {
            for (const LayerEffect &backdrop_effect : child.effects) {
                if (!eval_effect_enabled(backdrop_effect, child_local_time) ||
                    !backdrop_effect.affect_layers_behind)
                    continue;
                Layer adjustment_child = child;
                adjustment_child.effects.clear();
                adjustment_child.effects.push_back(backdrop_effect);
                gs_texture_t *adjusted_background =
                    apply_gpu_layer_effect_stack(
                        session, adjustment_child, child_time, frame,
                        session->width, session->height, nullptr, true, frame, true);
                if (!adjusted_background || adjusted_background == frame)
                    continue;
                gs_texrender_t *backdrop_target = current_ping ? pong : ping;
                gs_texture_t *mixed_background = mix_gpu_adjustment_layer(
                    session, title, adjustment_child, title_time, frame,
                    adjusted_background, child_silhouette, backdrop_target,
                    true);
                if (mixed_background && mixed_background != frame) {
                    frame = mixed_background;
                    current_ping = !current_ping;
                }
            }
        }

        gs_texrender_t *next = current_ping ? pong : ping;
        if (composite_gpu_frame_layer(session, frame, foreground,
                                      child.blend_mode,
                                      gpu_layer_local_opacity(child, child_time),
                                      next)) {
            frame = gs_texrender_get_texture(next);
            current_ping = !current_ping;
        }
        destroy_child_targets();
    }

    const double group_time = gpu_layer_render_time(title, group, title_time);
    const bool effects_after_group_mask = group_has_mask &&
                                          group.effect_stack_respects_masks;

    auto apply_group_target_mask = [&](gs_texture_t *texture) -> gs_texture_t * {
        if (!texture || !group_has_mask)
            return texture;
        const Layer *mask_layer = find_layer_by_id(title, group.mask_source_id);
        if (mask_layer &&
            gpu_layer_chain_active_as_matte(title, *mask_layer, title_time)) {
            return apply_gpu_track_matte_texture(
                session, title, texture, *mask_layer, title_time,
                gpu_session_layer_id(*mask_layer), group.parent_id,
                group.mask_mode, visiting);
        }
        return mask_mode_is_inverted(group.mask_mode) ? texture : nullptr;
    };

    /* Match the ordinary-layer effect/matte ordering. */
    if (effects_after_group_mask)
        frame = apply_group_target_mask(frame);
    if (frame)
        frame = apply_gpu_layer_effect_stack(session, group, group_time, frame,
                                             session->width, session->height,
                                             nullptr, false,
                                             composition_background, true);
    if (!effects_after_group_mask)
        frame = apply_group_target_mask(frame);

    gs_texture_t *result = nullptr;
    int result_index = -1;
    const gs_texture_t *texture_zero =
        gs_texrender_get_texture(result_cache.targets[0]);
    const gs_texture_t *texture_one =
        gs_texrender_get_texture(result_cache.targets[1]);
    if (frame == texture_zero)
        result_index = 0;
    else if (frame == texture_one)
        result_index = 1;

    /* Most groups finish directly in one of their persistent ping/pong
     * surfaces. Only masks/effects that return a shared external target need a
     * final publication copy. This removes per-frame texrender allocation and
     * the unconditional third full-canvas copy from animated groups. */
    if (result_index < 0 && frame) {
        const int publish_index = result_cache.active_target == 0 ? 1 : 0;
        if (copy_full_canvas_gpu_texture(
                session, frame, result_cache.targets[publish_index]))
            result_index = publish_index;
    }
    if (result_index >= 0) {
        result_cache.active_target = result_index;
        result_cache.width = session->width;
        result_cache.height = session->height;
        result_cache.key = std::to_string(title_time);
        result_cache.last_used = ++session->mask_texture_cache_tick;
        result = gs_texrender_get_texture(
            result_cache.targets[result_cache.active_target]);
    }

    visiting.erase(visit_id);
    const uint64_t elapsed_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - render_started).count());
    gpu_record_group_render_diagnostics(
        session, group, elapsed_us, diagnostic_child_count,
        target_creations, largest_keyframed_child);
    return result;
}

static bool gpu_cached_image_rect(const QImage &image, const Title &title,
                                  QRect &destination)
{
    return bgs::cache_frame_payload::resolve_placement(
        image, std::max(1, title.width), std::max(1, title.height),
        destination);
}

static void evict_gpu_cached_frame_textures(TitleGpuRenderSession *session)
{
    if (!session)
        return;
    while (session->cached_frame_texture_bytes >
               session->cached_frame_texture_budget &&
           !session->cached_frame_textures.empty()) {
        auto victim = session->cached_frame_textures.end();
        for (auto it = session->cached_frame_textures.begin();
             it != session->cached_frame_textures.end(); ++it) {
            if (it->first == session->submitted_final_image_key ||
                it->first == session->base_frame_image_key)
                continue;
            if (victim == session->cached_frame_textures.end() ||
                it->second.last_used < victim->second.last_used)
                victim = it;
        }
        if (victim == session->cached_frame_textures.end())
            break;
        if (victim->second.texture)
            gs_texture_destroy(victim->second.texture);
        session->cached_frame_texture_bytes -= std::min(
            session->cached_frame_texture_bytes, victim->second.bytes);
        session->cached_frame_textures.erase(victim);
    }
}

static bool upload_gpu_cached_image(TitleGpuRenderSession *session,
                                    QImage &pending_image, bool &pending,
                                    qint64 image_key,
                                    gs_texture_t *&texture,
                                    uint32_t &texture_width,
                                    uint32_t &texture_height)
{
    if (!session)
        return false;

    if (image_key != 0) {
        auto cached = session->cached_frame_textures.find(image_key);
        if (cached != session->cached_frame_textures.end()) {
            cached->second.last_used = ++session->cached_frame_texture_tick;
            texture = cached->second.texture;
            texture_width = cached->second.width;
            texture_height = cached->second.height;
            pending_image = QImage();
            pending = false;
            return texture != nullptr;
        }
    }

    if (pending) {
        QImage upload = pending_image;
        if (upload.isNull())
            return false;
        if (upload.format() != QImage::Format_ARGB32_Premultiplied)
            upload = upload.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (upload.bytesPerLine() != upload.width() * 4)
            upload = upload.copy();
        if (upload.isNull() || upload.bytesPerLine() != upload.width() * 4)
            return false;

        const uint32_t width = static_cast<uint32_t>(upload.width());
        const uint32_t height = static_cast<uint32_t>(upload.height());
        const uint8_t *planes[1] = {upload.constBits()};
        gs_texture_t *created = gs_texture_create(width, height, GS_BGRA, 1,
                                                  planes, GS_DYNAMIC);
        if (!created)
            return false;

        if (image_key != 0) {
            TitleGpuRenderSession::CachedFrameTexture entry;
            entry.texture = created;
            entry.width = width;
            entry.height = height;
            entry.bytes = static_cast<quint64>(width) *
                          static_cast<quint64>(height) * 4ull;
            entry.last_used = ++session->cached_frame_texture_tick;
            session->cached_frame_texture_bytes += entry.bytes;
            session->cached_frame_textures.insert_or_assign(image_key, entry);
            texture = created; // borrowed from cached_frame_textures
            evict_gpu_cached_frame_textures(session);
        } else {
            /* Unkeyed payloads are rare (temporary screenshots). Preserve the
             * old single-texture behavior for them. */
            if (texture)
                gs_texture_destroy(texture);
            texture = created;
        }
        texture_width = width;
        texture_height = height;
        pending_image = QImage();
        pending = false;
    }
    return texture != nullptr;
}

static bool draw_gpu_cached_image(TitleGpuRenderSession *session,
                                  gs_texture_t *texture,
                                  const QRect &destination,
                                  gs_texrender_t *target)
{
    if (!session || !texture || !destination.isValid() ||
        destination.isEmpty() || !target)
        return false;
    struct vec4 clear;
    vec4_zero(&clear);
    if (!begin_gpu_target(target, session->width, session->height, clear))
        return false;

    gs_eparam_t *image = bgl_effect_param(session->blit_effect,
                                                     "image");
    if (!image) {
        gs_texrender_end(target);
        return false;
    }
    gs_effect_set_texture(image, texture);
    gs_enable_blending(true);
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
    gs_matrix_push();
    gs_matrix_identity();
    gs_matrix_translate3f(static_cast<float>(destination.x()),
                          static_cast<float>(destination.y()), 0.0f);
    while (gs_effect_loop(session->blit_effect, "Draw"))
        gs_draw_sprite(texture, 0,
                       static_cast<uint32_t>(destination.width()),
                       static_cast<uint32_t>(destination.height()));
    gs_matrix_pop();
    gs_texrender_end(target);
    return gs_texrender_get_texture(target) != nullptr;
}

static uint64_t gpu_ram_frame_metadata_bytes(std::size_t tile_count)
{
    return static_cast<uint64_t>(sizeof(GpuRamFrameEntry)) + 128ull +
           static_cast<uint64_t>(tile_count) *
               (static_cast<uint64_t>(sizeof(GpuRamTileRef)) + 64ull);
}

static void release_gpu_ram_frame_locked(GpuRamFrameEntry &frame)
{
    g_gpu_frame_cache_bytes -= std::min(
        g_gpu_frame_cache_bytes, frame.metadata_bytes);
    for (const GpuRamTileRef &reference : frame.tiles) {
        auto tile = g_gpu_ram_tiles.find(reference.digest);
        if (tile == g_gpu_ram_tiles.end())
            continue;
        if (tile->second.references > 0)
            --tile->second.references;
        if (tile->second.references != 0)
            continue;
        if (tile->second.texture)
            gs_texture_destroy(tile->second.texture);
        g_gpu_frame_cache_bytes -= std::min(
            g_gpu_frame_cache_bytes, tile->second.bytes);
        g_gpu_ram_tiles.erase(tile);
    }
    frame = {};
}

static void evict_global_gpu_frame_cache_locked()
{
    while (g_gpu_frame_cache_bytes > g_gpu_frame_cache_budget &&
           !g_gpu_frame_cache.empty()) {
        auto victim = g_gpu_frame_cache.end();
        for (auto it = g_gpu_frame_cache.begin();
             it != g_gpu_frame_cache.end(); ++it) {
            if (victim == g_gpu_frame_cache.end() ||
                it->second.last_used < victim->second.last_used)
                victim = it;
        }
        if (victim == g_gpu_frame_cache.end())
            break;
        release_gpu_ram_frame_locked(victim->second);
        g_gpu_frame_cache.erase(victim);
    }
}

static std::string gpu_ram_tile_digest_key(
    const bgs::cache_tile_payload::Tile &tile)
{
    if (tile.digest.isEmpty())
        return std::string();
    return tile.digest.toHex().toStdString();
}

static bool acquire_gpu_ram_tile_locked(
    const bgs::cache_tile_payload::Tile &tile, std::string &digest_key)
{
    digest_key = gpu_ram_tile_digest_key(tile);
    if (digest_key.empty() || tile.image.isNull() ||
        tile.rect.size() != tile.image.size())
        return false;

    auto found = g_gpu_ram_tiles.find(digest_key);
    if (found != g_gpu_ram_tiles.end()) {
        ++found->second.references;
        found->second.last_used = ++g_gpu_frame_cache_tick;
        return true;
    }

    QImage upload = tile.image.format() == QImage::Format_ARGB32_Premultiplied
        ? tile.image
        : tile.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (upload.bytesPerLine() != upload.width() * 4)
        upload = upload.copy();
    if (upload.isNull() || upload.bytesPerLine() != upload.width() * 4)
        return false;

    const uint8_t *planes[1] = {upload.constBits()};
    gs_texture_t *texture = gs_texture_create(
        static_cast<uint32_t>(upload.width()),
        static_cast<uint32_t>(upload.height()), GS_BGRA, 1, planes, 0);
    if (!texture)
        return false;

    GpuRamTileEntry entry;
    entry.texture = texture;
    entry.width = static_cast<uint32_t>(upload.width());
    entry.height = static_cast<uint32_t>(upload.height());
    entry.bytes = static_cast<uint64_t>(entry.width) *
                      static_cast<uint64_t>(entry.height) * 4ull +
                  static_cast<uint64_t>(sizeof(GpuRamTileEntry)) + 96ull;
    entry.references = 1;
    entry.last_used = ++g_gpu_frame_cache_tick;
    g_gpu_frame_cache_bytes += entry.bytes;
    g_gpu_ram_tiles.emplace(digest_key, entry);
    return true;
}

static void release_acquired_gpu_ram_tiles_locked(
    const std::vector<GpuRamTileRef> &references)
{
    for (const GpuRamTileRef &reference : references) {
        auto tile = g_gpu_ram_tiles.find(reference.digest);
        if (tile == g_gpu_ram_tiles.end())
            continue;
        if (tile->second.references > 0)
            --tile->second.references;
        if (tile->second.references != 0)
            continue;
        if (tile->second.texture)
            gs_texture_destroy(tile->second.texture);
        g_gpu_frame_cache_bytes -= std::min(
            g_gpu_frame_cache_bytes, tile->second.bytes);
        g_gpu_ram_tiles.erase(tile);
    }
}

static bool store_global_gpu_frame_tiles_locked(
    const std::string &cache_key,
    const QVector<bgs::cache_tile_payload::Tile> &extracted,
    uint32_t canvas_width, uint32_t canvas_height)
{
    if (cache_key.empty() || canvas_width == 0 || canvas_height == 0)
        return false;

    std::vector<GpuRamTileRef> acquired;
    acquired.reserve(static_cast<std::size_t>(extracted.size()));
    for (const auto &tile : extracted) {
        std::string digest;
        if (!acquire_gpu_ram_tile_locked(tile, digest)) {
            release_acquired_gpu_ram_tiles_locked(acquired);
            return false;
        }
        GpuRamTileRef reference;
        reference.digest = std::move(digest);
        reference.destination = tile.rect;
        acquired.push_back(std::move(reference));
    }

    auto existing = g_gpu_frame_cache.find(cache_key);
    if (existing != g_gpu_frame_cache.end()) {
        release_gpu_ram_frame_locked(existing->second);
        g_gpu_frame_cache.erase(existing);
    }

    GpuRamFrameEntry frame;
    frame.width = canvas_width;
    frame.height = canvas_height;
    frame.tiles = std::move(acquired);
    frame.metadata_bytes = gpu_ram_frame_metadata_bytes(frame.tiles.size());
    frame.last_used = ++g_gpu_frame_cache_tick;
    g_gpu_frame_cache_bytes += frame.metadata_bytes;
    g_gpu_frame_cache.emplace(cache_key, std::move(frame));
    evict_global_gpu_frame_cache_locked();
    return g_gpu_frame_cache.find(cache_key) != g_gpu_frame_cache.end();
}

static bool draw_global_gpu_frame_locked(TitleGpuRenderSession *session,
                                         const std::string &cache_key,
                                         gs_texrender_t *target)
{
    if (!session || cache_key.empty() || !target || !session->blit_effect)
        return false;
    std::lock_guard<std::mutex> cache_lock(g_gpu_frame_cache_mutex);
    auto found = g_gpu_frame_cache.find(cache_key);
    if (found == g_gpu_frame_cache.end() ||
        found->second.width != session->width ||
        found->second.height != session->height)
        return false;

    struct vec4 clear;
    vec4_zero(&clear);
    if (!begin_gpu_target(target, session->width, session->height, clear))
        return false;

    gs_eparam_t *image = bgl_effect_param(session->blit_effect,
                                                     "image");
    if (!image) {
        gs_texrender_end(target);
        return false;
    }

    gs_enable_blending(true);
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
    bool complete = true;
    for (const GpuRamTileRef &reference : found->second.tiles) {
        auto tile = g_gpu_ram_tiles.find(reference.digest);
        if (tile == g_gpu_ram_tiles.end() || !tile->second.texture) {
            complete = false;
            break;
        }
        gs_effect_set_texture(image, tile->second.texture);
        gs_matrix_push();
        gs_matrix_identity();
        gs_matrix_translate3f(
            static_cast<float>(reference.destination.x()),
            static_cast<float>(reference.destination.y()), 0.0f);
        while (gs_effect_loop(session->blit_effect, "Draw"))
            gs_draw_sprite(
                tile->second.texture, 0,
                static_cast<uint32_t>(reference.destination.width()),
                static_cast<uint32_t>(reference.destination.height()));
        gs_matrix_pop();
        tile->second.last_used = ++g_gpu_frame_cache_tick;
    }
    gs_texrender_end(target);
    if (!complete)
        return false;
    found->second.last_used = ++g_gpu_frame_cache_tick;
    return gs_texrender_get_texture(target) != nullptr;
}

static bool gpu_session_final_matches_model(
    const TitleGpuRenderSession *session)
{
    return session && session->final_texture && session->has_title &&
           session->published_model_revision == session->model_revision &&
           session->published_title_id == session->title.id;
}

static bool gpu_session_has_published_frame_for_current_title(
    const TitleGpuRenderSession *session)
{
    /* Interactive edits advance model_revision before the replacement raster
     * reaches the graphics thread. The previously published frame is still a
     * safe fallback when it belongs to the same non-empty title identity; it
     * must not be rejected merely because its revision is one edit behind.
     * Lifecycle invalidation clears final_texture/published_title_id before a
     * title, project or scene-collection generation can be replaced. */
    return session && session->final_texture && session->has_title &&
           !session->title.id.empty() &&
           session->published_title_id == session->title.id;
}

static gs_texture_t *publish_stable_gpu_frame(TitleGpuRenderSession *session,
                                              gs_texture_t *frame)
{
    if (!session || !frame)
        return nullptr;
    const int publish_index = session->active_presentation_target == 0 ? 1 : 0;
    gs_texrender_t *target = session->presentation_targets[publish_index];
    if (!target ||
        !draw_gpu_cached_image(session, frame,
                               QRect(0, 0, static_cast<int>(session->width),
                                     static_cast<int>(session->height)),
                               target)) {
        return nullptr;
    }
    gs_texture_t *published = gs_texrender_get_texture(target);
    if (!published)
        return nullptr;
    session->active_presentation_target = publish_index;
    session->final_texture = published;
    session->published_title_id = session->title.id;
    session->published_model_revision = session->model_revision;
    return published;
}

class ScopedGpuCompositorState {
public:
    ScopedGpuCompositorState()
    {
        gs_viewport_push();
        gs_projection_push();
        gs_blend_state_push();
        gs_matrix_push();
        gs_matrix_identity();
    }
    ~ScopedGpuCompositorState()
    {
        gs_matrix_pop();
        gs_blend_state_pop();
        gs_projection_pop();
        gs_viewport_pop();
    }
};

/* Resolve the exact render camera used by a layer. Hardware depth is only
 * meaningful inside a single view/projection, so camera changes split runs
 * even when the layers are otherwise adjacent depth candidates. */
static std::string gpu_hardware_depth_camera_id(const Title &title,
                                                const Layer &layer,
                                                double title_time)
{
    if (!title.render_camera_override_id.empty())
        return title.render_camera_override_id;
    const std::string assigned = bgs::transform3d::resolved_layer_camera_id(
        title, layer, title_time);
    return assigned.empty()
        ? bgs::transform3d::resolved_active_camera_id(title, title_time)
        : assigned;
}

static gs_texture_t *render_gpu_hardware_depth_run(
    TitleGpuRenderSession *session, const Title &title,
    const std::vector<const Layer *> &layers, double title_time,
    gs_texture_t *background, const std::string &compositor_group_id)
{
    if (!session || !session->depth_target || !background || layers.empty() ||
        !session->blit_effect)
        return nullptr;

    auto authored_index = [&](const Layer *needle) {
        for (std::size_t index = 0; index < title.layers.size(); ++index) {
            if (title.layers[index].get() == needle)
                return index;
        }
        return title.layers.size();
    };

    /* Opaque planes establish the persistent Z surface first. Transparent
     * planes then render far-to-near against that surface. Stable sorting keeps
     * authored layer order as the deterministic tie-break for coplanar planes. */
    std::vector<const Layer *> opaque_layers;
    std::vector<const Layer *> transparent_layers;
    opaque_layers.reserve(layers.size());
    transparent_layers.reserve(layers.size());
    for (const Layer *layer : layers) {
        if (!layer)
            continue;
        if (bgs::transform3d::hardware_depth_transparent_candidate(
                title, *layer, title_time))
            transparent_layers.push_back(layer);
        else
            opaque_layers.push_back(layer);
    }
    std::stable_sort(opaque_layers.begin(), opaque_layers.end(),
                     [&](const Layer *left, const Layer *right) {
        return authored_index(left) < authored_index(right);
    });
    std::stable_sort(transparent_layers.begin(), transparent_layers.end(),
                     [&](const Layer *left, const Layer *right) {
        const double left_depth = bgs::transform3d::evaluate(
            title, *left, title_time).camera_depth;
        const double right_depth = bgs::transform3d::evaluate(
            title, *right, title_time).camera_depth;
        if (std::abs(left_depth - right_depth) > 0.000001)
            return left_depth > right_depth;
        return authored_index(left) < authored_index(right);
    });

    auto snapshot_texture = [&](gs_texture_t *texture) -> gs_texrender_t * {
        if (!texture)
            return nullptr;
        gs_texrender_t *snapshot = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
        if (!snapshot ||
            !copy_full_canvas_gpu_texture(session, texture, snapshot)) {
            if (snapshot)
                gs_texrender_destroy(snapshot);
            return nullptr;
        }
        return snapshot;
    };

    auto draw_background = [&](gs_texture_t *texture) -> bool {
        gs_eparam_t *image = bgl_effect_param(session->blit_effect,
                                                         "image");
        if (!image || !texture)
            return false;
        gs_effect_set_texture(image, texture);
        gs_enable_color(true, true, true, true);
        gs_enable_depth_test(false);
        gs_enable_blending(false);
        gs_matrix_push();
        gs_matrix_identity();
        while (gs_effect_loop(session->blit_effect, "Draw"))
            gs_draw_sprite(texture, 0, session->width, session->height);
        gs_matrix_pop();
        return true;
    };

    struct PreparedDepthSurface {
        gs_texture_t *texture = nullptr;
        gs_texture_t *projected_mask = nullptr;
        MaskMode projected_mask_mode = MaskMode::None;
        QRectF projected_bounds;
        double opacity = 0.0;
        bool visible = true;
    };
    struct ScopedOwnedDepthTextures {
        std::vector<gs_texture_t *> textures;
        ~ScopedOwnedDepthTextures()
        {
            for (gs_texture_t *texture : textures) {
                if (texture)
                    gs_texture_destroy(texture);
            }
        }
    } owned_depth_textures;
    std::unordered_map<const Layer *, PreparedDepthSurface> prepared_surfaces;
    std::unordered_set<std::string> depth_mask_visiting;

    /* Resolve all effect and matte graph textures before depth_target begins.
     * OBS texrenders cannot be nested portably, and both the effect stack and
     * matte graph use their own offscreen targets. */
    for (const Layer *layer : layers) {
        if (!layer)
            continue;
        auto found = session->layers.find(gpu_session_layer_id(*layer));
        if (found == session->layers.end() || !found->second.texture)
            return nullptr;
        auto &entry = found->second;
        PreparedDepthSurface prepared;
        prepared.texture = entry.texture;
        const double resolved_time = gpu_layer_render_time(
            title, *layer, title_time);
        prepared.opacity = std::clamp(
            gpu_layer_local_opacity(*layer, resolved_time), 0.0, 1.0);

        /* The raster already includes the conservative padding required by
         * layer-space shadow/glow/blur/outline passes. Project that complete
         * local surface before drawing so perspective rotation cannot clip it
         * back to the original 2D layer box. Homogeneous frustum clipping also
         * provides a stable off-camera rejection without producing enormous
         * screen coordinates near the camera plane. Free-transform grids are
         * conservatively retained because their warped perimeter is not a
         * simple rectangle. */
        const double local_time = std::max(0.0, resolved_time - layer->in_time);
        const double box_sx = std::max(0.0001, eval_box_width(*layer, local_time)) /
            std::max(0.0001, entry.base_box_width);
        const double box_sy = std::max(0.0001, eval_box_height(*layer, local_time)) /
            std::max(0.0001, entry.base_box_height);
        const QRectF padded_local_surface(
            entry.origin.x() * box_sx, entry.origin.y() * box_sy,
            entry.logical_width * box_sx, entry.logical_height * box_sy);
        if (!layer_has_free_transform_quad(*layer, local_time) &&
            !bgs::transform3d::projected_local_bounds(
                title, *layer, title_time, padded_local_surface,
                prepared.projected_bounds, nullptr,
                gpu_hardware_depth_camera_id(title, *layer, title_time))) {
            prepared.visible = false;
        }

        if (prepared.visible && !layer->effects.empty()) {
            gs_texture_t *effected = apply_gpu_layer_effect_stack(
                session, *layer, resolved_time, entry.texture, entry.width,
                entry.height, &entry, false, nullptr, false,
                static_cast<int>(LayerEffectSpace::LayerSpace));
            if (!effected)
                return nullptr;
            const gs_texture_t *cached_effect = entry.effect_cache
                ? gs_texrender_get_texture(entry.effect_cache) : nullptr;
            if (effected != entry.texture && effected != cached_effect) {
                gs_texture_t *snapshot = gs_texture_create(
                    entry.width, entry.height, GS_BGRA, 1, nullptr, 0);
                if (!snapshot)
                    return nullptr;
                gs_copy_texture(snapshot, effected);
                owned_depth_textures.textures.push_back(snapshot);
                effected = snapshot;
            }
            prepared.texture = effected;
        }

        if (layer->mask_mode != MaskMode::None &&
            !layer->mask_source_id.empty()) {
            const Layer *mask_layer = find_layer_by_id(
                title, layer->mask_source_id);
            if (mask_layer && gpu_layer_chain_active_as_matte(
                                  title, *mask_layer, title_time)) {
                const GpuMaskGraphPurpose purpose =
                    mask_mode_is_clipping(layer->mask_mode)
                    ? GpuMaskGraphPurpose::ClippingShape
                    : GpuMaskGraphPurpose::MatteArtwork;
                prepared.projected_mask = render_gpu_mask_graph_texture(
                    session, title, *mask_layer, title_time,
                    gpu_session_layer_id(*mask_layer), compositor_group_id,
                    depth_mask_visiting, purpose);
                if (!prepared.projected_mask)
                    return nullptr;
                prepared.projected_mask_mode = layer->mask_mode;
            } else if (!mask_mode_is_inverted(layer->mask_mode)) {
                prepared.visible = false;
            }
        }
        prepared_surfaces.emplace(layer, prepared);
    }

    auto draw_depth_layer = [&](const Layer &layer, bool write_color,
                                bool *drew_layer = nullptr) -> bool {
        if (drew_layer)
            *drew_layer = false;
        auto found = session->layers.find(gpu_session_layer_id(layer));
        auto prepared_it = prepared_surfaces.find(&layer);
        if (found == session->layers.end() ||
            prepared_it == prepared_surfaces.end())
            return false;
        auto &entry = found->second;
        const PreparedDepthSurface &prepared = prepared_it->second;
        if (!prepared.visible || prepared.opacity <= 0.000001)
            return true;

        /* libobs does not expose a portable depth-write mask. A read-only draw
         * therefore writes Z temporarily; its color is retained and persistent
         * writer depth is rebuilt immediately afterwards. */
        gs_enable_depth_test(true);
        gs_depth_function(layer.depth_test ? GS_LEQUAL : GS_ALWAYS);
        gs_enable_color(write_color, write_color, write_color, write_color);
        gs_enable_blending(write_color);
        if (write_color)
            gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
        const bool drawn = draw_gpu_layer_texture(
            session, &entry, title, layer, prepared.texture,
            entry.origin, entry.width, entry.height,
            entry.logical_width, entry.logical_height,
            entry.image_clip_rect, entry.base_box_width,
            entry.base_box_height, title_time,
            static_cast<float>(prepared.opacity), true,
            prepared.projected_mask, prepared.projected_mask_mode);
        if (drew_layer)
            *drew_layer = drawn;
        return drawn;
    };

    bool target_active = false;
    auto end_target = [&]() {
        if (!target_active)
            return;
        gs_enable_color(true, true, true, true);
        gs_enable_depth_test(false);
        gs_depth_function(GS_LEQUAL);
        gs_set_cull_mode(GS_NEITHER);
        gs_enable_blending(true);
        gs_texrender_end(session->depth_target);
        target_active = false;
    };

    auto begin_target = [&](gs_texture_t *pass_background) -> bool {
        gs_texrender_reset(session->depth_target);
        if (!gs_texrender_begin(session->depth_target, session->width,
                                session->height))
            return false;
        target_active = true;

        struct vec4 clear;
        vec4_zero(&clear);
        gs_ortho(0.0f, static_cast<float>(session->width),
                 0.0f, static_cast<float>(session->height), -100.0f, 100.0f);
        gs_clear(GS_CLEAR_COLOR | GS_CLEAR_DEPTH | GS_CLEAR_STENCIL,
                 &clear, 1.0f, 0);
        gs_set_cull_mode(GS_NEITHER);
        if (!draw_background(pass_background)) {
            end_target();
            return false;
        }
        return true;
    };

    /* Consecutive camera runs can use depth_target's previous color as input.
     * Snapshot before resetting so the render target never samples itself. */
    gs_texrender_t *background_snapshot = nullptr;
    if (background == gs_texrender_get_texture(session->depth_target)) {
        background_snapshot = snapshot_texture(background);
        if (!background_snapshot)
            return nullptr;
        background = gs_texrender_get_texture(background_snapshot);
    }

    bool success = begin_target(background);
    if (background_snapshot)
        gs_texrender_destroy(background_snapshot);
    if (!success)
        return nullptr;

    std::vector<const Layer *> persistent_writers;
    persistent_writers.reserve(layers.size());

    auto draw_layer_with_depth_semantics = [&](const Layer *layer) -> bool {
        if (!layer)
            return true;
        const bool writer = bgs::transform3d::hardware_depth_writer(
            title, *layer, title_time);
        const bool read_only = bgs::transform3d::hardware_depth_read_only(
            title, *layer, title_time);
        if (!writer && !read_only)
            return true;
        bool drew_layer = false;
        if (!draw_depth_layer(*layer, true, &drew_layer))
            return false;
        if (!drew_layer)
            return true;
        if (writer) {
            persistent_writers.push_back(layer);
            return true;
        }

        /* Preserve the just-composited color but remove the unavoidable
         * temporary Z write. Rebuild only persistent writers, including any
         * explicitly depth-writing transparent planes already rendered. */
        end_target();
        gs_texture_t *current = gs_texrender_get_texture(session->depth_target);
        gs_texrender_t *color_snapshot = snapshot_texture(current);
        if (!color_snapshot)
            return false;
        bool rebuilt = begin_target(gs_texrender_get_texture(color_snapshot));
        if (rebuilt) {
            for (const Layer *persistent : persistent_writers) {
                if (!persistent || !draw_depth_layer(*persistent, false)) {
                    rebuilt = false;
                    break;
                }
            }
        }
        gs_texrender_destroy(color_snapshot);
        return rebuilt;
    };

    for (const Layer *layer : opaque_layers) {
        if (!draw_layer_with_depth_semantics(layer)) {
            success = false;
            break;
        }
    }
    if (success) {
        for (const Layer *layer : transparent_layers) {
            if (!draw_layer_with_depth_semantics(layer)) {
                success = false;
                break;
            }
        }
    }

    end_target();
    return success ? gs_texrender_get_texture(session->depth_target) : nullptr;
}

static gs_texture_t *render_gpu_session_locked(
    TitleGpuRenderSession *session, gs_texture_t *external_background = nullptr,
    bool isolate_editor_background_from_effects = false)
{
    if (!session || !session->has_title)
        return nullptr;
    const bool destination_composite = external_background != nullptr;
    if (session->state_transaction_pending.load(std::memory_order_acquire))
        return destination_composite
            ? nullptr
            : (gpu_session_has_published_frame_for_current_title(session)
                   ? session->final_texture
                   : nullptr);
    const double render_scale = session->editor_draft
        ? std::clamp(static_cast<double>(session->preview_quality_scale), 0.25, 1.0)
        : 1.0;
    const uint32_t width = clamped_source_dimension(
        std::max(1, static_cast<int>(std::ceil(session->title.width * render_scale))));
    const uint32_t height = clamped_source_dimension(
        std::max(1, static_cast<int>(std::ceil(session->title.height * render_scale))));
    session->width = width;
    session->height = height;
    if (!ensure_gpu_session_objects(session, width, height))
        return nullptr;

    /* Raster uploads can execute shader draws (notably Phase 11 primitives).
     * Enter the isolated GPU state before either the complete-frame presentation
     * path or the live compositor path so no caller state leaks into texrender
     * operations. */
    ScopedGpuCompositorState state;

    /* A submitted final cache frame is already the complete compositor output.
     * It must be presented before inspecting or uploading live layer rasters.
     * The previous ordering validated every stale/pending text and shape raster
     * first, so a perfectly valid RAM/disk prerender was rejected with
     * "GPU raster set is incomplete". Fresh editor sessions hit this reliably;
     * OBS sources hit it nondeterministically depending on whether the live
     * first-frame raster happened to finish before the cache token arrived. */
    if (!destination_composite && !session->frame_dirty &&
        gpu_session_final_matches_model(session))
        return session->final_texture;

    if (!destination_composite && session->use_gpu_cached_final) {
        if (!draw_global_gpu_frame_locked(
                session, session->submitted_gpu_cache_key,
                session->frame_a)) {
            session->last_error = "GPU RAM cache frame is no longer resident.";
            return nullptr;
        }
        gs_texture_t *rendered = gs_texrender_get_texture(session->frame_a);
        gs_texture_t *published = publish_stable_gpu_frame(session, rendered);
        if (!published) {
            session->last_error = "Could not publish GPU RAM cache frame.";
            return gpu_session_has_published_frame_for_current_title(session)
                ? session->final_texture : nullptr;
        }
        session->frame_dirty = false;
        session->last_error = nullptr;
        if (TitlePreferences::cache_playback_logging_enabled()) {
            BGL_LOG_INFO("CachePlayback", QStringLiteral(
                "stage=gpu-publish-ram-final title=%1 revision=%2 key=%3 published=%4 presentationTarget=%5")
                .arg(QString::fromStdString(session->title.id))
                .arg(session->model_revision)
                .arg(QString::fromStdString(session->submitted_gpu_cache_key))
                .arg(reinterpret_cast<quintptr>(published), 0, 16)
                .arg(session->active_presentation_target));
        }
        return published;
    }

    if (!destination_composite && session->use_submitted_final) {
        if (!upload_gpu_cached_image(session,
                                     session->pending_submitted_final,
                                     session->submitted_final_pending,
                                     session->submitted_final_image_key,
                                     session->submitted_final_texture,
                                     session->submitted_final_width,
                                     session->submitted_final_height) ||
            !draw_gpu_cached_image(session, session->submitted_final_texture,
                                   session->submitted_final_rect,
                                   session->frame_a)) {
            session->last_error = "Could not upload cached GPU frame.";
            return nullptr;
        }
        session->uploaded_final_serial = session->submitted_final_serial;
        if (TitlePreferences::cache_playback_logging_enabled()) {
            BGL_LOG_INFO("CachePlayback", QStringLiteral(
                "stage=gpu-upload-final serial=%1 texture=%2 textureSize=%3x%4 rect=%5,%6,%7x%8")
                .arg(session->uploaded_final_serial)
                .arg(reinterpret_cast<quintptr>(session->submitted_final_texture), 0, 16)
                .arg(session->submitted_final_width).arg(session->submitted_final_height)
                .arg(session->submitted_final_rect.x()).arg(session->submitted_final_rect.y())
                .arg(session->submitted_final_rect.width()).arg(session->submitted_final_rect.height()));
        }
        gs_texture_t *rendered = gs_texrender_get_texture(session->frame_a);
        gs_texture_t *published = publish_stable_gpu_frame(session, rendered);
        if (!published) {
            session->last_error = "Could not publish cached GPU frame.";
            return gpu_session_has_published_frame_for_current_title(session)
                ? session->final_texture : nullptr;
        }
        session->published_final_serial = session->uploaded_final_serial;
        if (TitlePreferences::cache_playback_logging_enabled()) {
            BGL_LOG_INFO("CachePlayback", QStringLiteral(
                "stage=gpu-publish-final serial=%1 rendered=%2 published=%3 presentationTarget=%4")
                .arg(session->published_final_serial)
                .arg(reinterpret_cast<quintptr>(rendered), 0, 16)
                .arg(reinterpret_cast<quintptr>(published), 0, 16)
                .arg(session->active_presentation_target));
        }
        session->frame_dirty = false;
        session->last_error = nullptr;
        return published;
    }

    /* Only live composition and cached-prefix composition depend on layer
     * rasters. Validate them transactionally after the full-frame cache paths
     * have had the opportunity to publish their self-contained output. */
    bool all_required_rasters_ready = true;
    for (auto &pair : session->layers) {
        /* Scene-mask rasters are an auxiliary output family. They are uploaded
         * on demand by title_gpu_render_session_render_auxiliary_layer() and
         * must never gate the main title frame in OBS. */
        if (is_gpu_scene_mask_raster_id(pair.first))
            continue;
        auto &entry = pair.second;
        const bool was_pending = entry.pending_upload;
        const bool optional_gpu_raster = entry.gpu_text || entry.gpu_primitive;
        /* A null pending image with no primitive is an intentional retirement,
         * not a failed replacement. The upload helper destroys the old texture
         * and returns false because there is no new raster. Treating that as a
         * missing required raster preserved the previous complete compositor
         * frame for one extra draw, visibly retaining deleted/hidden content. */
        const bool retiring = was_pending && !entry.gpu_text &&
                              !entry.gpu_primitive &&
                              entry.pending_image.isNull();
        const bool ready = upload_gpu_layer_raster(session, entry);
        if (was_pending && !ready && !retiring) {
            if (optional_gpu_raster && entry.texture) {
                /* GPU text and primitive rasters are accelerators. A failure
                 * must never suppress an already-visible complete title. Their
                 * render helpers invalidate the layer key and request a
                 * compatibility rebuild; publish all other ready layers while
                 * retaining the last valid optional texture. */
                BGL_LOG_WARNING("GpuPipeline", QStringLiteral(
                    "stage=optional-raster-fail-open layer=%1 kind=%2 retainedTexture=%3")
                    .arg(QString::fromStdString(pair.first))
                    .arg(entry.text_layer ? QStringLiteral("text")
                                          : QStringLiteral("primitive"))
                    .arg(entry.texture ? 1 : 0));
            } else {
                /* Development Version 248: on a fresh editor session there may
                 * be no retained texture yet. Publishing anyway produces a
                 * blank text/vector layer until some unrelated interaction
                 * invalidates the frame. Defer first-frame publication and let
                 * the compatibility raster rebuild run instead. */
                all_required_rasters_ready = false;
            }
        }
    }

    /* Live frame publication remains transactional for the first frame as well
     * as later replacements. A same-title complete frame may remain visible
     * while live rasters retry; a new session/title returns no texture. */
    if (!all_required_rasters_ready) {
        session->frame_dirty = true;
        session->last_error =
            "GPU raster set is incomplete; frame publication deferred.";
        BGL_LOG_DEBUG("GpuPipeline", QStringLiteral(
            "stage=transactional-publication action=defer title=%1 revision=%2 hasPrevious=%3 mode=%4")
            .arg(QString::fromStdString(session->title.id))
            .arg(session->model_revision)
            .arg(gpu_session_has_published_frame_for_current_title(session) ? 1 : 0)
            .arg(session->use_base_frame ? QStringLiteral("cached-prefix")
                                         : QStringLiteral("live")));
        return gpu_session_has_published_frame_for_current_title(session)
            ? session->final_texture : nullptr;
    }

    if (!destination_composite && session->use_base_frame &&
        !session->use_gpu_cached_base &&
        !upload_gpu_cached_image(session,
                                 session->pending_base_frame,
                                 session->base_frame_pending,
                                 session->base_frame_image_key,
                                 session->base_frame_texture,
                                 session->base_frame_width,
                                 session->base_frame_height)) {
        session->last_error = "Could not upload cached GPU prefix.";
        return nullptr;
    }

    gs_texture_t *frame = nullptr;
    if (destination_composite && !isolate_editor_background_from_effects) {
        if (!copy_full_canvas_gpu_texture(session, external_background,
                                          session->frame_a)) {
            session->last_error =
                "Could not initialize GPU frame from destination background.";
            return nullptr;
        }
        frame = gs_texrender_get_texture(session->frame_a);
    } else if (destination_composite && isolate_editor_background_from_effects) {
        struct vec4 transparent; vec4_zero(&transparent);
        if (!begin_gpu_target(session->frame_a, width, height, transparent))
            return nullptr;
        gs_texrender_end(session->frame_a);
        frame = gs_texrender_get_texture(session->frame_a);
    } else if (session->use_base_frame && session->use_gpu_cached_base) {
        if (!draw_global_gpu_frame_locked(
                session, session->base_gpu_cache_key,
                session->frame_a)) {
            session->last_error = "GPU RAM cache prefix is no longer resident.";
            return nullptr;
        }
        frame = gs_texrender_get_texture(session->frame_a);
    } else if (session->use_base_frame && session->base_frame_texture) {
        if (!draw_gpu_cached_image(session, session->base_frame_texture,
                                   session->base_frame_rect,
                                   session->frame_a)) {
            session->last_error = "Could not initialize GPU frame from cached prefix.";
            return nullptr;
        }
        frame = gs_texrender_get_texture(session->frame_a);
    } else {
        struct vec4 background;
        vec4_zero(&background);
        if (session->first_layer == 0) {
            double r, g, b, a;
            unpack_color(session->title.bg_color, r, g, b, a);
            vec4_set(&background, static_cast<float>(r * a),
                     static_cast<float>(g * a), static_cast<float>(b * a),
                     static_cast<float>(a));
        }
        if (!begin_gpu_target(session->frame_a, width, height, background))
            return nullptr;
        gs_texrender_end(session->frame_a);
        frame = gs_texrender_get_texture(session->frame_a);
    }

    bool current_is_a = true;
    const std::size_t layer_count = session->title.layers.size();
    /* Respect the configured layer range for destination-aware passes too.
     * This allows OBS scene masks to be inserted at their real stack position
     * and then re-composite only the layers above them. */
    const std::size_t first = std::min(session->first_layer, layer_count);
    const std::size_t last = std::min(session->last_layer, layer_count);

    std::unordered_set<std::string> emitted_clipping_bases;
    auto composite_clipping_base = [&](const Layer &target) -> bool {
        if (!mask_mode_is_clipping(target.mask_mode) ||
            target.mask_source_id.empty())
            return true;
        if (emitted_clipping_bases.find(target.mask_source_id) !=
            emitted_clipping_bases.end())
            return true;
        const Layer *base_layer = find_layer_by_id(
            session->title, target.mask_source_id);
        if (!base_layer)
            return true;
        if (!gpu_layer_chain_active_as_matte(
                session->title, *base_layer, session->time))
            return true;
        /* Keep clipping-mask evaluation independent from source artwork
         * visibility.  MatteOnly remains an active mask while only
         * VisibleAndMatte is composited as the visible clipping base. */
        if (base_layer->matte_visibility_mode !=
            MatteVisibilityMode::VisibleAndMatte) {
            emitted_clipping_bases.insert(base_layer->id);
            return true;
        }
        std::unordered_set<std::string> base_visiting;
        gs_texture_t *base_texture = render_gpu_mask_graph_texture(
            session, session->title, *base_layer, session->time,
            gpu_session_layer_id(*base_layer), target.parent_id,
            base_visiting, GpuMaskGraphPurpose::MatteArtwork);
        if (!base_texture)
            return false;
        gs_texrender_t *next = current_is_a ? session->frame_b
                                            : session->frame_a;
        if (!composite_gpu_frame_layer(session, frame, base_texture,
                                       base_layer->blend_mode, 1.0, next))
            return false;
        frame = gs_texrender_get_texture(next);
        current_is_a = !current_is_a;
        emitted_clipping_bases.insert(base_layer->id);
        return true;
    };

    const std::vector<std::size_t> ordered_root_indices =
        bgs::transform3d::ordered_root_layer_indices(
            session->title, first, last, session->time);

    struct HardwareDepthRun {
        std::vector<std::size_t> indices;
        std::vector<const Layer *> layers;
        std::size_t writer_count = 0;
        std::size_t read_only_count = 0;
        std::size_t transparent_count = 0;
    };
    std::unordered_map<std::size_t, HardwareDepthRun> hardware_depth_runs;
    std::size_t depth_cursor = 0;
    while (depth_cursor < ordered_root_indices.size()) {
        const std::size_t index = ordered_root_indices[depth_cursor];
        const auto &layer_ptr = session->title.layers[index];
        const Layer *layer = layer_ptr.get();
        const bool visible_candidate = layer &&
            gpu_layer_chain_visible(session->title, *layer, session->time) &&
            layer_should_render_as_visible_content_for_gpu_session(session, session->title, *layer) &&
            bgs::transform3d::layer_passes_backface_culling(
                session->title, *layer, session->time) &&
            bgs::transform3d::hardware_depth_candidate(
                session->title, *layer, session->time) &&
            session->layers.find(gpu_session_layer_id(*layer)) !=
                session->layers.end();
        if (!visible_candidate) {
            ++depth_cursor;
            continue;
        }

        HardwareDepthRun run;
        const std::string camera_id = gpu_hardware_depth_camera_id(
            session->title, *layer, session->time);
        std::size_t run_cursor = depth_cursor;
        while (run_cursor < ordered_root_indices.size()) {
            const std::size_t candidate_index =
                ordered_root_indices[run_cursor];
            const auto &candidate_ptr =
                session->title.layers[candidate_index];
            const Layer *candidate = candidate_ptr.get();
            if (!candidate) {
                ++run_cursor;
                continue;
            }
            const bool candidate_visible = gpu_layer_chain_visible(
                    session->title, *candidate, session->time) &&
                layer_should_render_as_visible_content_for_gpu_session(
                    session, session->title, *candidate) &&
                bgs::transform3d::layer_passes_backface_culling(
                    session->title, *candidate, session->time);
            if (!candidate_visible) {
                ++run_cursor;
                continue;
            }
            if (!bgs::transform3d::hardware_depth_candidate(
                    session->title, *candidate, session->time) ||
                gpu_hardware_depth_camera_id(session->title, *candidate,
                                             session->time) !=
                    camera_id)
                break;
            const auto raster = session->layers.find(
                gpu_session_layer_id(*candidate));
            if (raster == session->layers.end() || !raster->second.texture)
                break;
            run.indices.push_back(candidate_index);
            run.layers.push_back(candidate);
            if (bgs::transform3d::hardware_depth_writer(
                    session->title, *candidate, session->time))
                ++run.writer_count;
            else if (bgs::transform3d::hardware_depth_read_only(
                         session->title, *candidate, session->time))
                ++run.read_only_count;
            if (bgs::transform3d::hardware_depth_transparent_candidate(
                    session->title, *candidate, session->time))
                ++run.transparent_count;
            ++run_cursor;
        }
        /* One persistent writer can occlude any later plane. A transparent-
         * only run is also useful when at least two planes need deterministic
         * far-to-near compositing, even if neither persists depth. */
        if (run.layers.size() >= 2 &&
            (run.writer_count >= 1 || run.transparent_count >= 2))
            hardware_depth_runs.emplace(run.indices.front(), std::move(run));
        depth_cursor = std::max(depth_cursor + 1, run_cursor);
    }

    std::unordered_set<std::size_t> rendered_hardware_depth_layers;
    for (std::size_t index : ordered_root_indices) {
        if (rendered_hardware_depth_layers.find(index) !=
            rendered_hardware_depth_layers.end())
            continue;
        const auto depth_run = hardware_depth_runs.find(index);
        if (depth_run != hardware_depth_runs.end()) {
            if (gs_texture_t *depth_frame = render_gpu_hardware_depth_run(
                    session, session->title, depth_run->second.layers,
                    session->time, frame)) {
                frame = depth_frame;
                rendered_hardware_depth_layers.insert(
                    depth_run->second.indices.begin(),
                    depth_run->second.indices.end());
                continue;
            }
        }
        const auto &layer_ptr = session->title.layers[index];
        if (!layer_ptr)
            continue;
        const Layer &layer = *layer_ptr;
        const double layer_time = gpu_layer_render_time(
            session->title, layer, session->time);
        if (!gpu_layer_chain_visible(session->title, layer, session->time) ||
            !layer_should_render_as_visible_content_for_gpu_session(session, session->title, layer) ||
            !bgs::transform3d::layer_passes_backface_culling(
                session->title, layer, session->time))
            continue;
        /* Children are composited through their group surface so the group can
         * behave exactly like a normal layer for effects, masks and blending. */
        if (!layer.parent_id.empty()) {
            const Layer *parent = find_layer_by_id(session->title, layer.parent_id);
            if (parent && layer_type_is_container(parent->type))
                continue;
        }
        if (!composite_clipping_base(layer))
            continue;
        if (layer_type_is_container(layer.type)) {
            /* Preserve a pre-group-effects copy only when an enabled effect
             * actually affects layers behind. The previous unconditional path
             * rendered every visible group twice per frame, even for an empty
             * effect stack, which turned animated groups into a permanent
             * full-canvas performance penalty. */
            gs_texrender_t *group_silhouette_target = nullptr;
            gs_texture_t *group_silhouette = nullptr;
            const double group_local_time =
                std::max(0.0, layer_time - layer.in_time);
            const bool has_group_backdrop_effect = std::any_of(
                layer.effects.begin(), layer.effects.end(),
                [&](const LayerEffect &effect) {
                    return effect.affect_layers_behind &&
                           eval_effect_enabled(effect, group_local_time);
                });
            if (has_group_backdrop_effect) {
                gpu_note_group_silhouette_pass(session, layer.id);
                Layer silhouette_group = layer;
                silhouette_group.effects.clear();
                std::unordered_set<std::string> silhouette_visiting;
                gs_texture_t *raw_group = render_gpu_group_graph_texture(
                    session, session->title, silhouette_group, session->time,
                    silhouette_visiting);
                if (raw_group) {
                    group_silhouette_target = gs_texrender_create(
                        GS_BGRA, GS_ZS_NONE);
                    if (group_silhouette_target &&
                        copy_full_canvas_gpu_texture(session, raw_group,
                                                     group_silhouette_target))
                        group_silhouette = gs_texrender_get_texture(
                            group_silhouette_target);
                }
            }

            std::unordered_set<std::string> visiting;
            gs_texture_t *foreground = render_gpu_group_graph_texture(
                session, session->title, layer, session->time, visiting,
                frame);
            if (!foreground) {
                if (group_silhouette_target)
                    gs_texrender_destroy(group_silhouette_target);
                continue;
            }
            if (has_group_backdrop_effect && !group_silhouette)
                group_silhouette = foreground;

