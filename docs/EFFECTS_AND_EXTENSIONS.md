# Effects and extensions

## Panel-based effect stack

Effects Settings is a direct representation of the layer effect stack. Each effect is a collapsible panel. Panel order is render order. Dragging a header reorders the underlying model; the header switch enables/disables the effect; the overflow menu provides Duplicate, Delete, Move Up, and Move Down.

The bottom toolbar contains **Add Effect** and **Respect Masks**. Effect settings are bound to their own effect instance so several expanded panels can remain visible without controls writing into the wrong stack entry.

The layer list keeps the **FX** badge visible whenever a stack exists. An enabled stack uses the active badge state; disabling the whole stack draws a diagonal strike-through over the badge so the user can distinguish “effects present but bypassed” from “no effects”. External-data binding dots remain independent from this visual state.

## Built-in effects and presets

Built-in effects use stable namespaced IDs (`bgl.builtin.*`) while retaining legacy numeric adapters for older project files. Presets and Add Effect menus are generated from the shared catalog. Built-ins include color/generate effects, blur families, shadows, glow, outline, grading, noise, vignette, emboss, lens flare, motion blur, and generated gradients.

Effect parameters may be static or animated. Position-like parameters can expose canvas handles, and keyframe diamonds use the same animation model as ordinary layer properties.

## 3D execution spaces

The host classifies every enabled effect into three stable effect execution spaces. This is derived from the existing effect type and flags, so projects require no migration:

- **Layer space:** standard artwork effects operate on the padded transform-neutral raster before 2D/3D projection. Expanding effects such as shadow, glow, blur, and outline therefore keep their complete bounds after perspective rotation.
- **Post-transform space:** Motion Blur evaluates projected transform samples, including parent and camera movement.
- **Screen space:** effects marked **Affect layers behind** read the already composited destination.

Track mattes are projected coverage rather than layer-space effects. Compatible hardware-depth planes sample their full-canvas matte during the same depth-tested draw; effects configured to respect the matte after masking remain on the full-frame compositor to preserve stack order. Groups resolve child depth in an offscreen surface before the group-level stack is applied once.

## Transition presets

Layer transitions are stored separately from effects but use the same preset/data packaging area. Text and general transitions can expose host-owned controls and previews without embedding third-party Qt widgets.

## Manifest extensions

Portable GPU extensions live under `broadcast-graphics-live/extensions/` and contain a `*.bgl-effect.json` manifest plus an OBS `.effect` shader. The manifest declares:

- globally unique effect ID;
- display/category/provider metadata;
- parameter schema and defaults;
- supported shader techniques;
- editor schema, presets, and assets where applicable;
- capability flags for keyframes and compound graphs.

Supported parameter concepts include float, integer, boolean, color, enum, texture, point, and string. Animatable parameters are evaluated by the host so timeline behavior remains consistent across built-in and external effects.

## Native extension ABI

Optional native extensions are loaded only from the user configuration extension directory. They expose the stable C ABI declared in `src/extensions/bgl-plugin-api.h` and must not depend on private Qt/C++ editor headers.

Compatibility rules:

- IDs are permanent and namespaced.
- API/ABI mismatches are rejected.
- Duplicate IDs are ignored and reported.
- Unknown extension IDs and raw parameter JSON round-trip without data loss.
- Missing extensions do not destroy project settings.
- Native extensions execute inside the OBS process and should only be installed from trusted sources.

## Compound graphs

API v2 can flatten ordered graph elements to bounded shader uniforms (`elementCount`, `element0_*` through `element15_*`). Animation paths use the same element/property names, allowing compound effect elements to be keyframed without extension-specific timeline code.


## 3D depth and material interaction

Depth Test and Write Depth are independent for compatible planar layers:

- **Test enabled, Write enabled:** compare against existing depth and publish passing depth.
- **Test enabled, Write disabled:** compare against persistent depth without contaminating later layers.
- **Test disabled, Write enabled:** draw in authored order and publish depth with the always-pass state.
- **Test disabled, Write disabled:** remain an authored-order compositing layer.

Backface classification uses final projected winding so negative scale, mirrored parents and perspective remain predictable. Transparent compatible planes are ordered far-to-near with authored order as the coplanar tie-break. Masks, destination-aware blend modes and groups retain their required compatibility/offscreen boundaries.
