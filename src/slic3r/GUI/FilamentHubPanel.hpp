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
 * Source: https://github.com/lizardjazz1/OrcaSlicer
 * Branch: filamenthub-integration
 * =============================================================================
 */

#ifndef __FILAMENTHUB_PANEL_HPP__
#define __FILAMENTHUB_PANEL_HPP__

// Include nlohmann/json первым (header-only library)
#include <nlohmann/json.hpp>

#include <wx/panel.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/webview.h>
#include <wx/event.h>
#include <wx/gauge.h>
#include <future>
#include <functional>
#include <mutex>
#include <vector>
#include <memory>

namespace Slic3r {
namespace GUI {

/**
 * \brief FilamentHub Panel for OrcaSlicer
 * 
 * This panel displays the FilamentHub web frontend in a WebView:
 * - Full React frontend from http://localhost:3000
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
    void load_url(const wxString& url = "http://localhost:3000");

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

private:
    /**
     * \brief Save access token and user_id to AppConfig
     */
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
     * \brief Save last sync time to AppConfig
     */
    void save_last_sync_time(int user_id, const std::string& timestamp);
    
    /**
     * \brief Load last sync time from AppConfig
     * 
     * \param user_id User ID
     * \return Last sync timestamp (ISO 8601) or empty string if not found
     */
    std::string load_last_sync_time(int user_id);
    
    /**
     * \brief Add [FilamentHub] postfix to preset name if not already present
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
     * Downloads and imports a preset silently, adding [FilamentHub] postfix and saving mapping.
     * 
     * \param preset_id Preset ID in FilamentHub
     * \param preset_name Preset name from FilamentHub
     * \param access_token Access token for API
     * \return true if imported successfully, false otherwise
     */
    bool import_preset_silent(int preset_id, const std::string& preset_name, const std::string& access_token);

    /**
     * \brief Import printer profile from FilamentHub without UI dialogs (for sync)
     * 
     * Downloads and imports a printer profile silently, adding [FilamentHub] postfix and saving mapping.
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
     * Downloads and imports a print profile silently, adding [FilamentHub] postfix and saving mapping.
     * 
     * \param profile_id Print profile ID in FilamentHub
     * \param profile_name Print profile name from FilamentHub
     * \param access_token Access token for API
     * \return true if imported successfully, false otherwise
     */
    bool import_print_profile_silent(int profile_id, const std::string& profile_name, const std::string& access_token);

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
        std::function<void(bool, bool, bool, bool)> on_complete,
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

private:
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
    wxButton* m_sync_button { nullptr }; // Sync button
    wxButton* m_catalog_button { nullptr }; // Catalog navigation button
    wxButton* m_profile_button { nullptr }; // Profile navigation button (only if logged in)
    wxButton* m_login_button { nullptr }; // Login button (only if not logged in)
    wxButton* m_logout_button { nullptr }; // Logout button (only if logged in)
    wxButton* m_settings_button { nullptr }; // Settings button for URLs
    bool m_is_syncing { false }; // Is sync in progress
    int m_active_syncs { 0 }; // Number of active sync operations (presets, printer profiles, print profiles)
    wxGauge* m_sync_progress { nullptr }; // Progress bar for sync operations
    wxStaticText* m_sync_status_label { nullptr }; // Status text for sync progress
    
    // Constants for AppConfig keys
    static const std::string CONFIG_SECTION_FILAMENTHUB;
    static const std::string CONFIG_KEY_ACCESS_TOKEN;
    static const std::string CONFIG_KEY_USER_ID;
    static const std::string CONFIG_KEY_LAST_SYNC_TIME;
    static const std::string CONFIG_KEY_PRESET_MAPPING;
    static const std::string CONFIG_KEY_PRINTER_PROFILE_MAPPING;
    static const std::string CONFIG_KEY_PRINT_PROFILE_MAPPING;
    static const std::string CONFIG_KEY_FRONTEND_URL;
    static const std::string CONFIG_KEY_API_BASE_URL;

    void load_configuration();
    void apply_configuration();
    void show_settings_dialog();
    void update_frontend_url(const wxString& url, bool persist = true, bool reload = true);
    void update_api_base_url(const std::string& url, bool persist = true);
    wxString build_frontend_url(const wxString& path_suffix = wxEmptyString) const;

    void import_profile_internal(int preset_id, const wxString& sequence_id, std::string api_base_url);

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

#endif // __FILAMENTHUB_PANEL_HPP__

