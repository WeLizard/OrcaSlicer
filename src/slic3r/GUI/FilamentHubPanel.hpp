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
 * New file for FilamentHub tab integration.
 * Original copyright (C) SoftFever/OrcaSlicer.
 *
 * Licensed under AGPL-3.0 (same as original OrcaSlicer)
 * Source: https://github.com/WeLizard/OrcaSlicer
 * Branch: filamenthub-integration
 * =============================================================================
 */

#ifndef FILAMENTHUB_PANEL_HPP
#define FILAMENTHUB_PANEL_HPP

// Include nlohmann/json первым (header-only library)
#include <nlohmann/json.hpp>

#include <wx/panel.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/webview.h>
#include <wx/event.h>
#include <wx/gauge.h>
#include "Widgets/Button.hpp"
#include <future>
#include <functional>
#include <atomic>
#include <mutex>
#include <vector>
#include <memory>
#include "../Utils/FilamentHubClient.hpp"

namespace Slic3r {
namespace GUI {

/**
 * \brief FilamentHub Panel for OrcaSlicer
 *
 * This panel displays the FilamentHub web frontend in a WebView:
 * - Full React frontend from https://filamenthub.ru
 * - Authentication, browsing, and profile management
 * - Import profiles from FilamentHub to OrcaSlicer
 * - Sync user presets with FilamentHub
 *
 * This panel appears as a new tab in the main window (next to Prepare, Preview, Printer, Project).
 */
class FilamentHubPanel : public wxPanel
{
public:
    FilamentHubPanel(wxWindow* parent, wxWindowID id = wxID_ANY, 
                     const wxPoint& pos = wxDefaultPosition, 
                     const wxSize& size = wxDefaultSize, 
                     long style = wxTAB_TRAVERSAL);
    virtual ~FilamentHubPanel();

    /**
     * \brief Initialize the panel UI and load the web frontend
     */
    void init();

    /**
     * \brief Load the FilamentHub frontend URL
     */
    void load_url(const wxString& url = "https://filamenthub.ru");

    /**
     * \brief Reload the web view
     */
    void reload();
    
    /**
     * \brief Update user info display (name, preset count)
     * 
     * Called after login or sync to update UI panel.
     */
    void update_user_info();
    
    /**
     * \brief Navigate to catalog page
     */
    void navigate_to_catalog();
    
    /**
     * \brief Navigate to profile page
     */
    void navigate_to_profile();

    /**
     * \brief Navigate to wiki page
     */
    void navigate_to_wiki();

    /**
     * \brief Show login dialog or redirect to login
     */
    void show_login();
    
    /**
     * \brief Logout user
     */
    void logout();

    /**
     * \brief Handle web view errors
     */
    void OnError(wxWebViewEvent& evt);

    /**
     * \brief Handle web view loaded
     */
    void OnLoaded(wxWebViewEvent& evt);

    /**
     * \brief Handle JavaScript messages from frontend
     */
    void OnScriptMessage(wxWebViewEvent& evt);

    /**
     * \brief Handle window close
     */
    void OnClose(wxCloseEvent& evt);

    /**
     * \brief Update state (show/hide)
     */
    bool Show(bool show = true) override;

private:
    /**
     * \brief Import filament profile from FilamentHub API
     * 
     * Downloads profile JSON from API, saves to temporary file, and imports into PresetBundle.
     * Sends response to frontend when complete.
     * 
     * \param preset_id Preset ID in FilamentHub
     * \param sequence_id Sequence ID from frontend for response matching
     */
    void import_profile(int preset_id, const wxString& sequence_id = "");

    /**
     * \brief Send response back to frontend via JavaScript
     */
    void send_response(const wxString& command, const wxString& status, const wxString& message = "", const wxString& sequence_id = "");
    
    /**
     * \brief Show notification in WebView (instead of modal dialog)
     * 
     * Sends a notification message to the FilamentHub frontend via JavaScript.
     * The frontend should display this as a non-blocking notification (e.g., toast).
     * 
     * \param message Notification message
     * \param type Notification type: "info", "warning", "error", "success"
     */
    void show_notification_in_webview(const wxString& message, const wxString& type = "info");

    /** Send a command to WebView frontend via postMessage (e.g. "sync_complete"). */
    void send_command_to_webview(const std::string& command);

    /**
     * \brief Synchronize user presets from FilamentHub
     * 
     * Downloads all user presets from FilamentHub API and imports them into OrcaSlicer PresetBundle.
     * Supports incremental sync via last_sync_time stored in AppConfig.
     * Checks user permissions before synchronizing.
     * 
     * \param force_full_sync If true, syncs all presets regardless of last_sync_time
     */
    void synchronize_presets(bool force_full_sync = false);
    
    /**
     * \brief Continue sync after token validation
     * 
     * Internal function called after token validation succeeds.
     * Performs the actual synchronization of presets.
     * 
     * \param user_id User ID
     * \param force_full_sync If true, syncs all presets regardless of last_sync_time
     * \param api_base_url API base URL
     * \param access_token Validated access token
     */
    void continue_sync_after_token_validation(int user_id, bool force_full_sync, const std::string& api_base_url, const std::string& access_token);

    /**
     * \brief Synchronize user printer profiles from FilamentHub
     * 
     * Downloads all user printer profiles from FilamentHub API and imports them into OrcaSlicer PresetBundle.
     * Supports incremental sync via last_sync_time stored in AppConfig.
     * Checks user permissions before synchronizing.
     * 
     * \param force_full_sync If true, syncs all profiles regardless of last_sync_time
     */
    void synchronize_printer_profiles(bool force_full_sync = false);

    /**
     * \brief Synchronize user print profiles from FilamentHub
     * 
     * Downloads all user print profiles from FilamentHub API and imports them into OrcaSlicer PresetBundle.
     * Supports incremental sync via last_sync_time stored in AppConfig.
     * Checks user permissions before synchronizing.
     * 
     * \param force_full_sync If true, syncs all profiles regardless of last_sync_time
     */
    void synchronize_print_profiles(bool force_full_sync = false);

    /**
     * \brief Export filament presets to FilamentHub
     * 
     * Exports all user filament presets from OrcaSlicer to FilamentHub as drafts.
     * Sends presets to Backend via API, Backend creates drafts and returns mappings.
     * 
     * This is a minimal implementation - all business logic is on Backend.
     * C++ only gets presets from PresetBundle, converts to JSON, and sends to API.
     * 
     * Checks user permissions before exporting.
     */
    void export_filament_presets_to_filamenthub();

    /** Try to acquire sync lock with 60s deadlock timeout. Returns true if acquired. */
    bool try_acquire_sync_lock();

    /** Reset m_is_syncing when export finishes (handles unified export counter). */
    void finish_export_operation();

    /**
     * \brief Export printer profiles to FilamentHub
     * 
     * Exports all user printer profiles from OrcaSlicer to FilamentHub as drafts.
     * Sends profiles to Backend via API, Backend creates drafts and returns mappings.
     * 
     * This is a minimal implementation - all business logic is on Backend.
     * C++ only gets profiles from PresetBundle, converts to JSON, and sends to API.
     * 
     * Checks user permissions before exporting.
     */
    void export_printer_profiles_to_filamenthub();

    /**
     * \brief Export print profiles to FilamentHub
     * 
     * Exports all user print profiles from OrcaSlicer to FilamentHub as drafts.
     * Sends profiles to Backend via API, Backend creates drafts and returns mappings.
     * 
     * This is a minimal implementation - all business logic is on Backend.
     * C++ only gets profiles from PresetBundle, converts to JSON, and sends to API.
     * 
     * Checks user permissions before exporting.
     */
    void export_print_profiles_to_filamenthub();

    /**
     * \brief Unified export of all profile types (filament, printer, print) to FilamentHub
     *
     * Checks permissions once and exports all enabled profile types.
     */
    void export_profiles_to_filamenthub();

    /**
     * \brief Scan for orphaned filament presets (on-demand, triggered by user)
     *
     * Recursively scans the user filament directory for .json files that were
     * not loaded by OrcaSlicer (broken inherits). Found presets are sent to
     * FilamentHub as drafts with orphaned=true flag.
     */
    void scan_orphaned_presets();

private:
    /**
     * \brief Internal method to export filament presets (called after permission check)
     * 
     * \param access_token JWT access token
     * \param api_base_url API base URL
     */
    void export_filament_presets_to_filamenthub_internal(const std::string& access_token, const std::string& api_base_url);

    /**
     * \brief Internal method to export printer profiles (called after permission check)
     * 
     * \param access_token JWT access token
     * \param api_base_url API base URL
     */
    void export_printer_profiles_to_filamenthub_internal(const std::string& access_token, const std::string& api_base_url);

    /**
     * \brief Internal method to export print profiles (called after permission check)
     * 
     * \param access_token JWT access token
     * \param api_base_url API base URL
     */
    void export_print_profiles_to_filamenthub_internal(const std::string& access_token, const std::string& api_base_url);

    /** Internal: scan orphaned presets and send to server. */
    void scan_orphaned_presets_internal(const std::string& access_token, const std::string& api_base_url);

    /**
     * \brief Save access token and user_id to AppConfig
     */
    void process_login_success(const std::string& access_token, const std::string& refresh_token, int user_id);

    void save_auth_token(const std::string& access_token, int user_id);
    
    /**
     * \brief Load access token and user_id from AppConfig
     * 
     * \param access_token Output: access token (empty if not found)
     * \param user_id Output: user ID (0 if not found)
     * \return true if token was found, false otherwise
     */
    bool load_auth_token(std::string& access_token, int& user_id);
    
    /**
     * \brief Diagnostic function to extract exp from JWT token (without signature verification)
     * 
     * Parses JWT token and extracts exp claim for diagnostics.
     * Does NOT verify signature or expiration - only for diagnostic purposes.
     * 
     * \param token JWT token string
     * \return exp timestamp if found, 0 otherwise
     */
    long long extract_jwt_exp_for_diagnostics(const std::string& token);
    
    /**
     * \brief Save preset mapping (preset_id → bundle_preset_name) to AppConfig
     */
    void save_preset_mapping(int preset_id, const std::string& bundle_preset_name);
    
    /**
     * \brief Load preset mapping (preset_id → bundle_preset_name) from AppConfig
     * 
     * \param preset_id Preset ID in FilamentHub
     * \return Bundle preset name or empty string if not found
     */
    std::string load_preset_mapping(int preset_id);
    
    /**
     * \brief Check if preset exists in PresetBundle by name
     * 
     * \param preset_name Preset name (bundle preset name)
     * \return true if preset exists in PresetBundle (user presets), false otherwise
     */
    bool preset_exists_in_bundle(const std::string& preset_name);
    
    /**
     * \brief Remove preset mapping from AppConfig
     */
    void remove_preset_mapping(int preset_id);
    
    /**
     * \brief Get all preset IDs from mappings in AppConfig
     * 
     * \return Vector of preset IDs that have mappings
     */
    std::vector<int> get_all_mapped_preset_ids();
    
    /**
     * \brief Save printer profile mapping (profile_id → bundle_profile_name) to AppConfig
     */
    void save_printer_profile_mapping(int profile_id, const std::string& bundle_profile_name);
    
    /**
     * \brief Load printer profile mapping (profile_id → bundle_profile_name) from AppConfig
     * 
     * \param profile_id Printer profile ID in FilamentHub
     * \return Bundle profile name or empty string if not found
     */
    std::string load_printer_profile_mapping(int profile_id);
    
    /**
     * \brief Save print profile mapping (profile_id → bundle_profile_name) to AppConfig
     */
    void save_print_profile_mapping(int profile_id, const std::string& bundle_profile_name);
    
    /**
     * \brief Load print profile mapping (profile_id → bundle_profile_name) from AppConfig
     * 
     * \param profile_id Print profile ID in FilamentHub
     * \return Bundle profile name or empty string if not found
     */
    std::string load_print_profile_mapping(int profile_id);
    
    /**
     * \brief Scoped sync timestamp type for incremental sync cursors
     *
     * We keep independent cursors for filament/printer/print sync, while
     * preserving backward compatibility with legacy single-key cursor.
     */
    enum class SyncTimestampType {
        Filament,
        Printer,
        Print
    };

    /**
     * \brief Save last sync time to AppConfig
     */
    void save_last_sync_time(
        int user_id,
        const std::string& timestamp,
        SyncTimestampType sync_type = SyncTimestampType::Filament
    );
    
    /**
     * \brief Load last sync time from AppConfig
     * 
     * \param user_id User ID
     * \return Last sync timestamp (ISO 8601) or empty string if not found
     */
    std::string load_last_sync_time(
        int user_id,
        SyncTimestampType sync_type = SyncTimestampType::Filament
    );
    
    /**
     * \brief Add [fh] postfix to preset name if not already present
     */
    std::string ensure_filamenthub_postfix(const std::string& preset_name);
    
    /**
     * \brief Ensure parent preset exists, replace with fallback if not found
     * 
     * Checks if the parent preset (inherits) exists in OrcaSlicer preset collection.
     * If not found, replaces with universal fallback "fdm_filament_common".
     * 
     * \param profile_json JSON profile to check and modify
     * \return true if parent preset was found, false if replaced with fallback
     */
    bool ensure_parent_preset_exists(nlohmann::json& profile_json);
    
    /**
     * \brief Import preset from FilamentHub without UI dialogs (for sync)
     * 
     * Downloads and imports a preset silently, adding [fh] postfix and saving mapping.
     * 
     * \param preset_id Preset ID in FilamentHub
     * \param preset_name Preset name from FilamentHub
     * \param access_token Access token for API
     * \return true if imported successfully, false otherwise
     */
    bool import_preset_silent(int preset_id, const std::string& preset_name, const std::string& access_token);

    /**
     * \brief Update preset .info file with FilamentHub metadata
     * 
     * After importing a preset via import_json_presets(), OrcaSlicer creates a .info file with empty values.
     * This method downloads the correct .info file from FilamentHub API and updates the preset file.
     * 
     * IMPORTANT: We don't use fields sync_info, user_id, setting_id, base_id, updated_time from Preset object,
     * as they may be overwritten by BambuLab system. Instead, we download the .info file from API.
     * 
     * \param preset_id Preset ID in FilamentHub
     * \param preset_name Preset name in OrcaSlicer (with [fh] postfix)
     * \param access_token JWT token for API access
     */
    void update_preset_info_file(int preset_id, const std::string& preset_name, const std::string& access_token);

    /**
     * \brief Import printer profile from FilamentHub without UI dialogs (for sync)
     * 
     * Downloads and imports a printer profile silently, adding [fh] postfix and saving mapping.
     * 
     * \param profile_id Printer profile ID in FilamentHub
     * \param profile_name Printer profile name from FilamentHub
     * \param access_token Access token for API
     * \return true if imported successfully, false otherwise
     */
    bool import_printer_profile_silent(int profile_id, const std::string& profile_name, const std::string& access_token);

    /**
     * \brief Import print profile from FilamentHub without UI dialogs (for sync)
     * 
     * Downloads and imports a print profile silently, adding [fh] postfix and saving mapping.
     * 
     * \param profile_id Print profile ID in FilamentHub
     * \param profile_name Print profile name from FilamentHub
     * \param access_token Access token for API
     * \return true if imported successfully, false otherwise
     */
    bool import_print_profile_silent(int profile_id, const std::string& profile_name, const std::string& access_token);

    /**
     * \brief Get deleted preset action from AppConfig
     * 
     * \return Action: "ask" (default), "import", "delete", "skip"
     */
    std::string get_deleted_preset_action();
    
    /**
     * \brief Set deleted preset action in AppConfig
     * 
     * \param action Action: "ask", "import", "delete", "skip"
     */
    void set_deleted_preset_action(const std::string& action);
    
    /**
     * \brief Ask user what to do with deleted preset
     * 
     * Shows a dialog with options: import, delete from FilamentHub, or cancel.
     * 
     * \param preset_id Preset ID in FilamentHub
     * \param preset_name Preset name
     * \return Action: "import", "delete", or "cancel"
     */
    std::string ask_deleted_preset_action(int preset_id, const std::string& preset_name);
    
    /**
     * \brief Delete preset from FilamentHub
     * 
     * \param preset_id Preset ID in FilamentHub
     * \param access_token Access token for API
     * \return true if deleted successfully, false otherwise
     */
    bool delete_preset_from_filamenthub(int preset_id, const std::string& access_token);

    /**
     * \brief Check user permissions before synchronization
     * 
     * Retrieves user info from FilamentHub API and checks if user has permissions
     * to import/export printer and print profiles.
     * 
     * \param access_token JWT access token
     * \param on_complete Called when permissions are checked. Parameters: (allow_printer_import, allow_printer_export, allow_print_import, allow_print_export)
     * \param on_error Called when check fails. Parameters: (error_message, http_status)
     */
    void check_user_permissions(
        const std::string& access_token,
        std::function<void(bool, bool, bool, bool, bool)> on_complete,
        std::function<void(std::string, unsigned)> on_error
    );

private:
    /**
     * \brief Handle sync button click
     */
    void on_sync_button_click(wxCommandEvent& evt);
    
    /**
     * \brief Update sync button state (enabled/disabled, text)
     */
    void update_sync_button_state(bool is_syncing);
    
    /**
     * \brief Update UI visibility based on login state
     */
    void update_ui_for_login_state(bool is_logged_in);
    
    /**
     * \brief Update active button style based on current page
     * 
     * Sets the active button (corresponding to current page) to Confirm style (green),
     * and resets other navigation buttons to Regular style.
     */
    void update_active_button_style();
    
    /**
     * \brief Navigate to URL without page reload using React Router
     * 
     * Uses JavaScript to navigate via React Router without reloading the page.
     * This is faster and preserves React state.
     * 
     * \param path URL path (e.g., "/", "/profile", "/admin")
     */
    void navigate_without_reload(const wxString& path);
    
    /**
     * \brief Update unread notifications count
     * 
     * Fetches unread notifications count from API and updates the badge on the notifications button.
     */
    void update_unread_notifications_count();
    
    /**
     * \brief Show notifications dropdown in WebView
     * 
     * Injects JavaScript to show notifications dropdown menu in the WebView.
     */
    void show_notifications_dropdown();

private:
    // Persistent FilamentHubClient — keeps Http::Ptr alive across async calls
    std::unique_ptr<Slic3r::FilamentHubClient> m_fhub_client;

    wxWebView* m_browser { nullptr };
    wxBoxSizer* m_main_sizer { nullptr };
    wxString m_url_deferred; // URL to load when panel becomes visible
    wxString m_frontend_url; // Current frontend URL
    std::string m_api_base_url; // Current API base URL
    static const wxString DEFAULT_FRONTEND_URL; // Default FilamentHub frontend URL
    
    // UI elements
    wxPanel* m_info_panel { nullptr }; // Panel with user info and sync button
    wxStaticText* m_user_name_label { nullptr }; // User name label
    wxStaticText* m_preset_count_label { nullptr }; // Preset count label
    Button* m_sync_button { nullptr }; // Sync button
    Button* m_catalog_button { nullptr }; // Catalog navigation button
    Button* m_profile_button { nullptr }; // Profile navigation button (only if logged in)
    Button* m_wiki_button { nullptr }; // Wiki navigation button
    Button* m_login_button { nullptr }; // Login button (only if not logged in)
    Button* m_logout_button { nullptr }; // Logout button (only if logged in)
    Button* m_settings_button { nullptr }; // Settings button for URLs

    Button* m_refresh_button { nullptr }; // Refresh/Reload button
    std::atomic<bool> m_is_syncing { false }; // Is sync in progress (atomic for thread safety)
    std::atomic<int> m_active_syncs { 0 }; // Number of active sync operations (presets, printer profiles, print profiles)
    std::atomic<int> m_active_exports { 0 }; // Number of active export operations in unified export
    std::atomic<bool> m_full_sync_attempted { false }; // Защита от зацикливания: была ли попытка полной синхронизации
    std::atomic<bool> m_sync_retry_attempted { false }; // Silent retry after 401 (wait for frontend token refresh)
    std::atomic<bool> m_initial_sync_done { false }; // Prevents repeated auto-sync after login
    std::chrono::steady_clock::time_point m_sync_started_at; // Timestamp when sync started (for timeout detection)
    wxGauge* m_sync_progress { nullptr }; // Progress bar for sync operations
    wxStaticText* m_sync_status_label { nullptr }; // Status text for sync progress
    int m_unread_notifications_count { 0 }; // Unread notifications count
    wxMenu* m_notifications_menu { nullptr }; // Popup menu for notifications
    std::string m_active_page { "" }; // Currently active page ("catalog", "profile", "admin", etc.)
    
    // Async preset import queue
    struct PresetImportTask {
        int preset_id;
        std::string preset_name;
        std::string access_token;
        std::string api_base_url;
        int user_id; // КРИТИЧНО: Нужен для обновления last_sync_time после завершения импорта
    };
    std::vector<PresetImportTask> m_preset_import_queue; // Queue of presets to import
    std::mutex m_preset_queue_mutex; // Mutex for preset queue
    int m_synced_count { 0 }; // Counter for successfully synced presets
    int m_error_count { 0 }; // Counter for failed presets
    int m_total_presets_to_sync { 0 }; // Total number of presets to sync
    bool m_processing_preset_queue { false }; // Is preset queue being processed
    std::vector<std::string> m_sync_detail_lines; // Per-preset sync details (for dev mode notifications)
    bool m_export_disabled_notified { false }; // Show "export disabled" notice only once per session
    
    // Constants for AppConfig keys
    static const std::string CONFIG_SECTION_FILAMENTHUB;
    static const std::string CONFIG_KEY_ACCESS_TOKEN;
    static const std::string CONFIG_KEY_USER_ID;
    static const std::string CONFIG_KEY_REFRESH_TOKEN;
    static const std::string CONFIG_KEY_LAST_SYNC_TIME;
    static const std::string CONFIG_KEY_PRESET_MAPPING;
    static const std::string CONFIG_KEY_PRINTER_PROFILE_MAPPING;
    static const std::string CONFIG_KEY_PRINT_PROFILE_MAPPING;
    static const std::string CONFIG_KEY_FRONTEND_URL;
    static const std::string CONFIG_KEY_API_BASE_URL;
    static const std::string CONFIG_KEY_DELETED_PRESET_ACTION; // "ask", "import", "delete", "skip"

    void load_configuration();
    void apply_configuration();
    void show_settings_dialog();
    void update_frontend_url(const wxString& url, bool persist = true, bool reload = true);
    void update_api_base_url(const std::string& url, bool persist = true);
    wxString build_frontend_url(const wxString& path_suffix = wxEmptyString) const;
    
    /**
     * \brief Helper function to set button style with square corners
     * 
     * Sets button style and corner radius to 0 (square corners) in one call.
     * This is needed because SetStyle() for ButtonType::Compact automatically sets corner radius to 8 DIP.
     */
    void set_button_square_style(Button* button, ButtonStyle style);

    void import_profile_internal(int preset_id, const wxString& sequence_id, std::string api_base_url);
    
    void process_preset_import_queue(); // Process preset import queue sequentially (legacy per-preset HTTP)
    void import_preset_silent_with_callback(int preset_id, const std::string& preset_name,
                                           const std::string& access_token,
                                           std::function<void(bool success)> on_complete);

    /**
     * \brief Import all presets from a batch-export API response (no per-preset HTTP).
     *
     * Called from the batch_download_profiles callback.  Parses the JSON array,
     * writes temp files, and imports every preset on the UI thread in one pass.
     *
     * \param batch_json  Raw JSON body from POST /orcaslicer/presets/batch-export
     * \param presets_meta Map preset_id → name (from the earlier get_my_presets response)
     * \param user_id     Needed to save last_sync_time after import
     * \param access_token Needed for ensure_parent_preset_exists (may resolve inherits)
     */
    void process_batch_export_response(
        const std::string& batch_json,
        const std::map<int, std::string>& presets_meta,
        int user_id,
        const std::string& access_token
    );

    void run_async(const std::string& job_name, std::function<void()> job);
    void cleanup_finished_tasks();
    void update_async_ui();
    void show_sync_progress(int total_steps);
    void update_sync_progress_ui(int completed, int total, const wxString& status_text);
    void hide_sync_progress();

    std::mutex m_async_mutex;
    std::vector<std::future<void>> m_async_tasks;
    size_t m_active_async_jobs { 0 };
};

}} // namespace Slic3r::GUI

#endif // FILAMENTHUB_PANEL_HPP
