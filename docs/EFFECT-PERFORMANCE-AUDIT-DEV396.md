# Development Version 396 — Effects Performance Regression Audit

## Scope

This audit targets the GPU effect-stack hot path in `apply_gpu_layer_effect_stack()`, including built-in effects, extension effects, transition blur, static effect-output caching and interaction with Motion Blur. It is intentionally narrow: it does not redesign the temporal compositor or change visual effect ordering.

## Regressions found

### 1. Static cache hits occurred too late

The existing effect-output cache was checked only after every selected effect had already been resolved and registered. On a static cached layer, each OBS draw could still:

- evaluate enabled/keyframed effect properties;
- enter the effect registry and perform shader lookup/queue checks;
- discover source-effect dependencies.

The GPU pass was avoided, but much of the CPU-side stack preparation remained on the hot path.

### 2. Neutral effects still consumed complete GPU passes

Several authored states are mathematically identical to the input but still acquired a shader, allocated/began a render target, bound uniforms and drew a full-frame pass. Examples include a zero-radius Blur, zero-opacity shadows/glows/overlays, identity Brightness/Contrast or Saturation, and zero-strength detail/distortion effects.

## Corrections

### Early effect-output cache preflight

Cache eligibility and the complete cache key are now evaluated before effect resolution and effect-registry work. A valid resident texture returns immediately. Source-aware effects such as Light Wrap and Displacement Map remain excluded because their output can depend on another layer or the current composition.

### Conservative no-op pass elision

Resolved built-in effects with a provable identity state are removed before shader lookup and render-target allocation. Ambiguous effects continue through the normal pipeline. Custom extension effects are never classified from built-in scalar fields unless their extension ID resolves to a registered built-in effect.

### Conservative exceptions retained

The audit also identified several values that look neutral in the UI but are not neutral in the current shaders, so they are deliberately **not** elided:

- Outline width `0` is clamped by the shader to a one-pixel sample radius.
- Zoom Blur radius `0` still applies the existing nine-tap kernel whose weights sum to `0.92`.
- Film, Analog and Digital Distortion can still quantize or reconstruct pixels when their nominal strength is `0`.
- Custom extension effects retain their own parameter semantics.

This preserves current output while limiting the optimization to states that are algebraically identical to the input under the active shaders.

### Diagnostics

Debug builds expose three additional counters:

- `effect_output_cache_hits`
- `effect_output_cache_misses`
- `effect_noop_passes_skipped`

## Preserved behavior

- Effect order and Layer/Composition execution-space filtering are unchanged.
- Adjustment-layer selection is unchanged.
- Transition Blur remains appended after the authored stack.
- The transform-only and full-pipeline Motion Blur temporal paths, sample caps, occupancy/coverage resolution and fallback behavior are unchanged.
- The historical GPU effect-cache namespace is retained and extended with the Dev396 invalidation token.

## Residual opportunities

`effect_layer_cache_key()` still computes the full layer render fingerprint. Replacing that with a dedicated incrementally maintained effect signature could reduce CPU cost further, but it is a broader invalidation-contract change and was deliberately excluded from this regression fix. Name-based shader parameter lookup also remains a candidate for lifecycle-safe handle caching.

## Native validation boundary

The source contracts verify ordering, cache invalidation, no-op safeguards, extension protection and retained Motion Blur topology. A native Windows OBS/Qt/MSVC build and visual comparison with animated/keyframed effects remain required before production deployment.
