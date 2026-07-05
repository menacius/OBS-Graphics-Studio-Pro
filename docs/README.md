# Broadcast Graphics Live documentation

These are the canonical documents for `v0.8.10-alpha` Development Version 219. Historical delivery reports and one-feature notes have been merged into the thematic guides and the consolidated changelog.

| Document | Purpose |
| --- | --- |
| [USER_GUIDE.md](USER_GUIDE.md) | Installation, title creation, 2D/3D editing, cueing, audio, Stingers, caching, and everyday use. |
| [EDITOR_WORKFLOW.md](EDITOR_WORKFLOW.md) | Canvas, layers, hierarchy, cameras, 3D gizmos, timeline, Graph Editor, XYZ motion paths, assets, and Stinger authoring. |
| [TEXT_AND_LIVE_DATA.md](TEXT_AND_LIVE_DATA.md) | Rich text, Text Animators, auto styling, exposed fields, Live Text Cues, and external data. |
| [EFFECTS_AND_EXTENSIONS.md](EFFECTS_AND_EXTENSIONS.md) | Effect stacks, 2D/3D execution spaces, presets, transitions, manifests, and native extensions. |
| [RENDERING_AND_CACHE.md](RENDERING_AND_CACHE.md) | 2D/3D rendering parity, motion blur, audio runtime, persistence, RAM/disk cache, scheduling, and performance. |
| [ARCHITECTURE_AND_BUILD.md](ARCHITECTURE_AND_BUILD.md) | Source ownership, serialization/migration, build, packaging, automated profiles, and the manual OBS regression matrix. |
| [CHANGELOG.md](CHANGELOG.md) | Consolidated development history and release notes. |

## Maintenance rule

Update the relevant canonical guide and `CHANGELOG.md`; do not add a new markdown file for each development delivery. Machine-readable inventories and audits belong under `tools/`, while executable contracts belong under `tests/`.
