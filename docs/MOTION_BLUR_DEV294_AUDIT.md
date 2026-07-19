# Motion Blur pipeline audit — Development Version 294

> **Development Version 295 note:** the sampling and effect-stack findings below remain current, but the final strongest-alpha/sharp-frame resolve is superseded by a premultiplied dry/wet resolve whose 100% output is the normalized shutter exposure.

## Regression identified

The newer live-source path had diverged from the dense temporal sampler preserved in the pre-regression source history in two ways:

1. transform-only motion was constrained to a 2–6 sample real-time ceiling, producing separated silhouettes and weaker exposure quality;
2. the sharp-frame-over-trail resolve allowed an opaque current pixel to hide all temporal color inside the current silhouette. Effects such as Noise therefore remained visually sharp even though their effected texture had entered the Motion Blur path.

## Current pipeline contract

Motion Blur is a final per-layer temporal operation. The temporal layer removes only Motion Blur itself; Trim Paths, Noise, Grain, shadows, glows, color effects and active transitions remain in the sampled visual pipeline, including effects authored after Motion Blur in the stack.

Static effected content is evaluated once and its final texture is sampled across transform, parent and camera motion. Time-varying effects and source geometry are reevaluated at each shutter sample. The generic procedural `effect_animated` flag and all native animated effect properties are temporal dependencies.

## Quality and performance split

Resident-texture transform sampling restores distance-adaptive density, with authored Samples acting as a minimum up to path-specific caps. GPU-only animated effects have a moderate complete-temporal budget. CPU-raster work such as animated Trim Paths and text geometry retains the strict source budget introduced by the performance audit.

## Alpha and color resolve

Temporal samples form a normalized premultiplied exposure. Final straight color is blended between current and temporal contributions, while alpha is resolved independently to the strongest authored coverage. This allows internal Noise/effect detail to blur without increasing the opacity of translucent fills, shadows, glows or antialiased edges.
