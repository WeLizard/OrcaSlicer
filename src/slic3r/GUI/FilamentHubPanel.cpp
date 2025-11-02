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

#include "FilamentHubPanel.hpp"
#include "../Utils/FilamentHubClient.hpp"
#include "GUI_Utils.hpp"
#include "I18N.hpp"
#include "Widgets/Button.hpp"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/msgdlg.h>
#include <boost/log/trivial.hpp>

namespace Slic3r {
namespace GUI {

FilamentHubPanel::FilamentHubPanel(wxWindow* parent, wxWindowID id, 
                                   const wxPoint& pos, 
                                   const wxSize& size, 
                                   long style)
    : wxPanel(parent, id, pos, size, style)
    , m_client(std::make_unique<Slic3r::FilamentHubClient>())
{
    SetBackgroundColour(*wxWHITE);
    init();
}

void FilamentHubPanel::init()
{
    create_layout();
    refresh();
}

void FilamentHubPanel::create_layout()
{
    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    
    // Top section: Status and auth buttons
    wxBoxSizer* top_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    // Status label
    m_status_label = new wxStaticText(this, wxID_ANY, _L("Not connected"), 
                                       wxDefaultPosition, wxDefaultSize);
    m_status_label->SetFont(::Label::Head_14);
    m_status_label->SetForegroundColour(wxColour(144, 144, 144));
    top_sizer->Add(m_status_label, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));
    
    top_sizer->AddStretchSpacer();
    
    // Test connection button
    m_test_btn = new Button(this, _L("Test Connection"));
    m_test_btn->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)
    ));
    m_test_btn->SetBorderColor(StateColor(std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal)));
    m_test_btn->SetTextColor(StateColor(std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal)));
    m_test_btn->SetMinSize(wxSize(FromDIP(120), FromDIP(30)));
    m_test_btn->Bind(wxEVT_BUTTON, &FilamentHubPanel::on_test_connection_click, this);
    top_sizer->Add(m_test_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    
    // Login button
    m_login_btn = new Button(this, _L("Login"));
    m_login_btn->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    ));
    m_login_btn->SetBorderColor(StateColor(std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)));
    m_login_btn->SetTextColor(StateColor(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)));
    m_login_btn->SetMinSize(wxSize(FromDIP(100), FromDIP(30)));
    m_login_btn->Bind(wxEVT_BUTTON, &FilamentHubPanel::on_login_click, this);
    top_sizer->Add(m_login_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    
    // Logout button (initially hidden)
    m_logout_btn = new Button(this, _L("Logout"));
    m_logout_btn->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)
    ));
    m_logout_btn->SetBorderColor(StateColor(std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal)));
    m_logout_btn->SetTextColor(StateColor(std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal)));
    m_logout_btn->SetMinSize(wxSize(FromDIP(100), FromDIP(30)));
    m_logout_btn->Hide();
    m_logout_btn->Bind(wxEVT_BUTTON, &FilamentHubPanel::on_logout_click, this);
    top_sizer->Add(m_logout_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    
    m_main_sizer->Add(top_sizer, 0, wxEXPAND | wxALL, FromDIP(10));
    
    // Separator line
    wxStaticLine* separator = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    m_main_sizer->Add(separator, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
    
    // Content section (placeholder for now)
    m_content_label = new wxStaticText(this, wxID_ANY, 
                                       _L("FilamentHub integration is being developed.\n\n"
                                          "This tab will allow you to:\n"
                                          "- Browse filament profiles from FilamentHub\n"
                                          "- Import profiles to OrcaSlicer\n"
                                          "- Sync your presets with FilamentHub"),
                                       wxDefaultPosition, wxDefaultSize);
    m_content_label->SetFont(::Label::Body_13);
    m_content_label->SetForegroundColour(wxColour(144, 144, 144));
    m_content_label->Wrap(FromDIP(400));
    m_main_sizer->Add(m_content_label, 1, wxEXPAND | wxALL, FromDIP(20));
    
    SetSizer(m_main_sizer);
    Layout();
}

void FilamentHubPanel::refresh()
{
    // Update UI based on authentication status
    if (m_client && m_client->is_authenticated()) {
        m_status_label->SetLabel(_L("Connected"));
        m_status_label->SetForegroundColour(wxColour(0, 150, 136));
        m_login_btn->Hide();
        m_logout_btn->Show();
    } else {
        m_status_label->SetLabel(_L("Not connected"));
        m_status_label->SetForegroundColour(wxColour(144, 144, 144));
        m_login_btn->Show();
        m_logout_btn->Hide();
    }
    Layout();
}

void FilamentHubPanel::on_login_click(wxCommandEvent& event)
{
    // TODO: Implement login dialog
    wxMessageBox(_L("Login functionality will be implemented soon."), 
                 _L("FilamentHub Login"), wxOK | wxICON_INFORMATION);
}

void FilamentHubPanel::on_logout_click(wxCommandEvent& event)
{
    if (m_client) {
        m_client->clear_access_token();
        refresh();
        wxMessageBox(_L("Logged out successfully."), 
                     _L("FilamentHub Logout"), wxOK | wxICON_INFORMATION);
    }
}

void FilamentHubPanel::on_test_connection_click(wxCommandEvent& event)
{
    if (!m_client) {
        wxMessageBox(_L("FilamentHub client not initialized."), 
                     _L("Error"), wxOK | wxICON_ERROR);
        return;
    }
    
    m_test_btn->Enable(false);
    m_status_label->SetLabel(_L("Testing connection..."));
    m_status_label->SetForegroundColour(wxColour(144, 144, 144));
    
    // Test connection
    m_client->test_connection(
        [this](std::string body, unsigned status) {
            // Success callback
            m_test_btn->Enable(true);
            m_status_label->SetLabel(_L("Connection successful"));
            m_status_label->SetForegroundColour(wxColour(0, 150, 136));
            wxMessageBox(wxString::Format(_L("Successfully connected to FilamentHub!\n\nStatus: %u\nResponse: %s"), 
                         status, wxString::FromUTF8(body)), 
                         _L("Connection Test"), wxOK | wxICON_INFORMATION);
            Layout();
        },
        [this](std::string body, std::string error, unsigned status) {
            // Error callback
            m_test_btn->Enable(true);
            m_status_label->SetLabel(_L("Connection failed"));
            m_status_label->SetForegroundColour(wxColour(200, 50, 50));
            wxString error_msg;
            if (status > 0) {
                error_msg = wxString::Format(_L("Failed to connect to FilamentHub.\n\nHTTP Status: %u\nError: %s"), 
                                            status, wxString::FromUTF8(error));
            } else {
                error_msg = wxString::Format(_L("Failed to connect to FilamentHub.\n\nError: %s"), 
                                            wxString::FromUTF8(error));
            }
            wxMessageBox(error_msg, _L("Connection Test"), wxOK | wxICON_ERROR);
            Layout();
        }
    );
}

}} // namespace Slic3r::GUI

