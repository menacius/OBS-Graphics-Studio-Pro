# Broadcast Graphics Live documentation

These are the canonical documents for `v0.8.14-alpha` Development Version 404. Current behavior belongs in the thematic guides; release history belongs in the consolidated changelog.

| Document | Purpose |
| --- | --- |
| [USER_GUIDE.md](USER_GUIDE.md) | Installation, title creation, editing, cueing, audio/video, Stingers, caching and everyday use. |
| [EDITOR_WORKFLOW.md](EDITOR_WORKFLOW.md) | Canvas, layers, hierarchy, cameras, 3D gizmos, timeline, Graph Editor, motion paths and assets. |
| [TEXT_AND_LIVE_DATA.md](TEXT_AND_LIVE_DATA.md) | Canonical rich text, Text Properties/styles, H/V Scale, stroke, Undo/Redo, Text Animators, cues and external data. |
| [EFFECTS_AND_EXTENSIONS.md](EFFECTS_AND_EXTENSIONS.md) | Effect stacks, execution spaces, presets, transitions, manifests and native extensions. |
| [RENDERING_AND_CACHE.md](RENDERING_AND_CACHE.md) | GPU/compatibility rendering, text performance, adaptive preview, motion blur, audio runtime and RAM/disk cache. |
| [ARCHITECTURE_AND_BUILD.md](ARCHITECTURE_AND_BUILD.md) | Source ownership, serialization/migration, build, packaging, automated profiles and manual OBS regression matrix. |
| [PACKED-TITLE-FORMAT.md](PACKED-TITLE-FORMAT.md) | Separate `.obgp` container, manifest, compression blocks and import-safety contract. |
| [CHANGELOG.md](CHANGELOG.md) | Consolidated development history and release notes. |
| [visual-effects-sdk.md](visual-effects-sdk.md) | Public modular visual-effects SDK and sample integration. |

Historical performance evidence remains available in [PERFORMANCE-AUDIT-DEV395.md](PERFORMANCE-AUDIT-DEV395.md) and [EFFECT-PERFORMANCE-AUDIT-DEV396.md](EFFECT-PERFORMANCE-AUDIT-DEV396.md), but the current runtime contract is maintained in [RENDERING_AND_CACHE.md](RENDERING_AND_CACHE.md).

## Maintenance rule

Update `README.md`, the relevant canonical guide, and `CHANGELOG.md` for each delivery. Do not add a new one-feature document for routine development versions. Keep historical validation evidence separate from the current behavioral contract; machine-readable inventories belong under `tools/`, and executable contracts belong under `tests/`.
