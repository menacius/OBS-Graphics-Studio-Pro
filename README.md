# Broadcast Graphics Live

<img width="1920" height="1044" alt="Screenshot 2026-07-03 105852" src="https://github.com/user-attachments/assets/28b51b4c-929a-4461-9568-a134d16e4d4d" />

**Broadcast Graphics Live** is a native C++/Qt broadcast-graphics plugin for OBS Studio. It combines a dockable title manager, layered motion-graphics editor, live text and image cueing, audio layers, reusable assets, native Stinger transitions, GPU rendering, and RAM/disk prerendering without browser sources or a separate playout application.

**Current build:** `v0.8.9-alpha` · `Development Version 189`

## What is new in v0.8.9-alpha

This release consolidates the work completed since `v0.8.8-alpha` Development Version 144.

### Complete audio-layer workflow

Audio is now a first-class layer type with asynchronous decoding, waveform generation, timeline range and fade handles, gain, pan, mute, solo, looping, independent playback, keyframes, and an audio-effect stack. The editor has synchronized monitor playback, including real reverse audio during reverse and ping-pong transport. OBS Audio Mixer devices are shown only for titles that actually contain audio layers and update dynamically when the title structure changes.

### Native OBS Stinger graphics

Stinger titles run as native OBS transitions with synchronized video and audio, transition-point switching, pre-roll/post-roll timeline regions, proxy validation, and safe live-render fallback. They support both **Switch at Point** and **Manual Scene Animation** modes; manual Scene A/B inputs behave like ordinary visual layers and can use transforms, timing, keyframes, hierarchy, masks, mattes, blend modes, transitions, and effects.

### Faster cache, prerender, and live cue playback

The cache pipeline now uses batched scheduling, an urgent realtime lane, constant-time duplicate/LRU bookkeeping, asynchronous disk hydration, bounded non-blocking disk writes, and coalesced UI progress updates. Live Cue Persistence treats transitions and keyframes as one persistent visual state, preventing transition replay during manual uncue or cue-row changes. Background caching yields to editing, cueing, and realtime playback.

### Reliability, migration, and editor improvements

Serialization now has a contiguous migration/validation path for the features introduced after Development Version 144, including audio, Stinger metadata, proxy state, temporal/spatial Bezier data, external bindings, and dock state. The release also includes cue/source and thumbnail fixes, dense-text GPU overdraw reduction, cross-platform compile repairs, broader automated contracts, and a clearer layer-list state: an inactive effect stack keeps its FX badge visible with a diagonal strike-through.

## Features

### Native OBS Integration

* Native C++/Qt plugin for OBS Studio.
* Native OBS title sources without browser sources or an external playout application.
* Native OBS Stinger transitions with synchronized video and audio.
* Dockable **Titles and Graphics** control panel.
* Add saved titles directly to OBS scenes.
* Rebind an existing OBS source to another saved title.
* Automatic OBS Audio Mixer visibility only for titles containing audio layers.
* OBS theme integration and persistent editor layout.

### Layer-Based Motion Graphics Editor

* Layered canvas and timeline workflow inspired by professional motion-graphics applications.
* Text, clock, ticker, shape, image, audio, asset, group, adjustment and Stinger Scene A/B layers.
* Layer reordering, duplication, locking, visibility and hierarchy controls.
* Independent parenting and grouping.
* Free Transform, corner pinning and direct vector editing.
* External text and image drag-and-drop.
* Canvas rulers, guides, snapping, origins and safe areas.
* Persistent layer, panel and workspace state.

### Text and Typography

* Rich-text layers with multiple styles inside the same text box.
* Direct inline editing on the canvas.
* Character and paragraph formatting.
* Font size, scale, tracking, baseline and paragraph-spacing controls.
* Text style and gradient presets.
* Automatic text-formatting rules.
* Learned formatting and structural-text recognition.
* Unicode-aware text processing.
* Clock and ticker layers.
* Unified Text Animators with range, procedural, text-based, wiggly and staggered selectors.
* Animated per-character properties and reusable text-animation presets.

### Animation and Timeline

* Keyframeable layer, text, effect and audio properties.
* Linear, Hold, Auto Bezier, Continuous Bezier and Manual Bezier interpolation.
* Easy Ease, Easy Ease In and Easy Ease Out.
* AE-style **Value Graph** and **Speed Graph** editors.
* Manual speed and influence handles.
* Numeric keyframe-velocity editing.
* Multi-keyframe selection and relative editing.
* Spatial Bezier motion paths editable directly on the canvas.
* Roving keyframes and independent spatial tangents.
* Layer transitions with editable timing handles.
* Play Once, Pause, Loop and Ping-Pong playback modes.
* Authored intros, outros and configurable end-of-cue behavior.

### Masks and Compositing

* Layer masks and scene masks.
* Alpha and Luma track mattes.
* Inverted and clipping mattes.
* Parent/child transform inheritance.
* Groups as composited containers.
* Blend modes.
* Mask-aware effects.
* Per-layer persistence controls.
* Scene A and Scene B compositing for manually animated Stinger transitions.

### Effects and Presets

* Collapsible, reorderable and bypassable effect stacks.
* Duplicate, remove and reorder individual effects.
* Keyframeable effect parameters.
* On-canvas controls for position-based effect properties.
* FX indicators in the layer list.
* Struck-through FX indicator when the complete stack is disabled.
* Built-in effects including:

  * Background and generated fills
  * Outlines and shadows
  * Blur effects
  * Glow and inner glow
  * Inner shadow
  * Brightness and contrast
  * Saturation and color overlays
  * Noise and vignette
  * Emboss and lens flare
  * Motion blur
  * Generated and four-color gradients
* Reusable effect and transition presets.
* Portable shader-based effect extensions.
* Optional native effect-extension API.
* Compound effect graphs with animatable parameters.

### Live Text and Image Cues

* Expose selected text and image properties to the OBS dock.
* Multiple cue rows with independent values.
* Immediate cue and uncue controls.
* Clear queued, active and outgoing states.
* Live cue timers and title thumbnails.
* Cue snapshots that remain stable while rows are edited.
* Cue persistence across keyframes and transitions.
* Manual uncue without transition replay.
* Per-layer **Ignore Persistence** support.
* Table-generated cue rows.
* Managed and manually editable external-data cells.
* Stable row IDs for synchronized datasets.

### External Data

* Provider-neutral external-data system.
* JSON, CSV, HTTP, WebSocket and text providers.
* Asynchronous network and file updates.
* Automatic field and table discovery.
* External field binding to exposed title properties.
* Table-column mapping to Live Text Cue columns.
* Replace, append and synchronize modes.
* Live result preview.
* Formatting and transformation pipeline.
* Diagnostics and external-data logging.
* Coalesced updates without blocking the UI or render thread.

### Audio Layers

* Audio files as first-class timeline layers.
* Asynchronous decoding and waveform generation.
* Timeline range and trim controls.
* Draggable fade-in and fade-out handles.
* Visible and editable fade curves.
* Gain, pan, mute and solo controls.
* Looping and independent playback.
* Keyframeable mix properties.
* Reorderable audio-effect stack.
* Synchronized editor monitoring.
* Sample-accurate forward playback.
* True reverse audio during reverse and Ping-Pong playback.
* Audio synchronized with title and Stinger playback.
* Dynamic OBS Audio Mixer integration.

### Native Stinger Transitions

* Titles can operate as native OBS Stinger transitions.
* Synchronized graphics and audio.
* Authored transition point.
* Pre-roll and post-roll timeline regions.
* **Switch at Point** mode.
* **Manual Scene Animation** mode.
* Animatable Scene A and Scene B input layers.
* Scene inputs support transforms, masks, mattes, parenting, grouping, blend modes, transitions and effects.
* Proxy validation and safe live-render fallback.
* Configurable mixing with outgoing and incoming scene audio.

### Assets, Templates and Libraries

* Reusable title templates.
* Reusable static and animated assets.
* Independent or timeline-synchronized asset playback.
* Asset trimming, looping and pause controls.
* Direct asset editing from the canvas or Asset Library.
* Static bounds covering the complete asset animation.
* Unified libraries for assets, styles, effects and transitions.
* Import and export of reusable presets and title content.

### Cache and Prerender

* RAM frame cache using rendered frame data.
* Optional compressed disk cache.
* Background title and Live Cue prerendering.
* Per-title and per-cue-row cache progress.
* Persistent-state prerendering.
* Dirty-region and targeted invalidation.
* Batched cache scheduling.
* Priority lane for realtime rendering and playback.
* Asynchronous disk loading and writing.
* Constant-time RAM-cache lookup and LRU management.
* Background caching that yields to editing, cueing and live playback.
* Cache exclusions for real-time clock and non-cacheable ticker behavior.
* Persistent cache between editor sessions.
* Configurable RAM limit, disk location and cleanup behavior.

### Performance and Reliability

* GPU-accelerated rendering.
* Monitor-refresh-rate editor presentation.
* Project-frame-rate authored playback.
* Optimized dense-text rendering.
* Responsive editing during background prerendering.
* Background decoding, networking, compression and disk operations.
* Coalesced UI and progress updates.
* Safe cancellation of background jobs when titles close.
* Versioned project serialization and migrations.
* Unknown extension and future property preservation.
* Automated source, migration, cache, audio and regression contracts.
* Windows and Linux compatibility.


## Build and install

Requirements are listed in [INSTALL.txt](INSTALL.txt). The build must use an OBS/libobs SDK and Qt toolchain compatible with the target OBS installation.

### Windows

```powershell
powershell -ExecutionPolicy Bypass -File .\build-windows.ps1
```

### Linux / WSL

```powershell
powershell -ExecutionPolicy Bypass -File .\build-ubuntu-wsl.ps1
```

### Incremental update, build, and package

```powershell
powershell -ExecutionPolicy Bypass -File .\update-and-build.ps1
```

A development source package follows this naming scheme:

```text
Broadcast_Graphics_Live_v0.8.9-alpha_development-version-189.zip
```

## Documentation

Documentation has been consolidated into maintained thematic guides instead of per-development-version notes:

- [Documentation index](docs/README.md)
- [User guide](docs/USER_GUIDE.md)
- [Editor workflow](docs/EDITOR_WORKFLOW.md)
- [Text, Text Animators, and live data](docs/TEXT_AND_LIVE_DATA.md)
- [Effects and extensions](docs/EFFECTS_AND_EXTENSIONS.md)
- [Rendering, audio, and cache](docs/RENDERING_AND_CACHE.md)
- [Architecture, build, and testing](docs/ARCHITECTURE_AND_BUILD.md)
- [Development changelog](docs/CHANGELOG.md)

## Tests and audits

Source contracts live in `tests/`; architecture, packaging, and regression audits live in `tools/`. Many source-only checks run without OBS, while native rendering and integration validation require a matching OBS/libobs SDK and runtime.

<p align="center">
  <img width="520" alt="Broadcast Graphics Live" src="data/icons/broadcast-graphics-live-logo.svg" />
</p>

<p align="center"><strong>Developed by: omniatv</strong></p>
<p align="center">
  <a href="https://omniatv.com">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="data/icons/omniainvert.svg" />
      <img width="230" alt="OmniaTV" src="data/icons/omnianormal.svg" />
    </picture>
  </a>
</p>

## Support

Broadcast Graphics Live is free and open source. Development can be supported through [OmniaTV](https://omniatv.com/en/support/).

## License

Broadcast Graphics Live is distributed under the terms in [LICENSE](LICENSE). Third-party dependencies and optional extension packages retain their own compatible licenses.
