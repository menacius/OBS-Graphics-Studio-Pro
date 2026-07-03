# Rendering and cache

## Rendering contract

The editor preview and OBS source are intended to use the same title model, effect order, masks, mattes, transforms, text layout, and playback state. Compatibility CPU paths exist for fallback and diagnostics, while supported content uses GPU-backed compositing and texture-resident intermediates.

Ordinary layer effects run in layer space on a padded raster. Full-canvas passes are reserved for effects or adjustment behavior that genuinely requires scene-space access. Background Color, Outline, Shadow, Glow, blur families, generated gradients, and masks must preserve transparent bounds and avoid accidental full-canvas expansion.

## Editor presentation

During normal editing and scrubbing the editor can refresh up to the active monitor rate. During authored playback, frame advancement follows the project frame rate. Interactive quality can be reduced temporarily during high-frequency manipulation, then restored for the settled frame.


## Audio runtime

Audio media is decoded and waveform data is generated outside the UI and OBS render threads. The source runtime mixes fixed-size sample blocks with continuous timestamps, bounded buffering, transport-discontinuity resets, and separate low-latency editor monitoring. Mix, trim, fade, pan, gain, mute/solo, loop, keyframes, and DSP changes do not rebuild unrelated visual cache frames.

Editor transport publishes an exact playhead and direction to its private monitor source. Reverse playback reads decoded samples in descending order while OBS packet timestamps remain monotonic. Only titles containing an Audio layer activate an OBS mixer device.

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
