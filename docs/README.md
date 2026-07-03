# Broadcast Graphics Live documentation

These are the canonical documents for `v0.8.9-alpha`. Historical one-feature reports, delivery validations, and per-development-version notes have been merged into the guides and changelog below.

| Document | Purpose |
| --- | --- |
| [USER_GUIDE.md](USER_GUIDE.md) | Installation, title creation, cueing, audio, Stingers, caching, and everyday use. |
| [EDITOR_WORKFLOW.md](EDITOR_WORKFLOW.md) | Canvas, layers, hierarchy, timeline, Graph Editor, motion paths, assets, and Stinger authoring. |
| [TEXT_AND_LIVE_DATA.md](TEXT_AND_LIVE_DATA.md) | Rich text, Text Animators, auto styling, exposed fields, Live Text Cues, and external data. |
| [EFFECTS_AND_EXTENSIONS.md](EFFECTS_AND_EXTENSIONS.md) | Effect stacks, FX state indicators, presets, transitions, manifests, and native extensions. |
| [RENDERING_AND_CACHE.md](RENDERING_AND_CACHE.md) | Rendering parity, audio runtime, persistence, RAM/disk cache, scheduling, and performance. |
| [ARCHITECTURE_AND_BUILD.md](ARCHITECTURE_AND_BUILD.md) | Source ownership, serialization, build, packaging, automated tests, and manual regression coverage. |
| [CHANGELOG.md](CHANGELOG.md) | Consolidated development history and release notes. |

## Maintenance rule

Update the relevant canonical guide and `CHANGELOG.md`; do not add a new root-level report for each development delivery. Machine-readable inventories and source audits belong under `tools/` and executable contracts under `tests/`.
