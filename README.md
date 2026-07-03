# Broadcast Graphics Live

<p align="center">
  <img width="520" alt="Broadcast Graphics Live" src="data/icons/broadcast-graphics-live-logo.svg" />
</p>

**Broadcast Graphics Live** is a native C++/Qt broadcast-graphics plugin for OBS Studio. It combines a dockable title manager, layered motion-graphics editor, live text and image cueing, audio layers, reusable assets, native Stinger transitions, GPU rendering, and RAM/disk prerendering without browser sources or a separate playout application.

**Current build:** `v0.8.9-alpha` · `Development Version 189`

<p align="center"><strong>Developed by: omniatv</strong></p>
<p align="center">
  <a href="https://omniatv.com">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="data/icons/omniainvert.svg" />
      <img width="230" alt="OmniaTV" src="data/icons/omnianormal.svg" />
    </picture>
  </a>
</p>

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

## Main features

- Native OBS title sources and native OBS Stinger transitions.
- Layered editor for text, clocks, tickers, shapes, images, audio, assets, groups, adjustment layers, and Stinger Scene A/B inputs.
- Rich text, inline editing, style presets, automatic formatting rules, and unified Text Animators.
- Timeline keyframes, spatial motion paths, temporal Value/Speed Graph Editor, easing, and transition handles.
- Masks, track mattes, parenting, grouping, blend modes, layer transitions, and reorderable effect stacks.
- Live Text Cues with exposed text/image properties, cue persistence, table mapping, and external JSON/CSV/HTTP/WebSocket/text data providers.
- Audio waveform editing, fades, mixer controls, DSP effects, synchronized editor monitoring, and forward/reverse playback.
- RAM and optional disk frame cache with background prerender, cue-row progress, persistence states, and dirty invalidation.
- Reusable title templates, assets, effect presets, transition presets, and portable effect extensions.
- OBS-theme-aware UI, collapsible title dock, persistent panel layout, guides, snapping, safe areas, and external drag-and-drop.

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

## Support

Broadcast Graphics Live is free and open source. Development can be supported through [OmniaTV](https://omniatv.com/en/support/).

## License

Broadcast Graphics Live is distributed under the terms in [LICENSE](LICENSE). Third-party dependencies and optional extension packages retain their own compatible licenses.
