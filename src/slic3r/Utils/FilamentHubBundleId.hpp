/*
 * FilamentHub Integration for OrcaSlicer
 *
 * Copyright (C) 2025 FilamentHub
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Source: https://github.com/WeLizard/OrcaSlicer
 * Branch: filamenthub-integration
 *
 * =============================================================================
 * FilamentHub bundle_id helpers
 *
 * OrcaSlicer 2.4 (PR #13414) introduced `Preset.bundle_id` as the canonical
 * identity for presets imported from any cloud bundle source. The format is
 * `"<provider>:<id>"` — for Orca Cloud it's `"orca:<uuid>"`, for our presets
 * it's `"filamenthub:<int_id>"` where int_id is our backend Preset/PrinterProfile/
 * PrintProfile id.
 *
 * These helpers replace the legacy `Preset::is_filamenthub` / `fhub_source` /
 * `fhub_id` fields that were removed during the 2.4 merge.
 *
 * Backward compatibility: read_fhub_id_from_settings() also accepts the legacy
 * fhub_source/fhub_id pair, so older .info files keep working after upgrade.
 * =============================================================================
 */

#ifndef slic3r_FilamentHubBundleId_hpp_
#define slic3r_FilamentHubBundleId_hpp_

#include <string>
#include <nlohmann/json.hpp>

namespace Slic3r {

// Bundle-id prefix that identifies a preset as belonging to FilamentHub.
// Stable across protocol versions; do not change without coordinating with
// backend `orcaslicer_exporter.py::profile["bundle_id"]` writes.
inline const std::string& fhub_bundle_id_prefix()
{
    static const std::string p = "filamenthub:";
    return p;
}

// Build canonical bundle_id for a FilamentHub preset/profile.
//   make_fhub_bundle_id(42) -> "filamenthub:42"
inline std::string make_fhub_bundle_id(int preset_id)
{
    return fhub_bundle_id_prefix() + std::to_string(preset_id);
}

// Extract the FilamentHub preset/profile int id from a bundle_id string.
// Returns 0 if the bundle_id is empty, malformed, or belongs to a different
// provider (e.g. "orca:..."). Never throws.
inline int extract_fhub_preset_id(const std::string& bundle_id)
{
    const std::string& prefix = fhub_bundle_id_prefix();
    if (bundle_id.size() <= prefix.size())
        return 0;
    if (bundle_id.compare(0, prefix.size(), prefix) != 0)
        return 0;
    try {
        size_t consumed = 0;
        int id = std::stoi(bundle_id.substr(prefix.size()), &consumed);
        // Reject trailing garbage like "filamenthub:42x"
        if (consumed != bundle_id.size() - prefix.size())
            return 0;
        return id > 0 ? id : 0;
    } catch (...) {
        return 0;
    }
}

// True if `bundle_id` identifies a FilamentHub preset.
inline bool is_fhub_bundle_id(const std::string& bundle_id)
{
    return extract_fhub_preset_id(bundle_id) > 0;
}

// Read FilamentHub preset id from an orcaslicer_settings JSON object.
//
// Accepts both formats:
//   - New: { "bundle_id": "filamenthub:42", ... }
//   - Legacy: { "fhub_source": "filamenthub", "fhub_id": "42", ... }
//             (fhub_id may be int OR string in legacy payloads)
//
// Returns 0 if no FilamentHub marker is present. Never throws.
inline int read_fhub_id_from_settings(const nlohmann::json& settings)
{
    if (!settings.is_object())
        return 0;

    // Prefer new bundle_id format.
    auto bundle_it = settings.find("bundle_id");
    if (bundle_it != settings.end() && bundle_it->is_string()) {
        int id = extract_fhub_preset_id(bundle_it->get<std::string>());
        if (id > 0)
            return id;
    }

    // Fall back to legacy fhub_source + fhub_id.
    auto source_it = settings.find("fhub_source");
    if (source_it == settings.end() || !source_it->is_string())
        return 0;
    if (source_it->get<std::string>() != "filamenthub")
        return 0;

    auto id_it = settings.find("fhub_id");
    if (id_it == settings.end())
        return 0;

    if (id_it->is_number_integer()) {
        int id = id_it->get<int>();
        return id > 0 ? id : 0;
    }
    if (id_it->is_string()) {
        try {
            size_t consumed = 0;
            const std::string& s = id_it->get_ref<const std::string&>();
            int id = std::stoi(s, &consumed);
            if (consumed != s.size())
                return 0;
            return id > 0 ? id : 0;
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

} // namespace Slic3r

#endif // slic3r_FilamentHubBundleId_hpp_
