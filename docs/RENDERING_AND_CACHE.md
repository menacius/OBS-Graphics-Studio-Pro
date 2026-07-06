# Rendering and cache

## Rendering contract

The editor preview and OBS source are intended to use the same title model, effect order, masks, mattes, transforms, text layout, and playback state. Compatibility CPU paths exist for fallback and diagnostics, while supported content uses GPU-backed compositing and texture-resident intermediates.

Ordinary layer effects run in layer space on a padded raster. Full-canvas passes are reserved for effects or adjustment behavior that genuinely requires scene-space access. Background Color, Outline, Shadow, Glow, blur families, generated gradients, and masks must preserve transparent bounds and avoid accidental full-canvas expansion.

## Effect hot-path and resource lifetime

Effect shaders are compiled on first use and retained in an indexed built-in cache; extension shaders use a stable-ID map. Rendering an effect stack reuses session-owned evaluated-state and shader-pointer arrays after their initial capacity growth. The compatibility GPU bridge retains a dimension-aware dynamic upload texture, two ping-pong render targets, one staging surface and reusable CPU transfer buffers. Resources are recreated only when dimensions change and are released with the OBS graphics subsystem. An empty or bypassed stack takes a direct fast path and creates no pass resources.

Debug counters record parameter-resolution count/time, shader-cache hits/misses, bounds evaluations, executed effect passes/pass time, compatibility resource-pool hits/misses and empty-stack fast paths. No file access or shader compilation occurs after a shader has entered the runtime cache. Native host/reference-frame testing remains the release gate for pixel parity and real GPU timing.

## Editor presentation

During normal editing and scrubbing the editor can refresh up to the active monitor rate. During authored playback, frame advancement follows the project frame rate. Interactive quality can be reduced temporarily during high-frequency manipulation, then restored for the settled frame.

### Interactive transforms and GPU model authority

3D Move, Rotate, and Scale reuse resident layer rasters and update only the evaluated GPU transform snapshot while the gizmo is active. Duplicate pointer coordinates are discarded before they can create another model revision, and repeated input is coalesced into one pending presentation rather than an unbounded render queue. Active matrix drags are paced by the detected monitor interval; full raster/layout edits remain bounded by measured render cost.

Transform-only state is strictly transient. Keyframe insertion/removal, interpolation changes, external property edits, and post-drag geometry publication require one authoritative GPU model snapshot before lightweight matrix updates can resume. Mouse release clears the transform-only flag before publishing `layer_geometry_changed()`. A separate settle-priority flag gives the exact final frame monitor-cadence scheduling without misclassifying it as transform-only. This prevents a stale render session after adding or editing keyframes.

While an authoritative editor model refresh is pending, cached final-frame submission is bypassed so an old prerender cannot replace the edited state. This does not clear or mutate the independent OBS/RAM/disk cache; normal cache invalidation remains owned by the title-edit pipeline.


## Audio runtime

Audio media is decoded and waveform data is generated outside the UI and OBS render threads. The source runtime mixes fixed-size sample blocks with continuous timestamps, bounded buffering, transport-discontinuity resets, and separate low-latency editor monitoring. Mix, trim, fade, pan, gain, mute/solo, loop, keyframes, and DSP changes do not rebuild unrelated visual cache frames.

Video frames are also decoded asynchronously. The renderer requests a frame for the absolute title time instead of advancing a private video clock; stale decode generations are discarded. Every embedded audio stream remains a normal Audio mixer clip, but its effective source, media range, loop and timeline range are resolved from its owner Video at mix time. This keeps picture and all streams on one transport without mutating the authoring model from playback threads.

Editor transport publishes an exact playhead and direction to its private monitor source. Reverse playback reads decoded samples in descending order while OBS packet timestamps remain monotonic. Titles containing standalone Audio or Video with embedded audio activate an OBS mixer device.

## RAM and disk cache

- RAM cache stores render-ready frame payloads for low-latency presentation.
- Disk cache stores compressed frame data and hydrates it back into runtime textures when needed.
- Cache identity includes title content, cue snapshot, render dimensions, project timing, persistence state, and other visual inputs.
- Reusing identical payloads avoids double-counting RAM ownership.
- Alpha-cropped/tiled payloads reduce storage for sparse graphics.

## Invalidation

Edits should invalidate only affected frames, layers, tiles, or cue states. Selection changes and UI-only changes must not invalidate rendered content. Structural changes such as layer order, effect order, timing, masks, or title dimensions may require broader invalidation.

Live cue rows use structural and value-aware invalidation so adding, deleting, or editing one row does not rebuild unrelated cached frames. Background Persistence states are included in the cache identity and progress calculation. Keyframes and layer transitions use the same frozen persistence sample time, so manual uncue and cue-row changes cannot replay or advance only the transition portion of a persistent state.

## Scheduling and playback

Realtime OBS output, cue/uncue, editor playback, scrubbing, and direct interaction have priority over background prerender. Cache jobs are inserted in batches, deduplicated through indexed keys, and split into urgent and background lanes. RAM LRU updates are constant-time. Disk frames are hydrated asynchronously by workers, while bounded disk writes remain best-effort under backpressure rather than blocking playback. UI progress and diagnostics are coalesced instead of emitted for every frame.

**Play after rendering** waits for required frames before authored playback. If an exact cached frame is unavailable, the source can use a real-time fallback rather than presenting an unrelated or stale frame.

## Cache exclusions

Real-time clocks and ordinary continuously advancing tickers are not cached. A ticker mode driven by a keyframeable completion property can be cacheable because the output is a deterministic function of the title timeline.

## Troubleshooting

- A title stuck at queued/100% indicates publication or generation-state disagreement; clear the affected cache generation rather than repeatedly rebuilding all titles.
- A first-frame flash generally indicates source startup state or cache hydration ordering; the source should present the correct authored frame before audio/video playback advances.
- Editor/source visual disagreement should be treated as a compositing-contract bug, not fixed with separate appearance tweaks.
- Shutdown must stop workers and release GPU/cache resources before OBS destroys the graphics subsystem.

## Text rendering compatibility boundaries

The editor and OBS source consume the same immutable text layout. The on-canvas `QTextEdit` remains a transparent IME/input bridge while visible glyphs, selection geometry, and caret overlays are owned by the shared layout/GPU path. Compatibility rasterization is limited to cases such as color-font glyphs, ticker output, or active per-character transitions that are not yet represented by the persistent atlas path.

Text stroke composition follows **Behind -> Fill -> Front** ordering. Text-only stroke masks preserve the outer, mid, and inner regions so inner and outer alignment remain consistent in both editor and source rendering.

## Planar 3D rendering pipeline

The legacy 2D affine compositor remains the compatibility baseline. Compatible opaque 3D planes can enter a persistent hardware-depth run with per-pixel alpha-clipped depth writing. Transparent compatible planes are resolved in a deterministic far-to-near pass. Masks, mattes, custom blend modes, groups, scene inputs and unsupported effects retain established offscreen/full-frame paths.

Ordinary artwork effects run on a padded layer-space raster before projection; Motion Blur is post-transform; effects that affect layers behind remain screen-space destination passes. Complete padded bounds are projected with homogeneous clipping so glow, shadow, outline and blur survive perspective rotation and near/canvas crossings without flickering or clipping.

## Camera-aware 3D motion blur

Every shutter sample evaluates the layer, all transform parents, assigned/active camera, projection and visibility at that title time. Covered motion includes XYZ paths, Rotation, Orientation, perspective scale from Z travel, parent motion, camera dolly/orbit and camera assignment. Projected travel is measured from corners, edge midpoints and center. Below the stationary threshold, the sharp result is rendered once, preventing transparent images from developing horizontal smears or duplicate alpha edges.

## Timeline/cache paint audit

Timeline/cache paint is a batched read path: frame states are indexed by title and fetched once for the visible range, while static-frame visual hashes are calculated through one contiguous window. Projected gizmo geometry is reused between hit testing and painting. Group rendering reuses persistent ping/pong targets and skips the additional silhouette pass unless an enabled effect actually reads layers behind.

Debug counters cover queue peaks, render/readback duration, notification coalescing, cache indexing, visible Timeline inspection, group calls/slow frames, active background work and gizmo-cache efficiency. Normal rendering, playback and interaction have priority over background work.

## Threading and lifetime contract

Network access, media decode, waveform generation, cache compression, disk hydration/writes and proxy generation stay outside UI and render threads. Audio processing avoids blocking file/network operations. Jobs carry cancellation/generation state and must stop before a title, source or OBS graphics subsystem is destroyed. Repeated title open/close, audio deletion, proxy deletion and OBS shutdown must release workers, GPU resources and cached data without retained-growth trends.
