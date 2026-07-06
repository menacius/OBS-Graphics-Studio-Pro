#pragma once

#include <stdint.h>

#ifdef _WIN32
#define BGL_PLUGIN_EXPORT __declspec(dllexport)
#else
#define BGL_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#define BGL_PLUGIN_API_VERSION_1 1u
#define BGL_PLUGIN_API_VERSION_2 2u
#define BGL_PLUGIN_API_VERSION_3 3u
#define BGL_PLUGIN_API_VERSION_4 4u
#define BGL_PLUGIN_API_VERSION BGL_PLUGIN_API_VERSION_4

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bgl_host_api_v1 {
    uint32_t api_version;
    void (*log)(int level, const char *component, const char *message);
} bgl_host_api_v1;

typedef struct bgl_effect_descriptor_v1 {
    const char *id;
    const char *display_name;
    const char *category;
    const char *shader_path;
    const char *manifest_json;
} bgl_effect_descriptor_v1;

typedef struct bgl_plugin_descriptor_v1 {
    uint32_t api_version;
    const char *id;
    const char *name;
    const char *version;
    uint32_t effect_count;
    const bgl_effect_descriptor_v1 *effects;
} bgl_plugin_descriptor_v1;

/* ABI v2 remains pure C and append-only. The host owns every editor widget.
 * Plugins describe compound editors, presets and asset packs as UTF-8 JSON.
 * Optional callbacks validate/migrate opaque project state without exposing Qt. */
typedef int (*bgl_validate_state_v2_fn)(const char *effect_id, const char *state_json,
                                        char *error_utf8, uint32_t error_capacity);
typedef const char *(*bgl_migrate_state_v2_fn)(const char *effect_id,
                                               uint32_t from_schema_version,
                                               const char *state_json);
typedef void (*bgl_release_string_v2_fn)(const char *value);

typedef struct bgl_effect_descriptor_v2 {
    bgl_effect_descriptor_v1 v1;
    uint32_t schema_version;
    const char *editor_schema_json; /* declarative host-owned editor */
    const char *preset_index_json;  /* categories + relative preset files */
    const char *asset_index_json;   /* textures/LUTs/icons + metadata */
    const char *capabilities_json;  /* compoundGraph, customAssets, keyframes, etc. */
    const char *animation_schema_json; /* animatable paths, interpolation/easing policy */
} bgl_effect_descriptor_v2;

/* ABI v3 adds host-rendered, host-hit-tested canvas controls. The JSON schema
 * describes handles by parameter path and coordinate space; plugins never
 * receive QWidget/QPainter pointers and remain ABI-stable across Qt versions. */
typedef struct bgl_effect_descriptor_v3 {
    bgl_effect_descriptor_v2 v2;
    const char *canvas_handles_schema_json;
} bgl_effect_descriptor_v3;

/* ABI v4 is the public Visual Effects SDK contract. It remains pure C and
 * append-only. GPU effects are described by shader passes and metadata; CPU
 * effects may only be scheduled through the host worker pipeline declared in
 * requirements_json/capabilities_json and must never execute on the BGL render
 * loop. Custom property widgets are declarative JSON widgets owned by BGL. */
typedef enum bgl_effect_color_space_v4 {
    BGL_COLOR_SPACE_PRESERVE_INPUT = 0,
    BGL_COLOR_SPACE_SCENE_LINEAR = 1,
    BGL_COLOR_SPACE_DISPLAY_REFERRED = 2,
    BGL_COLOR_SPACE_HDR_LINEAR = 3
} bgl_effect_color_space_v4;

typedef enum bgl_effect_alpha_contract_v4 {
    BGL_ALPHA_PREMULTIPLIED_PRESERVE = 0,
    BGL_ALPHA_PREMULTIPLIED_EXPAND = 1,
    BGL_ALPHA_PREMULTIPLIED_REPLACE = 2,
    BGL_ALPHA_STRAIGHT_INPUT_REQUIRED = 3
} bgl_effect_alpha_contract_v4;

typedef enum bgl_effect_backend_v4 {
    BGL_EFFECT_BACKEND_GPU_SHADER = 0,
    BGL_EFFECT_BACKEND_GPU_MULTI_PASS = 1,
    BGL_EFFECT_BACKEND_CPU_WORKER_ONLY = 2,
    BGL_EFFECT_BACKEND_HYBRID_WORKER_AND_GPU = 3
} bgl_effect_backend_v4;

typedef struct bgl_effect_descriptor_v4 {
    bgl_effect_descriptor_v3 v3;
    uint32_t descriptor_size;
    uint32_t input_count;               /* main input + declared aux inputs */
    bgl_effect_backend_v4 backend;
    bgl_effect_color_space_v4 color_space;
    bgl_effect_alpha_contract_v4 alpha_contract;
    const char *parameter_metadata_json; /* stable parameter ids, ranges, units, defaults */
    const char *custom_property_widgets_json; /* host-owned custom editors */
    const char *render_passes_json;      /* ordered GPU shader passes/targets */
    const char *inputs_json;             /* primary + auxiliary input declarations */
    const char *auxiliary_inputs_json;   /* named auxiliary composition/layer inputs */
    const char *layer_references_json;   /* explicit layer-reference policy */
    const char *requirements_json;       /* color/alpha/bounds/HDR/cache/thread requirements */
    const char *state_serialization_json;/* state format, defaults, missing-plugin fallback */
} bgl_effect_descriptor_v4;

typedef int (*bgl_plugin_can_unload_v4_fn)(const char *plugin_id);
typedef void (*bgl_plugin_before_unload_v4_fn)(const char *plugin_id);

typedef struct bgl_plugin_descriptor_v2 {
    bgl_plugin_descriptor_v1 v1;
    uint32_t descriptor_size;
    uint32_t effect_v2_count;
    const bgl_effect_descriptor_v2 *effects_v2;
    bgl_validate_state_v2_fn validate_state;
    bgl_migrate_state_v2_fn migrate_state;
    bgl_release_string_v2_fn release_string;
} bgl_plugin_descriptor_v2;

typedef struct bgl_plugin_descriptor_v3 {
    bgl_plugin_descriptor_v2 v2;
    uint32_t effect_v3_count;
    const bgl_effect_descriptor_v3 *effects_v3;
} bgl_plugin_descriptor_v3;

typedef struct bgl_plugin_descriptor_v4 {
    bgl_plugin_descriptor_v3 v3;
    uint32_t descriptor_size;
    uint32_t effect_v4_count;
    const bgl_effect_descriptor_v4 *effects_v4;
    bgl_plugin_can_unload_v4_fn can_unload;
    bgl_plugin_before_unload_v4_fn before_unload;
} bgl_plugin_descriptor_v4;

typedef const bgl_plugin_descriptor_v1 *(*bgl_plugin_query_v1_fn)(const bgl_host_api_v1 *host);
typedef const bgl_plugin_descriptor_v2 *(*bgl_plugin_query_v2_fn)(const bgl_host_api_v1 *host);
typedef const bgl_plugin_descriptor_v3 *(*bgl_plugin_query_v3_fn)(const bgl_host_api_v1 *host);
typedef const bgl_plugin_descriptor_v4 *(*bgl_plugin_query_v4_fn)(const bgl_host_api_v1 *host);

BGL_PLUGIN_EXPORT const bgl_plugin_descriptor_v1 *bgl_plugin_query_v1(const bgl_host_api_v1 *host);
BGL_PLUGIN_EXPORT const bgl_plugin_descriptor_v2 *bgl_plugin_query_v2(const bgl_host_api_v1 *host);
BGL_PLUGIN_EXPORT const bgl_plugin_descriptor_v3 *bgl_plugin_query_v3(const bgl_host_api_v1 *host);
BGL_PLUGIN_EXPORT const bgl_plugin_descriptor_v4 *bgl_plugin_query_v4(const bgl_host_api_v1 *host);

#ifdef __cplusplus
}
#endif
