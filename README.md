# Broadcast Graphics Live

**Broadcast Graphics Live (BGL)** is a native C++/Qt broadcast-graphics plugin for OBS Studio. It combines a dockable title manager, layered 2D/3D editor, rich text, live data and cueing, audio/video layers, reusable assets, native Stinger transitions, GPU rendering, and RAM/disk prerendering without browser sources or a separate playout application.

**Current source build:** `v0.8.12-alpha` · `Development Version 281`

Development Version 281 continues the `v0.8.12-alpha` series. It fixes the Development Version 280 stroked-text regression by removing advance-box clipping from ordinary exact glyphs, composing same-style static outlines as one continuous path, and restoring all transition-managed and manually animated text to the unified GPU animator pipeline. Static stroked text therefore keeps continuous cross-glyph outlines without borrowing neighbouring glyphs into rectangular animation crops, while text transitions retain their previous per-glyph timing and transforms.

## What changed since v0.8.11-alpha Development Version 239

Development Version 239 is the public repository baseline for this comparison. Development Versions 240–281 added or completed:

- unified inspector widgets, drag-label editing, compact property rows, reset/default actions and dock/UI consistency;
- video proxies, decode cache and hardware acceleration policy, time remapping, freeze sections, interpolation, serialization/migration audit and editor decode stability;
- editor audio monitoring, synchronized playback/cache modes, varispeed **Play Every Frame**, Clock presets and cache-dependent UI visibility;
- expanded Live Properties and cue rows for fill/stroke, per-row appearance, source clearing, provider-neutral external data and table-to-cue mapping;
- direct GPU image/video filtering and box-size interaction without CPU resample/upload churn;
- scene masks across visual layer types with alpha-shaped editor placeholders;
- effect-library cleanup and textured Film/Analog/Digital damage effects;
- editable Text Styles using the same Text Properties controls, live preview, stroke/gradients and drag-label behavior;
- canonical rich text with sparse overlapping multi-property ranges and title-level Undo/Redo;
- exact per-glyph H/V Scale for normal and multistyle text, shared selection/caret geometry, post-scale center/right/justify alignment, and axis-specific auto-size disable on manual bounding-box resize;
- Development Version 281 removes ordinary glyph advance clipping, merges static same-style strokes into continuous paths, and keeps animated/transition text on the unified GPU glyph pipeline so neighbouring glyph boxes cannot contaminate transitions.

## Core features

### Native OBS integration

- Native OBS sources and Stinger transitions.
- Dockable **Titles and Graphics** panel.
- Add saved titles to scenes and rebind existing BGL sources.
- Automatic OBS Audio Mixer visibility for titles that contain audio.
- OBS theme integration and persistent editor layout.

### Layer-based editor

- Text, Clock, Ticker, Shape, Image, Video, Audio, Asset, Group, Adjustment and Stinger Scene A/B layers.
- Layer ordering, visibility, locking, parenting, grouping, masks and mattes.
- Free Transform, corner pinning, vector editing, snapping, rulers, guides and safe areas.
- Timeline, keyframes, Graph Editor, motion paths and reusable assets.

### 3D layers and cameras

- Opt-in planar 3D while legacy 2D projects retain their original path.
- Position, Scale, Anchor, Rotation and Orientation XYZ.
- Perspective/orthographic cameras, animated switching, editor views, navigation and transform gizmos.
- Depth testing, culling, transparent sorting, camera-aware motion blur and projected effect bounds.

### Text and typography

- Multiple independent styles and properties inside one text box.
- Direct inline editing with shared selection/caret geometry.
- Font, size, H/V Scale, tracking, baseline, OpenType, fill, gradient and stroke controls.
- Paragraph alignment, justification, indents, spacing, vertical alignment, wrapping and auto-size.
- Text Styles, automatic formatting rules, Text Animators, Clock and Ticker layers.

### Effects, media and playout

- Searchable effect browser, favorites, recent items, presets and extension SDK.
- Keying, matte, spill suppression, blur/detail, optical, distortion, damage and finishing effects.
- Video layers with embedded multistream audio, trim/loop/reverse/time-remap/proxy workflows.
- RAM/disk prerendering, live fallback, cue/uncue behavior and synchronized editor monitoring.

## Building and documentation

The project uses CMake and vcpkg. See [INSTALL.txt](INSTALL.txt) and [docs/ARCHITECTURE_AND_BUILD.md](docs/ARCHITECTURE_AND_BUILD.md).

Canonical documentation is indexed in [docs/README.md](docs/README.md), including the user guide, editor workflow, text/live-data guide, rendering/cache guide, effects/extensions guide, changelog and Visual Effects SDK.

BGL is alpha software. Source-level and standalone native contracts are included, but every release should also be built and visually tested in the target Windows OBS/Qt/MSVC environment before production use.
