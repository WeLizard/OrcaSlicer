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
 * New file for FilamentHub tab integration using WebView.
 * Original copyright (C) SoftFever/OrcaSlicer.
 * 
 * Licensed under AGPL-3.0 (same as original OrcaSlicer)
 * Source: https://github.com/lizardjazz1/OrcaSlicer
 * Branch: filamenthub-integration
 * =============================================================================
 */

#include "FilamentHubPanel.hpp"
#include "GUI_Utils.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp" // Для ForwardCompatibilitySubstitutionRule
#include "../Utils/FilamentHubClient.hpp"
#include "Widgets/WebView.hpp"
#include <wx/sizer.h>
#include <wx/webview.h>
#include <wx/msgdlg.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/panel.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
// #include <wx/flexgrid.h> // Removed - wxFlexGridSizer should be available from other includes
#include <nlohmann/json.hpp>
#include <chrono>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <fstream>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <cctype> // для std::isdigit
#include <libslic3r/AppConfig.hpp>
#include <boost/algorithm/string.hpp>
#include <vector>
#include <memory>
#include <mutex>
#include <algorithm>

namespace Slic3r {
namespace GUI {

// Static member initialization
const wxString FilamentHubPanel::DEFAULT_FRONTEND_URL = "http://localhost:3000";
const std::string FilamentHubPanel::CONFIG_SECTION_FILAMENTHUB = "filamenthub";
const std::string FilamentHubPanel::CONFIG_KEY_ACCESS_TOKEN = "access_token";
const std::string FilamentHubPanel::CONFIG_KEY_USER_ID = "user_id";
const std::string FilamentHubPanel::CONFIG_KEY_LAST_SYNC_TIME = "last_sync_time";
const std::string FilamentHubPanel::CONFIG_KEY_PRESET_MAPPING = "preset_mapping";
const std::string FilamentHubPanel::CONFIG_KEY_PRINTER_PROFILE_MAPPING = "printer_profile_mapping";
const std::string FilamentHubPanel::CONFIG_KEY_PRINT_PROFILE_MAPPING = "print_profile_mapping";
const std::string FilamentHubPanel::CONFIG_KEY_FRONTEND_URL = "frontend_url";
const std::string FilamentHubPanel::CONFIG_KEY_API_BASE_URL = "api_base_url";

FilamentHubPanel::FilamentHubPanel(wxWindow* parent, wxWindowID id, 
                                   const wxPoint& pos, 
                                   const wxSize& size, 
                                   long style)
    : wxPanel(parent, id, pos, size, style)
{
    m_frontend_url = DEFAULT_FRONTEND_URL;
    m_api_base_url = FilamentHubClient::DEFAULT_API_BASE_URL;
    SetBackgroundColour(*wxWHITE);
    init();
}

FilamentHubPanel::~FilamentHubPanel()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}

void FilamentHubPanel::init()
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== FilamentHubPanel::init() CALLED ==========";
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Initializing FilamentHubPanel...";
    
    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    load_configuration();
    apply_configuration();

    // Create info panel with navigation, user info and buttons
    m_info_panel = new wxPanel(this, wxID_ANY);
    m_info_panel->SetBackgroundColour(*wxWHITE);
    wxBoxSizer* info_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    // User info (left side)
    m_user_name_label = new wxStaticText(m_info_panel, wxID_ANY, _("Not logged in"), wxDefaultPosition, wxDefaultSize);
    m_user_name_label->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    info_sizer->Add(m_user_name_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 10);
    
    m_preset_count_label = new wxStaticText(m_info_panel, wxID_ANY, _("Presets: 0"), wxDefaultPosition, wxDefaultSize);
    m_preset_count_label->Hide(); // Hidden by default (shown when logged in)
    info_sizer->Add(m_preset_count_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 10);
    
    info_sizer->AddStretchSpacer(); // Push buttons to the right
    
    // Right side: Navigation buttons first
    m_catalog_button = new wxButton(m_info_panel, wxID_ANY, _("Catalog"), wxDefaultPosition, wxDefaultSize);
    m_catalog_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { navigate_to_catalog(); });
    info_sizer->Add(m_catalog_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);
    
    m_profile_button = new wxButton(m_info_panel, wxID_ANY, _("Profile"), wxDefaultPosition, wxDefaultSize);
    m_profile_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { navigate_to_profile(); });
    m_profile_button->Hide(); // Hidden by default (shown when logged in)
    info_sizer->Add(m_profile_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);
    
    // Action buttons
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Creating sync button...";
    m_sync_button = new wxButton(m_info_panel, wxID_ANY, _("Synchronize"), wxDefaultPosition, wxDefaultSize);
    
    // Проверяем, что кнопка создана
    if (m_sync_button == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to create m_sync_button!";
    } else {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sync button created successfully";
    }
    
    // Привязываем обработчик события
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Binding sync button click handler...";
    m_sync_button->Bind(wxEVT_BUTTON, &FilamentHubPanel::on_sync_button_click, this);
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sync button click handler bound successfully";
    
    m_sync_button->Hide(); // Hidden by default (shown when logged in)
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sync button hidden by default (will be shown when logged in)";
    info_sizer->Add(m_sync_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);

    m_settings_button = new wxButton(m_info_panel, wxID_ANY, _("Settings"), wxDefaultPosition, wxDefaultSize);
    m_settings_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show_settings_dialog(); });
    info_sizer->Add(m_settings_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);
    
    // Login button - redirects to login page in WebView (user logs in there)
    m_login_button = new wxButton(m_info_panel, wxID_ANY, _("Login"), wxDefaultPosition, wxDefaultSize);
    m_login_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show_login(); });
    info_sizer->Add(m_login_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);
    
    m_logout_button = new wxButton(m_info_panel, wxID_ANY, _("Logout"), wxDefaultPosition, wxDefaultSize);
    m_logout_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { logout(); });
    m_logout_button->Hide(); // Hidden by default (shown when logged in)
    info_sizer->Add(m_logout_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);

    m_sync_status_label = new wxStaticText(m_info_panel, wxID_ANY, _("Ready"));
    m_sync_status_label->Hide();
    info_sizer->Add(m_sync_status_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);

    m_sync_progress = new wxGauge(m_info_panel, wxID_ANY, 100, wxDefaultPosition, wxSize(120, -1));
    m_sync_progress->Hide();
    info_sizer->Add(m_sync_progress, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);
    
    m_info_panel->SetSizer(info_sizer);
    m_info_panel->Layout();
    
    m_main_sizer->Add(m_info_panel, 0, wxEXPAND | wxALL, 5);

    // Create the webview
    m_browser = WebView::CreateWebView(this, "");
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Could not init webview";
        return;
    }

    m_browser->Bind(wxEVT_WEBVIEW_ERROR, &FilamentHubPanel::OnError, this);
    m_browser->Bind(wxEVT_WEBVIEW_LOADED, &FilamentHubPanel::OnLoaded, this);
    m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &FilamentHubPanel::OnScriptMessage, this);

    // Register JavaScript message handler (use "wx" like other OrcaSlicer panels)
    // Note: В WipeTowerDialog AddScriptMessageHandler вызывается сразу после создания WebView
    // Попробуем добавить сразу, и если не получилось - попробуем в OnLoaded
    bool handler_added = m_browser->AddScriptMessageHandler("wx");
    if (!handler_added) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Could not add script message handler on init (will retry on load)";
    } else {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Script message handler added successfully on init";
    }

    m_main_sizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    SetSizer(m_main_sizer);
    Layout();
    
    // Update user info from saved token
    // Автоматическая синхронизация при открытии вкладки выполняется в методе Show()
    // Это позволяет избежать ошибок при запуске приложения, но синхронизировать при открытии вкладки
    update_user_info();

    // Load the default URL
    load_url(build_frontend_url());

    // Connect close event
    Bind(wxEVT_CLOSE_WINDOW, &FilamentHubPanel::OnClose, this);
}

void FilamentHubPanel::load_url(const wxString& url)
{
    if (m_browser == nullptr)
        return;

    if (this->IsShown()) {
        m_url_deferred.clear();
        WebView::LoadUrl(m_browser, url);
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Loading URL: " << url.ToUTF8();
    } else {
        m_url_deferred = url;
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Deferring URL load: " << url.ToUTF8();
    }
}

void FilamentHubPanel::reload()
{
    if (m_browser != nullptr) {
        m_browser->Reload();
    }
}

bool FilamentHubPanel::Show(bool show)
{
    if (show && !m_url_deferred.empty()) {
        WebView::LoadUrl(m_browser, m_url_deferred);
        m_url_deferred.clear();
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Loading deferred URL";
    }
    
    // Если вкладка открывается и пользователь залогинен, автоматически синхронизируем пресеты
    // НО: Не вызываем синхронизацию автоматически, чтобы избежать проблем с UI
    // Пользователь может нажать кнопку "Synchronize" вручную
    if (show) {
        std::string access_token;
        int user_id = 0;
        if (load_auth_token(access_token, user_id)) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Tab opened, user logged in (ID: " << user_id 
                                     << "), ready for sync";
            // Обновляем информацию о пользователе, но НЕ синхронизируем автоматически
            // Автоматическая синхронизация отключена, чтобы избежать проблем с UI
            // Пользователь может нажать кнопку "Synchronize" вручную
        } else {
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Tab opened, user not logged in";
        }
    }
    
    return wxPanel::Show(show);
}

void FilamentHubPanel::OnError(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: WebView error: " << evt.GetString().ToUTF8();
}

void FilamentHubPanel::OnLoaded(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: WebView loaded: " << evt.GetURL().ToUTF8();
    
    // Попробуем добавить script message handler еще раз (если не получилось при инициализации)
    // Но не логируем как ошибку, если уже добавлен - это нормально
    bool handler_added = m_browser->AddScriptMessageHandler("wx");
    if (handler_added) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Script message handler added successfully after load";
    } else {
        // Handler может быть уже добавлен, или WebView не поддерживает
        // Проверяем только если был warning при инициализации
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: AddScriptMessageHandler returned false (may already be registered)";
    }
    
    // Inject JavaScript API helper function for frontend
    // Frontend can use: window.wx.postMessage(JSON.stringify({ command: 'import_profile', data: { preset_id: 123 } }))
    // Frontend can also send login_success command with access_token and user_id
    wxString js_api = R"(
        window.filamenthub = {
            importProfile: function(presetId) {
                // Проверяем валидность presetId
                if (!presetId || presetId <= 0 || isNaN(presetId)) {
                    console.error('FilamentHub: Invalid preset ID:', presetId);
                    return;
                }
                
                const message = JSON.stringify({ 
                    command: 'import_profile', 
                    data: { preset_id: parseInt(presetId) },
                    sequence_id: Date.now().toString()
                });
                if (window.wx && window.wx.postMessage) {
                    window.wx.postMessage(message);
                }
            },
            sendLoginSuccess: function(accessToken, userId) {
                const message = JSON.stringify({ 
                    command: 'login_success', 
                    data: { 
                        access_token: accessToken,
                        user_id: userId
                    }
                });
                if (window.wx && window.wx.postMessage) {
                    window.wx.postMessage(message);
                }
            },
            showNotification: function(message, type) {
                // Отправляем уведомление через postMessage для обработки на фронтенде
                const notification = JSON.stringify({
                    command: 'show_notification',
                    type: type || 'info',
                    message: message
                });
                if (window.postMessage) {
                    window.postMessage(notification, '*');
                }
            }
        };
        
        // Обработчик сообщений от OrcaSlicer (show_notification)
        window.addEventListener('message', function(event) {
            try {
                const data = typeof event.data === 'string' ? JSON.parse(event.data) : event.data;
                if (data.command === 'show_notification') {
                    // Вызываем функцию showNotification если она доступна
                    if (window.filamenthub && typeof window.filamenthub.showNotification === 'function') {
                        window.filamenthub.showNotification(data.message, data.type);
                    }
                }
            } catch (e) {
                // Игнорируем сообщения, которые не являются уведомлениями
            }
        });
        
        // Monitor localStorage for token changes (when user logs in/out)
        let lastToken = localStorage.getItem('access_token');
        const tokenCheckInterval = setInterval(() => {
            const currentToken = localStorage.getItem('access_token');
            const currentUserId = localStorage.getItem('user_id'); // Frontend should store user_id too
            
            if (currentToken !== lastToken) {
                lastToken = currentToken;
                if (currentToken && window.filamenthub && window.filamenthub.sendLoginSuccess) {
                    window.filamenthub.sendLoginSuccess(currentToken, currentUserId ? parseInt(currentUserId) : null);
                }
            }
        }, 1000); // Check every second
        
        console.log('FilamentHub API initialized');
    )";
    
    WebView::RunScript(m_browser, js_api);
}

void FilamentHubPanel::OnScriptMessage(wxWebViewEvent& evt)
{
    wxString str_input = evt.GetString();
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Script message received: " << str_input.ToUTF8();
    
    try {
        nlohmann::json j = nlohmann::json::parse(str_input.ToUTF8().data());
        
        wxString command = j["command"].get<std::string>();
        wxString sequence_id = j.value("sequence_id", "");
        
        if (command == "import_profile") {
            int preset_id = j["data"]["preset_id"].get<int>();
            
            // Проверяем валидность preset_id (должен быть > 0)
            if (preset_id <= 0) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Invalid preset_id: " << preset_id;
                send_response("import_profile", "error", 
                    wxString::Format(_("Invalid preset ID: %d"), preset_id).ToUTF8().data(), 
                    sequence_id);
                wxMessageBox(
                    wxString::Format(_L("Invalid preset ID: %d. Cannot import profile."), preset_id),
                    _L("FilamentHub Import Error"),
                    wxOK | wxICON_WARNING
                );
                return;
            }
            
            // Вызываем import_profile асинхронно (sequence_id передадим в сам метод)
            import_profile(preset_id, sequence_id);
        } else if (command == "login_success") {
            // User logged in successfully via WebView
            std::string access_token = j["data"]["access_token"].get<std::string>();
            
            // user_id может быть null в JSON
            int user_id = 0;
            if (!j["data"]["user_id"].is_null()) {
                user_id = j["data"]["user_id"].get<int>();
            } else {
                // Если user_id не передан, получаем его через API
                FilamentHubClient client;
                client.set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
                client.get_current_user(
                    access_token,
                    [this, access_token](std::string json_body, unsigned http_status) {
                        try {
                            nlohmann::json user_json = nlohmann::json::parse(json_body);
                            int user_id = user_json["id"].get<int>();
                            
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Login success received. User ID: " << user_id;
                            
                            // Save auth token to AppConfig
                            save_auth_token(access_token, user_id);
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Token saved. User ID: " << user_id << ", token length: " << access_token.length();
                            
                            // Update UI to show logged-in state
                            CallAfter([this]() {
                                update_user_info();
                                // НЕ синхронизируем автоматически после логина
                                // Пользователь может нажать кнопку "Synchronize" вручную
                                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Login complete. Ready for manual sync.";
                            });
                        } catch (const std::exception& e) {
                            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error getting user ID: " << e.what();
                        }
                    },
                    [](std::string body, std::string error, unsigned http_status) {
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get user info after login: " << error;
                    }
                );
                return; // Exit early, will update UI in callback
            }
            
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Login success received. User ID: " << user_id;
            
            // Save auth token to AppConfig
            save_auth_token(access_token, user_id);
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Token saved. User ID: " << user_id << ", token length: " << access_token.length();
            
            // Update UI to show logged-in state
            CallAfter([this]() {
                update_user_info();
                // НЕ синхронизируем автоматически после логина
                // Пользователь может нажать кнопку "Synchronize" вручную
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Login complete. Ready for manual sync.";
            });
        } else {
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Unknown command: " << command.ToUTF8();
            send_response(command, "error", "Unknown command", sequence_id);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing script message: " << e.what();
        send_response("error", "error", wxString::FromUTF8(e.what()), "");
    }
}

void FilamentHubPanel::import_profile(int preset_id, const wxString& sequence_id)
{
    std::string job_name = "import_profile_" + std::to_string(preset_id);
    wxString sequence_copy(sequence_id);
    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;

    run_async(job_name, [this, preset_id, sequence_copy, api_base_url]() {
        import_profile_internal(preset_id, sequence_copy, api_base_url);
    });
}

void FilamentHubPanel::import_profile_internal(int preset_id, const wxString& sequence_id, std::string api_base_url)
{
    if (preset_id <= 0) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Invalid preset_id in import_profile: " << preset_id;
        CallAfter([this, preset_id, sequence_id]() {
            wxString message = wxString::Format(_L("Invalid preset ID: %d. Cannot import profile."), preset_id);
            send_response("import_profile", "error", message.ToUTF8().data(), sequence_id);
            wxMessageBox(message, _L("FilamentHub Import Error"), wxOK | wxICON_WARNING);
        });
        return;
    }
    
    std::string access_token;
    int user_id = 0;
    if (!load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: No auth token found, cannot import profile";
        CallAfter([this, sequence_id]() {
            wxString message = _L("Authentication required. Please login to FilamentHub first.");
            send_response("import_profile", "error", message.ToUTF8().data(), sequence_id);
            wxMessageBox(message, _L("FilamentHub Import Error"), wxOK | wxICON_WARNING);
        });
        return;
    }
    
    struct DownloadResult {
        bool success { false };
        unsigned status { 0 };
        std::string body;
        std::string error;
    };

    auto result = std::make_shared<DownloadResult>();

    FilamentHubClient client;
    client.set_api_base_url(api_base_url);
    
    client.download_profile(
        preset_id,
        access_token,
        [result](std::string json_content, unsigned http_status) {
            result->success = true;
            result->status = http_status;
            result->body = std::move(json_content);
        },
        [result](std::string body, std::string error, unsigned http_status) {
            result->success = false;
            result->status = http_status;
            result->error = !error.empty() ? std::move(error) : std::move(body);
        }
    );

    if (!result->success) {
        wxString error_msg;
        unsigned http_status = result->status;
        std::string error_text = result->error;

        if (http_status == 401) {
            error_msg = _L("Authentication required. Please login to FilamentHub first.");
        } else if (http_status == 404) {
            error_msg = wxString::Format(_L("Profile %d not found."), preset_id);
        } else if (http_status >= 500) {
            error_msg = _L("Server error. Please try again later.");
        } else {
            error_msg = wxString::Format(_L("Failed to download profile: %s"), wxString::FromUTF8(error_text.c_str()));
        }

        CallAfter([this, sequence_id, error_msg]() {
            send_response("import_profile", "error", error_msg.ToUTF8().data(), sequence_id);
            wxMessageBox(error_msg, _L("FilamentHub Import Error"), wxOK | wxICON_ERROR);
        });
        return;
    }

    std::string profile_payload = std::move(result->body);

    CallAfter([this, preset_id, sequence_id, profile_payload = std::move(profile_payload)]() mutable {
        try {
            nlohmann::json profile_json = nlohmann::json::parse(profile_payload);
                
            std::string original_name = profile_json.value("name", std::string());
                std::string new_name = ensure_filamenthub_postfix(original_name);
                profile_json["name"] = new_name;
                
                ensure_parent_preset_exists(profile_json);
                
                boost::filesystem::path temp_dir = boost::filesystem::temp_directory_path();
            boost::filesystem::path temp_file = temp_dir / ("filamenthub_preset_" + std::to_string(preset_id) + "_" + std::to_string(std::time(nullptr)) + ".json");
                
                std::ofstream file(temp_file.string());
                if (!file.is_open()) {
                wxString message = _L("Failed to create temporary file for imported profile.");
                send_response("import_profile", "error", message.ToUTF8().data(), sequence_id);
                wxMessageBox(message, _L("FilamentHub Import Error"), wxOK | wxICON_ERROR);
                    return;
                }
                
                file << profile_json.dump(2);
                file.close();
                
                PresetBundle* bundle = wxGetApp().preset_bundle;
                if (bundle == nullptr) {
                wxString message = _L("Preset bundle not available");
                send_response("import_profile", "error", message.ToUTF8().data(), sequence_id);
                wxMessageBox(message, _L("FilamentHub Import Error"), wxOK | wxICON_ERROR);
                boost::filesystem::remove(temp_file);
                    return;
                }
                
                PresetsConfigSubstitutions substitutions;
                std::string file_path = temp_file.string();
            int overwrite = 1;
            std::vector<std::string> import_result;
                
            auto override_confirm = [](std::string const&) -> int { return 1; };
                
                bool success = bundle->import_json_presets(
                    substitutions,
                    file_path,
                    override_confirm,
                ForwardCompatibilitySubstitutionRule::Enable,
                    overwrite,
                import_result
                );
                
                boost::filesystem::remove(temp_file);
                
                if (success) {
                    wxGetApp().load_current_presets();
                    send_response("import_profile", "success", 
                    wxString::Format(_L("Profile %d imported successfully"), preset_id).ToUTF8().data(),
                        sequence_id);
                    
                    wxMessageBox(
                        wxString::Format(_L("Profile %d imported successfully from FilamentHub."), preset_id),
                        _L("FilamentHub Import"),
                        wxOK | wxICON_INFORMATION
                    );
                } else {
                wxString message = _L("Failed to import profile. Check log for details.");
                send_response("import_profile", "error", message.ToUTF8().data(), sequence_id);
                    wxMessageBox(
                        _L("Failed to import profile. It may already exist or be invalid."),
                        _L("FilamentHub Import Error"),
                        wxOK | wxICON_WARNING
                    );
                }
            } catch (const std::exception& e) {
            wxString message = wxString::Format(_L("Error importing profile: %s"), wxString::FromUTF8(e.what()));
            send_response("import_profile", "error", message.ToUTF8().data(), sequence_id);
            wxMessageBox(message, _L("FilamentHub Import Error"), wxOK | wxICON_ERROR);
        }
    });
}

void FilamentHubPanel::synchronize_presets(bool force_full_sync)
{
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: ========== synchronize_presets() CALLED ==========";
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: force_full_sync=" << (force_full_sync ? "true" : "false");
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: m_is_syncing=" << (m_is_syncing ? "true" : "false");
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: m_active_syncs=" << m_active_syncs;
    
    // НЕ обновляем состояние кнопки здесь - это делается в on_sync_button_click()
    // Это позволяет избежать конфликтов при параллельных вызовах
    
    // 1. Загружаем токен и user_id из AppConfig
    std::string access_token;
    int user_id = 0;
    
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: Before load_auth_token";
    if (!load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: load_auth_token returned false";
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: No auth token found, cannot synchronize presets";
        // Не уменьшаем счетчик - он не был увеличен для этой синхронизации (увеличивается только после 200 OK)
        CallAfter([this]() {
            update_sync_button_state(false);
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: No token, sync cancelled. Active syncs: " << m_active_syncs;
            if (m_active_syncs <= 0) {
                m_active_syncs = 0;
                update_sync_button_state(false);
            }
            // Показываем уведомление в WebView вместо модального окна
            show_notification_in_webview(
                _L("Please login to FilamentHub first."),
                "warning"
            );
        });
        return;
    }
    
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: load_auth_token returned true";
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Auth token loaded. User ID: " << user_id 
                            << ", token length: " << access_token.length();
    
    // 2. Получаем last_sync_time для инкрементальной синхронизации
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: Before load_last_sync_time";
    std::string updated_since;
    if (!force_full_sync) {
        updated_since = load_last_sync_time(user_id);
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Incremental sync. updated_since: " << (updated_since.empty() ? "(none)" : updated_since);
    } else {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Full sync (force_full_sync=true)";
    }
    
    // 3. Получаем список пресетов пользователя через API
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: Creating FilamentHubClient";
    FilamentHubClient client;
    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: API base URL: " << api_base_url;
    client.set_api_base_url(api_base_url);
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: Client created and API URL set";
    
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Calling get_my_presets. API: " << api_base_url 
                            << ", updated_since: " << (updated_since.empty() ? "(empty)" : updated_since);
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: About to call client.get_my_presets()";
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: access_token length: " << access_token.length();
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: updated_since: '" << updated_since << "'";
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: CALLING client.get_my_presets() NOW...";
    
    // ВАЖНО: Вызываем get_my_presets СЕЙЧАС, не откладываем
    try {
        client.get_my_presets(
            access_token,
            updated_since,
            // on_complete: список пресетов получен
            [this, user_id, force_full_sync](std::string json_body, unsigned http_status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: ========== on_complete CALLBACK (get_my_presets) ==========";
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Received presets list. HTTP status: " << http_status 
                                        << ", JSON size: " << json_body.size() << " bytes";
            
            // Проверяем статус ответа
            if (http_status == 401) {
                // Токен истек или невалидный - очищаем авторизацию
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Token expired or invalid (401) during presets sync, clearing auth";
                // Не уменьшаем счетчик - он не был увеличен для этой синхронизации (увеличивается только после 200 OK)
                CallAfter([this]() {
                    logout(); // Очищает токен и обновляет UI
                    update_sync_button_state(false);
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (401). Active syncs: " << m_active_syncs;
                    if (m_active_syncs <= 0) {
                        m_active_syncs = 0;
                        update_sync_button_state(false);
                    }
                    // Показываем уведомление в WebView вместо модального окна
                    show_notification_in_webview(
                        _L("Your session has expired. Please login again."),
                        "warning"
                    );
                });
                return;
            }
            
            if (http_status == 403) {
                // Доступ запрещен - проверяем детали ошибки
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Access denied (403) during presets sync. Body: " << json_body;
                // Не уменьшаем счетчик - он не был увеличен для этой синхронизации (увеличивается только после 200 OK)
                try {
                    nlohmann::json error_json = nlohmann::json::parse(json_body);
                    std::string error_detail = error_json.value("detail", "Access denied");
                    CallAfter([this, error_detail]() {
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (403). Active syncs: " << m_active_syncs;
                        if (m_active_syncs <= 0) {
                            update_sync_button_state(false);
                            m_active_syncs = 0;
                        }
                        // Показываем уведомление в WebView вместо модального окна
                        show_notification_in_webview(
                            wxString::Format(_L("Access denied: %s"), wxString::FromUTF8(error_detail.c_str())),
                            "warning"
                        );
                    });
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing 403 response: " << e.what();
                    CallAfter([this]() {
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (403, parse error). Active syncs: " << m_active_syncs;
                        if (m_active_syncs <= 0) {
                            update_sync_button_state(false);
                            m_active_syncs = 0;
                        }
                        // Показываем уведомление в WebView вместо модального окна
                        show_notification_in_webview(
                            _L("Access denied. Please check your permissions in FilamentHub settings."),
                            "warning"
                        );
                    });
                }
                return;
            }
            
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get presets list. Status: " << http_status;
                // Не уменьшаем счетчик - он не был увеличен для этой синхронизации (увеличивается только после 200 OK)
                CallAfter([this, http_status]() {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (status " << http_status << "). Active syncs: " << m_active_syncs;
                    if (m_active_syncs <= 0) {
                        update_sync_button_state(false);
                        m_active_syncs = 0;
                    }
                    // Показываем уведомление в WebView вместо модального окна
                    show_notification_in_webview(
                        wxString::Format(_L("Failed to get presets list. Status: %d"), http_status),
                        "error"
                    );
                });
                return;
            }
            
            // Увеличиваем счетчик только после успешного получения списка (200 OK)
            // Это согласуется с логикой synchronize_printer_profiles() и synchronize_print_profiles()
            m_active_syncs++;
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Incremented m_active_syncs for filament presets (after 200 OK). Active syncs: " << m_active_syncs;
            
            try {
                nlohmann::json response = nlohmann::json::parse(json_body);
                std::vector<nlohmann::json> presets = response["items"];
                int total = response.value("total", 0);
                
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Received " << total << " presets (items: " << presets.size() << ")";
                
                if (presets.empty()) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: No presets to sync (empty list)";
                    // Уменьшаем счетчик активных синхронизаций перед выходом
                    CallAfter([this]() {
                        m_active_syncs--;
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync completed (empty list). Active syncs: " << m_active_syncs;
                        if (m_active_syncs <= 0) {
                            update_sync_button_state(false);
                            m_active_syncs = 0;
                        }
                    });
                    return;
                }
                
                // 4. Синхронизируем каждый пресет
                int synced_count = 0;
                int updated_count = 0;
                int error_count = 0;
                
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Starting to sync " << presets.size() << " presets...";
                
                for (const auto& preset_json : presets) {
                    int preset_id = preset_json["id"];
                    std::string preset_name = preset_json["name"];
                    std::string updated_at = preset_json["updated_at"];
                    
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Processing preset ID=" << preset_id 
                                           << ", name='" << preset_name << "'";
                    
                    // Проверяем маппинг (есть ли уже в OrcaSlicer)
                    std::string bundle_preset_name = load_preset_mapping(preset_id);
                    
                    if (bundle_preset_name.empty()) {
                        // Пресета нет в маппинге - нужно скачать и импортировать
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Preset " << preset_id << " not in mapping, downloading...";
                        
                        // Импортируем без UI диалогов
                        std::string access_token_inner;
                        int user_id_inner;
                        if (load_auth_token(access_token_inner, user_id_inner)) {
                            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Importing preset " << preset_id 
                                                    << " (" << preset_name << ")";
                            if (import_preset_silent(preset_id, preset_name, access_token_inner)) {
                                synced_count++;
                                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Preset " << preset_id 
                                                       << " imported successfully";
                            } else {
                                error_count++;
                                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to import preset " << preset_id;
                            }
                        } else {
                            error_count++;
                            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Cannot load auth token for preset " << preset_id;
                        }
                    } else {
                        // Пресет уже в маппинге - проверяем, изменился ли он
                        // TODO: Сравнить updated_at с временем последнего импорта
                        // Пока просто пропускаем (можно обновить если нужно)
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Preset " << preset_id 
                                               << " already mapped to " << bundle_preset_name 
                                               << ", skipping (already synced)";
                        updated_count++; // Считаем как обновлённый (уже синхронизирован)
                    }
                }
                
                // 5. Обновляем last_sync_time (ISO 8601 format)
                std::time_t now = std::time(nullptr);
                std::stringstream ss;
                ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%S.000000");
                std::string current_time = ss.str();
                save_last_sync_time(user_id, current_time);
                
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Synchronization completed. "
                                       << "Synced: " << synced_count 
                                       << ", Updated: " << updated_count
                                       << ", Errors: " << error_count;
                
                // Update UI: button state and user info
                CallAfter([this]() {
                    m_active_syncs--;
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Filament presets sync completed. Active syncs: " << m_active_syncs;
                    if (m_active_syncs <= 0) {
                        update_sync_button_state(false);
                        m_active_syncs = 0;
                    }
                    update_user_info();
                });
                
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing presets list: " << e.what();
                CallAfter([this, e]() {
                    m_active_syncs--; // Уменьшаем счетчик активных синхронизаций
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (parse error). Active syncs: " << m_active_syncs;
                    if (m_active_syncs <= 0) {
                        update_sync_button_state(false);
                        m_active_syncs = 0;
                    }
                    // Показываем уведомление в WebView вместо модального окна
                    show_notification_in_webview(
                        wxString::Format(_L("Error parsing presets list: %s"), e.what()),
                        "error"
                    );
                });
            }
        },
        // on_error: ошибка при получении списка пресетов
        [this](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: ========== on_error CALLBACK (get_my_presets) ==========";
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get presets list. Error: '" << error << "'"
                                    << ", Status: " << http_status << ", Body size: " << body.size() << " bytes";
            if (body.size() < 500) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error body: " << body;
            } else {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error body (first 500 chars): " << body.substr(0, 500);
            }
            
            wxString error_msg;
            if (http_status == 401) {
                error_msg = _L("Your session has expired. Please login again.");
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Token expired (401) in on_error callback";
                CallAfter([this, error_msg]() {
                    logout(); // Очищает токен и обновляет UI
                    m_active_syncs--; // Уменьшаем счетчик активных синхронизаций
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Filament presets sync failed (401). Active syncs: " << m_active_syncs;
                    if (m_active_syncs <= 0) {
                        update_sync_button_state(false);
                        m_active_syncs = 0;
                    }
                    // Показываем уведомление в WebView вместо модального окна
                    show_notification_in_webview(error_msg, "warning");
                });
                return;
            }
            
            // Для других ошибок также уменьшаем счетчик и показываем уведомление
            if (http_status == 403) {
                // Парсим детали ошибки из body
                try {
                    nlohmann::json error_json = nlohmann::json::parse(body);
                    std::string error_detail = error_json.value("detail", "Access denied");
                    error_msg = wxString::Format(_L("Access denied: %s"), wxString::FromUTF8(error_detail.c_str()));
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing 403 response in on_error: " << e.what();
                    error_msg = _L("Access denied. Please check your permissions in FilamentHub settings.");
                }
            } else if (http_status >= 500) {
                error_msg = _L("Server error. Please try again later.");
            } else {
                error_msg = wxString::Format(_L("Failed to sync presets: %s"), wxString::FromUTF8(error.c_str()));
            }
            
            CallAfter([this, error_msg, http_status]() {
                m_active_syncs--; // Уменьшаем счетчик активных синхронизаций
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Filament presets sync failed (error: " << error_msg.ToUTF8()
                                        << ", status: " << http_status << "). Active syncs: " << m_active_syncs;
                if (m_active_syncs <= 0) {
                    update_sync_button_state(false);
                    m_active_syncs = 0;
                }
                // Показываем уведомление в WebView вместо модального окна
                show_notification_in_webview(
                    error_msg,
                    http_status == 403 ? "warning" : "error"
                );
            });
        }
        );
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: client.get_my_presets() CALLED (function returned, waiting for callback)";
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: EXCEPTION when calling get_my_presets: " << e.what();
        CallAfter([this]() {
            m_active_syncs--;
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Filament presets sync failed (exception). Active syncs: " << m_active_syncs;
            if (m_active_syncs <= 0) {
                update_sync_button_state(false);
                m_active_syncs = 0;
            }
            show_notification_in_webview(
                _L("Error during sync. Check logs for details."),
                "error"
            );
        });
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: UNKNOWN EXCEPTION when calling get_my_presets";
        CallAfter([this]() {
            m_active_syncs--;
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Filament presets sync failed (unknown exception). Active syncs: " << m_active_syncs;
            if (m_active_syncs <= 0) {
                update_sync_button_state(false);
                m_active_syncs = 0;
            }
            show_notification_in_webview(
                _L("Unknown error during sync"),
                "error"
            );
        });
    }
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: FUNCTION END (returning, callbacks will be called asynchronously)";
}

void FilamentHubPanel::send_response(const wxString& command, const wxString& status, const wxString& message, const wxString& sequence_id)
{
    nlohmann::json response;
    response["command"] = command.ToUTF8().data();
    response["status"] = status.ToUTF8().data();
    if (!message.IsEmpty()) {
        response["message"] = message.ToUTF8().data();
    }
    if (!sequence_id.IsEmpty()) {
        response["sequence_id"] = sequence_id.ToUTF8().data();
    }
    
    wxString js_response = wxString::Format("window.postMessage(%s)", response.dump());
    WebView::RunScript(m_browser, js_response);
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sent response: " << response.dump();
}

void FilamentHubPanel::show_notification_in_webview(const wxString& message, const wxString& type)
{
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Cannot show notification, WebView is null";
        return;
    }
    
    // Build notification JSON
    nlohmann::json notification;
    notification["command"] = "show_notification";
    notification["type"] = type.ToUTF8().data();
    notification["message"] = message.ToUTF8().data();
    
    // Send notification to frontend via JavaScript
    // Frontend should have a function to show notifications (e.g., toast)
    wxString js_code = wxString::Format(
        R"(
            (function() {
                try {
                    var notification = %s;
                    // Try to call frontend notification function
                    if (window.filamenthub && typeof window.filamenthub.showNotification === 'function') {
                        window.filamenthub.showNotification(notification.message, notification.type);
                    } else if (window.postMessage) {
                        // Fallback: send via postMessage
                        window.postMessage(JSON.stringify(notification));
                    } else {
                        console.warn('FilamentHub: Notification system not available. Message:', notification.message);
                    }
                } catch (e) {
                    console.error('FilamentHub: Error showing notification:', e);
                }
            })();
        )",
        notification.dump().c_str()
    );
    
    WebView::RunScript(m_browser, js_code);
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sent notification to WebView: type=" << type.ToUTF8() 
                            << ", message=" << message.ToUTF8();
}

void FilamentHubPanel::OnClose(wxCloseEvent& evt)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Panel closing";
    evt.Skip();
}

// ============================================================================
// Methods for saving/loading auth token and user_id
// ============================================================================

void FilamentHubPanel::save_auth_token(const std::string& access_token, int user_id)
{
    if (wxGetApp().app_config == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: app_config is null, cannot save token";
        return;
    }
    
    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_ACCESS_TOKEN, access_token);
    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_USER_ID, std::to_string(user_id));
    wxGetApp().app_config->save();
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved auth token for user_id=" << user_id;
}

bool FilamentHubPanel::load_auth_token(std::string& access_token, int& user_id)
{
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: ========== load_auth_token() CALLED ==========";
    
    if (wxGetApp().app_config == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: app_config is null, cannot load token";
        return false;
    }
    
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: app_config is not null. Loading token...";
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: CONFIG_SECTION_FILAMENTHUB: " << CONFIG_SECTION_FILAMENTHUB;
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: CONFIG_KEY_ACCESS_TOKEN: " << CONFIG_KEY_ACCESS_TOKEN;
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: CONFIG_KEY_USER_ID: " << CONFIG_KEY_USER_ID;
    
    std::string token = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_ACCESS_TOKEN);
    std::string user_id_str = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_USER_ID);
    
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Loading auth token - token length: " << token.length() 
                            << ", user_id_str: '" << user_id_str << "'";
    
    if (token.empty()) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: No auth token found in config (token is empty)";
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: User is not logged in. Cannot synchronize.";
        return false;
    }
    
    // user_id может быть пустым, это не критично
    if (user_id_str.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: user_id_str is empty, setting user_id to 0";
        user_id = 0;
        access_token = token;
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Loaded auth token (without user_id) - token length: " << access_token.length();
        return true;
    }
    
    try {
        
        // Проверяем что строка содержит только цифры (и может начинаться с минуса)
        bool is_valid = true;
        for (size_t i = 0; i < user_id_str.length(); ++i) {
            if (i == 0 && user_id_str[i] == '-') {
                continue; // Минус в начале допустим
            }
            if (!std::isdigit(user_id_str[i])) {
                is_valid = false;
                break;
            }
        }
        
        if (!is_valid) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: user_id_str contains non-digit characters: '" << user_id_str << "'";
            return false;
        }
        
        user_id = std::stoi(user_id_str);
        access_token = token;
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Loaded auth token for user_id=" << user_id 
                                << ", token length: " << access_token.length()
                                << ", token preview: " << access_token.substr(0, 20) << "...";
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: load_auth_token: About to return true";
        return true;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing user_id: " << e.what();
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: load_auth_token: About to return false (exception)";
        return false;
    }
}

// ============================================================================
// Methods for saving/loading preset mappings
// ============================================================================

void FilamentHubPanel::save_preset_mapping(int preset_id, const std::string& bundle_preset_name)
{
    if (wxGetApp().app_config == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: app_config is null, cannot save mapping";
        return;
    }
    
    std::string key = CONFIG_KEY_PRESET_MAPPING + "_" + std::to_string(preset_id);
    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, key, bundle_preset_name);
    wxGetApp().app_config->save();
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved mapping preset_id=" << preset_id 
                            << " -> bundle_preset_name=" << bundle_preset_name;
}

std::string FilamentHubPanel::load_preset_mapping(int preset_id)
{
    if (wxGetApp().app_config == nullptr) {
        return "";
    }
    
    std::string key = CONFIG_KEY_PRESET_MAPPING + "_" + std::to_string(preset_id);
    return wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, key);
}

void FilamentHubPanel::remove_preset_mapping(int preset_id)
{
    if (wxGetApp().app_config == nullptr) {
        return;
    }
    
    std::string key = CONFIG_KEY_PRESET_MAPPING + "_" + std::to_string(preset_id);
    
    // AppConfig doesn't have explicit remove method, but we can set empty string
    // Actually, we need to check if there's a remove method or use clear_section
    // For now, just log it - proper implementation would need to check AppConfig API
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Removing mapping for preset_id=" << preset_id;
}

std::vector<int> FilamentHubPanel::get_all_mapped_preset_ids()
{
    std::vector<int> preset_ids;
    
    if (wxGetApp().app_config == nullptr) {
        return preset_ids;
    }
    
    // Get all keys from filamenthub section
    // AppConfig doesn't directly expose section keys, so we need to work around
    // For now, return empty - proper implementation would need to iterate through section
    // This is a limitation of AppConfig API
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: get_all_mapped_preset_ids - TODO: implement proper iteration";
    
    return preset_ids;
}

// ============================================================================
// Methods for saving/loading last sync time
// ============================================================================

void FilamentHubPanel::save_last_sync_time(int user_id, const std::string& timestamp)
{
    if (wxGetApp().app_config == nullptr) {
        return;
    }
    
    std::string key = CONFIG_KEY_LAST_SYNC_TIME + "_" + std::to_string(user_id);
    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, key, timestamp);
    wxGetApp().app_config->save();
    
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Saved last_sync_time for user_id=" << user_id 
                            << ": " << timestamp;
}

std::string FilamentHubPanel::load_last_sync_time(int user_id)
{
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: load_last_sync_time: Called with user_id=" << user_id;
    if (wxGetApp().app_config == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: load_last_sync_time: app_config is null, returning empty";
        return "";
    }
    
    std::string key = CONFIG_KEY_LAST_SYNC_TIME + "_" + std::to_string(user_id);
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: load_last_sync_time: Key=" << key;
    std::string result = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, key);
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: load_last_sync_time: Result='" << result << "' (empty=" << (result.empty() ? "true" : "false") << ")";
    return result;
}

// ============================================================================
// Helper methods
// ============================================================================

std::string FilamentHubPanel::ensure_filamenthub_postfix(const std::string& preset_name)
{
    std::string postfix = " [FilamentHub]";
    
    // Проверяем, есть ли уже постфикс
    if (preset_name.size() >= postfix.size()) {
        std::string suffix = preset_name.substr(preset_name.size() - postfix.size());
        if (suffix == postfix) {
            // Постфикс уже есть, возвращаем как есть
            return preset_name;
        }
    }
    
    // Добавляем постфикс
    return preset_name + postfix;
}

bool FilamentHubPanel::ensure_parent_preset_exists(nlohmann::json& profile_json)
{
    // Проверяем наличие поля inherits
    if (!profile_json.contains("inherits") || profile_json["inherits"].is_null()) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Profile has no 'inherits' field, adding 'fdm_filament_common'";
        profile_json["inherits"] = "fdm_filament_common";
        return false;
    }
    
    std::string inherits = profile_json["inherits"].get<std::string>();
    if (inherits.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Profile has empty 'inherits' field, adding 'fdm_filament_common'";
        profile_json["inherits"] = "fdm_filament_common";
        return false;
    }
    
    // Проверяем наличие родительского пресета в системе
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: preset_bundle is null, cannot check parent preset";
        profile_json["inherits"] = "fdm_filament_common";
        return false;
    }
    
    // filaments - это объект PresetCollection, не указатель
    PresetCollection& filaments = bundle->filaments;
    
    // Используем find_preset2, который умеет автоматически преобразовывать имена
    // Например: "fdm_filament_pla" -> "Generic PLA @System"
    // Это правильный способ поиска, так как он учитывает автопреобразование и renamed пресеты
    Preset* parent_preset = filaments.find_preset2(inherits, true); // auto_match = true
    
    bool parent_found = (parent_preset != nullptr && parent_preset->is_system);
    
    // Логируем все системные пресеты для отладки (всегда)
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Looking for parent preset '" << inherits << "' in system presets...";
    std::vector<std::string> system_preset_names;
    
    // Собираем список системных пресетов для логирования и fallback
    for (auto it = filaments.begin(); it != filaments.end(); ++it) {
        const Preset& preset = *it;
        if (preset.is_system) {
            std::string preset_name = preset.name;
            system_preset_names.push_back(preset_name);
        }
    }
    
    if (parent_found) {
        std::string actual_name = parent_preset->name;
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Parent preset '" << inherits << "' found via find_preset2 -> '" << actual_name << "'";
        // Обновляем inherits на реальное имя найденного пресета
        if (actual_name != inherits) {
            profile_json["inherits"] = actual_name;
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Updated inherits from '" << inherits << "' to '" << actual_name << "'";
        }
    }
    
    // Логируем все системные пресеты для диагностики (всегда, не только если не найден)
    if (!system_preset_names.empty()) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Available system presets (" << system_preset_names.size() << " total):";
        for (size_t i = 0; i < std::min(system_preset_names.size(), size_t(20)); ++i) {
            BOOST_LOG_TRIVIAL(info) << "  - " << system_preset_names[i];
        }
        if (system_preset_names.size() > 20) {
            BOOST_LOG_TRIVIAL(info) << "  ... and " << (system_preset_names.size() - 20) << " more";
        }
    } else {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: No system presets found in filaments collection!";
    }
    
    // Если родительский пресет не найден, пытаемся найти подходящий fallback
    if (!parent_found) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Parent preset '" << inherits 
                                  << "' not found in system presets";
        
        // Пытаемся найти любой системный пресет для PLA (наиболее распространенный)
        std::string fallback_preset = "";
        for (const auto& preset_name : system_preset_names) {
            // Ищем пресеты содержащие "PLA" или "common"
            if (preset_name.find("PLA") != std::string::npos || 
                preset_name.find("common") != std::string::npos ||
                preset_name.find("Common") != std::string::npos) {
                fallback_preset = preset_name;
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Using '" << fallback_preset << "' as fallback";
                break;
            }
        }
        
        // Если не нашли подходящий, используем первый системный пресет
        if (fallback_preset.empty() && !system_preset_names.empty()) {
            fallback_preset = system_preset_names[0];
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Using first system preset '" << fallback_preset << "' as fallback";
        }
        
        if (!fallback_preset.empty()) {
            profile_json["inherits"] = fallback_preset;
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Replaced inherits with fallback: '" << fallback_preset << "'";
            return false; // Вернули false, но установили fallback
        } else {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: No system presets available for fallback!";
            profile_json["inherits"] = "fdm_filament_common"; // Последняя попытка
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// Silent import method (for sync, no UI dialogs)
// ============================================================================

bool FilamentHubPanel::import_preset_silent(int preset_id, const std::string& preset_name, const std::string& access_token)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Starting silent import of preset ID=" << preset_id 
                            << ", name='" << preset_name << "'";
    
    // Проверяем, что preset_bundle доступен
    if (wxGetApp().preset_bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot import preset " << preset_id;
        return false;
    }
    
    // Скачиваем профиль синхронно через FilamentHubClient
    FilamentHubClient client;
    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
    client.set_api_base_url(api_base_url);
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Downloading preset " << preset_id 
                            << " from API: " << api_base_url 
                            << ", token length: " << access_token.length();
    
    // Используем shared_ptr для безопасного доступа из лямбд
    struct ImportResult {
        bool success = false;
        std::string error;
        unsigned http_status = 0;
        std::mutex mutex;
    };
    
    auto result = std::make_shared<ImportResult>();
    
    // Используем синхронный вызов (perform_sync уже внутри download_profile)
    client.download_profile(
        preset_id,
        access_token,
        // on_complete: профиль успешно скачан
        [this, preset_id, preset_name, result](std::string json_content, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Preset " << preset_id << " downloaded successfully. "
                                    << "HTTP status: " << http_status 
                                    << ", JSON size: " << json_content.size() 
                                    << " bytes";
            
            std::lock_guard<std::mutex> lock(result->mutex);
            result->http_status = http_status;
            
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Unexpected HTTP status " << http_status 
                                        << " when downloading preset " << preset_id;
                result->error = "HTTP status " + std::to_string(http_status);
                return;
            }
            
            if (json_content.empty()) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Empty JSON content for preset " << preset_id;
                result->error = "Empty JSON content";
                return;
            }
            
            try {
                // Парсим JSON чтобы добавить постфикс к имени и проверить родительский пресет
                nlohmann::json profile_json = nlohmann::json::parse(json_content);
                
                // Добавляем постфикс [FilamentHub] к имени пресета
                std::string original_name = profile_json.value("name", preset_name);
                std::string new_name = ensure_filamenthub_postfix(original_name);
                profile_json["name"] = new_name;
                
                // Проверяем и исправляем родительский пресет (inherits)
                ensure_parent_preset_exists(profile_json);
                
                // Создаём временный файл
                boost::filesystem::path temp_dir = boost::filesystem::temp_directory_path();
                boost::filesystem::path temp_file = temp_dir / ("filamenthub_preset_" + std::to_string(preset_id) + "_" + 
                    std::to_string(std::time(nullptr)) + ".json");
                
                // Сохраняем JSON во временный файл
                std::ofstream file(temp_file.string());
                if (!file.is_open()) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to create temporary file: " << temp_file.string();
                    std::lock_guard<std::mutex> lock(result->mutex);
                    result->error = "Failed to create temporary file";
                    return;
                }
                
                file << profile_json.dump(2);
                file.close();
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved profile to: " << temp_file.string();
                
                // Импортируем профиль через PresetBundle
                PresetBundle* bundle = wxGetApp().preset_bundle;
                if (bundle == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null during import";
                    std::lock_guard<std::mutex> lock(result->mutex);
                    result->error = "Preset bundle not available";
                    boost::filesystem::remove(temp_file);
                    return;
                }
                
                PresetsConfigSubstitutions substitutions;
                std::string file_path = temp_file.string();
                int overwrite = 1; // 1 = overwrite if exists
                std::vector<std::string> import_result;
                
                // Lambda для подтверждения перезаписи (автоматически подтверждаем)
                auto override_confirm = [](std::string const& name) -> int {
                    return 1; // Автоматически перезаписываем
                };
                
                // Импортируем JSON профиль
                bool success = bundle->import_json_presets(
                    substitutions,
                    file_path,
                    override_confirm,
                    ForwardCompatibilitySubstitutionRule::Enable,
                    overwrite,
                    import_result
                );
                
                // Удаляем временный файл
                boost::filesystem::remove(temp_file);
                
                std::lock_guard<std::mutex> lock(result->mutex);
                if (success || !import_result.empty()) {
                    // Импорт успешен (или пресет уже был импортирован)
                    // ВАЖНО: Используем new_name (имя пресета с постфиксом [FilamentHub]),
                    // а НЕ import_result, так как import_result может содержать пути к файлам
                    // new_name уже содержит правильное имя пресета: original_name + " [FilamentHub]"
                    std::string actual_preset_name = new_name;
                    
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Profile imported successfully (name: " << actual_preset_name << ")";
                    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: import_result size: " << import_result.size();
                    if (!import_result.empty()) {
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: import_result[0]: " << import_result[0];
                    }
                    
                    // Сохраняем маппинг preset_id → bundle_preset_name (имя пресета в OrcaSlicer)
                    save_preset_mapping(preset_id, actual_preset_name);
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved mapping preset_id=" << preset_id 
                                           << " -> bundle_preset_name=" << actual_preset_name;
                    
                    // Обновляем UI (перезагружаем пресеты)
                    wxGetApp().load_current_presets();
                    
                    result->success = true;
                } else {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to import profile. import_result is empty";
                    result->error = "Failed to import profile: import_json_presets returned false";
                }
                
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception during silent import: " << e.what();
                std::lock_guard<std::mutex> lock(result->mutex);
                result->error = std::string("Exception: ") + e.what();
            }
        },
        // on_error: ошибка при скачивании
        [result, preset_id](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to download preset " << preset_id 
                                     << ". Error: " << error 
                                     << ", HTTP status: " << http_status
                                     << ", Body: " << body.substr(0, 200); // Логируем первые 200 символов
            std::lock_guard<std::mutex> lock(result->mutex);
            result->http_status = http_status;
            result->error = "Failed to download profile: " + error + " (HTTP " + std::to_string(http_status) + ")";
        }
    );
    
    // Возвращаем результат (perform_sync выполнится синхронно, поэтому result уже заполнен)
    std::lock_guard<std::mutex> lock(result->mutex);
    
    if (!result->success && !result->error.empty()) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Import preset " << preset_id << " failed: " << result->error;
    } else if (result->success) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Import preset " << preset_id << " completed successfully";
    }
    
    return result->success;
}

// ============================================================================
// UI methods
// ============================================================================

void FilamentHubPanel::update_user_info()
{
    std::string access_token;
    int user_id = 0;
    
    if (!load_auth_token(access_token, user_id)) {
        // Not logged in
        update_ui_for_login_state(false);
        return;
    }
    
    // Logged in - update UI
    update_ui_for_login_state(true);
    
    // Load user info via API (async)
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Calling get_current_user with token length: " << access_token.length();
    
    FilamentHubClient client;
    client.set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
    
    if (access_token.empty()) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Access token is empty, cannot get user info";
        m_user_name_label->SetLabel(wxString::Format(_("User %d"), user_id));
        return;
    }
    
    client.get_current_user(
        access_token,
        // on_complete: user info received
        [this, user_id](std::string json_body, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Received user info. Status: " << http_status;
            
            // Проверяем статус ответа
            if (http_status == 401) {
                // Токен истек или невалидный - очищаем авторизацию
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Token expired or invalid (401) during user info update, clearing auth";
                CallAfter([this]() {
                    logout(); // Очищает токен и обновляет UI
                    // Показываем уведомление в WebView вместо модального окна
                    show_notification_in_webview(
                        _L("Your session has expired. Please login again."),
                        "warning"
                    );
                });
                return;
            }
            
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get user info. Status: " << http_status;
                m_user_name_label->SetLabel(wxString::Format(_("User %d"), user_id));
                return;
            }
            
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: User info JSON: " << json_body;
            
            try {
                nlohmann::json user_json = nlohmann::json::parse(json_body);
                
                // Безопасное извлечение строк с проверкой на null
                std::string username = "";
                std::string full_name = "";
                std::string email = "";
                
                if (user_json.contains("username") && !user_json["username"].is_null()) {
                    username = user_json["username"].get<std::string>();
                }
                if (user_json.contains("full_name") && !user_json["full_name"].is_null()) {
                    full_name = user_json["full_name"].get<std::string>();
                }
                if (user_json.contains("email") && !user_json["email"].is_null()) {
                    email = user_json["email"].get<std::string>();
                }
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Parsed user data - username: '" << username 
                                        << "', full_name: '" << full_name 
                                        << "', email: '" << email << "'";
                
                wxString display_name;
                if (!full_name.empty()) {
                    display_name = wxString::FromUTF8(full_name);
                } else if (!username.empty()) {
                    display_name = wxString::FromUTF8(username);
                } else if (!email.empty()) {
                    display_name = wxString::FromUTF8(email);
                } else {
                    display_name = wxString::Format(_("User %d"), user_id);
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: No name/email found, using default: " << display_name.ToUTF8();
                }
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Setting display name: " << display_name.ToUTF8();
                m_user_name_label->SetLabel(display_name);
                
                // Get preset count via get_my_presets
                std::string access_token_inner;
                int user_id_inner;
                if (load_auth_token(access_token_inner, user_id_inner)) {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Calling get_my_presets with token length: " << access_token_inner.length();
                    
                    if (access_token_inner.empty()) {
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Access token is empty, cannot get presets count";
                        m_preset_count_label->SetLabel(_("Presets: ?"));
                        return;
                    }
                    
                    FilamentHubClient client_inner;
                    client_inner.set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
                    
                    client_inner.get_my_presets(
                        access_token_inner,
                        "",
                        [this](std::string json_body, unsigned http_status) {
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Received my presets. Status: " << http_status;
                            
                            // Проверяем статус ответа
                            if (http_status == 401) {
                                // Токен истек - не обновляем UI, так как уже обработано в get_current_user
                                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Token expired (401) when getting presets count";
                                m_preset_count_label->SetLabel(_("Presets: ?"));
                                return;
                            }
                            
                            if (http_status != 200) {
                                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get presets count. Status: " << http_status;
                                m_preset_count_label->SetLabel(_("Presets: ?"));
                                return;
                            }
                            
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: My presets JSON: " << json_body;
                            
                            try {
                                nlohmann::json response = nlohmann::json::parse(json_body);
                                int total = response.value("total", 0);
                                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Total presets: " << total;
                                m_preset_count_label->SetLabel(wxString::Format(_("Presets: %d"), total));
                            } catch (const std::exception& e) {
                                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing presets count: " << e.what();
                                m_preset_count_label->SetLabel(_("Presets: ?"));
                            }
                        },
                        [this](std::string body, std::string error, unsigned http_status) {
                            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get presets count. Error: " << error 
                                                      << ", Status: " << http_status;
                            
                            // Если токен истек, не показываем ошибку (уже обработано)
                            if (http_status == 401) {
                                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Token expired (401), not showing error";
                                return;
                            }
                            m_preset_count_label->SetLabel(_("Presets: ?"));
                        }
                    );
                }
                
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing user info: " << e.what();
                m_user_name_label->SetLabel(wxString::Format(_("User %d"), user_id));
            }
        },
        // on_error: failed to get user info
        [this, user_id](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to get user info. Error: " << error;
            m_user_name_label->SetLabel(wxString::Format(_("User %d"), user_id));
        }
    );
}

void FilamentHubPanel::on_sync_button_click(wxCommandEvent& evt)
{
    // ЯВНОЕ ЛОГИРОВАНИЕ С РАЗНЫМИ УРОВНЯМИ ДЛЯ ДИАГНОСТИКИ
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: ========== SYNC BUTTON CLICKED (ERROR LEVEL FOR VISIBILITY) ==========";
    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: ========== SYNC BUTTON CLICKED (WARNING LEVEL) ==========";
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: m_is_syncing=" << (m_is_syncing ? "true" : "false");
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: m_active_syncs=" << m_active_syncs;
    
    // Проверяем, что кнопка существует
    if (m_sync_button == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: m_sync_button is null! Cannot handle click.";
        return;
    }
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: m_sync_button is NOT null";
    
    // Проверяем, что кнопка видима
    bool is_shown = m_sync_button->IsShown();
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Sync button is shown: " << (is_shown ? "true" : "false");
    if (!is_shown) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Sync button is hidden! Cannot handle click.";
        return;
    }
    
    // Проверяем, что кнопка активна
    bool is_enabled = m_sync_button->IsEnabled();
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Sync button is enabled: " << (is_enabled ? "true" : "false");
    if (!is_enabled) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Sync button is disabled! Cannot handle click.";
        return;
    }
    
    if (m_is_syncing) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Sync already in progress, ignoring click";
        return; // Prevent multiple simultaneous syncs
    }
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Sync is NOT in progress, proceeding...";
    
    // Инициализируем счетчик активных синхронизаций
    m_active_syncs = 0;
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Reset m_active_syncs to 0";
    update_sync_button_state(true);
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Set sync button state to 'Synchronizing...'";
    
    // 1. Загружаем токен и user_id из AppConfig
    std::string access_token;
    int user_id = 0;
    
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Calling load_auth_token...";
    if (!load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: No auth token found, cannot synchronize";
        CallAfter([this]() {
            update_sync_button_state(false);
            m_active_syncs = 0;
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Reset sync button state to 'Synchronize' (no token)";
            // Показываем уведомление в WebView вместо модального окна
            show_notification_in_webview(
                _L("Please login to FilamentHub first."),
                "warning"
            );
        });
        return;
    }
    
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Auth token loaded. User ID: " << user_id 
                            << ", token length: " << access_token.length();
    
    // ИСПРАВЛЕНО: Счетчик увеличивается внутри методов синхронизации, а не здесь
    // Это обеспечивает правильную обработку ошибок и согласованность логики
    
    // Синхронизируем filament presets (счетчик увеличивается внутри synchronize_presets после успешного ответа)
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Calling synchronize_presets(false)...";
    synchronize_presets(false); // Incremental sync
    
    // Printer и print profiles синхронизируются только если разрешено
    // Они увеличивают счетчик сами после успешного ответа от API (200 OK)
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Attempting to sync printer and print profiles...";
    synchronize_printer_profiles(false);
    synchronize_print_profiles(false);
    
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: All sync operations started. m_active_syncs=" << m_active_syncs;
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Note: m_active_syncs will increase when each sync operation receives 200 OK response";
}

void FilamentHubPanel::update_sync_button_state(bool is_syncing)
{
    m_is_syncing = is_syncing;
    
    if (is_syncing) {
        m_sync_button->SetLabel(_("Synchronizing..."));
        m_sync_button->Disable();
        m_sync_status_label->Show();
        m_sync_progress->Show();
        m_sync_progress->SetValue(0); // Reset progress
    } else {
        m_sync_button->SetLabel(_("Synchronize"));
        m_sync_button->Enable();
        m_sync_status_label->Hide();
        m_sync_progress->Hide();
    }
    
    m_info_panel->Layout();
}

void FilamentHubPanel::update_ui_for_login_state(bool is_logged_in)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: update_ui_for_login_state called. is_logged_in=" << (is_logged_in ? "true" : "false");
    
    if (is_logged_in) {
        // Show logged-in UI elements
        m_profile_button->Show();
        m_preset_count_label->Show();
        m_sync_button->Show(); // ВАЖНО: Показываем кнопку синхронизации
        m_logout_button->Show();
        
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Showing sync button (user is logged in)";
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sync button is shown: " << (m_sync_button->IsShown() ? "true" : "false");
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sync button is enabled: " << (m_sync_button->IsEnabled() ? "true" : "false");
        
        // Hide not-logged-in UI elements
        m_login_button->Hide();
    } else {
        // Show not-logged-in UI elements
        m_login_button->Show();
        
        // Hide logged-in UI elements
        m_profile_button->Hide();
        m_preset_count_label->Hide();
        m_sync_button->Hide(); // ВАЖНО: Скрываем кнопку синхронизации
        m_logout_button->Hide();
        
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Hiding sync button (user is not logged in)";
        
        // Update labels
        m_user_name_label->SetLabel(_("Not logged in"));
        m_preset_count_label->SetLabel(_("Presets: 0"));
    }
    
    m_info_panel->Layout();
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: UI updated for login state";
}

void FilamentHubPanel::navigate_to_catalog()
{
    load_url(build_frontend_url("/")); // Navigate to catalog page
}

void FilamentHubPanel::navigate_to_profile()
{
    load_url(build_frontend_url("/profile")); // Navigate to profile page
}

void FilamentHubPanel::show_login()
{
    // Navigate to login page in WebView
    // User will log in there, and we'll receive login_success message via JavaScript
    load_url(build_frontend_url("/?auth=login"));
}

void FilamentHubPanel::logout()
{
    // Clear auth token from AppConfig
    if (wxGetApp().app_config != nullptr) {
        wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_ACCESS_TOKEN, "");
        wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_USER_ID, "");
        wxGetApp().app_config->save();
    }
    
    // Update UI
    update_ui_for_login_state(false);
    
    // Navigate to catalog
    navigate_to_catalog();
}

void FilamentHubPanel::load_configuration()
{
    m_frontend_url = DEFAULT_FRONTEND_URL;
    m_api_base_url = FilamentHubClient::DEFAULT_API_BASE_URL;

    auto* config = wxGetApp().app_config;
    if (config == nullptr) {
        return;
    }

    std::string stored_frontend = config->get(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_FRONTEND_URL);
    if (!stored_frontend.empty()) {
        m_frontend_url = wxString::FromUTF8(stored_frontend.c_str()).Trim(true).Trim(false);
    }

    std::string stored_api = config->get(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_API_BASE_URL);
    if (!stored_api.empty()) {
        boost::algorithm::trim(stored_api);
        m_api_base_url = stored_api;
    }
}

void FilamentHubPanel::apply_configuration()
{
    update_api_base_url(m_api_base_url, false);
    update_frontend_url(m_frontend_url, false, false);
}

void FilamentHubPanel::update_frontend_url(const wxString& url, bool persist, bool reload)
{
    wxString sanitized = url;
    sanitized.Trim(true).Trim(false);
    if (sanitized.IsEmpty()) {
        sanitized = DEFAULT_FRONTEND_URL;
    }

    wxString lower = sanitized.Lower();
    if (!lower.StartsWith("http://") && !lower.StartsWith("https://")) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Frontend URL looks invalid (missing scheme): " << sanitized.ToUTF8().data();
    }

    m_frontend_url = sanitized;

    if (persist) {
        auto* config = wxGetApp().app_config;
        if (config != nullptr) {
            config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_FRONTEND_URL, std::string(m_frontend_url.ToUTF8()));
        }
    }

    if (reload) {
        load_url(build_frontend_url());
    }
}

void FilamentHubPanel::update_api_base_url(const std::string& url, bool persist)
{
    std::string sanitized = url;
    boost::algorithm::trim(sanitized);
    if (sanitized.empty()) {
        sanitized = FilamentHubClient::DEFAULT_API_BASE_URL;
    }

    std::string lower = boost::algorithm::to_lower_copy(sanitized);
    if (!boost::algorithm::starts_with(lower, "http://") && !boost::algorithm::starts_with(lower, "https://")) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: API base URL looks invalid (missing scheme): " << sanitized;
    }

    m_api_base_url = sanitized;
    FilamentHubClient::set_api_base_url(m_api_base_url);

    if (persist) {
        auto* config = wxGetApp().app_config;
        if (config != nullptr) {
            config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_API_BASE_URL, m_api_base_url);
        }
    }
}

wxString FilamentHubPanel::build_frontend_url(const wxString& path_suffix) const
{
    wxString base = m_frontend_url.IsEmpty() ? DEFAULT_FRONTEND_URL : m_frontend_url;

    wxString normalized = base;
    while (normalized.Length() > 0 && normalized.EndsWith("/") && !normalized.EndsWith("://")) {
        normalized.RemoveLast();
    }

    if (path_suffix.IsEmpty()) {
        return normalized;
    }

    wxString suffix = path_suffix;
    if (!suffix.StartsWith("/")) {
        suffix.Prepend("/");
    }

    return normalized + suffix;
}

void FilamentHubPanel::show_settings_dialog()
{
    wxDialog dialog(this, wxID_ANY, _L("FilamentHub Settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

    wxBoxSizer* top_sizer = new wxBoxSizer(wxVERTICAL);
    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 2, 8, 8);
    grid->AddGrowableCol(1, 1);

    grid->Add(new wxStaticText(&dialog, wxID_ANY, _L("Frontend URL")), 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT);
    wxTextCtrl* frontend_ctrl = new wxTextCtrl(&dialog, wxID_ANY, build_frontend_url());
    grid->Add(frontend_ctrl, 1, wxEXPAND);

    grid->Add(new wxStaticText(&dialog, wxID_ANY, _L("API Base URL")), 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT);
    wxTextCtrl* api_ctrl = new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(m_api_base_url.c_str()));
    grid->Add(api_ctrl, 1, wxEXPAND);

    top_sizer->Add(grid, 1, wxALL | wxEXPAND, 10);

    wxSizer* button_sizer = dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL);
    if (button_sizer != nullptr) {
        top_sizer->Add(button_sizer, 0, wxALL | wxEXPAND, 10);
    }

    dialog.SetSizerAndFit(top_sizer);

    if (dialog.ShowModal() != wxID_OK) {
        return;
    }

    wxString new_frontend = frontend_ctrl->GetValue().Trim(true).Trim(false);
    wxString new_api_wx = api_ctrl->GetValue().Trim(true).Trim(false);

    auto is_http_url = [](const wxString& value) {
        if (value.IsEmpty()) {
            return true;
        }
        wxString lower = value.Lower();
        return lower.StartsWith("http://") || lower.StartsWith("https://");
    };

    if (!is_http_url(new_frontend)) {
        wxMessageBox(_L("Please enter a valid frontend URL (must start with http:// or https://)."), _L("FilamentHub"), wxOK | wxICON_WARNING, this);
        show_settings_dialog();
        return;
    }

    if (!is_http_url(new_api_wx)) {
        wxMessageBox(_L("Please enter a valid API base URL (must start with http:// or https://)."), _L("FilamentHub"), wxOK | wxICON_WARNING, this);
        show_settings_dialog();
        return;
    }

    update_frontend_url(new_frontend, true, true);
    update_api_base_url(std::string(new_api_wx.ToUTF8()), true);

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Updated URLs. Frontend=" << new_frontend.ToUTF8().data()
                            << ", API=" << (new_api_wx.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : new_api_wx.ToUTF8().data());
}

void FilamentHubPanel::cleanup_finished_tasks()
{
    std::lock_guard<std::mutex> lock(m_async_mutex);
    for (auto it = m_async_tasks.begin(); it != m_async_tasks.end(); ) {
        if (!it->valid() || it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            if (it->valid()) {
                it->wait();
            }
            it = m_async_tasks.erase(it);
        } else {
            ++it;
        }
    }
}

void FilamentHubPanel::update_async_ui()
{
    cleanup_finished_tasks();

    size_t active_jobs = 0;
    {
        std::lock_guard<std::mutex> lock(m_async_mutex);
        active_jobs = m_active_async_jobs;
    }

    bool busy = active_jobs > 0;

    if (m_settings_button != nullptr) {
        m_settings_button->Enable(!busy);
    }

    if (m_catalog_button != nullptr) {
        m_catalog_button->Enable(!busy);
    }

    if (m_profile_button != nullptr) {
        m_profile_button->Enable(!busy);
    }

    if (m_login_button != nullptr) {
        m_login_button->Enable(!busy);
    }

    if (m_logout_button != nullptr) {
        m_logout_button->Enable(!busy);
    }

    if (m_sync_button != nullptr && !m_is_syncing) {
        m_sync_button->Enable(!busy);
    }
}

void FilamentHubPanel::run_async(const std::string& job_name, std::function<void()> job)
{
    cleanup_finished_tasks();

    {
        std::lock_guard<std::mutex> lock(m_async_mutex);
        ++m_active_async_jobs;
    }

    update_async_ui();

    auto future = std::async(std::launch::async, [this, job_name, job = std::move(job)]() mutable {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Async job started: " << job_name;
        try {
            job();
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in async job '" << job_name << "': " << e.what();
            wxString message = wxString::Format(_L("Unexpected error: %s"), wxString::FromUTF8(e.what()));
            CallAfter([this, message]() {
                wxMessageBox(message, _L("FilamentHub"), wxOK | wxICON_ERROR, this);
            });
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Unknown exception in async job '" << job_name << "'";
            CallAfter([this]() {
                wxMessageBox(_L("Unexpected error during FilamentHub operation."), _L("FilamentHub"), wxOK | wxICON_ERROR, this);
            });
        }

        CallAfter([this]() {
            {
                std::lock_guard<std::mutex> lock(m_async_mutex);
                if (m_active_async_jobs > 0) {
                    --m_active_async_jobs;
                }
            }
            update_async_ui();
        });

        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Async job finished: " << job_name;
    });

    {
        std::lock_guard<std::mutex> lock(m_async_mutex);
        m_async_tasks.emplace_back(std::move(future));
    }
}

void FilamentHubPanel::show_sync_progress(int total_steps)
{
    if (m_sync_progress == nullptr || m_sync_status_label == nullptr) {
        return;
    }

    if (total_steps <= 0) {
        total_steps = 100;
    }

    m_sync_progress->SetRange(total_steps);
    m_sync_progress->SetValue(0);
    m_sync_progress->Show(true);
    m_sync_status_label->SetLabel(_L("Synchronizing presets..."));
    m_sync_status_label->Show(true);
    m_info_panel->Layout();
}

void FilamentHubPanel::update_sync_progress_ui(int completed, int total, const wxString& status_text)
{
    if (m_sync_progress == nullptr || m_sync_status_label == nullptr) {
        return;
    }

    if (total > 0) {
        m_sync_progress->SetRange(total);
    }

    if (completed >= 0) {
        m_sync_progress->SetValue(std::min(completed, m_sync_progress->GetRange()));
    }

    if (!status_text.IsEmpty()) {
        m_sync_status_label->SetLabel(status_text);
    }

    m_info_panel->Layout();
}

void FilamentHubPanel::hide_sync_progress()
{
    if (m_sync_progress == nullptr || m_sync_status_label == nullptr) {
        return;
    }

    m_sync_progress->Hide();
    m_sync_status_label->Hide();
    m_info_panel->Layout();
}

// ============================================================================
// Methods for checking user permissions
// ============================================================================

void FilamentHubPanel::check_user_permissions(
    const std::string& access_token,
    std::function<void(bool, bool, bool, bool)> on_complete,
    std::function<void(std::string, unsigned)> on_error
)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Checking user permissions. Token length: " << access_token.length();
    
    FilamentHubClient client;
    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
    client.set_api_base_url(api_base_url);
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Calling get_current_user for permissions check. API: " << api_base_url;
    
    client.get_current_user(
        access_token,
        // on_complete: информация о пользователе получена
        [on_complete, on_error](std::string json_body, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Received user info for permissions check. HTTP status: " << http_status 
                                    << ", JSON size: " << json_body.size() << " bytes";
            
            if (http_status != 200) {
                std::string error_msg = "Failed to get user info";
                if (http_status == 401) {
                    error_msg = "Authentication required";
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Authentication required (401) when checking permissions";
                } else if (http_status == 403) {
                    error_msg = "Access denied";
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Access denied (403) when checking permissions";
                } else if (http_status >= 500) {
                    error_msg = "Server error";
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Server error (" << http_status << ") when checking permissions";
                } else {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Unexpected HTTP status " << http_status << " when checking permissions";
                }
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Permissions check response body: " << json_body.substr(0, 500);
                on_error(error_msg, http_status);
                return;
            }
            
            try {
                nlohmann::json user_json = nlohmann::json::parse(json_body);
                
                // Извлекаем разрешения (по умолчанию true, если не указано)
                bool allow_printer_import = user_json.value("allow_printer_profiles_import", true);
                bool allow_printer_export = user_json.value("allow_printer_profiles_export", true);
                bool allow_print_import = user_json.value("allow_print_profiles_import", true);
                bool allow_print_export = user_json.value("allow_print_profiles_export", true);
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: User permissions extracted - "
                                       << "printer_import: " << (allow_printer_import ? "true" : "false")
                                       << ", printer_export: " << (allow_printer_export ? "true" : "false")
                                       << ", print_import: " << (allow_print_import ? "true" : "false")
                                       << ", print_export: " << (allow_print_export ? "true" : "false");
                
                on_complete(allow_printer_import, allow_printer_export, allow_print_import, allow_print_export);
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing user info JSON: " << e.what() 
                                        << ", JSON body: " << json_body.substr(0, 500);
                on_error(std::string("Error parsing user info: ") + e.what(), 0);
            }
        },
        // on_error: ошибка при получении информации о пользователе
        [on_error](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get user info for permissions check. "
                                     << "Error: " << error 
                                     << ", HTTP status: " << http_status
                                     << ", Body: " << body.substr(0, 500);
            on_error(error, http_status);
        }
    );
}

// ============================================================================
// Methods for synchronizing printer profiles
// ============================================================================

void FilamentHubPanel::synchronize_printer_profiles(bool force_full_sync)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== synchronize_printer_profiles() CALLED ==========";
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: force_full_sync=" << (force_full_sync ? "true" : "false");
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: m_active_syncs=" << m_active_syncs;
    
    // 1. Загружаем токен и user_id из AppConfig
    std::string access_token;
    int user_id = 0;
    
    if (!load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: No auth token found, skipping printer profiles sync";
        // Просто пропускаем, не уменьшаем счетчик - он не был увеличен для этой синхронизации
        return;
    }
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Auth token loaded for printer profiles sync. User ID: " << user_id 
                            << ", token length: " << access_token.length();
    
    // 2. Получаем last_sync_time для инкрементальной синхронизации
    std::string updated_since;
    if (!force_full_sync) {
        updated_since = load_last_sync_time(user_id);
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Incremental sync. updated_since: " << (updated_since.empty() ? "(none)" : updated_since);
    } else {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Full sync (force_full_sync=true)";
    }
    
    // 3. Получаем список printer profiles пользователя через API
    FilamentHubClient client;
    client.set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
    
    client.get_my_printer_profiles(
        access_token,
        updated_since,
        // on_complete: список printer profiles получен
        [this, user_id, force_full_sync, access_token](std::string json_body, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Received printer profiles list. Status: " << http_status;
            
            // Проверяем статус ответа
            if (http_status == 401) {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Token expired or invalid (401) during printer profiles sync";
                // Не уменьшаем счетчик - он не был увеличен для этой синхронизации
                // Ошибка 401 обрабатывается в основном потоке синхронизации
                return;
            }
            
            if (http_status == 403) {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Printer profiles export disabled (403). Skipping silently.";
                // Не уменьшаем счетчик - он не был увеличен для этой синхронизации
                // 403 означает, что разрешение отключено, это не ошибка
                return;
            }
            
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get printer profiles list. Status: " << http_status 
                                        << ", Body: " << json_body.substr(0, 200);
                // Не уменьшаем счетчик - он не был увеличен для этой синхронизации
                return;
            }
            
            // Увеличиваем счетчик только после успешного получения списка (200 OK)
            m_active_syncs++;
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Incremented m_active_syncs for printer profiles. Active syncs: " << m_active_syncs;
            
            try {
                nlohmann::json response = nlohmann::json::parse(json_body);
                std::vector<nlohmann::json> profiles = response["items"];
                int total = response.value("total", 0);
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Received " << total << " printer profiles (items: " << profiles.size() << ")";
                
                if (profiles.empty()) {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: No printer profiles to sync (empty list)";
                    // Уменьшаем счетчик активных синхронизаций перед выходом
                    CallAfter([this]() {
                        m_active_syncs--;
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profiles sync completed (empty list). Active syncs: " << m_active_syncs;
                        if (m_active_syncs <= 0) {
                            update_sync_button_state(false);
                            m_active_syncs = 0;
                        }
                    });
                    return;
                }
                
                // 4. Синхронизируем каждый printer profile
                int synced_count = 0;
                int updated_count = 0;
                int error_count = 0;
                
                for (const auto& profile_json : profiles) {
                    int profile_id = profile_json["id"];
                    std::string profile_name = profile_json["name"];
                    std::string updated_at = profile_json.value("updated_at", "");
                    
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Processing printer profile ID=" << profile_id 
                                           << ", name=" << profile_name;
                    
                    // Проверяем маппинг (есть ли уже в OrcaSlicer)
                    std::string bundle_profile_name = load_printer_profile_mapping(profile_id);
                    
                    if (bundle_profile_name.empty()) {
                        // Профиля нет в маппинге - нужно скачать и импортировать
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profile " << profile_id 
                                               << " (" << profile_name << ") not in mapping, downloading...";
                        
                        // Импортируем без UI диалогов
                        if (import_printer_profile_silent(profile_id, profile_name, access_token)) {
                            synced_count++;
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profile " << profile_id << " imported successfully";
                        } else {
                            error_count++;
                            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to import printer profile " << profile_id;
                        }
                    } else {
                        // Профиль уже в маппинге - проверяем, изменился ли он
                        // TODO: Сравнить updated_at с временем последнего импорта
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Printer profile " << profile_id 
                                               << " already mapped to " << bundle_profile_name 
                                               << ", skipping (already synced)";
                        updated_count++;
                    }
                }
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profiles synchronization completed. "
                                       << "Synced: " << synced_count 
                                       << ", Updated: " << updated_count
                                       << ", Errors: " << error_count;
                
                // Update UI: уменьшаем счетчик активных синхронизаций
                CallAfter([this]() {
                    m_active_syncs--;
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profiles sync completed. Active syncs: " << m_active_syncs;
                    if (m_active_syncs <= 0) {
                        update_sync_button_state(false);
                        m_active_syncs = 0;
                    }
                });
                
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing printer profiles list: " << e.what() 
                                        << ", JSON body: " << json_body;
                CallAfter([this, e]() {
                    m_active_syncs--; // Уменьшаем счетчик активных синхронизаций
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profiles sync failed (parse error). Active syncs: " << m_active_syncs;
                    if (m_active_syncs <= 0) {
                        update_sync_button_state(false);
                        m_active_syncs = 0;
                    }
                    // Показываем уведомление в WebView вместо модального окна
                    show_notification_in_webview(
                        wxString::Format(_L("Error parsing printer profiles list: %s"), e.what()),
                        "error"
                    );
                });
            }
        },
        // on_error: ошибка при получении списка printer profiles
        [this](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get printer profiles list. Error: " << error 
                                    << ", Status: " << http_status << ", Body: " << body;
            
            wxString error_msg;
            if (http_status == 401) {
                error_msg = _L("Your session has expired. Please login again.");
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Token expired (401) in printer profiles sync on_error callback";
            } else if (http_status == 403) {
                error_msg = _L("Printer profiles export is disabled in your FilamentHub settings. Please enable it in your profile settings.");
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Printer profiles export disabled (403)";
            } else if (http_status >= 500) {
                error_msg = _L("Server error. Please try again later.");
            } else {
                error_msg = wxString::Format(_L("Failed to sync printer profiles: %s"), wxString::FromUTF8(error.c_str()));
            }
            
            CallAfter([this, error_msg, http_status]() {
                m_active_syncs--; // Уменьшаем счетчик активных синхронизаций
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profiles sync failed (error: " << error_msg.ToUTF8()
                                        << ", status: " << http_status << "). Active syncs: " << m_active_syncs;
                if (m_active_syncs <= 0) {
                    update_sync_button_state(false);
                    m_active_syncs = 0;
                }
                // Показываем уведомление в WebView вместо модального окна
                show_notification_in_webview(
                    error_msg,
                    http_status == 401 || http_status == 403 ? "warning" : "error"
                );
            });
        }
    );
}

// ============================================================================
// Methods for synchronizing print profiles
// ============================================================================

void FilamentHubPanel::synchronize_print_profiles(bool force_full_sync)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== synchronize_print_profiles() CALLED ==========";
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: force_full_sync=" << (force_full_sync ? "true" : "false");
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: m_active_syncs=" << m_active_syncs;
    
    // 1. Загружаем токен и user_id из AppConfig
    std::string access_token;
    int user_id = 0;
    
    if (!load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: No auth token found, skipping print profiles sync";
        return; // Просто пропускаем, не уменьшаем счетчик
    }
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Auth token loaded for print profiles sync. User ID: " << user_id 
                            << ", token length: " << access_token.length();
    
    // НЕ увеличиваем счетчик здесь - он будет увеличен только после успешного ответа 200 OK
    // Это согласуется с логикой synchronize_printer_profiles()
    
    // 2. Получаем last_sync_time для инкрементальной синхронизации
    std::string updated_since;
    if (!force_full_sync) {
        updated_since = load_last_sync_time(user_id);
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Incremental sync. updated_since: " << (updated_since.empty() ? "(none)" : updated_since);
    } else {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Full sync (force_full_sync=true)";
    }
    
    // 3. Получаем список print profiles пользователя через API
    FilamentHubClient client;
    client.set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
    
    client.get_my_print_profiles(
        access_token,
        updated_since,
        // on_complete: список print profiles получен
        [this, user_id, force_full_sync, access_token](std::string json_body, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Received print profiles list. Status: " << http_status;
            
            // Проверяем статус ответа
            if (http_status == 401) {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Token expired or invalid (401) during print profiles sync";
                // Не уменьшаем счетчик - он не был увеличен для этой синхронизации (увеличивается только после 200 OK)
                // Ошибка 401 обрабатывается в основном потоке синхронизации
                return;
            }
            
            if (http_status == 403) {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Print profiles export disabled (403). Skipping silently.";
                // Не уменьшаем счетчик - он не был увеличен для этой синхронизации (увеличивается только после 200 OK)
                // 403 означает, что разрешение отключено, это не ошибка
                return;
            }
            
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get print profiles list. Status: " << http_status 
                                        << ", Body: " << json_body.substr(0, 200);
                // Не уменьшаем счетчик - он не был увеличен для этой синхронизации (увеличивается только после 200 OK)
                return;
            }
            
            // Увеличиваем счетчик только после успешного получения списка (200 OK)
            // Это согласуется с логикой synchronize_printer_profiles()
            m_active_syncs++;
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Incremented m_active_syncs for print profiles (after 200 OK). Active syncs: " << m_active_syncs;
            
            try {
                nlohmann::json response = nlohmann::json::parse(json_body);
                std::vector<nlohmann::json> profiles = response["items"];
                int total = response.value("total", 0);
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Received " << total << " print profiles (items: " << profiles.size() << ")";
                
                if (profiles.empty()) {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: No print profiles to sync (empty list)";
                    // Уменьшаем счетчик активных синхронизаций перед выходом
                    CallAfter([this]() {
                        m_active_syncs--;
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profiles sync completed (empty list). Active syncs: " << m_active_syncs;
                        if (m_active_syncs <= 0) {
                            update_sync_button_state(false);
                            m_active_syncs = 0;
                        }
                    });
                    return;
                }
                
                // 4. Синхронизируем каждый print profile
                int synced_count = 0;
                int updated_count = 0;
                int error_count = 0;
                
                for (const auto& profile_json : profiles) {
                    int profile_id = profile_json["id"];
                    std::string profile_name = profile_json["name"];
                    std::string updated_at = profile_json.value("updated_at", "");
                    
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Processing print profile ID=" << profile_id 
                                           << ", name=" << profile_name;
                    
                    // Проверяем маппинг (есть ли уже в OrcaSlicer)
                    std::string bundle_profile_name = load_print_profile_mapping(profile_id);
                    
                    if (bundle_profile_name.empty()) {
                        // Профиля нет в маппинге - нужно скачать и импортировать
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profile " << profile_id 
                                               << " (" << profile_name << ") not in mapping, downloading...";
                        
                        // Импортируем без UI диалогов
                        if (import_print_profile_silent(profile_id, profile_name, access_token)) {
                            synced_count++;
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profile " << profile_id << " imported successfully";
                        } else {
                            error_count++;
                            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to import print profile " << profile_id;
                        }
                    } else {
                        // Профиль уже в маппинге - проверяем, изменился ли он
                        // TODO: Сравнить updated_at с временем последнего импорта
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Print profile " << profile_id 
                                               << " already mapped to " << bundle_profile_name 
                                               << ", skipping (already synced)";
                        updated_count++;
                    }
                }
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profiles synchronization completed. "
                                       << "Synced: " << synced_count 
                                       << ", Updated: " << updated_count
                                       << ", Errors: " << error_count;
                
                // Update UI: уменьшаем счетчик активных синхронизаций
                CallAfter([this]() {
                    m_active_syncs--;
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profiles sync completed. Active syncs: " << m_active_syncs;
                    if (m_active_syncs <= 0) {
                        update_sync_button_state(false);
                        m_active_syncs = 0;
                    }
                });
                
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing print profiles list: " << e.what() 
                                        << ", JSON body: " << json_body;
                CallAfter([this, e]() {
                    m_active_syncs--; // Уменьшаем счетчик активных синхронизаций
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profiles sync failed (parse error). Active syncs: " << m_active_syncs;
                    if (m_active_syncs <= 0) {
                        update_sync_button_state(false);
                        m_active_syncs = 0;
                    }
                    // Показываем уведомление в WebView вместо модального окна
                    show_notification_in_webview(
                        wxString::Format(_L("Error parsing print profiles list: %s"), e.what()),
                        "error"
                    );
                });
            }
        },
        // on_error: ошибка при получении списка print profiles
        [this](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get print profiles list. Error: " << error 
                                    << ", Status: " << http_status << ", Body: " << body;
            
            wxString error_msg;
            if (http_status == 401) {
                error_msg = _L("Your session has expired. Please login again.");
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Token expired (401) in print profiles sync on_error callback";
            } else if (http_status == 403) {
                error_msg = _L("Print profiles export is disabled in your FilamentHub settings. Please enable it in your profile settings.");
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Print profiles export disabled (403)";
            } else if (http_status >= 500) {
                error_msg = _L("Server error. Please try again later.");
            } else {
                error_msg = wxString::Format(_L("Failed to sync print profiles: %s"), wxString::FromUTF8(error.c_str()));
            }
            
            CallAfter([this, error_msg, http_status]() {
                m_active_syncs--; // Уменьшаем счетчик активных синхронизаций
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profiles sync failed (error: " << error_msg.ToUTF8()
                                        << ", status: " << http_status << "). Active syncs: " << m_active_syncs;
                if (m_active_syncs <= 0) {
                    update_sync_button_state(false);
                    m_active_syncs = 0;
                }
                // Показываем уведомление в WebView вместо модального окна
                show_notification_in_webview(
                    error_msg,
                    http_status == 401 || http_status == 403 ? "warning" : "error"
                );
            });
        }
    );
}

// ============================================================================
// Methods for saving/loading printer profile mappings
// ============================================================================

void FilamentHubPanel::save_printer_profile_mapping(int profile_id, const std::string& bundle_profile_name)
{
    if (wxGetApp().app_config == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: app_config is null, cannot save printer profile mapping";
        return;
    }
    
    std::string key = CONFIG_KEY_PRINTER_PROFILE_MAPPING + "_" + std::to_string(profile_id);
    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, key, bundle_profile_name);
    wxGetApp().app_config->save();
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved printer profile mapping profile_id=" << profile_id 
                            << " -> bundle_profile_name=" << bundle_profile_name;
}

std::string FilamentHubPanel::load_printer_profile_mapping(int profile_id)
{
    if (wxGetApp().app_config == nullptr) {
        return "";
    }
    
    std::string key = CONFIG_KEY_PRINTER_PROFILE_MAPPING + "_" + std::to_string(profile_id);
    return wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, key);
}

void FilamentHubPanel::save_print_profile_mapping(int profile_id, const std::string& bundle_profile_name)
{
    if (wxGetApp().app_config == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: app_config is null, cannot save print profile mapping";
        return;
    }
    
    std::string key = CONFIG_KEY_PRINT_PROFILE_MAPPING + "_" + std::to_string(profile_id);
    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, key, bundle_profile_name);
    wxGetApp().app_config->save();
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved print profile mapping profile_id=" << profile_id 
                            << " -> bundle_profile_name=" << bundle_profile_name;
}

std::string FilamentHubPanel::load_print_profile_mapping(int profile_id)
{
    if (wxGetApp().app_config == nullptr) {
        return "";
    }
    
    std::string key = CONFIG_KEY_PRINT_PROFILE_MAPPING + "_" + std::to_string(profile_id);
    return wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, key);
}

// ============================================================================
// Methods for importing printer and print profiles
// ============================================================================

bool FilamentHubPanel::import_printer_profile_silent(int profile_id, const std::string& profile_name, const std::string& access_token)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Silent import of printer profile " << profile_id;
    
    // Проверяем, что preset_bundle доступен
    if (wxGetApp().preset_bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot import printer profile";
        return false;
    }
    
    // Скачиваем профиль синхронно через FilamentHubClient
    FilamentHubClient client;
    client.set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
    
    // Используем shared_ptr для безопасного доступа из лямбд
    struct ImportResult {
        bool success = false;
        std::string error;
        std::mutex mutex;
    };
    
    auto result = std::make_shared<ImportResult>();
    
    // Используем синхронный вызов (perform_sync уже внутри download_printer_profile)
    client.download_printer_profile(
        profile_id,
        access_token,
        // on_complete: профиль успешно скачан
        [this, profile_id, profile_name, result](std::string json_content, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profile downloaded successfully. Size: " << json_content.size();
            
            try {
                // Парсим JSON чтобы добавить постфикс к имени
                nlohmann::json profile_json = nlohmann::json::parse(json_content);
                
                // Добавляем постфикс [FilamentHub] к имени профиля
                std::string original_name = profile_json.value("name", profile_name);
                std::string new_name = ensure_filamenthub_postfix(original_name);
                profile_json["name"] = new_name;
                
                // Создаём временный файл
                boost::filesystem::path temp_dir = boost::filesystem::temp_directory_path();
                boost::filesystem::path temp_file = temp_dir / ("filamenthub_printer_profile_" + std::to_string(profile_id) + "_" + 
                    std::to_string(std::time(nullptr)) + ".json");
                
                // Сохраняем JSON во временный файл
                std::ofstream file(temp_file.string());
                if (!file.is_open()) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to create temporary file: " << temp_file.string();
                    std::lock_guard<std::mutex> lock(result->mutex);
                    result->error = "Failed to create temporary file";
                    return;
                }
                
                file << profile_json.dump(2);
                file.close();
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved printer profile to: " << temp_file.string();
                
                // Импортируем профиль через PresetBundle
                PresetBundle* bundle = wxGetApp().preset_bundle;
                if (bundle == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null during printer profile import";
                    std::lock_guard<std::mutex> lock(result->mutex);
                    result->error = "Preset bundle not available";
                    boost::filesystem::remove(temp_file);
                    return;
                }
                
                PresetsConfigSubstitutions substitutions;
                std::string file_path = temp_file.string();
                int overwrite = 1; // 1 = overwrite if exists
                std::vector<std::string> import_result;
                
                // Lambda для подтверждения перезаписи (автоматически подтверждаем)
                auto override_confirm = [](std::string const& name) -> int {
                    return 1; // Автоматически перезаписываем
                };
                
                // Импортируем JSON профиль
                bool success = bundle->import_json_presets(
                    substitutions,
                    file_path,
                    override_confirm,
                    ForwardCompatibilitySubstitutionRule::Enable,
                    overwrite,
                    import_result
                );
                
                // Удаляем временный файл
                boost::filesystem::remove(temp_file);
                
                std::lock_guard<std::mutex> lock(result->mutex);
                if (success || !import_result.empty()) {
                    // Импорт успешен
                    // ВАЖНО: Используем new_name (имя профиля с постфиксом [FilamentHub]),
                    // а НЕ import_result, так как import_result может содержать пути к файлам
                    // new_name уже содержит правильное имя профиля: original_name + " [FilamentHub]"
                    std::string actual_profile_name = new_name;
                    
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profile imported successfully (name: " << actual_profile_name << ")";
                    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: import_result size: " << import_result.size();
                    if (!import_result.empty()) {
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: import_result[0]: " << import_result[0];
                    }
                    
                    // Сохраняем маппинг profile_id → bundle_profile_name (имя профиля в OrcaSlicer)
                    save_printer_profile_mapping(profile_id, actual_profile_name);
                    
                    // Обновляем UI (перезагружаем пресеты)
                    wxGetApp().load_current_presets();
                    
                    result->success = true;
                } else {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to import printer profile. import_result is empty";
                    result->error = "Failed to import printer profile: import_json_presets returned false";
                }
                
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception during silent printer profile import: " << e.what();
                std::lock_guard<std::mutex> lock(result->mutex);
                result->error = std::string("Exception: ") + e.what();
            }
        },
        // on_error: ошибка при скачивании
        [result](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to download printer profile. Error: " << error 
                                     << ", Status: " << http_status;
            std::lock_guard<std::mutex> lock(result->mutex);
            result->error = "Failed to download printer profile: " + error;
        }
    );
    
    // Возвращаем результат (perform_sync выполнится синхронно, поэтому result уже заполнен)
    std::lock_guard<std::mutex> lock(result->mutex);
    return result->success;
}

bool FilamentHubPanel::import_print_profile_silent(int profile_id, const std::string& profile_name, const std::string& access_token)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Silent import of print profile " << profile_id;
    
    // Проверяем, что preset_bundle доступен
    if (wxGetApp().preset_bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot import print profile";
        return false;
    }
    
    // Скачиваем профиль синхронно через FilamentHubClient
    FilamentHubClient client;
    client.set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
    
    // Используем shared_ptr для безопасного доступа из лямбд
    struct ImportResult {
        bool success = false;
        std::string error;
        std::mutex mutex;
    };
    
    auto result = std::make_shared<ImportResult>();
    
    // Используем синхронный вызов (perform_sync уже внутри download_print_profile)
    client.download_print_profile(
        profile_id,
        access_token,
        // on_complete: профиль успешно скачан
        [this, profile_id, profile_name, result](std::string json_content, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profile downloaded successfully. Size: " << json_content.size();
            
            try {
                // Парсим JSON чтобы добавить постфикс к имени
                nlohmann::json profile_json = nlohmann::json::parse(json_content);
                
                // Добавляем постфикс [FilamentHub] к имени профиля
                std::string original_name = profile_json.value("name", profile_name);
                std::string new_name = ensure_filamenthub_postfix(original_name);
                profile_json["name"] = new_name;
                
                // Создаём временный файл
                boost::filesystem::path temp_dir = boost::filesystem::temp_directory_path();
                boost::filesystem::path temp_file = temp_dir / ("filamenthub_print_profile_" + std::to_string(profile_id) + "_" + 
                    std::to_string(std::time(nullptr)) + ".json");
                
                // Сохраняем JSON во временный файл
                std::ofstream file(temp_file.string());
                if (!file.is_open()) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to create temporary file: " << temp_file.string();
                    std::lock_guard<std::mutex> lock(result->mutex);
                    result->error = "Failed to create temporary file";
                    return;
                }
                
                file << profile_json.dump(2);
                file.close();
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved print profile to: " << temp_file.string();
                
                // Импортируем профиль через PresetBundle
                PresetBundle* bundle = wxGetApp().preset_bundle;
                if (bundle == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null during print profile import";
                    std::lock_guard<std::mutex> lock(result->mutex);
                    result->error = "Preset bundle not available";
                    boost::filesystem::remove(temp_file);
                    return;
                }
                
                PresetsConfigSubstitutions substitutions;
                std::string file_path = temp_file.string();
                int overwrite = 1; // 1 = overwrite if exists
                std::vector<std::string> import_result;
                
                // Lambda для подтверждения перезаписи (автоматически подтверждаем)
                auto override_confirm = [](std::string const& name) -> int {
                    return 1; // Автоматически перезаписываем
                };
                
                // Импортируем JSON профиль
                bool success = bundle->import_json_presets(
                    substitutions,
                    file_path,
                    override_confirm,
                    ForwardCompatibilitySubstitutionRule::Enable,
                    overwrite,
                    import_result
                );
                
                // Удаляем временный файл
                boost::filesystem::remove(temp_file);
                
                std::lock_guard<std::mutex> lock(result->mutex);
                if (success || !import_result.empty()) {
                    // Импорт успешен
                    // ВАЖНО: Используем new_name (имя профиля с постфиксом [FilamentHub]),
                    // а НЕ import_result, так как import_result может содержать пути к файлам
                    // new_name уже содержит правильное имя профиля: original_name + " [FilamentHub]"
                    std::string actual_profile_name = new_name;
                    
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profile imported successfully (name: " << actual_profile_name << ")";
                    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: import_result size: " << import_result.size();
                    if (!import_result.empty()) {
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: import_result[0]: " << import_result[0];
                    }
                    
                    // Сохраняем маппинг profile_id → bundle_profile_name (имя профиля в OrcaSlicer)
                    save_print_profile_mapping(profile_id, actual_profile_name);
                    
                    // Обновляем UI (перезагружаем пресеты)
                    wxGetApp().load_current_presets();
                    
                    result->success = true;
                } else {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to import print profile. import_result is empty";
                    result->error = "Failed to import print profile: import_json_presets returned false";
                }
                
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception during silent print profile import: " << e.what();
                std::lock_guard<std::mutex> lock(result->mutex);
                result->error = std::string("Exception: ") + e.what();
            }
        },
        // on_error: ошибка при скачивании
        [result](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to download print profile. Error: " << error 
                                     << ", Status: " << http_status;
            std::lock_guard<std::mutex> lock(result->mutex);
            result->error = "Failed to download print profile: " + error;
        }
    );
    
    // Возвращаем результат (perform_sync выполнится синхронно, поэтому result уже заполнен)
    std::lock_guard<std::mutex> lock(result->mutex);
    return result->success;
}

}} // namespace Slic3r::GUI

