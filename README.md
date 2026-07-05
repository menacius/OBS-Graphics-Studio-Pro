# Broadcast Graphics Live

<img width="1920" height="1044" alt="Screenshot 2026-07-05 173016" src="https://github.com/user-attachments/assets/90ae9318-dd42-47d9-b958-b5a6cebc5ce0" />

**Broadcast Graphics Live** is a native C++/Qt broadcast-graphics plugin for OBS Studio. It combines a dockable title manager, layered 2D/3D motion-graphics editor, live text and image cueing, audio layers, reusable assets, native Stinger transitions, GPU rendering, and RAM/disk prerendering without browser sources or a separate playout application.

**Current build:** `v0.8.10-alpha` · `Development Version 219`

## What is new in v0.8.10-alpha

This release consolidates the work completed since `v0.8.9-alpha` Development Version 189.

### Complete planar 3D workflow and animated cameras

Compatible visual layers can now be promoted from the unchanged legacy 2D path to a full planar 3D workflow with Position, Scale, Anchor, Rotation and Orientation XYZ channels, perspective or orthographic cameras, per-layer camera assignment, depth testing and writing, backface culling, double-sided rendering, transparent depth ordering, and hardware Z-buffer compositing. The editor adds Active Camera and orthographic/custom views, orbit/pan/dolly navigation, Frame Selected/All, and Move, Rotate and Scale gizmos with Local, Parent and World orientation. Cameras are first-class animated timeline objects with keyframeable transforms, focal length, FOV, zoom, clipping, projection, switching and assignment.

### 3D motion paths, Graph Editor and timeline completion

Position animation supports complete XYZ spatial Bezier paths, independent tangents, roving keyframes and direct on-canvas editing through transformed parent hierarchies and cameras. Layer List, Timeline and Graph Editor share one expanded Vector3 row model, including X/Y/Z and four-channel properties, color-matched channel toggles, sub-frame keyframe movement, compatible Vector2/Vector3 clipboard redirection, collision-safe paste, and unified context menus. Camera switches and projection changes remain discrete Hold tracks while participating in the standard copy, cut, paste, delete and undo workflow.

### Keyframe-safe hierarchy, compositing and motion blur

Grouping, ungrouping, transform-parent changes and parent deletion preserve authored animation through a static parent-bind matrix instead of sampling or baking transform tracks. Masks, mattes, blend modes, groups and effects now follow a defined 3D execution contract, with projected bounds for glow, shadow, blur and outline, near-plane-safe overlays, and camera-aware motion blur that includes parent motion, Z travel, rotation, perspective deformation and animated cameras without smearing stationary transparent artwork.

### Performance, migration and automated release gates

Cache and Timeline inspection use indexed and batched state reads, projected gizmo geometry is reused between hit-testing and painting, editor frame pacing and selection synchronization are more deterministic, and render diagnostics expose queue, cache, readback, grouping and background-work costs. Title schema 6 adds a contiguous migration and recovery path that preserves unknown future fields throughout nested cameras, keyframes, effects, transitions, audio effects, proxy metadata and external providers. A single automated suite now provides source, smoke, full and stress profiles with manifest validation, bounded timeouts, CTest integration and JSON reports. The Development Version 218 render regression was removed by keeping opaque future-schema payloads in immutable shared storage and outside render-fingerprint parsing.

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
* Independent parenting and grouping with keyframe-safe world-transform preservation.
* Free Transform, corner pinning and direct vector editing.
* External text and image drag-and-drop.
* Canvas rulers, guides, snapping, origins and safe areas.
* Persistent layer, panel and workspace state.

### 3D Layers and Cameras

* Opt-in planar 3D mode while legacy 2D projects retain their original rendering path.
* Position, Scale, Anchor, Rotation and Orientation XYZ properties.
* Perspective and orthographic cameras with Position, Point of Interest, Rotation, Orientation, focal length, FOV, zoom and clipping controls.
* Animated active-camera switching, camera assignment and projection changes.
* Active Camera, Front, Back, Left, Right, Top, Bottom and Custom Perspective editor views.
* Orbit, pan, dolly, Frame Selected and Frame All navigation.
* Move, Rotate and Scale gizmos with Local, Parent and World axes, snapping and hover highlighting.
* Hardware depth testing/writing, transparent far-to-near ordering, backface culling and double-sided rendering.
* 3D-aware masks, mattes, blend modes, groups, effects and projected effect bounds.

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

* Keyframeable layer, camera, text, effect and audio properties.
* Linear, Hold, Auto Bezier, Continuous Bezier and Manual Bezier interpolation.
* Easy Ease, Easy Ease In and Easy Ease Out.
* AE-style **Value Graph** and **Speed Graph** editors.
* Manual speed and influence handles and numeric Keyframe Velocity editing.
* Multi-keyframe selection, relative editing and sub-frame temporal movement.
* Unified X/Y/Z/W and A/R/G/B component rows across Layer List, Timeline and Graph Editor.
* Full 3D spatial motion paths with complete XYZ Bezier editing directly on the canvas.
* Roving keyframes and independent spatial tangents.
* Layer transitions with editable timing handles.
* Play Once, Pause, Loop and Ping-Pong playback modes.
* Authored intros, outros and configurable end-of-cue behavior.

### Masks and Compositing

* Layer masks and scene masks.
* Alpha and Luma track mattes.
* Inverted and clipping mattes.
* Parent/child transform inheritance.
* Groups as composited containers with internal 3D depth resolution.
* Blend modes.
* Mask-aware effects.
* Per-layer persistence controls.
* Scene A and Scene B compositing for manually animated Stinger transitions.

### Effects and Presets

* Collapsible, reorderable and bypassable effect stacks.
* Duplicate, remove and reorder individual effects.
* Keyframeable effect parameters.
* On-canvas controls for position-based effect properties.
* FX indicators in the layer list, including a struck-through state when the complete stack is disabled.
* Stable layer-space, post-transform and screen-space execution in 2D and 3D.
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
  * Camera-aware motion blur
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
* Draggable fade-in and fade-out handles with visible curves.
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
* Authored transition point and pre-roll/post-roll regions.
* **Switch at Point** and **Manual Scene Animation** modes.
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
* Batched cache scheduling and an urgent realtime lane.
* Indexed and batched Timeline cache-state inspection.
* Asynchronous disk loading and writing.
* Constant-time RAM-cache lookup and LRU management.
* Background caching that yields to editing, cueing and live playback.
* Cache exclusions for real-time clock and non-cacheable ticker behavior.
* Persistent cache between editor sessions.
* Configurable RAM limit, disk location and cleanup behavior.

### Performance and Reliability

* GPU-accelerated 2D/3D rendering with editor/OBS parity.
* Monitor-refresh-rate editor presentation and project-frame-rate authored playback.
* Optimized dense-text and group rendering.
* Responsive editing during background prerendering.
* Background decoding, networking, compression and disk operations.
* Coalesced UI and progress updates.
* Safe cancellation of background jobs when titles close.
* Versioned project serialization, contiguous migrations and malformed-entry recovery.
* Unknown extension and future property preservation without render-hot-path copying.
* Automated source, smoke, full and stress test profiles.
* Windows and Linux compatibility.

## Build and install

Requirements are listed in [INSTALL.txt](INSTALL.txt). The build must use an OBS/libobs SDK and Qt toolchain compatible with the target OBS installation.

### Windows

```powershell
powershell -ExecutionPolicy Bypass -File .\build-windows.ps1
```

### Incremental update, build, and package

```powershell
powershell -ExecutionPolicy Bypass -File .\update-and-build.ps1
```

A development source package follows this naming scheme:

```text
Broadcast_Graphics_Live_v0.8.10-alpha_development-version-219.zip
```

## Documentation

Documentation is consolidated into maintained thematic guides instead of per-development-version notes:

- [Documentation index](docs/README.md)
- [User guide](docs/USER_GUIDE.md)
- [Editor and 3D workflow](docs/EDITOR_WORKFLOW.md)
- [Text, Text Animators, and live data](docs/TEXT_AND_LIVE_DATA.md)
- [Effects and extensions](docs/EFFECTS_AND_EXTENSIONS.md)
- [Rendering, audio, and cache](docs/RENDERING_AND_CACHE.md)
- [Architecture, build, serialization, and testing](docs/ARCHITECTURE_AND_BUILD.md)
- [Development changelog](docs/CHANGELOG.md)

## Tests and audits

Run `python tools/run_automated_test_suite.py --profile source` for source-only validation. Native `smoke`, `full`, and `stress` profiles require a configured build with `OBS_BGS_BUILD_TESTS=ON`. Architecture, packaging and regression audits live under `tools/`; C++ and Python contracts live under `tests/`.

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
