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
     * \brief Synchronize user presets from FilamentHub
     * 
     * Downloads all user presets from FilamentHub API and imports them into OrcaSlicer PresetBundle.
     * Supports incremental sync via last_sync_time stored in AppConfig.
     * 
     * \param force_full_sync If true, syncs all presets regardless of last_sync_time
     */
    void synchronize_presets(bool force_full_sync = false);

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
    static const wxString s_default_url; // Default FilamentHub frontend URL
    
    // UI elements
    wxPanel* m_info_panel { nullptr }; // Panel with user info and sync button
    wxStaticText* m_user_name_label { nullptr }; // User name label
    wxStaticText* m_preset_count_label { nullptr }; // Preset count label
    wxButton* m_sync_button { nullptr }; // Sync button
    wxButton* m_catalog_button { nullptr }; // Catalog navigation button
    wxButton* m_profile_button { nullptr }; // Profile navigation button (only if logged in)
    wxButton* m_login_button { nullptr }; // Login button (only if not logged in)
    wxButton* m_logout_button { nullptr }; // Logout button (only if logged in)
    bool m_is_syncing { false }; // Is sync in progress
    
    // Constants for AppConfig keys
    static const std::string CONFIG_SECTION_FILAMENTHUB;
    static const std::string CONFIG_KEY_ACCESS_TOKEN;
    static const std::string CONFIG_KEY_USER_ID;
    static const std::string CONFIG_KEY_LAST_SYNC_TIME;
    static const std::string CONFIG_KEY_PRESET_MAPPING;
};

}} // namespace Slic3r::GUI

#endif // __FILAMENTHUB_PANEL_HPP__

