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
#include <nlohmann/json.hpp>
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

namespace Slic3r {
namespace GUI {

// Static member initialization
const wxString FilamentHubPanel::s_default_url = "http://localhost:3000";

// AppConfig constants
const std::string FilamentHubPanel::CONFIG_SECTION_FILAMENTHUB = "filamenthub";
const std::string FilamentHubPanel::CONFIG_KEY_ACCESS_TOKEN = "access_token";
const std::string FilamentHubPanel::CONFIG_KEY_USER_ID = "user_id";
const std::string FilamentHubPanel::CONFIG_KEY_LAST_SYNC_TIME = "last_sync_time";
const std::string FilamentHubPanel::CONFIG_KEY_PRESET_MAPPING = "preset_mapping";

FilamentHubPanel::FilamentHubPanel(wxWindow* parent, wxWindowID id, 
                                   const wxPoint& pos, 
                                   const wxSize& size, 
                                   long style)
    : wxPanel(parent, id, pos, size, style)
{
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
    m_main_sizer = new wxBoxSizer(wxVERTICAL);

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
    m_sync_button = new wxButton(m_info_panel, wxID_ANY, _("Synchronize"), wxDefaultPosition, wxDefaultSize);
    m_sync_button->Bind(wxEVT_BUTTON, &FilamentHubPanel::on_sync_button_click, this);
    m_sync_button->Hide(); // Hidden by default (shown when logged in)
    info_sizer->Add(m_sync_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);
    
    // Login button - redirects to login page in WebView (user logs in there)
    m_login_button = new wxButton(m_info_panel, wxID_ANY, _("Login"), wxDefaultPosition, wxDefaultSize);
    m_login_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show_login(); });
    info_sizer->Add(m_login_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);
    
    m_logout_button = new wxButton(m_info_panel, wxID_ANY, _("Logout"), wxDefaultPosition, wxDefaultSize);
    m_logout_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { logout(); });
    m_logout_button->Hide(); // Hidden by default (shown when logged in)
    info_sizer->Add(m_logout_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);
    
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
    update_user_info();
    
    // Если пользователь уже залогинен, автоматически синхронизируем пресеты при открытии вкладки
    std::string access_token;
    int user_id = 0;
    if (load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: User already logged in (ID: " << user_id 
                                 << "), auto-syncing presets on panel load...";
        // Используем CallAfter для асинхронного вызова (чтобы UI успел отрисоваться)
        CallAfter([this]() {
            synchronize_presets(false); // false = инкрементальная синхронизация (если есть last_sync_time)
        });
    }

    // Load the default URL
    load_url(s_default_url);

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
            }
        };
        
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
                client.set_api_base_url("http://localhost:8000");
                client.get_current_user(
                    access_token,
                    [this, access_token](std::string json_body, unsigned http_status) {
                        try {
                            nlohmann::json user_json = nlohmann::json::parse(json_body);
                            int user_id = user_json["id"].get<int>();
                            
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Login success received. User ID: " << user_id;
                            
                            // Save auth token to AppConfig
                            save_auth_token(access_token, user_id);
                            
                            // Update UI to show logged-in state
                            update_user_info();
                            
                            // Автоматически синхронизируем пресеты после логина
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Auto-syncing presets after login...";
                            synchronize_presets(true); // force_full_sync = true для первого раза
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
            
            // Update UI to show logged-in state
            update_user_info();
            
            // Автоматически синхронизируем пресеты после логина
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Auto-syncing presets after login...";
            synchronize_presets(true); // force_full_sync = true для первого раза
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
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Importing profile " << preset_id;
    
    // Дополнительная проверка валидности preset_id
    if (preset_id <= 0) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Invalid preset_id in import_profile: " << preset_id;
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
    
    // Проверяем, что preset_bundle доступен
    if (wxGetApp().preset_bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot import";
        send_response("import_profile", "error", _("Preset bundle not available").ToUTF8().data(), sequence_id);
        wxMessageBox(
            _L("Error: Preset bundle is not available. Please restart OrcaSlicer."),
            _L("FilamentHub Import Error"),
            wxOK | wxICON_ERROR
        );
        return;
    }
    
    // Get FilamentHub API base URL
    std::string api_url = "http://localhost:8000";
    
    // Get access token from client (if available)
    FilamentHubClient client;
    client.set_api_base_url(api_url);
    std::string access_token = client.get_access_token();
    
    // Скачиваем профиль асинхронно
    client.download_profile(
        preset_id,
        access_token,
        // on_complete: профиль успешно скачан
        [this, preset_id, sequence_id](std::string json_content, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Profile downloaded successfully. Size: " << json_content.size();
            
            try {
                // Парсим JSON чтобы добавить постфикс к имени и проверить родительский пресет
                nlohmann::json profile_json = nlohmann::json::parse(json_content);
                
                // Добавляем постфикс [FilamentHub] к имени пресета
                std::string original_name = profile_json.value("name", "");
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
                    send_response("import_profile", "error", _("Failed to create temporary file").ToUTF8().data(), sequence_id);
                    wxMessageBox(
                        _L("Failed to create temporary file for import."),
                        _L("FilamentHub Import Error"),
                        wxOK | wxICON_ERROR
                    );
                    return;
                }
                
                file << profile_json.dump(2);
                file.close();
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved profile to: " << temp_file.string();
                
                // Импортируем профиль через PresetBundle
                PresetBundle* bundle = wxGetApp().preset_bundle;
                if (bundle == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null during import";
                    send_response("import_profile", "error", _("Preset bundle not available").ToUTF8().data(), sequence_id);
                    return;
                }
                
                PresetsConfigSubstitutions substitutions;
                std::string file_path = temp_file.string();
                int overwrite = 1; // 1 = overwrite if exists
                std::vector<std::string> result;
                
                // Lambda для подтверждения перезаписи (если профиль уже существует)
                auto override_confirm = [](std::string const& name) -> int {
                    // Возвращаем 1 (yes) для автоматического перезаписывания
                    // Или можно показать диалог: return wxMessageBox(...) == wxYES ? 1 : 0;
                    return 1;
                };
                
                // Импортируем JSON профиль
                bool success = bundle->import_json_presets(
                    substitutions,
                    file_path,
                    override_confirm,
                    ForwardCompatibilitySubstitutionRule::Enable, // Правило совместимости
                    overwrite,
                    result
                );
                
                // Удаляем временный файл
                boost::filesystem::remove(temp_file);
                
                if (success) {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Profile imported successfully";
                    
                    // Обновляем UI
                    wxGetApp().load_current_presets();
                    
                    // Отправляем успешный ответ во frontend
                    send_response("import_profile", "success", 
                        wxString::Format(_("Profile %d imported successfully"), preset_id).ToUTF8().data(), 
                        sequence_id);
                    
                    // Показываем уведомление пользователю
                    wxMessageBox(
                        wxString::Format(_L("Profile %d imported successfully from FilamentHub."), preset_id),
                        _L("FilamentHub Import"),
                        wxOK | wxICON_INFORMATION
                    );
                } else {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to import profile";
                    send_response("import_profile", "error", 
                        _("Failed to import profile. Check log for details.").ToUTF8().data(), 
                        sequence_id);
                    
                    wxMessageBox(
                        _L("Failed to import profile. It may already exist or be invalid."),
                        _L("FilamentHub Import Error"),
                        wxOK | wxICON_WARNING
                    );
                }
                
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception during import: " << e.what();
                send_response("import_profile", "error", 
                    wxString::Format(_("Error: %s"), e.what()).ToUTF8().data(), 
                    sequence_id);
                
                wxMessageBox(
                    wxString::Format(_L("Error importing profile: %s"), e.what()),
                    _L("FilamentHub Import Error"),
                    wxOK | wxICON_ERROR
                );
            }
        },
        // on_error: ошибка при скачивании
        [this, preset_id, sequence_id](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to download profile " << preset_id 
                                     << ". Error: " << error << ", Status: " << http_status;
            
            wxString error_msg;
            if (http_status == 401) {
                error_msg = _L("Authentication required. Please login to FilamentHub first.");
            } else if (http_status == 404) {
                error_msg = wxString::Format(_L("Profile %d not found."), preset_id);
            } else if (http_status >= 500) {
                error_msg = _L("Server error. Please try again later.");
            } else {
                error_msg = wxString::Format(_L("Failed to download profile: %s"), error);
            }
            
            send_response("import_profile", "error", error_msg.ToUTF8().data(), sequence_id);
            
            wxMessageBox(
                error_msg,
                _L("FilamentHub Import Error"),
                wxOK | wxICON_ERROR
            );
        }
    );
}

void FilamentHubPanel::synchronize_presets(bool force_full_sync)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Starting preset synchronization (force_full_sync=" << force_full_sync << ")";
    
    // Обновляем состояние кнопки синхронизации (если синхронизация не уже в процессе)
    if (!m_is_syncing) {
        update_sync_button_state(true);
    }
    
    // 1. Загружаем токен и user_id из AppConfig
    std::string access_token;
    int user_id = 0;
    
    if (!load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: No auth token found, cannot synchronize";
        update_sync_button_state(false);
        wxMessageBox(
            _L("Please login to FilamentHub first."),
            _L("FilamentHub Sync Error"),
            wxOK | wxICON_WARNING
        );
        return;
    }
    
    // 2. Получаем last_sync_time для инкрементальной синхронизации
    std::string updated_since;
    if (!force_full_sync) {
        updated_since = load_last_sync_time(user_id);
    }
    
    // 3. Получаем список пресетов пользователя через API
    FilamentHubClient client;
    client.set_api_base_url("http://localhost:8000");
    
    client.get_my_presets(
        access_token,
        updated_since,
        // on_complete: список пресетов получен
        [this, user_id, force_full_sync](std::string json_body, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Received presets list. Status: " << http_status;
            
            // Проверяем статус ответа
            if (http_status == 401) {
                // Токен истек или невалидный - очищаем и показываем форму входа
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Token expired or invalid (401) during sync, clearing auth";
                logout(); // Очищает токен и обновляет UI
                update_sync_button_state(false);
                wxMessageBox(
                    _L("Your session has expired. Please login again."),
                    _L("FilamentHub Sync Error"),
                    wxOK | wxICON_WARNING
                );
                return;
            }
            
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get presets list. Status: " << http_status;
                update_sync_button_state(false);
                wxMessageBox(
                    wxString::Format(_L("Failed to get presets list. Status: %d"), http_status),
                    _L("FilamentHub Sync Error"),
                    wxOK | wxICON_ERROR
                );
                return;
            }
            
            try {
                nlohmann::json response = nlohmann::json::parse(json_body);
                std::vector<nlohmann::json> presets = response["items"];
                int total = response.value("total", 0);
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Received " << total << " presets";
                
                if (presets.empty()) {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: No presets to sync";
                    return;
                }
                
                // 4. Синхронизируем каждый пресет
                int synced_count = 0;
                int updated_count = 0;
                int error_count = 0;
                
                for (const auto& preset_json : presets) {
                    int preset_id = preset_json["id"];
                    std::string preset_name = preset_json["name"];
                    std::string updated_at = preset_json["updated_at"];
                    
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Processing preset ID=" << preset_id 
                                           << ", name=" << preset_name;
                    
                    // Проверяем маппинг (есть ли уже в OrcaSlicer)
                    std::string bundle_preset_name = load_preset_mapping(preset_id);
                    
                    if (bundle_preset_name.empty()) {
                        // Пресета нет в маппинге - нужно скачать и импортировать
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Preset " << preset_id << " not in mapping, downloading...";
                        
                        // Импортируем без UI диалогов
                        std::string access_token_inner;
                        int user_id_inner;
                        if (load_auth_token(access_token_inner, user_id_inner)) {
                            if (import_preset_silent(preset_id, preset_name, access_token_inner)) {
                                synced_count++;
                            } else {
                                error_count++;
                            }
                        } else {
                            error_count++;
                        }
                    } else {
                        // Пресет уже в маппинге - проверяем, изменился ли он
                        // TODO: Сравнить updated_at с временем последнего импорта
                        // Пока просто пропускаем (можно обновить если нужно)
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Preset " << preset_id 
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
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Synchronization completed. "
                                       << "Synced: " << synced_count 
                                       << ", Updated: " << updated_count
                                       << ", Errors: " << error_count;
                
                // Update UI: button state and user info
                update_sync_button_state(false);
                update_user_info();
                
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing presets list: " << e.what();
                wxMessageBox(
                    wxString::Format(_L("Error parsing presets list: %s"), e.what()),
                    _L("FilamentHub Sync Error"),
                    wxOK | wxICON_ERROR
                );
            }
        },
        // on_error: ошибка при получении списка пресетов
        [this](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get presets list. Error: " << error 
                                    << ", Status: " << http_status;
            
            wxString error_msg;
            if (http_status == 401) {
                error_msg = _L("Authentication required. Please login to FilamentHub first.");
                // Можно очистить токен если он невалидный
            } else if (http_status >= 500) {
                error_msg = _L("Server error. Please try again later.");
            } else {
                error_msg = wxString::Format(_L("Failed to get presets list: %s"), error);
            }
            
            wxMessageBox(
                error_msg,
                _L("FilamentHub Sync Error"),
                wxOK | wxICON_ERROR
            );
            
            // Update UI: button state
            update_sync_button_state(false);
        }
    );
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
    if (wxGetApp().app_config == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: app_config is null, cannot load token";
        return false;
    }
    
    std::string token = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_ACCESS_TOKEN);
    std::string user_id_str = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_USER_ID);
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Loading auth token - token length: " << token.length() 
                            << ", user_id_str: '" << user_id_str << "'";
    
    if (token.empty() || user_id_str.empty()) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: No auth token found in config (token empty: " << token.empty() 
                                << ", user_id_str empty: " << user_id_str.empty() << ")";
        return false;
    }
    
    try {
        // Проверяем что user_id_str не пустая и содержит только цифры (может быть с минусом)
        if (user_id_str.empty()) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: user_id_str is empty, cannot parse";
            return false;
        }
        
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
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Loaded auth token for user_id=" << user_id 
                                << ", token length: " << access_token.length()
                                << ", token preview: " << access_token.substr(0, 20) << "...";
        return true;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing user_id: " << e.what();
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
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved last_sync_time for user_id=" << user_id 
                            << ": " << timestamp;
}

std::string FilamentHubPanel::load_last_sync_time(int user_id)
{
    if (wxGetApp().app_config == nullptr) {
        return "";
    }
    
    std::string key = CONFIG_KEY_LAST_SYNC_TIME + "_" + std::to_string(user_id);
    return wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, key);
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
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Silent import of preset " << preset_id;
    
    // Проверяем, что preset_bundle доступен
    if (wxGetApp().preset_bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot import";
        return false;
    }
    
    // Скачиваем профиль синхронно через FilamentHubClient
    FilamentHubClient client;
    client.set_api_base_url("http://localhost:8000");
    
    // Используем shared_ptr для безопасного доступа из лямбд
    struct ImportResult {
        bool success = false;
        std::string error;
        std::mutex mutex;
    };
    
    auto result = std::make_shared<ImportResult>();
    
    // Используем синхронный вызов (perform_sync уже внутри download_profile)
    client.download_profile(
        preset_id,
        access_token,
        // on_complete: профиль успешно скачан
        [this, preset_id, preset_name, result](std::string json_content, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Profile downloaded successfully. Size: " << json_content.size();
            
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
                    // Используем имя из import_result если доступно, иначе используем new_name
                    std::string actual_preset_name = new_name;
                    if (!import_result.empty()) {
                        // import_result содержит имена импортированных пресетов
                        actual_preset_name = import_result[0];
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Profile imported with name: " << actual_preset_name;
                    } else {
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Profile imported successfully (name: " << new_name << ")";
                    }
                    
                    // Сохраняем маппинг preset_id → bundle_preset_name
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
        [result](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to download profile. Error: " << error 
                                     << ", Status: " << http_status;
            std::lock_guard<std::mutex> lock(result->mutex);
            result->error = "Failed to download profile: " + error;
        }
    );
    
    // Возвращаем результат (perform_sync выполнится синхронно, поэтому result уже заполнен)
    std::lock_guard<std::mutex> lock(result->mutex);
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
    client.set_api_base_url("http://localhost:8000");
    
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
                // Токен истек или невалидный - очищаем и показываем форму входа
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Token expired or invalid (401), clearing auth";
                logout(); // Очищает токен и обновляет UI
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
                    client_inner.set_api_base_url("http://localhost:8000");
                    
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
    if (m_is_syncing) {
        return; // Prevent multiple simultaneous syncs
    }
    
    update_sync_button_state(true);
    
    // Start synchronization
    synchronize_presets(false); // Incremental sync
    
    // Note: synchronize_presets is async, so we'll update button state in callback
    // For now, just update after delay (TODO: improve with proper callback)
}

void FilamentHubPanel::update_sync_button_state(bool is_syncing)
{
    m_is_syncing = is_syncing;
    
    if (is_syncing) {
        m_sync_button->SetLabel(_("Synchronizing..."));
        m_sync_button->Disable();
    } else {
        m_sync_button->SetLabel(_("Synchronize"));
        m_sync_button->Enable();
    }
    
    m_info_panel->Layout();
}

void FilamentHubPanel::update_ui_for_login_state(bool is_logged_in)
{
    if (is_logged_in) {
        // Show logged-in UI elements
        m_profile_button->Show();
        m_preset_count_label->Show();
        m_sync_button->Show();
        m_logout_button->Show();
        
        // Hide not-logged-in UI elements
        m_login_button->Hide();
    } else {
        // Show not-logged-in UI elements
        m_login_button->Show();
        
        // Hide logged-in UI elements
        m_profile_button->Hide();
        m_preset_count_label->Hide();
        m_sync_button->Hide();
        m_logout_button->Hide();
        
        // Update labels
        m_user_name_label->SetLabel(_("Not logged in"));
        m_preset_count_label->SetLabel(_("Presets: 0"));
    }
    
    m_info_panel->Layout();
}

void FilamentHubPanel::navigate_to_catalog()
{
    load_url(s_default_url + "/"); // Navigate to catalog page
}

void FilamentHubPanel::navigate_to_profile()
{
    load_url(s_default_url + "/profile"); // Navigate to profile page
}

void FilamentHubPanel::show_login()
{
    // Navigate to login page in WebView
    // User will log in there, and we'll receive login_success message via JavaScript
    load_url(s_default_url + "/?auth=login");
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

}} // namespace Slic3r::GUI

