#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bgs::serialization {

using json = nlohmann::json;

/* The title store intentionally remains a top-level JSON array so builds made
 * before schema versioning can still open newly-saved scene collections. The
 * schema/development identity therefore lives on every title object. Template
 * exports additionally expose the same values on their object root. */
inline constexpr int kCurrentTitleSchemaVersion = 4;
inline constexpr int kCurrentDevelopmentVersion = 189;
inline constexpr int kFirstAuditedDevelopmentVersion = 144;
inline constexpr int kCurrentProxyManifestSchemaVersion = 2;
inline constexpr int kCurrentDockSettingsSchemaVersion = 2;
inline constexpr int kCurrentFormattingSchemaVersion = 2;
inline constexpr int kCurrentPatternSchemaVersion = 1;

struct MigrationReport {
    int source_schema_version = 0;
    int source_development_version = 0;
    int target_schema_version = kCurrentTitleSchemaVersion;
    int target_development_version = kCurrentDevelopmentVersion;
    std::vector<int> applied_development_versions;
    std::vector<std::string> recoveries;
    std::vector<std::string> warnings;

    bool changed() const
    {
        return source_schema_version != target_schema_version ||
               source_development_version != target_development_version ||
               !recoveries.empty() || !applied_development_versions.empty();
    }
};

inline int safe_integer(const json &object, const char *key, int fallback)
{
    if (!object.is_object())
        return fallback;
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer())
        return fallback;
    const auto value = it->get<std::int64_t>();
    if (value < 0 || value > 1000000)
        return fallback;
    return static_cast<int>(value);
}

inline double safe_number(const json &object, const char *key, double fallback)
{
    if (!object.is_object())
        return fallback;
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number())
        return fallback;
    const double value = it->get<double>();
    return std::isfinite(value) ? value : fallback;
}

inline bool title_has_layer_type(const json &title, int type)
{
    const auto layers = title.find("layers");
    if (layers == title.end() || !layers->is_array())
        return false;
    for (const auto &layer : *layers) {
        if (layer.is_object() && safe_integer(layer, "type", -1) == type)
            return true;
    }
    return false;
}

inline int infer_development_version(const json &title)
{
    const int explicit_version = safe_integer(title, "development_version", 0);
    if (explicit_version > 0)
        return explicit_version;

    /* Inference is deliberately conservative. It is used only to skip
     * migrations that clearly predate the fields already present. */
    if (title.contains("stinger_switch_mode") || title_has_layer_type(title, 11))
        return 168;
    if (title.contains("stinger_transition_point") ||
        safe_integer(title, "graphic_type", 0) == 3)
        return 167;
    if (title_has_layer_type(title, 10))
        return 145;
    return kFirstAuditedDevelopmentVersion - 1;
}

inline void recover_array_member(json &object, const char *key,
                                 MigrationReport &report)
{
    if (!object.is_object() || !object.contains(key))
        return;
    if (!object[key].is_array()) {
        object[key] = json::array();
        report.recoveries.emplace_back(std::string(key) + " was not an array");
    }
}

inline void recover_object_member(json &object, const char *key,
                                  MigrationReport &report)
{
    if (!object.is_object() || !object.contains(key))
        return;
    if (!object[key].is_object()) {
        object[key] = json::object();
        report.recoveries.emplace_back(std::string(key) + " was not an object");
    }
}

inline void migrate_formatting_and_patterns(json &title, MigrationReport &report)
{
    if (!title.contains("layers") || !title["layers"].is_array())
        return;
    for (auto &layer : title["layers"]) {
        if (!layer.is_object() || !layer.contains("rich_text"))
            continue;
        if (!layer["rich_text"].is_object()) {
            layer.erase("rich_text");
            report.recoveries.emplace_back("discarded malformed rich_text object");
            continue;
        }
        auto &document = layer["rich_text"];
        document["formatting_schema_version"] = kCurrentFormattingSchemaVersion;
        recover_array_member(document, "auto_style_rules", report);
        if (document.contains("auto_style_rules") && document["auto_style_rules"].is_array()) {
            auto &rules = document["auto_style_rules"];
            const auto old_size = rules.size();
            rules.erase(std::remove_if(rules.begin(), rules.end(),
                                       [](const json &rule) { return !rule.is_object(); }),
                        rules.end());
            if (rules.size() != old_size)
                report.recoveries.emplace_back("discarded malformed formatting/pattern rules");
            for (auto &rule : document["auto_style_rules"]) {
                rule["pattern_schema_version"] = kCurrentPatternSchemaVersion;
                recover_array_member(rule, "excludes_rule_ids", report);
            }
        }
    }
}

inline void migrate_external_data(json &title, MigrationReport &report)
{
    recover_array_member(title, "external_data_sources", report);
    recover_array_member(title, "live_text_external_bindings", report);
    recover_array_member(title, "live_text_table_bindings", report);

    if (title.contains("external_data_sources") &&
        title["external_data_sources"].is_array()) {
        for (auto &source : title["external_data_sources"]) {
            if (!source.is_object())
                continue;
            recover_array_member(source, "fields", report);
            if (source.contains("provider") && !source["provider"].is_object()) {
                source["provider"] = json::object();
                report.recoveries.emplace_back("reset malformed external data provider");
            }
        }
    }

    if (title.contains("layers") && title["layers"].is_array()) {
        for (auto &layer : title["layers"]) {
            if (layer.is_object())
                recover_array_member(layer, "external_bindings", report);
        }
    }

    if (title.contains("live_text_external_bindings") &&
        title["live_text_external_bindings"].is_array()) {
        for (auto &cell : title["live_text_external_bindings"]) {
            if (!cell.is_object())
                continue;
            if (cell.contains("binding") && !cell["binding"].is_object()) {
                cell["binding"] = json::object();
                report.recoveries.emplace_back("reset malformed live-text external binding");
            }
        }
    }

    if (title.contains("live_text_table_bindings") &&
        title["live_text_table_bindings"].is_array()) {
        for (auto &mapping : title["live_text_table_bindings"]) {
            if (mapping.is_object())
                recover_array_member(mapping, "columns", report);
        }
    }
}

inline void migrate_audio_layers(json &title, MigrationReport &report)
{
    if (!title.contains("layers") || !title["layers"].is_array())
        return;
    for (auto &layer : title["layers"]) {
        if (!layer.is_object() ||
            (safe_integer(layer, "type", -1) != 10 && !layer.contains("audio_source")))
            continue;
        recover_array_member(layer, "audio_effects", report);
        recover_array_member(layer, "audio_waveform", report);
        if (!layer.contains("audio_volume_prop")) {
            layer["audio_volume_prop"] = {
                {"static_value", safe_number(layer, "audio_volume", 1.0)},
                {"keyframes", json::array()}};
        }
        if (!layer.contains("audio_pan_prop")) {
            layer["audio_pan_prop"] = {
                {"static_value", safe_number(layer, "audio_pan", 0.0)},
                {"keyframes", json::array()}};
        }
    }
}

inline void migrate_stinger_metadata(json &title, MigrationReport &report)
{
    const bool stinger = safe_integer(title, "graphic_type", 0) == 3 ||
                         title.contains("stinger_transition_point") ||
                         title.contains("stinger_transition_point_frames") ||
                         title.contains("stinger_transition_point_percent");
    if (!stinger)
        return;

    const double duration = std::max(0.1, safe_number(title, "duration", 5.0));
    if (!title.contains("stinger_transition_point")) {
        if (title.contains("stinger_transition_point_frames")) {
            const double fps = std::max(1.0, safe_number(title, "frame_rate", 30.0));
            title["stinger_transition_point"] =
                safe_number(title, "stinger_transition_point_frames", duration * fps * 0.5) / fps;
            report.recoveries.emplace_back("migrated Stinger transition point from frames");
        } else if (title.contains("stinger_transition_point_percent")) {
            const double percent = std::clamp(
                safe_number(title, "stinger_transition_point_percent", 50.0), 0.0, 100.0);
            title["stinger_transition_point"] = duration * percent / 100.0;
            report.recoveries.emplace_back("migrated Stinger transition point from percentage");
        }
    }
    if (!title.contains("stinger_pre_roll"))
        title["stinger_pre_roll"] = 0.0;
    if (!title.contains("stinger_post_roll"))
        title["stinger_post_roll"] = 0.0;
    if (!title.contains("stinger_audio_enabled"))
        title["stinger_audio_enabled"] = true;
    if (!title.contains("stinger_alpha_output"))
        title["stinger_alpha_output"] = true;
}

inline void migrate_stinger_switch_contract(json &title, MigrationReport &report)
{
    if (safe_integer(title, "graphic_type", 0) != 3 &&
        !title.contains("stinger_switch_mode"))
        return;
    if (!title.contains("stinger_switch_mode"))
        title["stinger_switch_mode"] = 0;
    const int background = safe_integer(title, "stinger_editor_background", 3);
    if (background == 0 || background == 1) {
        title["stinger_editor_background"] = 3;
        report.recoveries.emplace_back("migrated static Stinger A/B preview to follow-switch-point");
    }
}

inline void migrate_proxy_metadata(json &title, MigrationReport &report)
{
    if (!title.contains("proxy_metadata"))
        return;
    if (!title["proxy_metadata"].is_object()) {
        title["proxy_metadata"] = json::object();
        report.recoveries.emplace_back("reset malformed proxy metadata");
    }
    title["proxy_metadata"]["schema_version"] = kCurrentProxyManifestSchemaVersion;
}

inline void validate_recover_title_shape(json &title, MigrationReport &report)
{
    if (!title.is_object()) {
        title = json::object();
        report.recoveries.emplace_back("title root was not an object");
    }
    recover_array_member(title, "layers", report);
    recover_array_member(title, "external_data_sources", report);
    recover_array_member(title, "live_text_rows", report);
    recover_array_member(title, "live_text_row_ids", report);
    recover_array_member(title, "live_text_column_order", report);
    recover_array_member(title, "live_text_external_bindings", report);
    recover_array_member(title, "live_text_table_bindings", report);

    /* Invalid children are dropped individually so one damaged layer/source
     * never prevents the rest of the title from opening. Existing IDs and all
     * unknown fields on valid objects retain their original JSON values. */
    auto remove_non_objects = [&](const char *key) {
        if (!title.contains(key) || !title[key].is_array())
            return;
        auto &items = title[key];
        const auto old_size = items.size();
        items.erase(std::remove_if(items.begin(), items.end(),
                                   [](const json &item) { return !item.is_object(); }),
                    items.end());
        if (items.size() != old_size)
            report.recoveries.emplace_back(std::string("discarded malformed entries from ") + key);
    };
    remove_non_objects("layers");
    remove_non_objects("external_data_sources");
    remove_non_objects("live_text_external_bindings");
    remove_non_objects("live_text_table_bindings");
}

inline void apply_development_migration(int target_version, json &title,
                                        MigrationReport &report)
{
    /* Every development delivery from 144 onward has an explicit migration
     * step. Most are intentionally no-ops because their changes were UI or
     * renderer-only; keeping the contiguous ledger prevents silent gaps. */
    switch (target_version) {
    case 144:
        migrate_external_data(title, report);
        migrate_formatting_and_patterns(title, report);
        break;
    case 145:
    case 146:
    case 147:
    case 148:
    case 149:
    case 150:
    case 151:
    case 152:
    case 153:
    case 154:
    case 155:
    case 156:
    case 157:
    case 158:
    case 159:
    case 160:
    case 161:
    case 162:
        migrate_audio_layers(title, report);
        break;
    case 163:
    case 164:
        break;
    case 165:
        migrate_proxy_metadata(title, report);
        break;
    case 166:
    case 167:
        migrate_stinger_metadata(title, report);
        break;
    case 168:
    case 169:
    case 170:
        migrate_stinger_switch_contract(title, report);
        break;
    case 171:
    case 172:
    case 173:
    case 174:
    case 175:
    case 176:
    case 177:
    case 178:
        break;
    case 179:
        validate_recover_title_shape(title, report);
        migrate_external_data(title, report);
        migrate_formatting_and_patterns(title, report);
        migrate_audio_layers(title, report);
        migrate_stinger_metadata(title, report);
        migrate_stinger_switch_contract(title, report);
        migrate_proxy_metadata(title, report);
        break;
    case 180:
        /* Performance/cache/threading audit: no persisted schema changes.
         * Re-run current recovery so malformed documents remain loadable. */
        validate_recover_title_shape(title, report);
        break;
    case 181:
        /* Audio scheduler repair and automated regression pass introduce no
         * persisted fields. Keep current recovery idempotent. */
        validate_recover_title_shape(title, report);
        break;
    case 182:
        /* Portable worker-thread naming is runtime-only and does not alter
         * persisted title data. Keep current recovery idempotent. */
        validate_recover_title_shape(title, report);
        break;
    case 183:
        /* Editor monitor cadence is runtime-only. Program/source buffering
         * and persisted title data remain unchanged. */
        validate_recover_title_shape(title, report);
        break;
    case 184:
        /* Editor audio delivery restoration and diagnostics are runtime-only.
         * No persisted title fields changed. */
        validate_recover_title_shape(title, report);
        break;
    case 185:
        /* Realtime editor monitor pacing and BGL audio diagnostics are
         * runtime-only. No persisted title fields changed. */
        validate_recover_title_shape(title, report);
        break;
    case 186:
        /* Sample-locked editor monitor cadence is runtime-only. No persisted
         * title fields changed. */
        validate_recover_title_shape(title, report);
        break;
    case 187:
        /* Live-cue transition persistence and cache/prerender scheduling are
         * runtime-only. No persisted title fields changed. */
        validate_recover_title_shape(title, report);
        break;
    case 188:
        /* Dynamic OBS mixer visibility and editor reverse-audio transport are
         * runtime-only. No persisted title fields changed. */
        validate_recover_title_shape(title, report);
        break;
    case 189:
        /* The disabled-FX visual indicator and documentation consolidation are
         * editor/UI-only. No persisted title fields changed. */
        validate_recover_title_shape(title, report);
        break;
    default:
        break;
    }
}

inline json migrate_title_json(const json &input, MigrationReport *report_out = nullptr)
{
    MigrationReport report;
    json title = input;
    report.source_schema_version = safe_integer(title, "schema_version", 0);
    report.source_development_version = infer_development_version(title);

    validate_recover_title_shape(title, report);
    const int first = std::max(kFirstAuditedDevelopmentVersion,
                               report.source_development_version + 1);
    for (int version = first; version <= kCurrentDevelopmentVersion; ++version) {
        apply_development_migration(version, title, report);
        report.applied_development_versions.push_back(version);
    }

    /* Always run current validation. A file may claim a current/future
     * development version while still containing malformed known fields. */
    if (first > kCurrentDevelopmentVersion)
        apply_development_migration(kCurrentDevelopmentVersion, title, report);

    title["schema_version"] = kCurrentTitleSchemaVersion;
    title["development_version"] = kCurrentDevelopmentVersion;

    if (report.source_schema_version > kCurrentTitleSchemaVersion) {
        report.warnings.emplace_back(
            "title was created by a newer schema; unknown fields were preserved");
    }
    if (report.source_development_version > kCurrentDevelopmentVersion) {
        report.warnings.emplace_back(
            "title was created by a newer development version; best-effort loading was used");
    }

    if (report_out)
        *report_out = std::move(report);
    return title;
}

inline std::vector<int> development_migration_ledger()
{
    std::vector<int> versions;
    versions.reserve(kCurrentDevelopmentVersion - kFirstAuditedDevelopmentVersion + 1);
    for (int version = kFirstAuditedDevelopmentVersion;
         version <= kCurrentDevelopmentVersion; ++version)
        versions.push_back(version);
    return versions;
}

} // namespace bgs::serialization
