# Broadcast Graphics Live documentation

These are the canonical documents for `v0.8.13-alpha` Development Version 394. Historical one-feature delivery notes have been merged into the thematic guides and the consolidated changelog.

- `PACKED-TITLE-FORMAT.md` specifies the separate `.obgp` container, manifest,
  compression blocks and import-safety contract.

| Document | Purpose |
| --- | --- |
| [USER_GUIDE.md](USER_GUIDE.md) | Installation, title creation, editing, cueing, audio/video, Stingers, caching and everyday use. |
| [EDITOR_WORKFLOW.md](EDITOR_WORKFLOW.md) | Canvas, layers, hierarchy, cameras, 3D gizmos, timeline, Graph Editor, motion paths and assets. |
| [TEXT_AND_LIVE_DATA.md](TEXT_AND_LIVE_DATA.md) | Canonical rich text, Text Properties/styles, H/V Scale, stroke, Undo/Redo, Text Animators, cues and external data. |
| [EFFECTS_AND_EXTENSIONS.md](EFFECTS_AND_EXTENSIONS.md) | Effect stacks, execution spaces, presets, transitions, manifests and native extensions. |
| [RENDERING_AND_CACHE.md](RENDERING_AND_CACHE.md) | GPU/compatibility rendering, text performance, adaptive preview, motion blur, audio runtime and RAM/disk cache. |
| [ARCHITECTURE_AND_BUILD.md](ARCHITECTURE_AND_BUILD.md) | Source ownership, serialization/migration, build, packaging, automated profiles and manual OBS regression matrix. |
| [CHANGELOG.md](CHANGELOG.md) | Consolidated development history and release notes. |
| [visual-effects-sdk.md](visual-effects-sdk.md) | Public modular visual-effects SDK and sample integration. |

## Maintenance rule

Update the relevant canonical guide and `CHANGELOG.md`; do not add a new markdown file for each development delivery. Machine-readable inventories and audits belong under `tools/`, while executable contracts belong under `tests/`.
