# Broadcast Graphics Live

**Broadcast Graphics Live (BGL)** is a native C++/Qt broadcast-graphics plugin for OBS Studio. It combines a dockable title manager, layered 2D/3D editor, rich text, live data and cueing, audio/video layers, reusable assets, native Stinger transitions, GPU rendering, and RAM/disk prerendering without browser sources or a separate playout application.

**Current source build:** `v0.8.13-alpha` · `Development Version 394`

Development Version 394 starts the `v0.8.13-alpha` series and consolidates the work completed since Development Version 281. It retains the Development Version 393 Motion Blur/transition render-deadlock correction, the Development Version 392 crash containment and Spot/Parallel shadow projection fix, and the current 256–4096 px shadow-map range with a 2048 px default.

## Highlights since Development Version 281

### Vector stroke animation

- Added geometry-stage **Trim Paths** with animatable Start, End, Offset and Simultaneously/Individually modes.
- Added general **Stroke Offset**, keyframes, serialization, migration, cache invalidation and matching editor/live output.
- Kept trimming and stroke geometry inside the normal effect, transition and Motion Blur pipelines.

### Motion Blur and temporal rendering

- Rebuilt Motion Blur around complete shutter-time sampling of transforms, text transitions, Trim Paths, effects, shadows, glows, masks and projected output.
- Added coverage-preserving and temporal-occupancy alpha resolution so translucent artwork does not accumulate opacity and opaque moving bodies remain solid.
- Added distance-adaptive quality, separate live-output budgets, transform-only GPU reuse and reduced editor/UI overhead during playback.
- Added high-precision temporal accumulation where supported, Background Persistence compatibility, safe temporal fallbacks and compositor exception containment.
- Fixed the Motion Blur plus active text-transition crash and the subsequent render-session deadlock that could leave output blank, stale or permanently deferred.

### 3D lighting, materials and shadows

- Added native **Ambient, Point, Spot, Parallel and Environment Light layers** with animated color, brightness, position, point of interest, source size, falloff, cone and shadow controls.
- Added opt-in per-layer material response: ambient, diffuse, specular, shininess, metallic, roughness, reflection, emission and lighting acceptance.
- Added alpha-aware per-light Spot/Parallel shadow maps and omnidirectional Point-light shadows through a six-face atlas.
- Added contact-hardening/soft shadow filtering, receiver-plane-aware self-shadow handling, distance-stable depth ranges and real-time shadow updates while dragging.
- Added Preferences > Advanced **Shadowmap Size** choices from 256 to 4096 px, with 2048 px as the default.
- Moved first-use shader compilation off the frame presentation path and added a centered compile-progress screen while retaining the last stable frame.

### 3D editing, cameras and extrusion

- Added Light and Empty layers to the normal hierarchy, timeline and transform-parent workflow.
- Expanded the 3D Camera, Light, Environment, Material and Geometry inspectors with keyframes and responsive XYZ controls.
- Improved local/world/parent rotation, first-drag behavior, projection-independent rotation scrubbing, double-sided text and authored-track visibility.
- Added transactional frame publication and stale-frame recovery for 3D transforms, negative rotation, lights and extrusion.
- Improved Text/Clock/Ticker/Shape extrusion with hardware depth, adaptive shell density, lighting and shadow interaction.
- Isolated embedded 3D asset projection space from the parent title/graphic and retained independent asset playback.

### Assets and document import

- Added a separate selective packed title format for optionally embedding images, video and fonts while preserving the normal external-asset workflow.
- Added layered SVG, GIMP and Photoshop import through one unified **File > Import** command.
- Expanded SVG/PSD support for fills, gradients, strokes, fonts, font sizes, rich text, blend modes and enabled layer effects.
- Added canvas-space horizontal/vertical flip, collapsed imported groups and Photoshop text-scale/content corrections.

### Live cueing and preview

- Added a read-only cue Preview with green Preview and red Program row states, Take/Cancel controls and next/previous cue preview hotkeys.
- Added OBS Studio Mode routing for duplicate-scene Preview workflows while keeping the local preview available as an option.
- Split **Select Row Before Cue** from the optional **Show Preview** behavior.
- Moved the authoritative Preview into the external Live Text Cues window when that window is open and blocked editing in all preview canvases/scenes.
- Improved cue ending-state handoff, cache-column visibility and complete-panel pop-out behavior.

### Editor, layers, audio and workspace

- Added responsive inspector layouts, right-aligned keyframe navigation, top-positioned tabs, consistent labels and toggle-switch controls.
- Added per-layer custom colors, continuous color-backed layer/timeline rows, aligned fixed columns, drag handles and hierarchy-safe structural drops.
- Added persistent numeric Volume/Pan controls, an improved Audio Editor meter and cleaner Audio/Video audio property sections.
- Added responsive Titles and Graphics list rows, dock locking/recovery, hidden tabbed-dock headers and safer layout reset/preferences handling.
- Added improved loop/restart transport behavior, cross-session clipboard support, correlated render diagnostics and source/runtime logging.

## Core features

### Native OBS integration

- Native OBS sources and BGL Stinger transitions.
- Dockable **Titles and Graphics** and **Live Text Cues** workflows.
- Add saved titles to scenes, rebind existing BGL sources and route cue Preview in Studio Mode.
- Automatic OBS Audio Mixer visibility for titles that contain audio.
- OBS theme integration, persistent editor layouts and configurable dock locking.

### Layer-based editor

- Text, Clock, Ticker, Shape, Image, Video, Audio, Asset, Group, Adjustment, Light, Empty and Stinger Scene A/B layers.
- Layer ordering, visibility, locking, parenting, grouping, masks, mattes, per-layer colors and structural drag/drop.
- Free Transform, corner pinning, vector editing, snapping, rulers, guides and safe areas.
- Timeline, keyframes, Graph Editor, motion paths, reusable assets and independent asset playback.

### 3D layers, cameras and lights

- Opt-in planar 3D while legacy 2D projects retain their original rendering path.
- Position, Scale, Anchor, Rotation and Orientation XYZ with local, world and parent axes.
- Perspective/orthographic cameras, animated switching, editor views, navigation and transform gizmos.
- Depth testing, culling, transparent sorting, extrusion, camera-aware Motion Blur and projected effect bounds.
- Ambient, Point, Spot, Parallel and Environment lights with per-layer material response and opt-in shadows.

### Text and typography

- Multiple independent styles and properties inside one text box.
- Direct inline editing with shared selection/caret geometry and title-level Undo/Redo.
- Font, size, H/V Scale, tracking, baseline, OpenType, fill, gradient, stroke, Stroke Offset and Trim Paths controls.
- Paragraph alignment, justification, indents, spacing, vertical alignment, wrapping and auto-size.
- Text Styles, automatic formatting rules, Text Animators, Clock and Ticker layers.

### Effects, media and playout

- Searchable effect browser, favorites, recent items, presets and extension SDK.
- Keying, matte, spill suppression, blur/detail, optical, distortion, damage, finishing and transition effects.
- Video layers with embedded multistream audio, trim, loop, reverse, time remap, interpolation, proxy and decode-cache workflows.
- Full-pipeline Motion Blur, Background Persistence, RAM/disk prerendering, live fallback and synchronized editor monitoring.
- Selective packed title files and expanded layered SVG/GIMP/PSD import.

## Building and documentation

The project uses CMake and vcpkg. See [INSTALL.txt](INSTALL.txt) and [docs/ARCHITECTURE_AND_BUILD.md](docs/ARCHITECTURE_AND_BUILD.md).

Canonical documentation is indexed in [docs/README.md](docs/README.md), including the user guide, editor workflow, text/live-data guide, rendering/cache guide, effects/extensions guide, packed-title format, consolidated changelog and Visual Effects SDK.

BGL is alpha software. Source-level and standalone native contracts are included, but every release should also be built and visually tested in the target Windows OBS/Qt/MSVC environment before production use.
