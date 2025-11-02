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

#include <wx/panel.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <memory>

namespace Slic3r {

class FilamentHubClient;

namespace GUI {

/**
 * \brief FilamentHub Panel for OrcaSlicer
 * 
 * This panel provides integration with FilamentHub API:
 * - Authentication (login/logout)
 * - Browse and search filament profiles
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
    ~FilamentHubPanel() = default;

    /**
     * \brief Initialize the panel UI and bind events
     */
    void init();

    /**
     * \brief Refresh panel content (e.g., after login/logout)
     */
    void refresh();

protected:
    /**
     * \brief Create the main UI layout
     */
    void create_layout();

    /**
     * \brief Create authentication UI (login/logout button, status)
     */
    void create_auth_ui();

    /**
     * \brief Create main content UI (filament list, search, etc.)
     */
    void create_content_ui();

    /**
     * \brief Handle login button click
     */
    void on_login_click(wxCommandEvent& event);

    /**
     * \brief Handle logout button click
     */
    void on_logout_click(wxCommandEvent& event);

    /**
     * \brief Handle test connection button click
     */
    void on_test_connection_click(wxCommandEvent& event);

private:
    // UI Components
    wxBoxSizer* m_main_sizer { nullptr };
    wxStaticText* m_status_label { nullptr };
    wxButton* m_login_btn { nullptr };
    wxButton* m_logout_btn { nullptr };
    wxButton* m_test_btn { nullptr };
    wxStaticText* m_content_label { nullptr };

    // FilamentHub Client
    std::unique_ptr<Slic3r::FilamentHubClient> m_client;
};

}} // namespace Slic3r::GUI

#endif // __FILAMENTHUB_PANEL_HPP__

