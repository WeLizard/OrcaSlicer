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
 * =============================================================================
 * MODIFICATIONS BY FILAMENTHUB (2025)
 * 
 * New file for FilamentHub API integration.
 * Original copyright (C) SoftFever/OrcaSlicer.
 *
 * Licensed under AGPL-3.0 (same as original OrcaSlicer)
 * Source: https://github.com/WeLizard/OrcaSlicer
 * Branch: filamenthub-integration
 * =============================================================================
 */

#ifndef __FILAMENTHUB_CLIENT_HPP__
#define __FILAMENTHUB_CLIENT_HPP__

#include <string>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include <mutex>
#include "Http.hpp"

namespace Slic3r {

// Forward declarations
class DynamicPrintConfig;

/**
 * \brief Client for FilamentHub API integration
 * 
 * This class provides methods to interact with FilamentHub REST API:
 * - Authentication (login/logout)
 * - Fetching filament profiles
 * - Syncing presets
 * 
 * All HTTP requests use the Http class (libcurl-based) already available in OrcaSlicer.
 */
class FilamentHubClient
{
public:
    // Constructor
    FilamentHubClient();
    ~FilamentHubClient();

    /**
     * \brief Cancel all active HTTP requests
     */
    void cancel_all();

    // API base URL (configurable, defaults to production)
    static const std::string DEFAULT_API_BASE_URL;
    static std::string get_api_base_url();
    static void set_api_base_url(const std::string& url);

    // API endpoint paths
    static constexpr const char* API_HEALTH                    = "/health";
    static constexpr const char* API_AUTH_LOGIN                 = "/api/v1/auth/login";
    static constexpr const char* API_AUTH_ME                    = "/api/v1/auth/me";
    static constexpr const char* API_AUTH_MY_PRESETS            = "/api/v1/auth/my-presets";
    static constexpr const char* API_AUTH_PRESETS_STATS         = "/api/v1/auth/me/presets-stats";
    static constexpr const char* API_PRESETS_BASE               = "/api/v1/presets/";
    static constexpr const char* API_EXPORT_JSON_SUFFIX         = "/export/orcaslicer.json";
    static constexpr const char* API_EXPORT_INFO_SUFFIX         = "/export/orcaslicer.info";
    static constexpr const char* API_BATCH_EXPORT               = "/api/v1/orcaslicer/presets/batch-export";
    static constexpr const char* API_PRINTER_PROFILES           = "/api/v1/orcaslicer/printer-profiles";
    static constexpr const char* API_PRINT_PROFILES             = "/api/v1/orcaslicer/print-profiles";
    static constexpr const char* API_PRINTER_PROFILES_BASE      = "/api/v1/printer-profiles/";
    static constexpr const char* API_PRINT_PROFILES_BASE        = "/api/v1/print-profiles/";
    static constexpr const char* API_PRINTER_PROFILES_IMPORT    = "/api/v1/orcaslicer/printer-profiles/import";
    static constexpr const char* API_PRINT_PROFILES_IMPORT      = "/api/v1/orcaslicer/print-profiles/import";
    static constexpr const char* API_FILAMENTS_IMPORT           = "/api/v1/orcaslicer/filaments/import";
    static constexpr const char* API_DELETED_PRESETS            = "/api/v1/orcaslicer/deleted-presets";
    static constexpr const char* API_NOTIFICATIONS_UNREAD_COUNT = "/api/v1/notifications/unread-count";
    static constexpr const char* API_SPOOL_PRESET_MAPPING       = "/api/v1/orcaslicer/spool-preset-mapping";

    // Timeout constants (seconds)
    static constexpr int TIMEOUT_CONNECT_DEFAULT = 10;
    static constexpr int TIMEOUT_MAX_DEFAULT     = 30;
    static constexpr int TIMEOUT_CONNECT_HEALTH  = 5;
    static constexpr int TIMEOUT_MAX_HEALTH      = 10;
    static constexpr int TIMEOUT_CONNECT_BATCH   = 10;
    static constexpr int TIMEOUT_MAX_BATCH       = 60;
    static constexpr int TIMEOUT_CONNECT_SPOOL   = 3;
    static constexpr int TIMEOUT_MAX_SPOOL       = 5;
    static constexpr int TIMEOUT_MAX_IMPORT      = 120; // Large preset/profile exports need more time

    // Retry constants
    static constexpr int RETRY_MAX_ATTEMPTS       = 3;
    static constexpr int RETRY_INITIAL_DELAY_MS   = 500; // Doubles each attempt (exponential backoff)

    /**
     * \brief Test connection to FilamentHub API
     * 
     * Sends a simple GET request to /api/v1/health or /api/v1/ to verify connectivity.
     * 
     * \param on_complete Called when request succeeds. Parameters: (response_body, http_status)
     * \param on_error Called when request fails. Parameters: (response_body, error_message, http_status)
     * \return true if request was initiated, false otherwise
     */
    bool test_connection(
        std::function<void(std::string /* body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Login to FilamentHub
     * 
     * Authenticates user with email/username and password.
     * Returns JWT token for subsequent API calls.
     * 
     * \param email_or_username User email or username
     * \param password User password
     * \param on_complete Called when login succeeds. Parameters: (response_body, http_status)
     * \param on_error Called when login fails. Parameters: (response_body, error_message, http_status)
     */
    void login(
        const std::string& email_or_username,
        const std::string& password,
        std::function<void(std::string /* body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Get current user info
     * 
     * Retrieves information about the currently authenticated user.
     * 
     * \param access_token JWT access token
     * \param on_complete Called when request succeeds. Parameters: (response_body, http_status)
     * \param on_error Called when request fails. Parameters: (response_body, error_message, http_status)
     */
    void get_current_user(
        const std::string& access_token,
        std::function<void(std::string /* body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Check if user is authenticated
     * 
     * \return true if access token is set, false otherwise
     */
    bool is_authenticated() const;

    /**
     * \brief Get stored access token
     * 
     * \return Access token string or empty string if not authenticated
     */
    std::string get_access_token() const;

    /**
     * \brief Set access token
     * 
     * \param token JWT access token
     */
    void set_access_token(const std::string& token);

    /**
     * \brief Clear access token (logout)
     */
    void clear_access_token();

    /**
     * \brief Download preset profile in OrcaSlicer JSON format
     * 
     * Downloads a preset profile from FilamentHub API in OrcaSlicer-compatible JSON format.
     * 
     * \param preset_id Preset ID in FilamentHub
     * \param access_token JWT access token (optional, for private presets)
     * \param on_complete Called when download succeeds. Parameters: (json_content, http_status)
     * \param on_error Called when download fails. Parameters: (response_body, error_message, http_status)
     */
    void download_profile(
        int preset_id,
        const std::string& access_token,
        std::function<void(std::string /* json_content */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Download preset .info file in INI format
     * 
     * Downloads a preset .info file from FilamentHub API in OrcaSlicer-compatible INI format.
     * This is used to preserve FilamentHub metadata (user_id, setting_id, updated_time) after import.
     * 
     * \param preset_id Preset ID in FilamentHub
     * \param access_token JWT access token
     * \param on_complete Called when download succeeds. Parameters: (info_content, http_status)
     * \param on_error Called when download fails. Parameters: (response_body, error_message, http_status)
     */
    void download_profile_info(
        int preset_id,
        const std::string& access_token,
        std::function<void(std::string /* info_content */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Batch-download multiple presets (OrcaSlicer JSON + .info) in one request
     *
     * Replaces N individual download_profile + download_profile_info calls with
     * a single POST to /api/v1/orcaslicer/presets/batch-export.
     *
     * Response JSON: { "profiles": [ { "preset_id": N, "config": {...}, "info": "...", "status": "ok"|"error" }, ... ] }
     *
     * \param preset_ids Vector of preset IDs to download
     * \param access_token JWT access token
     * \param on_complete Called on success. Parameters: (response_body_json, http_status)
     * \param on_error Called on failure. Parameters: (response_body, error_message, http_status)
     */
    void batch_download_profiles(
        const std::vector<int>& preset_ids,
        const std::string& access_token,
        std::function<void(std::string /* json_body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Get user's presets (created + saved from catalog)
     *
     * Retrieves all presets belonging to the authenticated user.
     * Supports incremental sync via updated_since parameter.
     * 
     * \param access_token JWT access token
     * \param updated_since Optional ISO 8601 timestamp for incremental sync
     * \param on_complete Called when request succeeds. Parameters: (json_body, http_status)
     * \param on_error Called when request fails. Parameters: (response_body, error_message, http_status)
     */
    void get_my_presets(
        const std::string& access_token,
        const std::string& updated_since = "",
        std::function<void(std::string /* json_body */, unsigned /* http_status */)> on_complete = nullptr,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error = nullptr
    );

    /**
     * \brief Get user's printer profiles for OrcaSlicer synchronisation
     * 
     * Retrieves all printer profiles belonging to the authenticated user.
     * Supports incremental sync via updated_since parameter.
     * 
     * \param access_token JWT access token
     * \param updated_since Optional ISO 8601 timestamp for incremental sync
     * \param on_complete Called when request succeeds. Parameters: (json_body, http_status)
     * \param on_error Called when request fails. Parameters: (response_body, error_message, http_status)
     */
    void get_my_printer_profiles(
        const std::string& access_token,
        const std::string& updated_since = "",
        std::function<void(std::string /* json_body */, unsigned /* http_status */)> on_complete = nullptr,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error = nullptr
    );

    /**
     * \brief Get user's print profiles for OrcaSlicer synchronisation
     * 
     * Retrieves all print profiles belonging to the authenticated user.
     * Supports incremental sync via updated_since parameter.
     * 
     * \param access_token JWT access token
     * \param updated_since Optional ISO 8601 timestamp for incremental sync
     * \param on_complete Called when request succeeds. Parameters: (json_body, http_status)
     * \param on_error Called when request fails. Parameters: (response_body, error_message, http_status)
     */
    void get_my_print_profiles(
        const std::string& access_token,
        const std::string& updated_since = "",
        std::function<void(std::string /* json_body */, unsigned /* http_status */)> on_complete = nullptr,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error = nullptr
    );

    /**
     * \brief Download printer profile in OrcaSlicer JSON format
     * 
     * Downloads a printer profile from FilamentHub API in OrcaSlicer-compatible JSON format.
     * 
     * \param profile_id Printer profile ID in FilamentHub
     * \param access_token JWT access token
     * \param on_complete Called when download succeeds. Parameters: (json_content, http_status)
     * \param on_error Called when download fails. Parameters: (response_body, error_message, http_status)
     */
    void download_printer_profile(
        int profile_id,
        const std::string& access_token,
        std::function<void(std::string /* json_content */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Download print profile in OrcaSlicer JSON format
     * 
     * Downloads a print profile from FilamentHub API in OrcaSlicer-compatible JSON format.
     * 
     * \param profile_id Print profile ID in FilamentHub
     * \param access_token JWT access token
     * \param on_complete Called when download succeeds. Parameters: (json_content, http_status)
     * \param on_error Called when download fails. Parameters: (response_body, error_message, http_status)
     */
    void download_print_profile(
        int profile_id,
        const std::string& access_token,
        std::function<void(std::string /* json_content */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Import printer profiles to FilamentHub
     * 
     * Sends printer profiles from OrcaSlicer to FilamentHub for synchronisation.
     * 
     * \param access_token JWT access token
     * \param profiles_json JSON array of printer profiles in OrcaSlicer format
     * \param on_complete Called when import succeeds. Parameters: (response_body, http_status)
     * \param on_error Called when import fails. Parameters: (response_body, error_message, http_status)
     */
    void import_printer_profiles(
        const std::string& access_token,
        const std::string& profiles_json,
        std::function<void(std::string /* json_body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Import print profiles to FilamentHub
     * 
     * Sends print profiles from OrcaSlicer to FilamentHub for synchronisation.
     * 
     * \param access_token JWT access token
     * \param profiles_json JSON array of print profiles in OrcaSlicer format
     * \param on_complete Called when import succeeds. Parameters: (response_body, http_status)
     * \param on_error Called when import fails. Parameters: (response_body, error_message, http_status)
     */
    void import_print_profiles(
        const std::string& access_token,
        const std::string& profiles_json,
        std::function<void(std::string /* json_body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Import filament presets to FilamentHub
     * 
     * Sends filament presets from OrcaSlicer to FilamentHub for synchronisation.
     * 
     * \param access_token JWT access token
     * \param presets_json JSON array of filament presets in OrcaSlicer format
     * \param on_complete Called when import succeeds. Parameters: (response_body, http_status)
     * \param on_error Called when import fails. Parameters: (response_body, error_message, http_status)
     */
    void import_filament_presets(
        const std::string& access_token,
        const std::string& presets_json,
        std::function<void(std::string /* json_body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Delete preset from FilamentHub
     * 
     * Deletes a preset from FilamentHub. Only the owner can delete their own presets.
     * 
     * \param preset_id Preset ID in FilamentHub
     * \param access_token JWT access token
     * \param on_complete Called when deletion succeeds. Parameters: (response_body, http_status)
     * \param on_error Called when deletion fails. Parameters: (response_body, error_message, http_status)
     */
    void delete_preset(
        int preset_id,
        const std::string& access_token,
        std::function<void(std::string /* response_body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Report deleted presets to FilamentHub
     * 
     * Reports presets that were deleted locally in OrcaSlicer.
     * This allows the backend to create notifications and handle user preferences.
     * 
     * \param access_token JWT access token
     * \param deleted_presets_json JSON array of deleted presets with preset_id, preset_name, bundle_preset_name
     * \param on_complete Called when report succeeds. Parameters: (response_body, http_status)
     * \param on_error Called when report fails. Parameters: (response_body, error_message, http_status)
     */
    void report_deleted_presets(
        const std::string& access_token,
        const std::string& deleted_presets_json,
        std::function<void(std::string /* response_body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Get unread notifications count
     * 
     * Retrieves the count of unread notifications for the authenticated user.
     * 
     * \param access_token JWT access token
     * \param on_complete Called when request succeeds. Parameters: (response_body with unread_count, http_status)
     * \param on_error Called when request fails. Parameters: (response_body, error_message, http_status)
     */
    void get_unread_notifications_count(
        const std::string& access_token,
        std::function<void(std::string /* response_body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

    /**
     * \brief Resolve spool IDs to OrcaSlicer preset setting_ids (synchronous)
     *
     * Calls GET /api/v1/orcaslicer/spool-preset-mapping?spool_ids=...
     * Returns a map of spool_id -> preset_id. Used by MoonrakerPrinterAgent
     * to match HH gate spools to installed FilamentHub presets during AMS sync.
     * Must be called from a background thread (uses perform_sync).
     *
     * \param access_token JWT access token
     * \param spool_ids Comma-separated spool IDs (e.g. "1,5,8")
     * \return Map of spool_id -> preset_id (only entries with valid presets)
     */
    std::map<int, int> resolve_spool_presets_sync(
        const std::string& access_token,
        const std::string& spool_ids
    );

    /**
     * \brief Get presets statistics
     *
     * Retrieves statistics about user's presets (total count and synced count).
     *
     * \param access_token JWT access token
     * \param on_complete Called when request succeeds. Parameters: (response_body with total_presets and synced_presets, http_status)
     * \param on_error Called when request fails. Parameters: (response_body, error_message, http_status)
     */
    void get_presets_stats(
        const std::string& access_token,
        std::function<void(std::string /* response_body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    );

private:
    /**
     * \brief Perform an HTTP request with automatic retry on transient failures
     *
     * Retries on timeout (status 0), 429 Too Many Requests, and 5xx server errors.
     * Uses exponential backoff between attempts. The build_request lambda is called
     * each retry to create a fresh Http object (since Http is non-copyable).
     *
     * \param tag Method name for logging
     * \param build_request Factory that returns a configured Http (without callbacks)
     * \param on_complete Success callback
     * \param on_error Final error callback (called after all retries exhausted)
     * \param max_retries Maximum number of attempts (default: RETRY_MAX_ATTEMPTS)
     */
    void perform_with_retry(
        const char* tag,
        std::function<Http()> build_request,
        Http::CompleteFn on_complete,
        Http::ErrorFn on_error,
        int max_retries = RETRY_MAX_ATTEMPTS
    );

    /**
     * \brief Store an active request to prevent premature destruction
     *
     * Http::perform() returns a shared_ptr. The request runs in a background thread
     * and will be cancelled if the shared_ptr is destroyed. We store all active
     * requests here and clean them up when callbacks fire.
     */
    void store_request(Http::Ptr request);

    /**
     * \brief Remove completed requests from the active list
     */
    void cleanup_completed_requests();

    std::string m_access_token;
    static std::string s_api_base_url;

    std::vector<Http::Ptr> m_active_requests;
    mutable std::mutex m_requests_mutex;
};

} // namespace Slic3r

#endif // __FILAMENTHUB_CLIENT_HPP__

