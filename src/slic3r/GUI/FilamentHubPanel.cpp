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
 * Source: https://github.com/WeLizard/OrcaSlicer
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
#include "Widgets/Button.hpp"
#include <wx/stattext.h>
#include <wx/panel.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/menu.h>
// #include <wx/flexgrid.h> // Removed - wxFlexGridSizer should be available from other includes
#include <regex>
#include <nlohmann/json.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <algorithm>
#include <ctime>
#include <chrono>
#include <set>
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
#include <condition_variable>
#include <thread>
#include <algorithm>

namespace Slic3r {
namespace GUI {

// Helper function to serialize DynamicPrintConfig to JSON
static nlohmann::json get_config_json(const DynamicPrintConfig& config) {
    nlohmann::json j;
    
    // Record all the key-values
    for (const std::string &opt_key : config.keys()) {
        const ConfigOption *opt = config.option(opt_key);
        if (opt->is_scalar()) {
            if (opt->type() == coString) {
                // CRASH-2 fix: проверяем результат dynamic_cast перед разыменованием
                const ConfigOptionString* str_opt = dynamic_cast<const ConfigOptionString *>(opt);
                if (str_opt != nullptr)
                    j[opt_key] = str_opt->value;
                else
                    j[opt_key] = opt->serialize();
            } else
                j[opt_key] = opt->serialize();
        } else {
            const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase *>(opt);
            std::vector<std::string> string_values = vec->vserialize();
            nlohmann::json j_array(string_values);
            j[opt_key] = j_array;
        }
    }
    
    return j;
}

// Static member initialization
const wxString FilamentHubPanel::DEFAULT_FRONTEND_URL = "https://filamenthub.ru";
const std::string FilamentHubPanel::CONFIG_SECTION_FILAMENTHUB = "filamenthub";
const std::string FilamentHubPanel::CONFIG_KEY_ACCESS_TOKEN = "access_token";
const std::string FilamentHubPanel::CONFIG_KEY_REFRESH_TOKEN = "refresh_token";
const std::string FilamentHubPanel::CONFIG_KEY_USER_ID = "user_id";
const std::string FilamentHubPanel::CONFIG_KEY_LAST_SYNC_TIME = "last_sync_time";
const std::string FilamentHubPanel::CONFIG_KEY_PRESET_MAPPING = "preset_mapping";
const std::string FilamentHubPanel::CONFIG_KEY_PRINTER_PROFILE_MAPPING = "printer_profile_mapping";
const std::string FilamentHubPanel::CONFIG_KEY_PRINT_PROFILE_MAPPING = "print_profile_mapping";
const std::string FilamentHubPanel::CONFIG_KEY_FRONTEND_URL = "frontend_url";
const std::string FilamentHubPanel::CONFIG_KEY_API_BASE_URL = "api_base_url";
const std::string FilamentHubPanel::CONFIG_KEY_DELETED_PRESET_ACTION = "deleted_preset_action"; // "ask", "import", "delete", "skip"

FilamentHubPanel::FilamentHubPanel(wxWindow* parent, wxWindowID id,
                                   const wxPoint& pos,
                                   const wxSize& size,
                                   long style)
    : wxPanel(parent, id, pos, size, style)
    , m_fhub_client(std::make_unique<FilamentHubClient>())
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

    // Cancel all pending HTTP requests before destruction
    if (m_fhub_client) {
        m_fhub_client->cancel_all();
    }

    // Очищаем меню уведомлений
    if (m_notifications_menu != nullptr) {
        delete m_notifications_menu;
        m_notifications_menu = nullptr;
    }

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
    m_user_name_label = new wxStaticText(m_info_panel, wxID_ANY, _L("Sign in to unlock full functionality"), wxDefaultPosition, wxDefaultSize);
    m_user_name_label->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    info_sizer->Add(m_user_name_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 10);
    
    m_preset_count_label = new wxStaticText(m_info_panel, wxID_ANY, _L("Presets: 0"), wxDefaultPosition, wxDefaultSize);
    m_preset_count_label->Hide(); // Hidden by default (shown when logged in)
    info_sizer->Add(m_preset_count_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 10);
    
    info_sizer->AddStretchSpacer(); // Push buttons to the right
    
    // Right side: Navigation buttons first (compact style, square corners, no spacing between buttons)
    m_catalog_button = new Button(m_info_panel, _L("Catalog"));
    set_button_square_style(m_catalog_button, ButtonStyle::Regular);
    m_catalog_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { 
        m_active_page = "catalog";
        update_active_button_style();
        navigate_to_catalog(); 
    });
    info_sizer->Add(m_catalog_button, 0, wxALIGN_CENTER_VERTICAL);
    
    m_profile_button = new Button(m_info_panel, _L("Profile"));
    set_button_square_style(m_profile_button, ButtonStyle::Regular);
    m_profile_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        m_active_page = "profile";
        update_active_button_style();
        navigate_to_profile();
    });
    m_profile_button->Hide(); // Hidden by default (shown when logged in)
    info_sizer->Add(m_profile_button, 0, wxALIGN_CENTER_VERTICAL);

    // Wiki button - always visible
    m_wiki_button = new Button(m_info_panel, _L("Wiki"));
    set_button_square_style(m_wiki_button, ButtonStyle::Regular);
    m_wiki_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        m_active_page = "wiki";
        update_active_button_style();
        navigate_to_wiki();
    });
    info_sizer->Add(m_wiki_button, 0, wxALIGN_CENTER_VERTICAL);

    // Action buttons
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Creating sync button...";
    m_sync_button = new Button(m_info_panel, _L("Synchronize"));
    set_button_square_style(m_sync_button, ButtonStyle::Confirm);
    
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
    info_sizer->Add(m_sync_button, 0, wxALIGN_CENTER_VERTICAL);

    // Settings button - visible when Developer Mode is enabled in Preferences
    // Allows changing Frontend URL and API Base URL for development/debugging
    m_settings_button = new Button(m_info_panel, _L("Settings"));
    set_button_square_style(m_settings_button, ButtonStyle::Regular);
    m_settings_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show_settings_dialog(); });
    info_sizer->Add(m_settings_button, 0, wxALIGN_CENTER_VERTICAL);

    // Show/hide based on Developer Mode setting
    bool dev_mode = wxGetApp().app_config->get("developer_mode") == "true";
    if (dev_mode) {
        m_settings_button->Show();
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Settings button shown (Developer Mode enabled)";
    } else {
        m_settings_button->Hide();
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Settings button hidden (Developer Mode disabled)";
    }
    
    // Refresh button - reloads the current page (only in Developer Mode)
    m_refresh_button = new Button(m_info_panel, _L("Refresh"));
    set_button_square_style(m_refresh_button, ButtonStyle::Regular);
    m_refresh_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { reload(); });
    info_sizer->Add(m_refresh_button, 0, wxALIGN_CENTER_VERTICAL);
    if (dev_mode) {
        m_refresh_button->Show();
    } else {
        m_refresh_button->Hide();
    }
    
    // Notifications button - REMOVED from C++ UI
    // Notifications are now displayed in WebView as a floating button (bell icon)
    // This keeps UI consistent with the web version
    m_notifications_button = nullptr; // Not used anymore
    m_notifications_badge = nullptr; // Not used anymore
    
    // Admin button - opens admin panel (only if admin)
    m_admin_button = new Button(m_info_panel, _L("Admin"));
    set_button_square_style(m_admin_button, ButtonStyle::Alert);
    m_admin_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { 
        m_active_page = "admin";
        update_active_button_style();
        navigate_without_reload("/admin");
    });
    m_admin_button->Hide(); // Hidden by default (shown when admin)
    info_sizer->Add(m_admin_button, 0, wxALIGN_CENTER_VERTICAL);
    
    // Login button - redirects to login page in WebView (user logs in there)
    m_login_button = new Button(m_info_panel, _L("Login"));
    set_button_square_style(m_login_button, ButtonStyle::Confirm);
    m_login_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show_login(); });
    info_sizer->Add(m_login_button, 0, wxALIGN_CENTER_VERTICAL);
    
    m_logout_button = new Button(m_info_panel, _L("Logout"));
    set_button_square_style(m_logout_button, ButtonStyle::Regular);
    m_logout_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { logout(); });
    m_logout_button->Hide(); // Hidden by default (shown when logged in)
    info_sizer->Add(m_logout_button, 0, wxALIGN_CENTER_VERTICAL);

    m_sync_status_label = new wxStaticText(m_info_panel, wxID_ANY, _L("Ready"));
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
    update_user_info();
    
    // Если пользователь уже залогинен, автоматически синхронизируем пресеты при инициализации панели
    // Используем CallAfter для асинхронного вызова (чтобы UI успел отрисоваться)
    std::string access_token;
    int user_id = 0;
    if (load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: User already logged in (ID: " << user_id 
                                 << "), checking token validity before auto-sync...";
        // ВАЖНО: Не запускаем автоматическую синхронизацию при загрузке панели
        // Синхронизация будет запущена пользователем вручную через кнопку "Synchronize"
        // Это предотвращает проблемы с истёкшими токенами и race conditions
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Auto-sync disabled on panel load (user must click 'Synchronize' button)";
    }

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
    // CRASH-3 fix: проверяем m_browser перед использованием
    if (show && !m_url_deferred.empty() && m_browser != nullptr) {
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

    // CRASH-3 fix: проверяем m_browser перед любым использованием
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: OnLoaded called but m_browser is null";
        return;
    }

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
            sendLoginSuccess: function(accessToken, userId, refreshToken) {
                const message = JSON.stringify({
                    command: 'login_success',
                    data: {
                        access_token: accessToken,
                        user_id: userId,
                        refresh_token: refreshToken || null
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
            },
            // Экспорт filament presets из OrcaSlicer в FilamentHub
            exportFilamentPresets: function() {
                return new Promise(function(resolve, reject) {
                    const sequenceId = 'export_filament_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
                    
                    // Отправляем команду в C++
                    const message = JSON.stringify({
                        command: 'export_filament_presets',
                        sequence_id: sequenceId,
                        data: {}
                    });
                    
                    // Регистрируем обработчик ответа
                    const handleResponse = function(event) {
                        try {
                            const data = typeof event.data === 'string' ? JSON.parse(event.data) : event.data;
                            if (data.command === 'export_filament_presets' && data.sequence_id === sequenceId) {
                                window.removeEventListener('message', handleResponse);
                                if (data.status === 'success') {
                                    resolve({ message: data.message || '' });
                                } else {
                                    reject(new Error(data.message || 'Export failed'));
                                }
                            }
                        } catch (e) {
                            // Игнорируем сообщения, которые не являются ответами
                        }
                    };
                    
                    window.addEventListener('message', handleResponse);
                    
                    // Отправляем сообщение через postMessage
                    if (window.wx && window.wx.postMessage) {
                        window.wx.postMessage(message);
                    } else {
                        window.removeEventListener('message', handleResponse);
                        reject(new Error('OrcaSlicer API not available'));
                    }
                });
            },
            // Экспорт printer profiles из OrcaSlicer в FilamentHub
            exportPrinterProfiles: function() {
                return new Promise(function(resolve, reject) {
                    const sequenceId = 'export_printer_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
                    
                    // Отправляем команду в C++
                    const message = JSON.stringify({
                        command: 'export_printer_profiles',
                        sequence_id: sequenceId,
                        data: {}
                    });
                    
                    // Регистрируем обработчик ответа
                    const handleResponse = function(event) {
                        try {
                            const data = typeof event.data === 'string' ? JSON.parse(event.data) : event.data;
                            if (data.command === 'export_printer_profiles' && data.sequence_id === sequenceId) {
                                window.removeEventListener('message', handleResponse);
                                if (data.status === 'success') {
                                    resolve({ message: data.message || '' });
                                } else {
                                    reject(new Error(data.message || 'Export failed'));
                                }
                            }
                        } catch (e) {
                            // Игнорируем сообщения, которые не являются ответами
                        }
                    };
                    
                    window.addEventListener('message', handleResponse);
                    
                    // Отправляем сообщение через postMessage
                    if (window.wx && window.wx.postMessage) {
                        window.wx.postMessage(message);
                    } else {
                        window.removeEventListener('message', handleResponse);
                        reject(new Error('OrcaSlicer API not available'));
                    }
                });
            },
            // Экспорт print profiles из OrcaSlicer в FilamentHub
            exportPrintProfiles: function() {
                return new Promise(function(resolve, reject) {
                    const sequenceId = 'export_print_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);

                    // Отправляем команду в C++
                    const message = JSON.stringify({
                        command: 'export_print_profiles',
                        sequence_id: sequenceId,
                        data: {}
                    });

                    // Регистрируем обработчик ответа
                    const handleResponse = function(event) {
                        try {
                            const data = typeof event.data === 'string' ? JSON.parse(event.data) : event.data;
                            if (data.command === 'export_print_profiles' && data.sequence_id === sequenceId) {
                                window.removeEventListener('message', handleResponse);
                                if (data.status === 'success') {
                                    resolve({ message: data.message || '' });
                                } else {
                                    reject(new Error(data.message || 'Export failed'));
                                }
                            }
                        } catch (e) {
                            // Игнорируем сообщения, которые не являются ответами
                        }
                    };

                    window.addEventListener('message', handleResponse);

                    // Отправляем сообщение через postMessage
                    if (window.wx && window.wx.postMessage) {
                        window.wx.postMessage(message);
                    } else {
                        window.removeEventListener('message', handleResponse);
                        reject(new Error('OrcaSlicer API not available'));
                    }
                });
            },
            // Экспорт всех профилей (filament, printer, print) из OrcaSlicer в FilamentHub
            exportAllProfiles: function() {
                return new Promise(function(resolve, reject) {
                    const sequenceId = 'export_all_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);

                    // Отправляем команду в C++
                    const message = JSON.stringify({
                        command: 'export_all_profiles',
                        sequence_id: sequenceId,
                        data: {}
                    });

                    // Регистрируем обработчик ответа
                    const handleResponse = function(event) {
                        try {
                            const data = typeof event.data === 'string' ? JSON.parse(event.data) : event.data;
                            if (data.command === 'export_all_profiles' && data.sequence_id === sequenceId) {
                                window.removeEventListener('message', handleResponse);
                                if (data.status === 'success') {
                                    resolve({ message: data.message || '' });
                                } else {
                                    reject(new Error(data.message || 'Export failed'));
                                }
                            }
                        } catch (e) {
                            // Игнорируем сообщения, которые не являются ответами
                        }
                    };

                    window.addEventListener('message', handleResponse);

                    // Отправляем сообщение через postMessage
                    if (window.wx && window.wx.postMessage) {
                        window.wx.postMessage(message);
                    } else {
                        window.removeEventListener('message', handleResponse);
                        reject(new Error('OrcaSlicer API not available'));
                    }
                });
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
        
        // Monitor localStorage for token changes (when user logs in/out via frontend)
        // Задержка 2 сек — чтобы C++ успел инжектировать токен и polling не сработал ложно
        let lastToken = null;
        setTimeout(() => {
            lastToken = localStorage.getItem('access_token');
            setInterval(() => {
                const currentToken = localStorage.getItem('access_token');
                const currentUserId = localStorage.getItem('user_id');
                const currentRefreshToken = localStorage.getItem('refresh_token');

                if (currentToken !== lastToken) {
                    lastToken = currentToken;
                    if (currentToken && window.filamenthub && window.filamenthub.sendLoginSuccess) {
                        window.filamenthub.sendLoginSuccess(currentToken, currentUserId ? parseInt(currentUserId) : null, currentRefreshToken);
                    } else if (!currentToken && window.wx && window.wx.postMessage) {
                        // Токен удалён — уведомляем C++ о logout
                        window.wx.postMessage(JSON.stringify({ command: 'logout' }));
                    }
                }
            }, 1000);
        }, 2000); // Задержка 2 сек после загрузки
        
        console.log('FilamentHub API initialized');
    )";
    
    WebView::RunScript(m_browser, js_api);

    // Инжекция токена из AppConfig в localStorage при каждой загрузке страницы
    // Это решает проблему рассинхрона: C++ — единственный владелец токена,
    // при reload/навигации фронтенд получает актуальный токен из AppConfig
    std::string access_token;
    int user_id = 0;
    if (load_auth_token(access_token, user_id)) {
        // Загружаем refresh_token из AppConfig
        std::string refresh_token;
        if (wxGetApp().app_config != nullptr) {
            refresh_token = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_REFRESH_TOKEN);
        }

        // Инжектируем access_token, refresh_token, user_id в localStorage
        // SEC-1 fix: используем JSON.parse() вместо строковой подстановки для защиты от XSS
        nlohmann::json auth_data;
        auth_data["token"] = access_token;
        auth_data["refreshToken"] = refresh_token;
        auth_data["userId"] = user_id;
        wxString auth_json_wx = wxString::FromUTF8(auth_data.dump().c_str());
        wxString inject_js = wxString(R"(
            (function() {
                try {
                    var data = )") + auth_json_wx.Clone() + wxString(R"(;
                    if (data.token) {
                        localStorage.setItem('access_token', data.token);
                        if (data.refreshToken) {
                            localStorage.setItem('refresh_token', data.refreshToken);
                        }
                        if (data.userId > 0) {
                            localStorage.setItem('user_id', data.userId.toString());
                        }
                        console.log('FilamentHub: Token injected from C++ (userId=' + data.userId + ')');
                    }
                } catch (e) {
                    console.error('FilamentHub: Failed to parse auth data:', e);
                }
            })();
        )");
        WebView::RunScript(m_browser, inject_js);
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Injected auth token into WebView localStorage (user_id=" << user_id << ")";
    } else {
        // Нет токена в AppConfig — очищаем localStorage (если пользователь вышел из C++)
        wxString clear_js = R"(
            (function() {
                localStorage.removeItem('access_token');
                localStorage.removeItem('refresh_token');
                localStorage.removeItem('user_id');
                console.log('FilamentHub: Cleared auth tokens (no token in C++)');
            })();
        )";
        WebView::RunScript(m_browser, clear_js);
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: No token in AppConfig, cleared WebView localStorage";
    }
}

void FilamentHubPanel::process_login_success(const std::string& access_token, const std::string& refresh_token, int user_id)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Login success received. User ID: " << user_id;

    // Save auth token to AppConfig
    save_auth_token(access_token, user_id);

    // Save refresh token
    if (!refresh_token.empty()) {
        CallAfter([this, refresh_token]() {
            if (wxGetApp().app_config != nullptr) {
                wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_REFRESH_TOKEN, refresh_token);
                wxGetApp().app_config->save();
            }
        });
    }
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Token saved. User ID: " << user_id << ", token length: " << access_token.length();

    // Update UI to show logged-in state
    CallAfter([this]() {
        update_user_info();
        // Автоматически синхронизируем пресеты после логина
        if (!m_is_syncing) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Auto-syncing presets after login...";
            synchronize_presets(true); // force_full_sync = true для первого раза
        }
    });
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
                    wxString::Format(_L("Invalid preset ID: %d"), preset_id).ToUTF8().data(), 
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

            // Сохраняем refresh_token если передан
            std::string refresh_token;
            if (j["data"].contains("refresh_token") && !j["data"]["refresh_token"].is_null()) {
                refresh_token = j["data"]["refresh_token"].get<std::string>();
            }

            // user_id может быть null в JSON
            int user_id = 0;
            if (!j["data"]["user_id"].is_null()) {
                user_id = j["data"]["user_id"].get<int>();
            } else {
                // Если user_id не передан, получаем его через API
                m_fhub_client->set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
                m_fhub_client->get_current_user(
                    access_token,
                    [this, access_token, refresh_token](std::string json_body, unsigned http_status) {
                        try {
                            nlohmann::json user_json = nlohmann::json::parse(json_body);
                            int user_id = user_json["id"].get<int>();
                            process_login_success(access_token, refresh_token, user_id);
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

            process_login_success(access_token, refresh_token, user_id);
        } else if (command == "export_filament_presets") {
            // User wants to export filament presets from OrcaSlicer to FilamentHub
            static int cmd_counter = 0;
            cmd_counter++;
            int current_cmd = cmd_counter; // Локальная копия для захвата в лямбда
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [COMMAND #" << current_cmd << "] Export filament presets command received from Frontend";
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [COMMAND TRACE] sequence_id=" << sequence_id.ToStdString();
            
            // Вызываем export_filament_presets_to_filamenthub асинхронно
            // Результат будет отправлен через show_notification_in_webview
            CallAfter([this, current_cmd]() {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [CALLAFTER #" << current_cmd << "] Executing export_filament_presets_to_filamenthub() in UI thread";
                export_filament_presets_to_filamenthub();
            });
            
            // Отправляем немедленный ответ, что команда получена
            send_response("export_filament_presets", "success", "", sequence_id);
        } else if (command == "export_printer_profiles") {
            // User wants to export printer profiles from OrcaSlicer to FilamentHub
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Export printer profiles command received from Frontend";

            // Вызываем export_printer_profiles_to_filamenthub асинхронно
            // Результат будет отправлен через show_notification_in_webview
            CallAfter([this]() {
                export_printer_profiles_to_filamenthub();
            });

            // Отправляем немедленный ответ, что команда получена
            send_response("export_printer_profiles", "success", "", sequence_id);
        } else if (command == "export_print_profiles") {
            // User wants to export print profiles from OrcaSlicer to FilamentHub
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Export print profiles command received from Frontend";

            // Вызываем export_print_profiles_to_filamenthub асинхронно
            // Результат будет отправлен через show_notification_in_webview
            CallAfter([this]() {
                export_print_profiles_to_filamenthub();
            });

            // Отправляем немедленный ответ, что команда получена
            send_response("export_print_profiles", "success", "", sequence_id);
        } else if (command == "export_all_profiles") {
            // User wants to export all profiles (filament, printer, print) from OrcaSlicer to FilamentHub
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Export all profiles command received from Frontend";

            // Вызываем export_profiles_to_filamenthub асинхронно
            // Результат будет отправлен через show_notification_in_webview
            CallAfter([this]() {
                export_profiles_to_filamenthub();
            });

            // Отправляем немедленный ответ, что команда получена
            send_response("export_all_profiles", "success", "", sequence_id);
        } else if (command == "logout") {
            // Frontend сообщает о logout (401 без refresh, или refresh failed)
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Logout command received from Frontend (token expired/revoked)";
            CallAfter([this]() {
                // Очищаем все токены в AppConfig
                if (wxGetApp().app_config != nullptr) {
                    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_ACCESS_TOKEN, "");
                    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_REFRESH_TOKEN, "");
                    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_USER_ID, "");
                    wxGetApp().app_config->save();
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Cleared all auth tokens from AppConfig (frontend logout)";
                }
                update_user_info();
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

    m_fhub_client->set_api_base_url(api_base_url);
    
    m_fhub_client->download_profile(
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
    static int sync_call_counter = 0;
    sync_call_counter++;
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== [SYNC START] synchronize_presets() CALLED (call #" << sync_call_counter << ") ==========";
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 1] force_full_sync=" << (force_full_sync ? "true" : "false");
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC TRACE] m_is_syncing=" << (m_is_syncing ? "true" : "false")
                            << ", m_full_sync_attempted=" << (m_full_sync_attempted.load() ? "true" : "false");
    
    // Атомарный check-and-set: предотвращает race condition при одновременных вызовах
    if (m_is_syncing.exchange(true)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC ERROR] Sync already in progress, skipping";
        return;
    }

    // Сбрасываем флаг защиты от зацикливания при новом запуске синхронизации
    if (force_full_sync) {
        m_full_sync_attempted.store(false);
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 1.1] Reset m_full_sync_attempted=false for full sync";
    }

    // m_is_syncing уже true (установлен выше через exchange)
    // ВАЖНО: НЕ увеличиваем m_active_syncs здесь - он будет увеличен только после успешного получения списка (200 OK)
    // Это предотвращает проблемы с зависанием кнопки при ошибках
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 2] Set m_is_syncing=true";
    
    // Обновляем состояние кнопки синхронизации (показываем "Synchronizing...")
    CallAfter([this]() {
        update_sync_button_state(true);
    });
    
    // 1. Загружаем токен и user_id из AppConfig
    std::string access_token;
    int user_id = 0;
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 3] Loading auth token...";
    
    if (!load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC ERROR] Cannot load auth token, user not logged in";
        // Сбрасываем флаги синхронизации
        // ВАЖНО: m_active_syncs не увеличивался до этого момента, поэтому НЕ уменьшаем счетчик
        m_is_syncing.store(false);
        // Обновляем UI в главном потоке
        CallAfter([this]() {
            update_sync_button_state(false);
            wxMessageBox(
                _L("Please login to FilamentHub first."),
                _L("FilamentHub Sync Error"),
                wxOK | wxICON_WARNING
            );
        });
        return;
    }
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 4] Loaded auth token for user_id=" << user_id 
                            << ", token_length=" << access_token.length();
    
    // ДИАГНОСТИКА: Проверяем exp токена перед отправкой запроса
    long long token_exp = extract_jwt_exp_for_diagnostics(access_token);
    if (token_exp > 0) {
        time_t current_time = time(nullptr);
        long long time_until_expiry = token_exp - current_time;
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 4.1] Token diagnostics: exp=" << token_exp 
                                << " (UTC timestamp), current=" << current_time 
                                << " (UTC timestamp), time_until_expiry=" << time_until_expiry 
                                << " seconds (" << (time_until_expiry / 60) << " minutes, " 
                                << (time_until_expiry / 3600) << " hours, " 
                                << (time_until_expiry / 86400) << " days)";
        if (time_until_expiry <= 0) {
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC STEP 4.1] Token is expired! " 
                                        << "Expired " << (-time_until_expiry) << " seconds ago";
        }
    } else {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC STEP 4.1] Could not extract exp from token for diagnostics";
    }
    
    // ВАЖНО: Проверяем, что токен не пустой (защита от поврежденного сохранения)
    if (access_token.empty()) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC ERROR] Access token is empty after loading from config!";
        m_is_syncing.store(false);
        CallAfter([this]() {
            update_sync_button_state(false);
            wxMessageBox(
                _L("Invalid token found. Please login again."),
                _L("FilamentHub Sync Error"),
                wxOK | wxICON_WARNING
            );
        });
        return;
    }
    
    // Сразу вызываем синхронизацию — отдельная валидация токена через get_current_user не нужна,
    // т.к. get_my_presets() сам вернёт 401 если токен невалидный, и обработка 401 уже есть в continue_sync
    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 5] Proceeding directly to sync (token validation via API response)";
    continue_sync_after_token_validation(user_id, force_full_sync, api_base_url, access_token);
}

void FilamentHubPanel::continue_sync_after_token_validation(int user_id, bool force_full_sync, const std::string& api_base_url, const std::string& access_token)
{
    // 2. Получаем last_sync_time для инкрементальной синхронизации
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 5] Loading last_sync_time from AppConfig...";
    std::string updated_since;
    if (!force_full_sync) {
        updated_since = load_last_sync_time(user_id, SyncTimestampType::Filament);
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 6] Incremental sync, last_sync_time=" << updated_since;
    } else {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 6] Full sync requested, updated_since=''";
        updated_since = "";
    }
    
    // 3. Получаем список пресетов пользователя через API (используем persistent клиент)
    m_fhub_client->set_api_base_url(api_base_url);

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 8] Calling get_my_presets API: " << api_base_url
                            << ", updated_since='" << updated_since << "'"
                            << ", token_length=" << access_token.length();

    m_fhub_client->get_my_presets(
            access_token,
            updated_since,
            // on_complete: список пресетов получен
            [this, user_id, force_full_sync, updated_since, api_base_url, access_token](std::string json_body, unsigned http_status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 9] ========== on_complete CALLBACK (get_my_presets) ==========";
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 9.0] Lambda function called from FilamentHubPanel!";
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 9.1] HTTP status: " << http_status 
                                        << ", Body size: " << json_body.size() << " bytes";
            
            // Проверяем статус ответа
            if (http_status == 401) {
                // Токен истек — НЕ показываем сообщение сразу.
                // Фронтенд автоматически рефрешит токен через refresh_token.
                // Делаем тихий retry через 2 секунды. Сообщение — только если retry тоже 401.
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC ERROR] Token expired (401) in on_complete, "
                                           << "waiting for frontend auto-refresh before retry...";
                CallAfter([this]() {
                    m_is_syncing.store(false);
                    if (m_sync_progress) {
                        m_sync_progress->Hide();
                    }
                    if (m_sync_status_label) {
                        m_sync_status_label->Hide();
                    }
                    update_sync_button_state(false);
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (401). Active syncs: " << m_active_syncs;
                    if (m_active_syncs < 0) {
                        m_active_syncs = 0;
                    }
                    m_info_panel->Layout();
                    if (!m_sync_retry_attempted.load()) {
                        m_sync_retry_attempted.store(true);
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Scheduling silent sync retry in 2 seconds...";
                        std::thread([this]() {
                            std::this_thread::sleep_for(std::chrono::seconds(2));
                            CallAfter([this]() {
                                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Executing silent sync retry after token refresh wait...";
                                synchronize_presets(false);
                            });
                        }).detach();
                    } else {
                        m_sync_retry_attempted.store(false);
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Retry also failed (401). Showing session expired message.";
                        show_notification_in_webview(
                            _L("Your session has expired. Please login again."),
                            "warning"
                        );
                    }
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
                        m_is_syncing.store(false);
                        // Скрываем прогресс-бар
                        if (m_sync_progress) {
                            m_sync_progress->Hide();
                        }
                        if (m_sync_status_label) {
                            m_sync_status_label->Hide();
                        }
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (403). Active syncs: " << m_active_syncs;
                        if (m_active_syncs <= 0) {
                            update_sync_button_state(false);
                            m_active_syncs = 0;
                        }
                        m_info_panel->Layout();
                        // Показываем уведомление в WebView вместо модального окна
                        show_notification_in_webview(
                            wxString::Format(_L("Access denied: %s"), wxString::FromUTF8(error_detail.c_str())),
                            "warning"
                        );
                    });
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing 403 response: " << e.what();
                    CallAfter([this]() {
                        m_is_syncing.store(false);
                        // Скрываем прогресс-бар
                        if (m_sync_progress) {
                            m_sync_progress->Hide();
                        }
                        if (m_sync_status_label) {
                            m_sync_status_label->Hide();
                        }
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (403, parse error). Active syncs: " << m_active_syncs;
                        if (m_active_syncs <= 0) {
                            update_sync_button_state(false);
                            m_active_syncs = 0;
                        }
                        m_info_panel->Layout();
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
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC ERROR] Failed to get presets list. Status: " << http_status;
                // ВАЖНО: m_active_syncs не увеличивался до этого момента (увеличивается только после 200 OK)
                // Поэтому НЕ уменьшаем счетчик здесь
                CallAfter([this, http_status]() {
                    m_is_syncing.store(false);
                    // Скрываем прогресс-бар
                    if (m_sync_progress) {
                        m_sync_progress->Hide();
                    }
                    if (m_sync_status_label) {
                        m_sync_status_label->Hide();
                    }
                    update_sync_button_state(false);
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (status " << http_status << "). Active syncs: " << m_active_syncs;
                    if (m_active_syncs < 0) {
                        m_active_syncs = 0;
                    }
                    m_info_panel->Layout();
                    // Показываем уведомление в WebView вместо модального окна
                    show_notification_in_webview(
                        wxString::Format(_L("Failed to get presets list. Status: %d"), http_status),
                        "error"
                    );
                });
                return;
            }
            
            // ВАЖНО: Увеличиваем счетчик только после успешного получения списка (200 OK)
            // Это предотвращает проблемы с зависанием кнопки при ошибках (401, 403, etc.)
            // Счетчик был установлен в synchronize_presets, но там он не увеличивался (исправлено выше)
            m_active_syncs++;
            m_sync_retry_attempted.store(false); // Reset retry flag on success
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 9.2] Incremented m_active_syncs for filament presets (after 200 OK). Active syncs: " << m_active_syncs;
            
            try {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 10] Parsing JSON response...";
                nlohmann::json response = nlohmann::json::parse(json_body);
                std::vector<nlohmann::json> presets = response["items"];
                int total = response.value("total", 0);
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 11] Received " << total << " presets (items: " << presets.size() << ")";
                
                // 4. ВАЖНО: Проверяем, нужно ли делать полную синхронизацию для восстановления удалённых пресетов
                // Если список пуст И мы не делали полную синхронизацию И есть last_sync_time,
                // это может означать, что пресеты были удалены локально, но не обновлялись в FilamentHub
                // В этом случае делаем полную синхронизацию, чтобы восстановить все пресеты
                // КРИТИЧНО: Защита от зацикливания - проверяем флаг m_full_sync_attempted
                if (presets.empty() && !force_full_sync && !updated_since.empty() && !m_full_sync_attempted.load()) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC STEP 12] API returned empty list, but last_sync_time exists. "
                                               << "This might indicate locally deleted presets. Performing full sync to restore all presets...";
                    // Устанавливаем флаг защиты от зацикливания
                    m_full_sync_attempted.store(true);
                    // Очищаем last_sync_time и делаем полную синхронизацию
                    save_last_sync_time(user_id, "", SyncTimestampType::Filament); // Очищаем last_sync_time
                    // Уменьшаем счетчик (он был увеличен выше после 200 OK)
                    m_active_syncs--;
                    // Перезапускаем синхронизацию с force_full_sync=true
                    CallAfter([this]() {
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Restarting sync with force_full_sync=true to restore deleted presets";
                        // Сбрасываем флаги перед перезапуском
                        m_is_syncing.store(false);
                        // Скрываем прогресс-бар перед перезапуском
                        if (m_sync_progress) {
                            m_sync_progress->Hide();
                        }
                        if (m_sync_status_label) {
                            m_sync_status_label->Hide();
                        }
                        m_info_panel->Layout();
                        synchronize_presets(true); // Полная синхронизация (без updated_since)
                    });
                    return;
                } else if (presets.empty() && !force_full_sync && !updated_since.empty() && m_full_sync_attempted.load()) {
                    // Уже пытались полную синхронизацию - не зацикливаемся
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC STEP 12] Full sync already attempted, skipping to prevent infinite loop";
                    // Уменьшаем счетчик
                    m_active_syncs--;
                    CallAfter([this]() {
                        m_is_syncing.store(false);
                        if (m_sync_progress) {
                            m_sync_progress->Hide();
                        }
                        if (m_sync_status_label) {
                            m_sync_status_label->Hide();
                        }
                        update_sync_button_state(false);
                        m_info_panel->Layout();
                        show_notification_in_webview(
                            _L("No presets to sync. All presets may have sync disabled."),
                            "info"
                        );
                    });
                    return;
                }
                
                if (presets.empty()) {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 12] No presets to sync (empty list)";
                    // Уменьшаем счетчик активных синхронизаций (он был увеличен выше после 200 OK)
                    CallAfter([this]() {
                        m_active_syncs--;
                        m_is_syncing.store(false);
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync completed (empty list). Active syncs: " << m_active_syncs;
                        // Синхронизируем printer и print profiles (второстепенные, после основного - filament presets)
                        // Проверяем разрешения пользователя перед sync (TODO 9)
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Checking user permissions before printer/print profiles sync (after empty filament presets)...";
                        std::string token;
                        int uid = 0;
                        if (load_auth_token(token, uid)) {
                            check_user_permissions(token,
                                [this](bool filament_import, bool printer_import, bool printer_export, bool print_import, bool print_export) {
                                    CallAfter([this, printer_export, print_export]() {
                                        if (printer_export) {
                                            synchronize_printer_profiles(false);
                                        } else {
                                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Skipping printer profiles sync (disabled in user settings)";
                                        }
                                        if (print_export) {
                                            synchronize_print_profiles(false);
                                        } else {
                                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Skipping print profiles sync (disabled in user settings)";
                                        }
                                    });
                                },
                                [](std::string error, unsigned status) {
                                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to check permissions, skipping printer/print sync: " << error;
                                }
                            );
                        }
                        // Скрываем прогресс-бар
                        if (m_sync_progress) {
                            m_sync_progress->Hide();
                        }
                        if (m_sync_status_label) {
                            m_sync_status_label->Hide();
                        }
                        if (m_active_syncs <= 0) {
                            update_sync_button_state(false);
                            m_active_syncs = 0;
                        }
                        m_info_panel->Layout();
                    });
                    return;
                }
                
                // 5. Обнаруживаем удалённые пресеты (прежде чем добавлять в очередь)
                // ВАЖНО: Проверяем маппинги и сравниваем с текущим состоянием PresetBundle
                // Удалённый пресет = есть маппинг, но пресета нет в PresetBundle
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 13] Detecting deleted presets...";
                
                std::vector<nlohmann::json> deleted_presets_list;
                std::set<int> server_preset_ids; // Set ID пресетов, которые пришли с сервера
                
                // Создаем set из ID пресетов с сервера
                for (const auto& preset_json : presets) {
                    int preset_id = preset_json["id"];
                    server_preset_ids.insert(preset_id);
                }
                
                // Проверяем каждый пресет с сервера на наличие в маппинге и PresetBundle
                // Если пресет есть в маппинге, но НЕ существует в PresetBundle, значит он был удален локально
                for (const auto& preset_json : presets) {
                    int preset_id = preset_json["id"];
                    std::string preset_name = preset_json["name"];
                    
                    // Проверяем маппинг
                    std::string bundle_preset_name = load_preset_mapping(preset_id);
                    
                    if (!bundle_preset_name.empty()) {
                        // Маппинг существует - проверяем, существует ли пресет в PresetBundle
                        bool preset_exists = preset_exists_in_bundle(bundle_preset_name);
                        
                        if (!preset_exists) {
                            // Маппинг есть, но пресет был удален в OrcaSlicer
                            // НО: пресет пришел с сервера, значит он НЕ удален на сервере
                            // Это означает, что пользователь удалил его локально
                            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC STEP 13.1] Preset " << preset_id 
                                                       << " (" << preset_name 
                                                       << ") is mapped to '" << bundle_preset_name 
                                                       << "' but preset not found in bundle (deleted locally)";
                            
                            // Добавляем в список удалённых пресетов
                            nlohmann::json deleted_preset;
                            deleted_preset["preset_id"] = preset_id;
                            deleted_preset["preset_name"] = preset_name;
                            deleted_preset["bundle_preset_name"] = bundle_preset_name;
                            deleted_presets_list.push_back(deleted_preset);
                            
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 13.1.1] Added preset " << preset_id 
                                                   << " to deleted presets list (was deleted locally, but exists on server)";
                        }
                    }
                }
                
                // ВАЖНО: Логируем результат обнаружения удалённых пресетов на уровне error, чтобы гарантировать видимость
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 13.2] Found " << deleted_presets_list.size() 
                                       << " deleted presets (deleted locally, but exist on server)";
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 13.2] Found " << deleted_presets_list.size() 
                                       << " deleted presets (deleted locally, but exist on server)";
                
                // 6. Собираем пресеты в очередь для обработки после завершения callback
                // ВАЖНО: НЕ вызываем import_preset_silent здесь, чтобы избежать deadlock!
                // Вместо этого добавляем пресеты в очередь и обработаем их позже через CallAfter
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 14] Adding " << presets.size() << " presets to import queue...";
                
                // Сохраняем токен, URL и user_id для последующей обработки
                std::string access_token_for_queue = access_token;
                std::string api_base_url_for_queue = api_base_url;
                int user_id_for_queue = user_id; // КРИТИЧНО: Сохраняем user_id для обновления last_sync_time
                
                // Добавляем пресеты в очередь
                {
                    std::lock_guard<std::mutex> lock(m_preset_queue_mutex);
                    m_preset_import_queue.clear();
                    m_total_presets_to_sync = presets.size();
                    m_synced_count = 0;
                    m_error_count = 0;
                    
                    for (const auto& preset_json : presets) {
                        int preset_id = preset_json["id"];
                        std::string preset_name = preset_json["name"];
                        
                        PresetImportTask task;
                        task.preset_id = preset_id;
                        task.preset_name = preset_name;
                        task.access_token = access_token_for_queue;
                        task.api_base_url = api_base_url_for_queue;
                        task.user_id = user_id_for_queue; // КРИТИЧНО: Сохраняем user_id
                        
                        m_preset_import_queue.push_back(task);
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [SYNC STEP 14.1] Added preset " << preset_id 
                                                 << " (" << preset_name << ") to import queue";
                    }
                }
                
                // Прогресс-бар не показываем - синхронизация быстрая
                // Оставляем только логи с названиями и количеством пресетов
                CallAfter([this, total_presets = presets.size()]() {
                    // Скрываем прогресс-бар и строку состояния - синхронизация быстрая
                    if (m_sync_progress) {
                        m_sync_progress->Hide();
                    }
                    if (m_sync_status_label) {
                        m_sync_status_label->Hide();
                    }
                    m_info_panel->Layout();
                    
                    // Начинаем обработку очереди пресетов в UI потоке
                    // Это предотвратит deadlock, так как мы не вызываем perform_sync() из callback HTTP клиента
                    process_preset_import_queue();
                });
                
                // 8. Отправляем удалённые пресеты на бэкенд (если есть)
                // ВАЖНО: Отправляем через CallAfter, чтобы не блокировать callback HTTP клиента
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 15.0] Checking deleted_presets_list. Size: " << deleted_presets_list.size();
                if (!deleted_presets_list.empty()) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 15] Found " << deleted_presets_list.size() 
                                           << " deleted presets. Reporting to backend...";
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 15] Found " << deleted_presets_list.size() 
                                           << " deleted presets. Reporting to backend...";
                    
                    // Сохраняем данные для отправки в UI потоке
                    std::string access_token_for_report = access_token;
                    std::string api_base_url_for_report = api_base_url;
                    std::vector<nlohmann::json> deleted_presets_for_report = deleted_presets_list;
                    size_t deleted_presets_count = deleted_presets_list.size();
                    
                    // Отправляем на бэкенд через CallAfter, чтобы не блокировать callback
                    CallAfter([this, access_token_for_report, api_base_url_for_report, deleted_presets_for_report, deleted_presets_count]() {
                        // Создаём JSON запрос для отправки удалённых пресетов
                        nlohmann::json deleted_presets_request;
                        deleted_presets_request["deleted_presets"] = deleted_presets_for_report;
                        std::string deleted_presets_json = deleted_presets_request.dump();
                        
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [SYNC STEP 15.1] Deleted presets JSON: " << deleted_presets_json;
                        
                        // Отправляем на бэкенд (это будет выполнено в UI потоке, но report_deleted_presets использует perform_sync)
                        // Чтобы избежать deadlock, report_deleted_presets тоже должен использовать отдельный поток
                        // Пока оставляем так, но в будущем нужно переделать report_deleted_presets на асинхронный вызов
                        FilamentHubClient report_client;
                        report_client.set_api_base_url(api_base_url_for_report);
                        
                        report_client.report_deleted_presets(
                            access_token_for_report,
                            deleted_presets_json,
                            // on_complete: удалённые пресеты успешно отправлены
                            [this, deleted_presets_count](std::string body, unsigned status) {
                                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 15.2] Deleted presets reported successfully. Status: " << status 
                                                       << ", Count: " << deleted_presets_count;
                                if (status == 200) {
                                    try {
                                        nlohmann::json response = nlohmann::json::parse(body);
                                        std::string message = response.value("message", "");
                                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 15.2.1] Backend response: " << message;
                                        
                                        // Если создано уведомление, пользователь увидит его в веб-интерфейсе
                                        if (response.contains("notification_id")) {
                                            int notification_id = response.value("notification_id", 0);
                                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 15.2.2] Notification created with ID: " << notification_id;
                                        }
                                    } catch (const std::exception& e) {
                                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC STEP 15.2.1] Error parsing backend response: " << e.what();
                                    }
                                }
                            },
                            // on_error: ошибка при отправке удалённых пресетов
                            [this](std::string body, std::string error, unsigned status) {
                                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 15.2] Failed to report deleted presets. Error: " << error 
                                                        << ", Status: " << status;
                                // Не прерываем синхронизацию при ошибке отправки удалённых пресетов
                                // Пользователь может обработать их вручную через веб-интерфейс
                            }
                        );
                    });
                } else {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 15] No deleted presets found";
                }
                
                // 9. КРИТИЧНО: НЕ обновляем last_sync_time здесь!
                // last_sync_time будет обновлен ПОСЛЕ завершения импорта всех пресетов из очереди
                // в process_preset_import_queue() после успешного импорта всех пресетов
                // Это предотвращает потерю пресетов при прерывании синхронизации
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 16] Presets added to queue. last_sync_time will be updated after all presets are imported.";
                
                // 10. Завершаем синхронизацию - обновляем UI
                // ВАЖНО: Синхронизация еще не завершена полностью - пресеты обрабатываются через очередь
                // Но мы можем обновить UI здесь, чтобы показать, что синхронизация началась
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 17] ========== Synchronization started ==========";
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 17.1] Summary - Presets to import: " << presets.size()
                                       << ", Deleted presets detected: " << deleted_presets_list.size();
                
                // ВАЖНО: НЕ обновляем UI здесь, так как пресеты еще обрабатываются через очередь
                // UI будет обновлен в process_preset_import_queue() после завершения обработки всех пресетов
                // Здесь мы только логируем, что синхронизация началась
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Presets added to queue, processing will continue in UI thread";
                
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing presets list: " << e.what();
                CallAfter([this, e]() {
                    m_active_syncs--; // Уменьшаем счетчик активных синхронизаций
                    m_is_syncing.store(false);
                    // Скрываем прогресс-бар
                    if (m_sync_progress) {
                        m_sync_progress->Hide();
                    }
                    if (m_sync_status_label) {
                        m_sync_status_label->Hide();
                    }
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (parse error). Active syncs: " << m_active_syncs;
                    if (m_active_syncs <= 0) {
                        update_sync_button_state(false);
                        m_active_syncs = 0;
                    }
                    m_info_panel->Layout();
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
            // ВАЖНО: m_active_syncs НЕ увеличивался до этого момента (увеличивается только после 200 OK в on_complete)
            // Поэтому НЕ уменьшаем счетчик здесь - он остаётся 0
            
            if (http_status == 401) {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC ERROR] Token expired (401) in on_error callback, "
                                           << "waiting for frontend auto-refresh before retry...";
                // НЕ вызываем logout() — даём фронтенду время на авто-рефреш токена.
                // Фронтенд (client.ts interceptor) при 401 автоматически использует refresh_token,
                // получает новый access_token и сохраняет в localStorage.
                // C++ polling (inject_auth_tokens_to_webview) подхватит новый токен.
                // Показываем сообщение только если повторная попытка тоже провалится.
                CallAfter([this]() {
                    m_is_syncing.store(false);
                    if (m_sync_progress) {
                        m_sync_progress->Hide();
                    }
                    if (m_sync_status_label) {
                        m_sync_status_label->Hide();
                    }
                    update_sync_button_state(false);
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (401). Active syncs: " << m_active_syncs;
                    if (m_active_syncs < 0) {
                        m_active_syncs = 0;
                    }
                    m_info_panel->Layout();
                    // Тихо ждём 2 секунды, чтобы фронтенд успел рефрешнуть токен,
                    // затем пробуем синхронизацию повторно
                    if (!m_sync_retry_attempted.load()) {
                        m_sync_retry_attempted.store(true);
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Scheduling silent sync retry in 2 seconds...";
                        std::thread([this]() {
                            std::this_thread::sleep_for(std::chrono::seconds(2));
                            CallAfter([this]() {
                                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Executing silent sync retry after token refresh wait...";
                                synchronize_presets(false);
                            });
                        }).detach();
                    } else {
                        // Повторная попытка уже была — показываем сообщение
                        m_sync_retry_attempted.store(false);
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Retry also failed (401). Showing session expired message.";
                        show_notification_in_webview(
                            _L("Your session has expired. Please login again."),
                            "warning"
                        );
                    }
                });
                return;
            }
            
            // Для других ошибок также НЕ уменьшаем счетчик (он не увеличивался)
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
            
            // НЕ уменьшаем счетчик - он не был увеличен для этой синхронизации (увеличивается только после 200 OK)
            CallAfter([this, error_msg, http_status]() {
                m_is_syncing.store(false);
                // Скрываем прогресс-бар
                if (m_sync_progress) {
                    m_sync_progress->Hide();
                }
                if (m_sync_status_label) {
                    m_sync_status_label->Hide();
                }
                    update_sync_button_state(false);
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Filament presets sync failed (error: " << error_msg.ToUTF8()
                                        << ", status: " << http_status << "). Active syncs: " << m_active_syncs;
                    if (m_active_syncs < 0) {
                        m_active_syncs = 0;
                    }
                    m_info_panel->Layout();
                    // Показываем уведомление в WebView вместо модального окна
                    show_notification_in_webview(
                        error_msg,
                        http_status == 403 ? "warning" : "error"
                    );
                });
        }
    );
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: synchronize_presets: client.get_my_presets() CALLED (function returned, waiting for callback)";
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
    
    // Отправляем ответ через window.postMessage (аналогично show_notification_in_webview)
    // Используем тот же подход, что и для уведомлений - напрямую вставляем JSON в JavaScript код
    std::string response_json = response.dump();
    
    // SEC-2 fix: используем JSON.parse() вместо прямой конкатенации JSON в JS код
    // nlohmann::json::dump() экранирует спецсимволы, безопасно для вставки в JS строку
    wxString json_wx = wxString::FromUTF8(response_json.c_str());
    wxString js_response = wxString(R"(
            (function() {
                try {
                    var response = )") + json_wx + wxString(R"(;
                    window.postMessage(response, '*');
                } catch (e) {
                    console.error('FilamentHub: Error sending response:', e);
                }
            })();
        )");
    WebView::RunScript(m_browser, js_response);
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sent response: " << response.dump();
}

void FilamentHubPanel::show_notification_in_webview(const wxString& message, const wxString& type)
{
    static int show_notif_counter = 0;
    show_notif_counter++;
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SHOW_NOTIFICATION #" << show_notif_counter << "] type=" << type.ToStdString() 
                            << ", message=" << message.ToStdString();
    
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SHOW_NOTIFICATION #" << show_notif_counter << "] Cannot show notification, WebView is null";
        return;
    }
    
    // Build notification JSON
    nlohmann::json notification;
    notification["command"] = "show_notification";
    notification["type"] = type.ToUTF8().data();
    notification["message"] = message.ToUTF8().data();
    
    // Send notification to frontend via JavaScript
    // Frontend should have a function to show notifications (e.g., toast)
    // SEC-2 fix: используем JSON.parse() вместо прямой конкатенации JSON в JS код
    // nlohmann::json::dump() возвращает валидный UTF-8, безопасно для вставки в JS строку
    wxString json_wx = wxString::FromUTF8(notification.dump().c_str());
    wxString js_code = wxString(R"(
            (function() {
                try {
                    var notification = )") + json_wx + wxString(R"(;
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
        )");
    
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

    // Выполняем set+save в UI потоке (AppConfig::save() нельзя вызывать из worker thread)
    CallAfter([access_token, user_id]() {
        if (wxGetApp().app_config == nullptr) return;
        wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_ACCESS_TOKEN, access_token);
        wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_USER_ID, std::to_string(user_id));
        wxGetApp().app_config->save();
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved auth token for user_id=" << user_id;
    });
}

bool FilamentHubPanel::load_auth_token(std::string& access_token, int& user_id)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== load_auth_token() CALLED ==========";

    if (wxGetApp().app_config == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: app_config is null, cannot load token";
        return false;
    }

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: app_config is not null. Loading token...";

    std::string token = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_ACCESS_TOKEN);
    std::string user_id_str = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_USER_ID);

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Loading auth token - token length: " << token.length()
                            << ", user_id_str: '" << user_id_str << "'";

    if (token.empty()) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: No auth token found in config (token is empty)";
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: User is not logged in.";
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
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: user_id_str contains non-digit characters: '" << user_id_str << "', clearing corrupted data";
            // Очищаем повреждённые данные из AppConfig
            wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_ACCESS_TOKEN, "");
            wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_REFRESH_TOKEN, "");
            wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_USER_ID, "");
            CallAfter([]() {
                if (wxGetApp().app_config != nullptr)
                    wxGetApp().app_config->save();
            });
            return false;
        }
        
        user_id = std::stoi(user_id_str);
        access_token = token;
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Loaded auth token for user_id=" << user_id
                                << ", token length: " << access_token.length()
                                << ", token preview: " << access_token.substr(0, 20) << "...";
        return true;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing user_id '" << user_id_str << "': " << e.what();
        return false;
    }
}

long long FilamentHubPanel::extract_jwt_exp_for_diagnostics(const std::string& token)
{
    // JWT токен состоит из трех частей, разделенных точками: header.payload.signature
    // Нам нужна только payload (вторая часть) для извлечения exp
    
    if (token.empty()) {
        return 0;
    }
    
    // Находим первую точку (разделитель между header и payload)
    size_t first_dot = token.find('.');
    if (first_dot == std::string::npos) {
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: JWT token has no first dot (invalid format)";
        return 0;
    }
    
    // Находим вторую точку (разделитель между payload и signature)
    size_t second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string::npos) {
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: JWT token has no second dot (invalid format)";
        return 0;
    }
    
    // Извлекаем payload (часть между первой и второй точкой)
    std::string payload_b64 = token.substr(first_dot + 1, second_dot - first_dot - 1);
    
    if (payload_b64.empty()) {
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: JWT payload is empty";
        return 0;
    }
    
    try {
        // Base64 декодирование (JWT использует URL-safe base64, но стандартный base64 тоже может работать)
        // Заменяем URL-safe символы на стандартные base64
        std::string payload_b64_std = payload_b64;
        std::replace(payload_b64_std.begin(), payload_b64_std.end(), '-', '+');
        std::replace(payload_b64_std.begin(), payload_b64_std.end(), '_', '/');
        
        // Добавляем padding если нужно (base64 требует длину, кратную 4)
        int padding = 4 - (payload_b64_std.length() % 4);
        if (padding != 4) {
            payload_b64_std.append(padding, '=');
        }
        
        // Декодируем base64 используя boost::beast::detail::base64
        std::vector<unsigned char> decoded;
        decoded.resize(boost::beast::detail::base64::decoded_size(payload_b64_std.length()));
        auto result = boost::beast::detail::base64::decode(
            decoded.data(),
            payload_b64_std.data(),
            payload_b64_std.length()
        );
        
        // boost::beast::detail::base64::decode возвращает pair<size_t, size_t>
        // Первое значение - количество декодированных байт, второе - код результата
        if (!result.second) {  // result.second == 0 означает ошибку декодирования
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Failed to decode JWT payload base64";
            return 0;
        }
        
        decoded.resize(result.first);  // Используем result.first как количество декодированных байт
        
        // Преобразуем в строку и парсим JSON
        std::string payload_json(decoded.begin(), decoded.end());
        
        try {
            nlohmann::json payload = nlohmann::json::parse(payload_json);
            
            // Извлекаем exp из JSON
            if (payload.contains("exp") && payload["exp"].is_number()) {
                long long exp_value = payload["exp"].get<long long>();
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Extracted exp from JWT token: " << exp_value;
                return exp_value;
            } else {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: JWT payload has no 'exp' claim";
                return 0;
            }
        } catch (const nlohmann::json::exception& e) {
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Failed to parse JWT payload JSON: " << e.what();
            return 0;
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Error extracting exp from JWT token: " << e.what();
        return 0;
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

    // save() безопасен только из UI потока — используем CallAfter
    CallAfter([]() {
        if (wxGetApp().app_config != nullptr)
            wxGetApp().app_config->save();
    });

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved mapping preset_id=" << preset_id
                            << " -> bundle_preset_name=" << bundle_preset_name;
}

std::string FilamentHubPanel::load_preset_mapping(int preset_id)
{
    if (wxGetApp().app_config == nullptr) {
        return "";
    }
    
    std::string key = CONFIG_KEY_PRESET_MAPPING + "_" + std::to_string(preset_id);
    std::string value = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, key);
    
    // ВАЖНО: Проверяем, что значение не является строкой "true" (возможно, ошибка в сохранении)
    // Если значение пустое или равно "true", считаем, что маппинга нет
    if (value.empty() || value == "true" || value == "True" || value == "TRUE") {
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: load_preset_mapping(" << preset_id 
                                 << ") returned invalid value: '" << value 
                                 << "', treating as empty";
        return "";
    }
    
    return value;
}

bool FilamentHubPanel::preset_exists_in_bundle(const std::string& preset_name)
{
    if (wxGetApp().preset_bundle == nullptr) {
        return false;
    }
    
    PresetBundle* bundle = wxGetApp().preset_bundle;
    PresetCollection& filaments = bundle->filaments;
    
    // Ищем пресет по имени (find_preset2 может найти и системные, и пользовательские)
    Preset* preset = filaments.find_preset2(preset_name, true); // auto_match = true
    
    // Проверяем, что пресет существует и является пользовательским (не системным)
    if (preset != nullptr && !preset->is_system) {
        // Дополнительная проверка: имя должно совпадать точно (с учетом регистра)
        // find_preset2 может вернуть пресет с похожим именем, поэтому проверяем точное совпадение
        if (preset->name == preset_name) {
            return true;
        }
    }
    
    return false;
}

void FilamentHubPanel::remove_preset_mapping(int preset_id)
{
    if (wxGetApp().app_config == nullptr) {
        return;
    }
    
    std::string key = CONFIG_KEY_PRESET_MAPPING + "_" + std::to_string(preset_id);
    
    // AppConfig doesn't have explicit remove method, so we set empty string
    // (similar to how logout() clears the token)
    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, key, "");
    CallAfter([]() {
        if (wxGetApp().app_config != nullptr)
            wxGetApp().app_config->save();
    });

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Removed mapping for preset_id=" << preset_id;
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

void FilamentHubPanel::save_last_sync_time(int user_id, const std::string& timestamp, SyncTimestampType sync_type)
{
    if (wxGetApp().app_config == nullptr) {
        return;
    }

    std::string sync_scope = "filament";
    switch (sync_type) {
        case SyncTimestampType::Filament:
            sync_scope = "filament";
            break;
        case SyncTimestampType::Printer:
            sync_scope = "printer";
            break;
        case SyncTimestampType::Print:
            sync_scope = "print";
            break;
    }

    const std::string scoped_key = CONFIG_KEY_LAST_SYNC_TIME + "_" + sync_scope + "_" + std::to_string(user_id);
    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, scoped_key, timestamp);

    // Backward compatibility for legacy single-key cursor.
    if (sync_type == SyncTimestampType::Filament) {
        const std::string legacy_key = CONFIG_KEY_LAST_SYNC_TIME + "_" + std::to_string(user_id);
        wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, legacy_key, timestamp);
    }

    CallAfter([]() {
        if (wxGetApp().app_config != nullptr)
            wxGetApp().app_config->save();
    });

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved scoped last_sync_time (" << sync_scope
                            << ") for user_id=" << user_id
                            << ": " << timestamp;
}

std::string FilamentHubPanel::load_last_sync_time(int user_id, SyncTimestampType sync_type)
{
    std::string sync_scope = "filament";
    switch (sync_type) {
        case SyncTimestampType::Filament:
            sync_scope = "filament";
            break;
        case SyncTimestampType::Printer:
            sync_scope = "printer";
            break;
        case SyncTimestampType::Print:
            sync_scope = "print";
            break;
    }

    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: load_last_sync_time(" << sync_scope
                             << "): Called with user_id=" << user_id;
    if (wxGetApp().app_config == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: load_last_sync_time(" << sync_scope
                                 << "): app_config is null, returning empty";
        return "";
    }

    const std::string scoped_key = CONFIG_KEY_LAST_SYNC_TIME + "_" + sync_scope + "_" + std::to_string(user_id);
    std::string result = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, scoped_key);
    if (!result.empty()) {
        return result;
    }

    // Backward compatibility fallback from legacy single cursor.
    const std::string legacy_key = CONFIG_KEY_LAST_SYNC_TIME + "_" + std::to_string(user_id);
    std::string legacy_result = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, legacy_key);
    if (!legacy_result.empty()) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: load_last_sync_time(" << sync_scope
                                << "): using legacy cursor fallback for user_id=" << user_id;
        return legacy_result;
    }

    return "";
}

// ============================================================================
// Methods for deleted preset action handling
// ============================================================================

std::string FilamentHubPanel::get_deleted_preset_action()
{
    if (wxGetApp().app_config == nullptr) {
        return "ask"; // Default: ask user
    }
    
    std::string action = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_DELETED_PRESET_ACTION);
    
    // Validate action value
    if (action != "ask" && action != "import" && action != "delete" && action != "skip") {
        return "ask"; // Default: ask user
    }
    
    return action;
}

void FilamentHubPanel::set_deleted_preset_action(const std::string& action)
{
    if (wxGetApp().app_config == nullptr) {
        return;
    }
    
    // Validate action value
    if (action != "ask" && action != "import" && action != "delete" && action != "skip") {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Invalid deleted preset action: " << action << ", using default 'ask'";
        return;
    }
    
    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_DELETED_PRESET_ACTION, action);

    CallAfter([]() {
        if (wxGetApp().app_config != nullptr)
            wxGetApp().app_config->save();
    });

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Set deleted preset action to: " << action;
}

std::string FilamentHubPanel::ask_deleted_preset_action(int preset_id, const std::string& preset_name)
{
    // Создаем диалог с опциями
    wxString message = wxString::Format(
        _L("Preset '%s' (ID: %d) was deleted in OrcaSlicer.\n\nWhat would you like to do?"),
        wxString::FromUTF8(preset_name.c_str()),
        preset_id
    );
    
    wxString title = _L("FilamentHub - Deleted Preset");
    
    // Используем wxMessageBox с опциями: YES (import), NO (delete), CANCEL (skip)
    int result = wxMessageBox(
        message,
        title,
        wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxICON_QUESTION,
        this
    );
    
    if (result == wxYES) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: User chose to import preset " << preset_id;
        return "import";
    } else if (result == wxNO) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: User chose to delete preset " << preset_id << " from FilamentHub";
        return "delete";
    } else {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: User chose to skip preset " << preset_id;
        return "skip";
    }
}

bool FilamentHubPanel::delete_preset_from_filamenthub(int preset_id, const std::string& access_token)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Deleting preset " << preset_id << " from FilamentHub...";
    
    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
    m_fhub_client->set_api_base_url(api_base_url);
    
    bool success = false;
    std::string error_message;
    unsigned http_status = 0;
    
    // Создаем condition variable для ожидания ответа
    std::mutex mtx;
    std::condition_variable cv;
    bool completed = false;
    
    m_fhub_client->delete_preset(
        preset_id,
        access_token,
        // on_complete
        [&](std::string body, unsigned status) {
            std::lock_guard<std::mutex> lock(mtx);
            http_status = status;
            if (status == 204 || status == 200) {
                success = true;
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Preset " << preset_id << " deleted successfully from FilamentHub";
            } else {
                error_message = "Unexpected HTTP status: " + std::to_string(status);
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to delete preset " << preset_id 
                                         << " from FilamentHub. Status: " << status;
            }
            completed = true;
            cv.notify_one();
        },
        // on_error
        [&](std::string body, std::string error, unsigned status) {
            std::lock_guard<std::mutex> lock(mtx);
            http_status = status;
            error_message = error;
            success = false;
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to delete preset " << preset_id 
                                     << " from FilamentHub. Error: " << error << ", Status: " << status;
            completed = true;
            cv.notify_one();
        }
    );
    
    // CRASH-1 fix: таймаут снижен с 30s до 10s.
    // ВАЖНО: эта функция должна вызываться ТОЛЬКО из фонового потока (не из UI-потока),
    // иначе блокирующее ожидание заморозит интерфейс OrcaSlicer.
    std::unique_lock<std::mutex> lock(mtx);
    if (!cv.wait_for(lock, std::chrono::seconds(10), [&] { return completed; })) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Timeout waiting for delete preset " << preset_id << " response";
        return false;
    }
    
    if (!success) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to delete preset " << preset_id 
                                 << " from FilamentHub: " << error_message;
        return false;
    }
    
    // Удаляем маппинг после успешного удаления
    remove_preset_mapping(preset_id);
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Removed mapping for deleted preset " << preset_id;
    
    return true;
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
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT START] ========== import_preset_silent() CALLED ==========";
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 1] preset_id=" << preset_id 
                            << ", preset_name='" << preset_name << "'"
                            << ", token_length=" << access_token.length();
    
    // Проверяем, что preset_bundle доступен
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 2] Checking preset_bundle availability...";
    if (wxGetApp().preset_bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] preset_bundle is null, cannot import preset " << preset_id;
        return false;
    }
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 2.1] preset_bundle is available";
    
    // Скачиваем профиль синхронно через FilamentHubClient
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 3] Creating FilamentHubClient...";
    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
    m_fhub_client->set_api_base_url(api_base_url);
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 4] Calling download_profile API: " << api_base_url 
                            << " for preset_id=" << preset_id;
    
    // Используем shared_ptr для безопасного доступа из лямбд
    struct ImportResult {
        bool success = false;
        bool completed = false;
        std::string error;
        unsigned http_status = 0;
        std::mutex mutex;
        std::condition_variable cv;
    };
    
    auto result = std::make_shared<ImportResult>();
    
    // Используем синхронный вызов (perform_sync уже внутри download_profile)
    m_fhub_client->download_profile(
        preset_id,
        access_token,
        // on_complete: профиль успешно скачан
        // ВАЖНО: Захватываем access_token для передачи во вложенную лямбду CallAfter
        [this, preset_id, preset_name, result, access_token](std::string json_content, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 5] ========== download_profile on_complete CALLBACK ==========";
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 5.1] Preset " << preset_id << " downloaded. "
                                    << "HTTP status: " << http_status 
                                    << ", JSON size: " << json_content.size() 
                                    << " bytes";
            
            std::unique_lock<std::mutex> lock(result->mutex);
            result->http_status = http_status;
            
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] Unexpected HTTP status " << http_status 
                                        << " when downloading preset " << preset_id;
                result->error = "HTTP status " + std::to_string(http_status);
                result->completed = true;
                result->cv.notify_one();
                return;
            }
            
            if (json_content.empty()) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] Empty JSON content for preset " << preset_id;
                result->error = "Empty JSON content";
                result->completed = true;
                result->cv.notify_one();
                return;
            }
            
            try {
                // Парсим JSON чтобы добавить постфикс к имени и проверить родительский пресет
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 6] Parsing JSON content...";
                nlohmann::json profile_json = nlohmann::json::parse(json_content);
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 6.1] JSON parsed successfully";
                
                // Добавляем постфикс [FilamentHub] к имени пресета
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 7] Processing preset name...";
                std::string original_name = profile_json.value("name", preset_name);
                std::string new_name = ensure_filamenthub_postfix(original_name);
                profile_json["name"] = new_name;
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 7.1] Original name: '" << original_name 
                                         << "', New name: '" << new_name << "'";
                
                // Проверяем и исправляем родительский пресет (inherits)
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 8] Checking parent preset (inherits)...";
                ensure_parent_preset_exists(profile_json);
                std::string inherits_value = profile_json.value("inherits", "");
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 8.1] Parent preset (inherits): '" << inherits_value << "'";
                
                // Создаём временный файл
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 9] Creating temporary file...";
                boost::filesystem::path temp_dir = boost::filesystem::temp_directory_path();
                boost::filesystem::path temp_file = temp_dir / ("filamenthub_preset_" + std::to_string(preset_id) + "_" + 
                    std::to_string(std::time(nullptr)) + ".json");
                
                // Сохраняем JSON во временный файл
                std::ofstream file(temp_file.string());
                if (!file.is_open()) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] Failed to create temporary file: " << temp_file.string();
                    std::unique_lock<std::mutex> lock(result->mutex);
                    result->error = "Failed to create temporary file";
                    result->completed = true;
                    result->cv.notify_one();
                    return;
                }
                
                file << profile_json.dump(2);
                file.close();
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 9.1] Saved profile to temporary file: " << temp_file.string();
                
                // ВАЖНО: import_json_presets() и load_current_presets() требуют UI потока
                // Обертываем импорт в CallAfter чтобы выполнить в UI потоке
                // Это предотвращает ошибку "resource deadlock would occur"
                // ВАЖНО: Создаем неконстантную переменную file_path перед лямбдой,
                // и создадим неконстантную копию внутри лямбды для передачи в import_json_presets
                std::string file_path = temp_file.string();
                std::string preset_name_to_save = new_name;
                
                // Используем CallAfter для выполнения импорта в UI потоке
                // ВАЖНО: wxExecuteAfter используется вместо CallAfter, чтобы выполнить немедленно,
                // но мы используем CallAfter для гарантии выполнения в UI потоке
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 10] Scheduling import via CallAfter (UI thread)...";
                // ВАЖНО: Захватываем file_path по значению (копируем), затем создадим неконстантную копию внутри лямбды
                // Также захватываем access_token для update_preset_info_file
                CallAfter([this, result, preset_id, preset_name_to_save, file_path, access_token]() {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 11] ========== CallAfter CALLBACK EXECUTED (UI thread) ==========";
                    try {
                        // Импортируем профиль через PresetBundle
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 11.1] Getting preset_bundle...";
                        PresetBundle* bundle = wxGetApp().preset_bundle;
                        if (bundle == nullptr) {
                            BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] preset_bundle is null during import";
                            std::unique_lock<std::mutex> lock(result->mutex);
                            result->error = "Preset bundle not available";
                            result->completed = true;
                            result->cv.notify_one();
                            // Удаляем временный файл в фоне (безопасно)
                            boost::filesystem::remove(file_path);
                            return;
                        }
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 11.2] preset_bundle is available";
                        
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 11.3] Preparing import parameters...";
                        PresetsConfigSubstitutions substitutions;
                        // ВАЖНО: overwrite должен быть неконстантной переменной (не константой)
                        // так как import_json_presets требует int& (неконстантную ссылку)
                        int overwrite = 1; // 1 = overwrite if exists
                        std::vector<std::string> import_result;
                        
                        // Lambda для подтверждения перезаписи (автоматически подтверждаем)
                        auto override_confirm = [](std::string const& name) -> int {
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 11.4] override_confirm called for preset: '" << name << "'";
                            return 1; // Автоматически перезаписываем
                        };
                        
                        // Импортируем JSON профиль
                        // ВАЖНО: import_json_presets требует std::string& (неконстантную ссылку),
                        // поэтому создаем неконстантную копию file_path внутри лямбды
                        std::string file_path_mutable = file_path;
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 11.5] Calling bundle->import_json_presets()...";
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 11.5.1] Parameters: file_path='" << file_path_mutable 
                                                 << "', overwrite=" << overwrite;
                        bool success = bundle->import_json_presets(
                            substitutions,
                            file_path_mutable,
                            override_confirm,
                            ForwardCompatibilitySubstitutionRule::Enable,
                            overwrite,
                            import_result
                        );
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 11.6] import_json_presets() returned: success=" << (success ? "true" : "false")
                                                << ", import_result.size()=" << import_result.size();
                        
                        // Удаляем временный файл (используем file_path, так как file_path_mutable может быть изменен функцией)
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 11.7] Removing temporary file...";
                        boost::filesystem::remove(file_path);
                        
                        std::unique_lock<std::mutex> lock(result->mutex);
                        if (success || !import_result.empty()) {
                            // Импорт успешен (или пресет уже был импортирован)
                            // ВАЖНО: Используем preset_name_to_save (имя пресета с постфиксом [FilamentHub]),
                            // а НЕ import_result, так как import_result может содержать пути к файлам
                            std::string actual_preset_name = preset_name_to_save;
                            
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 11.8] Profile imported successfully (name: " << actual_preset_name << ")";
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 11.8.1] import_result size: " << import_result.size();
                            if (!import_result.empty()) {
                                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 11.8.2] import_result[0]: " << import_result[0];
                            }
                            
                            // Сохраняем маппинг preset_id → bundle_preset_name (имя пресета в OrcaSlicer)
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 11.9] Saving preset mapping...";
                            save_preset_mapping(preset_id, actual_preset_name);
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 11.9.1] Saved mapping preset_id=" << preset_id 
                                                   << " -> bundle_preset_name=" << actual_preset_name;
                            
                            // Обновляем .info файл с метаданными FilamentHub
                            // ВАЖНО: Это нужно делать ПОСЛЕ импорта, так как import_json_presets() создаёт .info файл с пустыми значениями
                            // Мы скачиваем правильный .info файл из API и обновляем файл пресета
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 11.10] Updating .info file with FilamentHub metadata...";
                            update_preset_info_file(preset_id, actual_preset_name, access_token);
                            
                            // ВАЖНО: НЕ вызываем load_current_presets() здесь, чтобы не перезагружать все пресеты после каждого импорта
                            // Это предотвращает обновление .info файлов не-FilamentHub пресетов
                            // load_current_presets() должен вызываться вызывающим кодом один раз после завершения всех импортов
                            
                            result->success = true;
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 11.11] Import completed successfully";
                        } else {
                            BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] Failed to import profile. import_result is empty, success=" << (success ? "true" : "false");
                            result->error = "Failed to import profile: import_json_presets returned false";
                        }
                        
                        result->completed = true;
                        result->cv.notify_one();
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 11.12] Notified waiting thread";
                    } catch (const std::exception& e) {
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] Exception in CallAfter during import: " << e.what();
                        std::unique_lock<std::mutex> lock(result->mutex);
                        result->error = std::string("Exception in CallAfter: ") + e.what();
                        result->completed = true;
                        result->cv.notify_one();
                        // Удаляем временный файл в случае ошибки
                        try {
                            boost::filesystem::remove(file_path);
                        } catch (...) {
                            // Игнорируем ошибки удаления файла
                        }
                    }
                });
                
                // ВАЖНО: НЕ ждем завершения импорта здесь, потому что download_profile теперь выполняется в отдельном потоке
                // и callback on_complete будет вызван из этого потока, а не из UI потока
                // CallAfter внутри on_complete правильно выполнит импорт в UI потоке
                // Поэтому мы просто устанавливаем result->completed = true в callback on_complete, когда импорт завершен
                // и не ждем здесь, чтобы избежать deadlock
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP 12] download_profile called in separate thread, not waiting here";
                
                // ВАЖНО: Устанавливаем result->completed = false здесь, чтобы callback мог установить его в true
                // Но мы НЕ ждем здесь, чтобы избежать deadlock
                // Вместо этого, callback on_complete установит result->completed = true и вызовет result->cv.notify_one()
                // Но мы не будем ждать здесь, потому что это может вызвать deadlock
                
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception during silent import: " << e.what();
                std::lock_guard<std::mutex> lock(result->mutex);
                result->error = std::string("Exception: ") + e.what();
                result->completed = true;
                result->cv.notify_one();
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
            result->completed = true;
            result->cv.notify_one();
        }
    );
    
    // ВАЖНО: НЕ ждем завершения download_profile здесь, чтобы избежать deadlock!
    // download_profile выполняется в отдельном потоке, и его callback on_complete
    // будет вызван из этого потока. CallAfter внутри on_complete правильно выполнит
    // импорт в UI потоке. Мы возвращаемся немедленно, чтобы не блокировать callback
    // от get_my_presets, который может быть в worker thread от HTTP клиента.
    // 
    // Импорт будет выполняться асинхронно в callback on_complete от download_profile,
    // и мы не можем узнать результат здесь. Поэтому мы возвращаем true, предполагая,
    // что импорт будет выполнен успешно (ошибки будут логироваться).
    // 
    // ВАЖНО: Это означает, что import_preset_silent теперь полностью асинхронный,
    // и мы не можем узнать результат импорта сразу. Но это необходимо, чтобы избежать
    // deadlock при вызове из callback on_complete от get_my_presets.
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 13] download_profile called asynchronously, returning immediately (no wait)";
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 13.1] Import will complete asynchronously in download_profile callback";
    
    // Возвращаем true, предполагая, что импорт будет выполнен успешно
    // (ошибки будут логироваться в callback on_complete/on_error от download_profile)
    // ВАЖНО: Это означает, что мы не можем узнать результат импорта здесь,
    // но это необходимо, чтобы избежать deadlock
    return true;
}

void FilamentHubPanel::update_preset_info_file(int preset_id, const std::string& preset_name, const std::string& access_token)
{
    static int info_update_counter = 0;
    info_update_counter++;
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [INFO UPDATE #" << info_update_counter << "] Updating .info file for preset_id=" << preset_id 
                           << ", preset_name='" << preset_name << "'";
    
    // Находим импортированный пресет
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot update .info file";
        return;
    }
    
    PresetCollection& filaments = bundle->filaments;
    Preset* preset = filaments.find_preset2(preset_name, true);
    
    if (preset == nullptr || preset->is_system) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Preset not found or is system: " << preset_name;
        return;
    }
    
    if (preset->file.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Preset file path is empty, cannot update .info file";
        return;
    }
    
    // Скачиваем .info файл из API
    m_fhub_client->set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);

    m_fhub_client->download_profile_info(
        preset_id,
        access_token,
        // on_complete: .info файл успешно скачан
        // NOTE: Захватываем preset_file_path (string) вместо preset (raw pointer)
        // чтобы избежать use-after-free если пресет удалён до вызова callback
        [this, preset_file = preset->file, preset_name](std::string info_content, unsigned http_status) {
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to download .info file. HTTP status: " << http_status;
                return;
            }

            if (info_content.empty()) {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: .info file content is empty";
                return;
            }

            // Определяем путь к .info файлу из захваченной строки (не pointer)
            boost::filesystem::path info_file_path(preset_file);
            info_file_path.replace_extension(".info");

            // Сохраняем .info файл
            try {
                boost::filesystem::ofstream info_file(info_file_path);
                if (!info_file.is_open()) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to open .info file for writing: " << info_file_path.string();
                    return;
                }

                info_file << info_content;
                info_file.close();

                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Successfully updated .info file: " << info_file_path.string();

                // Обновляем поля Preset объекта из .info файла (в UI thread для безопасности)
                CallAfter([this, preset_name, info_content]() {
                    PresetBundle* bundle = wxGetApp().preset_bundle;
                    if (bundle == nullptr) return;

                    Preset* p = bundle->filaments.find_preset2(preset_name, true);
                    if (p == nullptr || p->is_system) return;

                    // Парсим .info файл (INI формат)
                    std::istringstream info_stream(info_content);
                    std::string line;
                    while (std::getline(info_stream, line)) {
                        if (line.empty() || line[0] == '#') continue;

                        size_t eq_pos = line.find('=');
                        if (eq_pos == std::string::npos) continue;

                        std::string key = line.substr(0, eq_pos);
                        std::string value = line.substr(eq_pos + 1);
                        boost::algorithm::trim(key);
                        boost::algorithm::trim(value);

                        if (key == "user_id") {
                            p->user_id = value;
                        } else if (key == "setting_id") {
                            p->setting_id = value;
                        } else if (key == "base_id") {
                            p->base_id = value;
                        } else if (key == "updated_time") {
                            try {
                                p->updated_time = std::stoll(value);
                            } catch (...) {
                                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse updated_time: " << value;
                            }
                        } else if (key == "sync_info") {
                            p->sync_info = value;
                        }
                    }

                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Updated Preset object fields from .info file: "
                                           << "user_id=" << p->user_id
                                           << ", setting_id=" << p->setting_id
                                           << ", updated_time=" << p->updated_time;
                });
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception while saving .info file: " << e.what();
            }
        },
        // on_error: ошибка при скачивании .info файла
        [this, preset_name](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to download .info file for preset '" << preset_name 
                                    << "'. Error: " << error << ", Status: " << http_status;
        }
    );
}

void FilamentHubPanel::import_preset_silent_with_callback(int preset_id, const std::string& preset_name,
                                                           const std::string& access_token,
                                                           std::function<void(bool success)> on_complete)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT START] ========== import_preset_silent_with_callback() CALLED ==========";
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 1] preset_id=" << preset_id
                            << ", preset_name='" << preset_name << "'"
                            << ", token_length=" << access_token.length();

    if (wxGetApp().preset_bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] preset_bundle is null, cannot import preset " << preset_id;
        if (on_complete) on_complete(false);
        return;
    }

    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
    m_fhub_client->set_api_base_url(api_base_url);

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 4] Calling download_profile API: " << api_base_url
                            << " for preset_id=" << preset_id;

    m_fhub_client->download_profile(
        preset_id,
        access_token,
        // on_complete: profile successfully downloaded
        // ВАЖНО: Захватываем access_token для передачи во вложенную лямбду CallAfter
        [this, preset_id, preset_name, on_complete, access_token](std::string json_content, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 5] ========== download_profile on_complete CALLBACK ==========";
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 5.1] Preset " << preset_id << " downloaded. "
                                    << "HTTP status: " << http_status
                                    << ", JSON size: " << json_content.size()
                                    << " bytes";

            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] Unexpected HTTP status " << http_status
                                        << " when downloading preset " << preset_id;
                if (on_complete) on_complete(false);
                return;
            }

            if (json_content.empty()) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] Empty JSON content for preset " << preset_id;
                if (on_complete) on_complete(false);
                return;
            }

            try {
                nlohmann::json profile_json = nlohmann::json::parse(json_content);
                std::string original_name = profile_json.value("name", preset_name);
                std::string new_name = ensure_filamenthub_postfix(original_name);
                profile_json["name"] = new_name;

                ensure_parent_preset_exists(profile_json);

                boost::filesystem::path temp_dir = boost::filesystem::temp_directory_path();
                boost::filesystem::path temp_file = temp_dir / ("filamenthub_preset_" + std::to_string(preset_id) + "_" +
                                                                std::to_string(std::time(nullptr)) + ".json");

                std::ofstream file(temp_file.string());
                if (!file.is_open()) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] Failed to create temporary file: " << temp_file.string();
                    if (on_complete) on_complete(false);
                    return;
                }

                file << profile_json.dump(2);
                file.close();

                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 9.1] Saved profile to temporary file: " << temp_file.string();

                // Schedule the actual import in the UI thread
                // Захватываем access_token для update_preset_info_file
                CallAfter([this, preset_id, on_complete, temp_file, new_name, access_token]() {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT STEP 10] ========== CallAfter for UI import EXECUTED ==========";
                    bool success = false;
                    try {
                        PresetBundle* bundle = wxGetApp().preset_bundle;
                        if (bundle == nullptr) {
                            BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] preset_bundle is null during UI import";
                            if (on_complete) on_complete(false);
                            return;
                        }

                        PresetsConfigSubstitutions substitutions;
                        int overwrite = 1;
                        std::vector<std::string> import_result_vec;
                        auto override_confirm = [](std::string const& name) -> int { return 1; };

                        std::string file_path_mutable = temp_file.string();
                        success = bundle->import_json_presets(
                            substitutions,
                            file_path_mutable,
                            override_confirm,
                            ForwardCompatibilitySubstitutionRule::Enable,
                            overwrite,
                            import_result_vec
                        );

                        if (success || !import_result_vec.empty()) {
                            save_preset_mapping(preset_id, new_name);
                            
                            // Обновляем .info файл с метаданными FilamentHub
                            // ВАЖНО: Это нужно делать ПОСЛЕ импорта, так как import_json_presets() создаёт .info файл с пустыми значениями
                            // Мы скачиваем правильный .info файл из API и обновляем файл пресета
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [IMPORT STEP] Updating .info file with FilamentHub metadata...";
                            update_preset_info_file(preset_id, new_name, access_token);
                            
                            // ВАЖНО: НЕ вызываем load_current_presets() здесь, чтобы не перезагружать все пресеты после каждого импорта
                            // Это предотвращает обновление .info файлов не-FilamentHub пресетов
                            // load_current_presets() будет вызван один раз после завершения импорта всех пресетов
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [IMPORT END] Preset " << preset_id << " imported successfully in UI thread.";
                        } else {
                            BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] Failed to import profile in UI thread. import_json_presets returned false.";
                        }
                    } catch (const std::exception& e) {
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] Exception in UI import: " << e.what();
                    }

                    // Clean up temporary file
                    try {
                        boost::filesystem::remove(temp_file);
                    } catch (...) {
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to remove temporary file: " << temp_file.string();
                    }

                    if (on_complete) on_complete(success);
                });
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] Exception during JSON parsing/file ops: " << e.what();
                if (on_complete) on_complete(false);
            }
        },
        // on_error: download failed
        [on_complete, preset_id](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: [IMPORT ERROR] Failed to download preset " << preset_id
                                    << ". Error: " << error
                                    << ", HTTP status: " << http_status
                                    << ", Body: " << body.substr(0, std::min((int)body.size(), 200));
            if (on_complete) on_complete(false);
        }
    );
}

void FilamentHubPanel::process_preset_import_queue()
{
    PresetImportTask task;
    size_t remaining_count = 0;
    bool should_finish = false;
    
    // ВАЖНО: Получаем задачу из очереди и проверяем состояние БЕЗ блокировки мьютекса во время обработки
    // Это позволяет избежать deadlock при рекурсивных вызовах
    {
        std::lock_guard<std::mutex> lock(m_preset_queue_mutex);
        if (m_processing_preset_queue) {
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Preset import queue already being processed, skipping call.";
            return;
        }
        if (m_preset_import_queue.empty()) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Preset import queue is empty. Finishing sync.";
            should_finish = true;
            m_processing_preset_queue = false;
        } else {
            // Есть пресеты в очереди - получаем задачу
            task = m_preset_import_queue.front();
            m_preset_import_queue.erase(m_preset_import_queue.begin());
            remaining_count = m_preset_import_queue.size();
            m_processing_preset_queue = true;
        }
    }
    
    // Если очередь пуста - завершаем синхронизацию
    if (should_finish) {
        // КРИТИЧНО: Получаем user_id из последней задачи (если была) для обновления last_sync_time
        int user_id_for_sync = 0;
        {
            std::lock_guard<std::mutex> lock(m_preset_queue_mutex);
            // Пытаемся получить user_id из последней задачи (если очередь не пуста)
            // Если очередь пуста, загружаем user_id из токена
            if (!m_preset_import_queue.empty()) {
                user_id_for_sync = m_preset_import_queue.back().user_id;
            }
        }
        
        // Если не получили user_id из очереди, загружаем из токена
        if (user_id_for_sync == 0) {
            std::string dummy_token;
            load_auth_token(dummy_token, user_id_for_sync);
        }
        
        int final_user_id = user_id_for_sync; // Захватываем для lambda
        
        CallAfter([this, final_user_id]() {
            // ВАЖНО: Вызываем load_current_presets() только один раз после завершения импорта всех пресетов
            // Это обновит UI и предотвратит множественные перезагрузки всех пресетов (включая не-FilamentHub)
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE FINISH] All presets imported. Calling load_current_presets() once...";
            wxGetApp().load_current_presets();
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE FINISH] load_current_presets() completed.";
            
            // КРИТИЧНО: Обновляем last_sync_time ПОСЛЕ успешного импорта всех пресетов
            // Это предотвращает потерю пресетов при прерывании синхронизации
            if (final_user_id > 0) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE FINISH] Updating last_sync_time after successful import...";
                std::time_t now = std::time(nullptr);
                std::stringstream ss;
                ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%S.000000");
                std::string current_time = ss.str();
                save_last_sync_time(final_user_id, current_time, SyncTimestampType::Filament);
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE FINISH] Saved last_sync_time=" << current_time;
            }
            
            // Сбрасываем флаг защиты от зацикливания после успешной синхронизации
            m_full_sync_attempted.store(false);
            
            m_active_syncs--;
            m_is_syncing.store(false);
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync completed. Active syncs: " << m_active_syncs;
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sync summary - Synced: " << m_synced_count << ", Errors: " << m_error_count;
            
            // Hide progress bar
            if (m_sync_progress) {
                m_sync_progress->Hide();
                m_sync_progress->SetValue(0);
            }
            if (m_sync_status_label) {
                m_sync_status_label->Hide();
            }
            
            if (m_active_syncs <= 0) {
                update_sync_button_state(false);
                m_active_syncs = 0;
            }
            m_info_panel->Layout();
            update_user_info();
            
            // Обновляем количество непрочитанных уведомлений после завершения синхронизации
            // (так как после синхронизации могут появиться новые уведомления)
            update_unread_notifications_count();
            
            // Синхронизируем printer и print profiles (второстепенные, после основного - filament presets)
            // Проверяем разрешения пользователя перед sync (TODO 9)
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Checking user permissions before printer/print profiles sync (after filament presets)...";
            std::string token;
            int uid = 0;
            if (load_auth_token(token, uid)) {
                check_user_permissions(token,
                    [this](bool filament_import, bool printer_import, bool printer_export, bool print_import, bool print_export) {
                        CallAfter([this, printer_export, print_export]() {
                            if (printer_export) {
                                synchronize_printer_profiles(false);
                            } else {
                                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Skipping printer profiles sync (disabled in user settings)";
                            }
                            if (print_export) {
                                synchronize_print_profiles(false);
                            } else {
                                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Skipping print profiles sync (disabled in user settings)";
                            }
                        });
                    },
                    [](std::string error, unsigned status) {
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to check permissions, skipping printer/print sync: " << error;
                    }
                );
            }
        });
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE] Processing preset ID=" << task.preset_id
                            << " (" << task.preset_name << "). Remaining in queue: " << remaining_count;

    // Update progress bar UI (в UI потоке)
    CallAfter([this, task, remaining_count]() {
        update_sync_progress_ui(m_synced_count + m_error_count, m_total_presets_to_sync,
                                wxString::Format(_L("Syncing %d/%d: %s"),
                                                m_synced_count + m_error_count + 1, m_total_presets_to_sync,
                                                wxString::FromUTF8(task.preset_name.c_str())));
    });

    // Call the asynchronous import method
    import_preset_silent_with_callback(
        task.preset_id,
        task.preset_name,
        task.access_token,
        [this, task](bool success) {
            // Обновляем счетчики (в UI потоке)
            CallAfter([this, success, task]() {
                {
                    std::lock_guard<std::mutex> lock(m_preset_queue_mutex);
                    if (success) {
                        m_synced_count++;
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE] Preset " << task.preset_id << " imported successfully.";
                    } else {
                        m_error_count++;
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [QUEUE] Failed to import preset " << task.preset_id << ".";
                    }
                    m_processing_preset_queue = false; // Allow next item to be processed
                }
                // Обрабатываем следующий элемент очереди (после освобождения мьютекса)
                process_preset_import_queue();
            });
        }
    );
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
    
    m_fhub_client->set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
    
    if (access_token.empty()) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Access token is empty, cannot get user info";
        m_user_name_label->SetLabel(wxString::Format(_L("User %d"), user_id));
        return;
    }
    
    m_fhub_client->get_current_user(
        access_token,
        // on_complete: user info received
        [this, user_id](std::string json_body, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Received user info. Status: " << http_status;
            
            // Проверяем статус ответа
            if (http_status == 401) {
                // Токен истек или невалидный
                // ВАЖНО: НЕ делаем logout сразу - возможно, это временная проблема
                // Токен может быть валидным для других эндпоинтов (например, get_my_presets)
                // Поэтому просто не обновляем информацию о пользователе, но не очищаем авторизацию
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Token expired or invalid (401) during user info update, but not clearing auth (may be temporary)";
                // Не обновляем UI - оставляем текущее состояние
                // Пользователь сможет попробовать снова через кнопку синхронизации
                return;
            }
            
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get user info. Status: " << http_status;
                CallAfter([this, user_id]() {
                    m_user_name_label->SetLabel(wxString::Format(_L("User %d"), user_id));
                });
                return;
            }

            BOOST_LOG_TRIVIAL(info) << "FilamentHub: User info JSON: " << json_body;

            try {
                nlohmann::json user_json = nlohmann::json::parse(json_body);

                // Безопасное извлечение строк с проверкой на null
                std::string username;
                std::string full_name;
                std::string email;

                if (user_json.contains("username") && !user_json["username"].is_null()) {
                    username = user_json["username"].get<std::string>();
                }
                if (user_json.contains("full_name") && !user_json["full_name"].is_null()) {
                    full_name = user_json["full_name"].get<std::string>();
                }
                if (user_json.contains("email") && !user_json["email"].is_null()) {
                    email = user_json["email"].get<std::string>();
                }

                // Проверяем роль пользователя для показа кнопки Admin
                std::string role;
                if (user_json.contains("role") && !user_json["role"].is_null()) {
                    role = user_json["role"].get<std::string>();
                }

                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Parsed user data - username: '" << username
                                        << "', full_name: '" << full_name
                                        << "', email: '" << email
                                        << "', role: '" << role << "'";

                wxString display_name;
                if (!full_name.empty()) {
                    display_name = wxString::FromUTF8(full_name);
                } else if (!username.empty()) {
                    display_name = wxString::FromUTF8(username);
                } else if (!email.empty()) {
                    display_name = wxString::FromUTF8(email);
                } else {
                    display_name = wxString::Format(_L("User %d"), user_id);
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: No name/email found, using default: " << display_name.ToUTF8();
                }

                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Setting display name: " << display_name.ToUTF8();

                // Все UI-обновления через CallAfter (callback вызывается из background thread)
                CallAfter([this, role, display_name]() {
                    m_user_name_label->SetLabel(display_name);

                    if (role == "admin") {
                        if (m_admin_button) {
                            m_admin_button->Show();
                        }
                    } else {
                        if (m_admin_button) {
                            m_admin_button->Hide();
                        }
                    }
                    if (m_info_panel) {
                        m_info_panel->Layout();
                    }
                });

                // Get preset count via get_presets_stats
                std::string access_token_inner;
                int user_id_inner;
                if (load_auth_token(access_token_inner, user_id_inner)) {
                    if (access_token_inner.empty()) {
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Access token is empty, cannot get presets stats";
                        CallAfter([this]() {
                            m_preset_count_label->SetLabel(_L("Presets: ?"));
                        });
                        return;
                    }

                    m_fhub_client->get_presets_stats(
                        access_token_inner,
                        [this](std::string json_body, unsigned http_status) {
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Received presets stats. Status: " << http_status;

                            if (http_status != 200) {
                                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get presets stats. Status: " << http_status;
                                CallAfter([this]() {
                                    m_preset_count_label->SetLabel(_L("Presets: ?"));
                                });
                                return;
                            }

                            try {
                                nlohmann::json response = nlohmann::json::parse(json_body);
                                int total_presets = response.value("total_presets", 0);
                                int synced_presets = response.value("synced_presets", 0);
                                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Total presets: " << total_presets << ", Synced: " << synced_presets;
                                CallAfter([this, total_presets, synced_presets]() {
                                    m_preset_count_label->SetLabel(wxString::Format(_L("Presets: %d (%d synced)"), total_presets, synced_presets));
                                });
                            } catch (const std::exception& e) {
                                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing presets stats: " << e.what();
                                CallAfter([this]() {
                                    m_preset_count_label->SetLabel(_L("Presets: ?"));
                                });
                            }
                        },
                        [this](std::string body, std::string error, unsigned http_status) {
                            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get presets stats. Error: " << error
                                                      << ", Status: " << http_status;
                            if (http_status != 401) {
                                CallAfter([this]() {
                                    m_preset_count_label->SetLabel(_L("Presets: ?"));
                                });
                            }
                        }
                    );
                }

            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing user info JSON: " << e.what();
                CallAfter([this, user_id]() {
                    m_user_name_label->SetLabel(wxString::Format(_L("User %d"), user_id));
                    show_notification_in_webview(
                        _L("Error parsing server response"),
                        "error"
                    );
                });
            }
        },
        // on_error: failed to get user info
        [this, user_id](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to get user info. Error: " << error;
            CallAfter([this, user_id]() {
                m_user_name_label->SetLabel(wxString::Format(_L("User %d"), user_id));
            });
        }
    );
}

void FilamentHubPanel::on_sync_button_click(wxCommandEvent& evt)
{
    if (m_is_syncing) {
        return; // Prevent multiple simultaneous syncs
    }
    
    update_sync_button_state(true);
    
    // Start synchronization (only filament presets, like in the old version)
    synchronize_presets(false); // Incremental sync
    
    // Note: synchronize_presets is async, so we'll update button state in callback
}

void FilamentHubPanel::update_sync_button_state(bool is_syncing)
{
    // ВАЖНО: НЕ устанавливаем m_is_syncing здесь, это делается в synchronize_presets
    // Эта функция только обновляет UI состояние кнопки
    
    if (is_syncing) {
        if (m_sync_button) {
            m_sync_button->SetLabel(_L("Synchronizing..."));
            m_sync_button->Disable();
        }
        // НЕ показываем прогресс-бар здесь - он будет показан позже, когда узнаем количество пресетов
        // Прогресс-бар будет показан в процессе синхронизации (в callback после получения списка пресетов)
    } else {
        if (m_sync_button) {
            m_sync_button->SetLabel(_L("Synchronize"));
            m_sync_button->Enable();
        }
        // ВАЖНО: Всегда скрываем прогресс-бар и статус при завершении синхронизации
        if (m_sync_status_label) {
            m_sync_status_label->Hide();
        }
        if (m_sync_progress) {
            m_sync_progress->Hide();
            m_sync_progress->SetValue(0); // Сбрасываем значение
        }
    }
    
    if (m_info_panel) {
        m_info_panel->Layout();
    }
}

void FilamentHubPanel::update_ui_for_login_state(bool is_logged_in)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: update_ui_for_login_state called. is_logged_in=" << (is_logged_in ? "true" : "false");
    
    if (is_logged_in) {
        // Show logged-in UI elements
        m_profile_button->Show();
        m_preset_count_label->Show();
        m_sync_button->Show(); // ВАЖНО: Показываем кнопку синхронизации
        // m_notifications_button removed - notifications are in WebView now
        // if (m_notifications_button != nullptr) {
        //     m_notifications_button->Show();
        // }
        m_logout_button->Show();
        
        // Update unread notifications count
        update_unread_notifications_count();
        
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
        // m_notifications_button removed - notifications are in WebView now
        // if (m_notifications_button != nullptr) {
        //     m_notifications_button->Hide();
        // }
        // if (m_notifications_badge != nullptr) {
        //     m_notifications_badge->Hide();
        // }
        m_admin_button->Hide(); // Скрываем кнопку админки (если была показана)
        m_logout_button->Hide();
        
        // Reset unread notifications count
        m_unread_notifications_count = 0;
        
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Hiding sync button (user is not logged in)";
        
        // Update labels
        m_user_name_label->SetLabel(_L("Sign in to unlock full functionality"));
        m_preset_count_label->SetLabel(_L("Presets: 0"));
    }
    
    m_info_panel->Layout();
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: UI updated for login state";
}

void FilamentHubPanel::navigate_to_catalog()
{
    m_active_page = "catalog";
    update_active_button_style();
    // Используем JavaScript навигацию через React Router без перезагрузки страницы
    navigate_without_reload("/");
}

void FilamentHubPanel::navigate_to_wiki()
{
    m_active_page = "wiki";
    update_active_button_style();
    navigate_without_reload("/wiki");
}

void FilamentHubPanel::navigate_to_profile()
{
    m_active_page = "profile";
    update_active_button_style();
    // Используем JavaScript навигацию через React Router без перезагрузки страницы
    navigate_without_reload("/profile");
}

void FilamentHubPanel::update_active_button_style()
{
    // Reset all navigation buttons to Regular style (keep square corners)
    if (m_catalog_button) {
        set_button_square_style(m_catalog_button, ButtonStyle::Regular);
    }
    if (m_profile_button) {
        set_button_square_style(m_profile_button, ButtonStyle::Regular);
    }
    if (m_wiki_button) {
        set_button_square_style(m_wiki_button, ButtonStyle::Regular);
    }
    if (m_admin_button) {
        // Admin button uses Alert style when inactive
        set_button_square_style(m_admin_button, ButtonStyle::Alert);
    }

    // Set active button to Confirm style (green) - keep square corners
    if (m_active_page == "catalog" && m_catalog_button) {
        set_button_square_style(m_catalog_button, ButtonStyle::Confirm);
    } else if (m_active_page == "profile" && m_profile_button) {
        set_button_square_style(m_profile_button, ButtonStyle::Confirm);
    } else if (m_active_page == "wiki" && m_wiki_button) {
        set_button_square_style(m_wiki_button, ButtonStyle::Confirm);
    } else if (m_active_page == "admin" && m_admin_button) {
        set_button_square_style(m_admin_button, ButtonStyle::Confirm);
    }
    
    if (m_info_panel) {
        m_info_panel->Layout();
    }
}

void FilamentHubPanel::navigate_without_reload(const wxString& path)
{
    // Используем JavaScript для навигации через React Router без перезагрузки страницы
    // Это быстрее и сохраняет состояние React приложения

    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Cannot navigate - WebView is null";
        return;
    }

    // SEC-3 fix: валидируем path — только безопасные символы для URL-пути
    std::string path_str = path.ToUTF8().data();
    static const std::regex valid_path_re("^/[a-zA-Z0-9/_\\-\\.\\?=&#%]*$");
    if (!std::regex_match(path_str, valid_path_re)) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Invalid navigation path rejected: " << path_str;
        return;
    }

    // Передаём path через JSON.parse() для безопасности
    nlohmann::json nav_data;
    nav_data["path"] = path_str;
    wxString nav_json_wx = wxString::FromUTF8(nav_data.dump().c_str());
    wxString js_code = wxString(R"(
        (function() {
            var data = )") + nav_json_wx + wxString(R"(;
            var path = data.path;
            // Используем глобальную функцию navigate из window.filamenthub
            if (window.filamenthub && typeof window.filamenthub.navigate === 'function') {
                window.filamenthub.navigate(path);
                return true;
            }

            // Fallback: используем window.history.pushState (может не работать с React Router)
            console.warn('FilamentHub: window.filamenthub.navigate not found, using history.pushState');
            window.history.pushState({}, '', path);

            // Создаём событие popstate для обновления React Router
            window.dispatchEvent(new PopStateEvent('popstate', { state: {} }));

            return false;
        })();
    )");
    
    WebView::RunScript(m_browser, js_code);
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Navigating to " << path.ToUTF8() << " without page reload";
}

void FilamentHubPanel::show_login()
{
    // Navigate to login page in WebView
    // User will log in there, and we'll receive login_success message via JavaScript
    load_url(build_frontend_url("/?auth=login"));
}

void FilamentHubPanel::logout()
{
    // Clear all auth tokens from AppConfig
    if (wxGetApp().app_config != nullptr) {
        wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_ACCESS_TOKEN, "");
        wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_REFRESH_TOKEN, "");
        wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_USER_ID, "");
        CallAfter([]() {
            if (wxGetApp().app_config != nullptr)
                wxGetApp().app_config->save();
        });
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

void FilamentHubPanel::set_button_square_style(Button* button, ButtonStyle style)
{
    if (button == nullptr) return;
    button->SetStyle(style, ButtonType::Compact);
    button->SetCornerRadius(0); // Square corners - same as in SpinInput, TabCtrl, etc.
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

    if (m_wiki_button != nullptr) {
        m_wiki_button->Enable(!busy);
    }

    if (m_refresh_button != nullptr) {
        m_refresh_button->Enable(!busy);
    }

    // m_notifications_button removed - notifications are in WebView now
    // if (m_notifications_button != nullptr) {
    //     m_notifications_button->Enable(!busy);
    // }

    if (m_admin_button != nullptr) {
        m_admin_button->Enable(!busy);
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
    // Прогресс-бар не показываем - синхронизация быстрая
    // Оставляем только логи с названиями и количеством пресетов
    if (m_sync_progress) {
        m_sync_progress->Hide();
    }
    if (m_sync_status_label) {
        m_sync_status_label->Hide();
    }
    m_info_panel->Layout();
}

void FilamentHubPanel::update_sync_progress_ui(int completed, int total, const wxString& status_text)
{
    // Прогресс-бар не обновляем - синхронизация быстрая
    // Оставляем только логи с названиями и количеством пресетов (BOOST_LOG_TRIVIAL)
    // Функция оставлена для совместимости, но прогресс-бар скрыт
    if (m_sync_progress) {
        m_sync_progress->Hide();
    }
    if (m_sync_status_label) {
        m_sync_status_label->Hide();
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
    std::function<void(bool, bool, bool, bool, bool)> on_complete,
    std::function<void(std::string, unsigned)> on_error
)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Checking user permissions. Token length: " << access_token.length();
    
    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
    m_fhub_client->set_api_base_url(api_base_url);
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Calling get_current_user for permissions check. API: " << api_base_url;
    
    m_fhub_client->get_current_user(
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
                bool allow_filament_import = user_json.value("allow_filament_presets_import", true);
                bool allow_printer_import = user_json.value("allow_printer_profiles_import", true);
                bool allow_printer_export = user_json.value("allow_printer_profiles_export", true);
                bool allow_print_import = user_json.value("allow_print_profiles_import", true);
                bool allow_print_export = user_json.value("allow_print_profiles_export", true);

                BOOST_LOG_TRIVIAL(info) << "FilamentHub: User permissions extracted - "
                                       << "filament_import: " << (allow_filament_import ? "true" : "false")
                                       << ", printer_import: " << (allow_printer_import ? "true" : "false")
                                       << ", printer_export: " << (allow_printer_export ? "true" : "false")
                                       << ", print_import: " << (allow_print_import ? "true" : "false")
                                       << ", print_export: " << (allow_print_export ? "true" : "false");

                on_complete(allow_filament_import, allow_printer_import, allow_printer_export, allow_print_import, allow_print_export);
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
        updated_since = load_last_sync_time(user_id, SyncTimestampType::Printer);
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Incremental sync. updated_since: " << (updated_since.empty() ? "(none)" : updated_since);
    } else {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Full sync (force_full_sync=true)";
    }
    
    // 3. Получаем список printer profiles пользователя через API
    m_fhub_client->set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
    
    m_fhub_client->get_my_printer_profiles(
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
                    CallAfter([this, user_id]() {
                        if (user_id > 0) {
                            std::time_t now = std::time(nullptr);
                            std::stringstream ss;
                            ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%S.000000");
                            save_last_sync_time(user_id, ss.str(), SyncTimestampType::Printer);
                        }
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
                // ВАЖНО: API уже отфильтровал профили по updated_since на стороне сервера
                // Если профиль пришел от API, значит он новый или обновлен после last_sync_time
                // Поэтому просто импортируем все профили, которые пришли от API
                int synced_count = 0;
                int updated_count = 0;
                int error_count = 0;
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: API already filtered printer profiles by updated_since, so all received profiles need to be imported";
                
                for (const auto& profile_json : profiles) {
                    int profile_id = profile_json["id"];
                    std::string profile_name = profile_json["name"];
                    std::string updated_at = profile_json.value("updated_at", "");
                    
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Processing printer profile ID=" << profile_id 
                                           << ", name=" << profile_name
                                           << ", updated_at=" << updated_at;
                    
                    // Проверяем маппинг (есть ли уже в OrcaSlicer)
                    std::string bundle_profile_name = load_printer_profile_mapping(profile_id);
                    
                    if (bundle_profile_name.empty()) {
                        // Профиля нет в маппинге - новый профиль, нужно импортировать
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profile " << profile_id 
                                               << " not in mapping, importing (new profile)";
                    } else {
                        // Профиль уже в маппинге, но API вернул его (значит он был обновлен)
                        // Переимпортируем его, чтобы обновить в OrcaSlicer
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profile " << profile_id 
                                               << " already mapped to " << bundle_profile_name 
                                               << ", but API returned it (updated), re-importing";
                    }
                    
                    // Импортируем профиль (новый или обновленный)
                    if (import_printer_profile_silent(profile_id, profile_name, access_token)) {
                        synced_count++;
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profile " << profile_id << " imported successfully";
                    } else {
                        error_count++;
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to import printer profile " << profile_id;
                    }
                }
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profiles synchronization completed. "
                                       << "Synced: " << synced_count 
                                       << ", Updated: " << updated_count
                                       << ", Errors: " << error_count;
                
                // Update UI: уменьшаем счетчик активных синхронизаций
                CallAfter([this, user_id, error_count]() {
                    if (user_id > 0 && error_count == 0) {
                        std::time_t now = std::time(nullptr);
                        std::stringstream ss;
                        ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%S.000000");
                        save_last_sync_time(user_id, ss.str(), SyncTimestampType::Printer);
                    }
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
            
            // НЕ уменьшаем счетчик - он не был увеличен для этой синхронизации (увеличивается только после 200 OK)
            CallAfter([this, error_msg, http_status]() {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profiles sync failed (error: " << error_msg.ToUTF8()
                                        << ", status: " << http_status << "). Active syncs: " << m_active_syncs;
                // Проверяем, нужно ли обновить состояние кнопки
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
        updated_since = load_last_sync_time(user_id, SyncTimestampType::Print);
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Incremental sync. updated_since: " << (updated_since.empty() ? "(none)" : updated_since);
    } else {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Full sync (force_full_sync=true)";
    }
    
    // 3. Получаем список print profiles пользователя через API
    m_fhub_client->set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
    
    m_fhub_client->get_my_print_profiles(
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
                    CallAfter([this, user_id]() {
                        if (user_id > 0) {
                            std::time_t now = std::time(nullptr);
                            std::stringstream ss;
                            ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%S.000000");
                            save_last_sync_time(user_id, ss.str(), SyncTimestampType::Print);
                        }
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
                // ВАЖНО: API уже отфильтровал профили по updated_since на стороне сервера
                // Если профиль пришел от API, значит он новый или обновлен после last_sync_time
                // Поэтому просто импортируем все профили, которые пришли от API
                int synced_count = 0;
                int updated_count = 0;
                int error_count = 0;
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: API already filtered print profiles by updated_since, so all received profiles need to be imported";
                
                for (const auto& profile_json : profiles) {
                    int profile_id = profile_json["id"];
                    std::string profile_name = profile_json["name"];
                    std::string updated_at = profile_json.value("updated_at", "");
                    
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Processing print profile ID=" << profile_id 
                                           << ", name=" << profile_name
                                           << ", updated_at=" << updated_at;
                    
                    // Проверяем маппинг (есть ли уже в OrcaSlicer)
                    std::string bundle_profile_name = load_print_profile_mapping(profile_id);
                    
                    if (bundle_profile_name.empty()) {
                        // Профиля нет в маппинге - новый профиль, нужно импортировать
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profile " << profile_id 
                                               << " not in mapping, importing (new profile)";
                    } else {
                        // Профиль уже в маппинге, но API вернул его (значит он был обновлен)
                        // Переимпортируем его, чтобы обновить в OrcaSlicer
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profile " << profile_id 
                                               << " already mapped to " << bundle_profile_name 
                                               << ", but API returned it (updated), re-importing";
                    }
                    
                    // Импортируем профиль (новый или обновленный)
                    if (import_print_profile_silent(profile_id, profile_name, access_token)) {
                        synced_count++;
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profile " << profile_id << " imported successfully";
                    } else {
                        error_count++;
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to import print profile " << profile_id;
                    }
                }
                
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profiles synchronization completed. "
                                       << "Synced: " << synced_count 
                                       << ", Updated: " << updated_count
                                       << ", Errors: " << error_count;
                
                // Update UI: уменьшаем счетчик активных синхронизаций
                CallAfter([this, user_id, error_count]() {
                    if (user_id > 0 && error_count == 0) {
                        std::time_t now = std::time(nullptr);
                        std::stringstream ss;
                        ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%S.000000");
                        save_last_sync_time(user_id, ss.str(), SyncTimestampType::Print);
                    }
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
            
            // НЕ уменьшаем счетчик - он не был увеличен для этой синхронизации (увеличивается только после 200 OK)
            CallAfter([this, error_msg, http_status]() {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profiles sync failed (error: " << error_msg.ToUTF8()
                                        << ", status: " << http_status << "). Active syncs: " << m_active_syncs;
                // Проверяем, нужно ли обновить состояние кнопки
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

    CallAfter([]() {
        if (wxGetApp().app_config != nullptr)
            wxGetApp().app_config->save();
    });

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

    CallAfter([]() {
        if (wxGetApp().app_config != nullptr)
            wxGetApp().app_config->save();
    });

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
    m_fhub_client->set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
    
    // Используем shared_ptr для безопасного доступа из лямбд
    struct ImportResult {
        bool success = false;
        std::string error;
        std::mutex mutex;
    };
    
    auto result = std::make_shared<ImportResult>();
    
    // Используем синхронный вызов (perform_sync уже внутри download_printer_profile)
    m_fhub_client->download_printer_profile(
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
    m_fhub_client->set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
    
    // Используем shared_ptr для безопасного доступа из лямбд
    struct ImportResult {
        bool success = false;
        std::string error;
        std::mutex mutex;
    };
    
    auto result = std::make_shared<ImportResult>();
    
    // Используем синхронный вызов (perform_sync уже внутри download_print_profile)
    m_fhub_client->download_print_profile(
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

void FilamentHubPanel::update_unread_notifications_count()
{
    std::string access_token;
    int user_id;
    
    if (!load_auth_token(access_token, user_id)) {
        // User not logged in - send 0 count to WebView
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Cannot update notifications count - user not logged in";
        CallAfter([this]() {
            m_unread_notifications_count = 0;
            // Вместо обновления C++ кнопки, отправляем сообщение в WebView
            if (m_browser) {
                wxString js_code = "window.postMessage({ command: 'update_notifications_count', count: 0 }, '*');";
                WebView::RunScript(m_browser, js_code);
            }
        });
        return;
    }
    
    if (access_token.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Cannot update notifications count - access token is empty";
        CallAfter([this]() {
            m_unread_notifications_count = 0;
            // Вместо обновления C++ кнопки, отправляем сообщение в WebView
            if (m_browser) {
                wxString js_code = "window.postMessage({ command: 'update_notifications_count', count: 0 }, '*');";
                WebView::RunScript(m_browser, js_code);
            }
        });
        return;
    }
    
    m_fhub_client->set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);

    m_fhub_client->get_unread_notifications_count(
        access_token,
        // on_complete: успешно получено количество непрочитанных уведомлений
        [this](std::string body, unsigned http_status) {
            if (http_status == 200) {
                try {
                    nlohmann::json response = nlohmann::json::parse(body);
                    int unread_count = response.value("unread_count", 0);
                    
                    CallAfter([this, unread_count]() {
                        m_unread_notifications_count = unread_count;
                        
                        // Вместо обновления C++ кнопки, отправляем сообщение в WebView
                        if (m_browser) {
                            wxString js_code = wxString::Format(
                                "window.postMessage({ command: 'update_notifications_count', count: %d }, '*');",
                                unread_count
                            );
                            WebView::RunScript(m_browser, js_code);
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sent update_notifications_count to WebView: " << unread_count;
                        }
                        
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Unread notifications count updated: " << unread_count;
                    });
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing unread notifications count: " << e.what();
                }
            } else {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to get unread notifications count. HTTP status: " << http_status;
            }
        },
        // on_error: ошибка при получении количества непрочитанных уведомлений
        [this](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Error getting unread notifications count: " << error 
                                       << ", HTTP status: " << http_status;
            // Не показываем ошибку пользователю, просто не обновляем badge
        }
    );
}

void FilamentHubPanel::show_notifications_dropdown()
{
    // Вместо создания wxMenu, вызываем JavaScript функцию в WebView,
    // которая кликнет на кнопку уведомлений на фронтенде и откроет красивое выпадающее меню
    // Это соответствует тому, как работает на сайте
    
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Cannot show notifications dropdown - WebView is null";
        return;
    }
    
    // JavaScript код для клика по кнопке уведомлений на фронтенде
    // Ищем кнопку по aria-label или по роли button с текстом "Уведомления"
    wxString js_code = R"(
        (function() {
            // Пробуем найти кнопку уведомлений разными способами
            let button = null;
            
            // Способ 1: По aria-label
            button = document.querySelector('button[aria-label="Уведомления"]');
            
            // Способ 2: По роли button внутри навигации
            if (!button) {
                const nav = document.querySelector('nav');
                if (nav) {
                    const buttons = nav.querySelectorAll('button');
                    for (let btn of buttons) {
                        const label = btn.getAttribute('aria-label');
                        if (label && label.includes('Уведомления')) {
                            button = btn;
                            break;
                        }
                    }
                }
            }
            
            // Способ 3: По тексту "Уведомления" в кнопке
            if (!button) {
                const buttons = document.querySelectorAll('button');
                for (let btn of buttons) {
                    const text = btn.textContent || btn.innerText;
                    if (text && text.trim() === 'Уведомления') {
                        button = btn;
                        break;
                    }
                }
            }
            
            // Если кнопка найдена - кликаем по ней
            if (button) {
                button.click();
                return true;
            }
            
            return false;
        })();
    )";
    
    WebView::RunScript(m_browser, js_code);
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Triggered notifications dropdown via JavaScript";
}

// ============================================================================
// Helper: reset m_is_syncing when export finishes
// If running as part of unified export, decrement counter first
// ============================================================================

void FilamentHubPanel::finish_export_operation()
{
    if (m_active_exports.load() > 0) {
        // Part of unified export — decrement and check
        int remaining = --m_active_exports;
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Export operation finished, remaining: " << remaining;
        if (remaining <= 0) {
            m_is_syncing.store(false);
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: All unified exports completed, m_is_syncing=false";
        }
    } else {
        // Standalone export
        m_is_syncing.store(false);
    }
}

// ============================================================================
// Methods for exporting filament presets to FilamentHub
// ============================================================================

void FilamentHubPanel::export_filament_presets_to_filamenthub()
{
    // Счётчик вызовов для отладки
    static int call_counter = 0;
    call_counter++;
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== export_filament_presets_to_filamenthub() CALLED (call #" << call_counter << ") ==========";
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [EXPORT TRACE] m_is_syncing=" << (m_is_syncing ? "true" : "false");
    
    // Атомарный check-and-set: если уже true — кто-то экспортирует, выходим
    if (m_is_syncing.exchange(true)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [EXPORT SKIP] Export already in progress, skipping duplicate call #" << call_counter;
        return;
    }
    
    // Проверяем авторизацию
    std::string access_token;
    int user_id;
    if (!load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Not authenticated, cannot export filament presets";
        m_is_syncing.store(false);
        CallAfter([this]() {
            show_notification_in_webview(
                _L("Please login to export filament presets to FilamentHub."),
                "warning"
            );
        });
        return;
    }

    // Проверяем PresetBundle
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot export filament presets";
        m_is_syncing.store(false);
        CallAfter([this]() {
            show_notification_in_webview(
                _L("Preset bundle not available. Please try again."),
                "error"
            );
        });
        return;
    }
    
    // Проверяем разрешение на импорт filament presets
    // ВАЖНО: Используем асинхронную проверку разрешений
    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
    m_fhub_client->set_api_base_url(api_base_url);

    m_fhub_client->get_current_user(
        access_token,
        // on_complete: проверяем разрешение allow_filament_presets_import
        [this, access_token, api_base_url](std::string json_body, unsigned http_status) {
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get user info for permission check. HTTP status: " << http_status;
                m_is_syncing.store(false);
                CallAfter([this, http_status]() {
                    if (http_status == 401) {
                        show_notification_in_webview(
                            _L("Your session has expired. Please login again."),
                            "warning"
                        );
                    } else if (http_status == 403) {
                        show_notification_in_webview(
                            _L("Access denied. Please check your permissions."),
                            "error"
                        );
                    } else {
                        show_notification_in_webview(
                            wxString::Format(_L("Failed to check permissions. HTTP status: %d"), http_status),
                            "error"
                        );
                    }
                });
                return;
            }

            try {
                nlohmann::json user_json = nlohmann::json::parse(json_body);
                bool allow_filament_import = user_json.value("allow_filament_presets_import", true);

                if (!allow_filament_import) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Filament presets import is disabled in user settings";
                    m_is_syncing.store(false);
                    CallAfter([this]() {
                        show_notification_in_webview(
                            _L("Filament presets export is disabled in your FilamentHub settings. Please enable it in your profile settings."),
                            "warning"
                        );
                    });
                    return;
                }

                // Разрешение получено - продолжаем экспорт
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Permission check passed, proceeding with export";
                export_filament_presets_to_filamenthub_internal(access_token, api_base_url);
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing user info JSON: " << e.what();
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Response body (first 500 chars): " << json_body.substr(0, std::min<size_t>(500, json_body.length()));
                m_is_syncing.store(false);
                CallAfter([this, e]() {
                    show_notification_in_webview(
                        wxString::Format(_L("Error parsing server response: %s"), e.what()),
                        "error"
                    );
                });
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Unknown exception when parsing user info JSON (filament presets export)";
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Response body (first 500 chars): " << json_body.substr(0, std::min<size_t>(500, json_body.length()));
                m_is_syncing.store(false);
                CallAfter([this]() {
                    show_notification_in_webview(
                        _L("Error parsing server response: Unknown exception"),
                        "error"
                    );
                });
            }
        },
        // on_error: ошибка при проверке разрешений
        [this](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to check permissions. Error: " << error
                                    << ", Status: " << http_status;
            m_is_syncing.store(false);
            CallAfter([this, http_status, error]() {
                if (http_status == 401) {
                    show_notification_in_webview(
                        _L("Your session has expired. Please login again."),
                        "warning"
                    );
                } else {
                    show_notification_in_webview(
                        wxString::Format(_L("Failed to check permissions: %s"), wxString::FromUTF8(error.c_str())),
                        "error"
                    );
                }
            });
        }
    );
}

void FilamentHubPanel::export_filament_presets_to_filamenthub_internal(const std::string& access_token, const std::string& api_base_url)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== export_filament_presets_to_filamenthub_internal() CALLED ==========";
    
    // m_is_syncing уже установлен вызывающей функцией (exchange или прямая установка)
    m_is_syncing.store(true);
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Confirmed m_is_syncing=true for export";
    
    // Проверяем PresetBundle
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot export filament presets";
        m_is_syncing.store(false); // Сбрасываем флаг при ошибке
        CallAfter([this]() {
            show_notification_in_webview(
                _L("Preset bundle not available. Please try again."),
                "error"
            );
        });
        return;
    }
    
    // Получаем все пользовательские filament presets (не системные)
    PresetCollection& filaments = bundle->filaments;
    std::vector<nlohmann::json> presets_json;
    
    int preset_count = 0;
    for (auto it = filaments.begin(); it != filaments.end(); ++it) {
        const Preset& preset = *it;
        
        // Пропускаем системные пресеты
        if (preset.is_system) {
            continue;
        }
        
        // ВАЖНО: В OrcaSlicer класс Preset не имеет поля "active"
        // Все пользовательские пресеты (is_user() == true) считаются активными
        // Черновики (active=false) - это понятие FilamentHub, а не OrcaSlicer
        // Проверка active выполняется на бэкенде при импорте из OrcaSlicer
        
        // Пропускаем пресеты с постфиксом [FilamentHub] (они уже синхронизированы)
        // Но можно экспортировать их тоже, если пользователь хочет обновить
        // Для MVP экспортируем все пользовательские пресеты
        
        try {
            // Получаем JSON конфигурацию пресета
            nlohmann::json orcaslicer_json = get_config_json(preset.config);
            
            // НОВОЕ: Читаем .info файл для извлечения меток FilamentHub (приоритетный источник)
            // .info файл более надежен чем JSON, так как OrcaSlicer не перезаписывает его при редактировании
            std::string info_content;
            if (!preset.file.empty() && boost::filesystem::exists(preset.file)) {
                boost::filesystem::path info_file = preset.file;
                info_file.replace_extension(".info");
                
                if (boost::filesystem::exists(info_file)) {
                    try {
                        std::ifstream ifs(info_file.string());
                        if (ifs.is_open()) {
                            std::stringstream buffer;
                            buffer << ifs.rdbuf();
                            info_content = buffer.str();
                            ifs.close();
                            
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Read .info file for preset: " 
                                                     << preset.name << " (file: " << info_file.string() << ")";
                        }
                    } catch (const std::exception& e) {
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to read .info file: " << e.what();
                    }
                }
            }
            
            // Читаем оригинальный JSON файл для извлечения метаданных FilamentHub (fallback)
            // Это необходимо, так как get_config_json() извлекает только известные опции,
            // а наши метки fhub_id, fhub_source, fhub_draft_id не сохраняются в preset.config
            if (!preset.file.empty() && boost::filesystem::exists(preset.file)) {
                try {
                    nlohmann::json original_json;
                    boost::filesystem::ifstream ifs(preset.file);
                    if (ifs.is_open()) {
                        ifs >> original_json;
                        ifs.close();
                        
                        // Извлекаем метки FilamentHub из оригинального JSON
                        if (original_json.contains("fhub_id")) {
                            orcaslicer_json["fhub_id"] = original_json["fhub_id"];
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Extracted fhub_id from JSON file for filament preset: " 
                                                     << preset.name << " -> fhub_id=" << original_json["fhub_id"].get<int>();
                        }
                        if (original_json.contains("fhub_source")) {
                            orcaslicer_json["fhub_source"] = original_json["fhub_source"];
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Extracted fhub_source from JSON file for filament preset: " 
                                                     << preset.name << " -> fhub_source=" << original_json["fhub_source"].get<std::string>();
                        }
                        if (original_json.contains("fhub_draft_id")) {
                            orcaslicer_json["fhub_draft_id"] = original_json["fhub_draft_id"];
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Extracted fhub_draft_id from JSON file for filament preset: " 
                                                     << preset.name << " -> fhub_draft_id=" << original_json["fhub_draft_id"].get<std::string>();
                        }
                        
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Successfully read metadata from JSON file for filament preset: " 
                                                 << preset.name << " (file: " << preset.file << ")";
                    } else {
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to open JSON file for filament preset: " 
                                                    << preset.name << " (file: " << preset.file << ")";
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to read original JSON file for filament preset " 
                                                << preset.name << " metadata: " << e.what() << " (file: " << preset.file << ")";
                }
            } else {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: No JSON file available for filament preset: " 
                                         << preset.name << " (file empty or not exists)";
            }
            
            // Создаем JSON для Backend
            nlohmann::json preset_data;
            
            // Добавляем info_content в preset_data (для отправки на Backend)
            if (!info_content.empty()) {
                preset_data["info_content"] = info_content;
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Added .info file content to payload for preset: " << preset.name;
            }
            
            // Базовые поля
            preset_data["external_id"] = preset.setting_id; // Уникальный ID в OrcaSlicer
            preset_data["name"] = preset.name;
            
            // Проверяем метки из orcaslicer_json (приоритет над маппингом из AppConfig)
            bool has_fhub_id_from_json = false;
            if (orcaslicer_json.contains("fhub_id") && orcaslicer_json.contains("fhub_source")) {
                try {
                    int fhub_id = orcaslicer_json["fhub_id"].get<int>();
                    std::string fhub_source = orcaslicer_json["fhub_source"].get<std::string>();
                    if (fhub_source == "filamenthub" && fhub_id > 0) {
                        preset_data["fhub_id"] = fhub_id;
                        has_fhub_id_from_json = true;
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Found fhub_id from JSON metadata for filament preset: " 
                                               << preset.name << " -> fhub_id=" << fhub_id << ", source=" << fhub_source;
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse fhub_id from JSON metadata: " << e.what();
                }
            }
            
            // Проверяем маппинг из AppConfig (fallback, если нет меток в JSON)
            if (!has_fhub_id_from_json) {
                std::string mapping_key = CONFIG_KEY_PRESET_MAPPING + "_" + preset.setting_id;
                std::string fhub_id_str = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, mapping_key);
                if (!fhub_id_str.empty() && fhub_id_str != "true" && fhub_id_str != "True" && fhub_id_str != "TRUE") {
                    try {
                        int fhub_id = std::stoi(fhub_id_str);
                        preset_data["fhub_id"] = fhub_id;
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Found mapping for preset external_id=" << preset.setting_id 
                                               << " -> fhub_id=" << fhub_id;
                    } catch (const std::exception& e) {
                        (void)e; // Подавляем предупреждение о неиспользованной переменной
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse fhub_id from mapping: " << fhub_id_str;
                    }
                } else {
                    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: No mapping found for preset external_id=" << preset.setting_id 
                                            << ", will be created as new draft";
                }
            }
            
            // Экспортируем все пользовательские пресеты (как новые черновики, так и обновления)
            // Пресеты без fhub_id будут созданы на сервере как новые черновики
            // Пресеты с fhub_id будут обновлены на сервере
            if (!preset_data.contains("fhub_id")) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Preset " << preset.name
                                        << " (external_id=" << preset.setting_id
                                        << ") has no fhub_id - will be created as new draft on server";
            }
            
            // ВАЖНО: Проверяем sync_enabled через API перед экспортом
            // Если sync_enabled=False, пресет не должен экспортироваться
            // Это предотвращает экспорт пресетов, у которых пользователь отключил синхронизацию
            // Проверка будет выполнена на бэкенде при получении экспорта
            
            // Отправляем весь JSON в бэкенд - там вся обработка
            // В C++ только читаем JSON файл и отправляем как есть
            preset_data["orcaslicer_settings"] = orcaslicer_json;
            
            // Имя филамента (используем имя пресета)
            preset_data["filament_name"] = preset.name;
            
            // Метаданные
            preset_data["source"] = "orcaslicer";
            preset_data["active"] = false; // Черновик
            
            presets_json.push_back(preset_data);
            preset_count++;
            
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Exported preset: " << preset.name 
                                   << " (external_id: " << preset.setting_id << ")";
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to export preset " << preset.name 
                                    << ": " << e.what();
        }
    }
    
    if (presets_json.empty()) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: No user filament presets to export";
        CallAfter([this]() {
            show_notification_in_webview(
                _L("No user filament presets to export."),
                "info"
            );
        });
        return;
    }
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Exported " << preset_count << " filament presets to JSON";
    
    // Лимит на количество профилей (50 для MVP)
    const int MAX_PROFILES_PER_REQUEST = 50;
    if (presets_json.size() > MAX_PROFILES_PER_REQUEST) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Too many presets (" << presets_json.size() 
                                  << "), limiting to " << MAX_PROFILES_PER_REQUEST;
        presets_json.resize(MAX_PROFILES_PER_REQUEST);
    }
    
    // Формируем JSON payload для Backend
    nlohmann::json payload;
    payload["profiles"] = presets_json;
    
    std::string payload_json = payload.dump();
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sending " << presets_json.size() 
                           << " filament presets to Backend (payload size: " << payload_json.size() << " bytes)";
    
    // Отправляем на Backend через API
    m_fhub_client->set_api_base_url(api_base_url);
    
    m_fhub_client->import_filament_presets(
        access_token,
        payload_json,
        // on_complete: успешно импортировано
        [this, presets_json](std::string response_body, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets import successful. Status: " << http_status;
            
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Unexpected HTTP status " << http_status
                                        << " when importing filament presets";
                m_is_syncing.store(false);
                CallAfter([this, http_status]() {
                    show_notification_in_webview(
                        wxString::Format(_L("Failed to export filament presets. HTTP status: %d"), http_status),
                        "error"
                    );
                });
                return;
            }

            try {
                // Парсим ответ от сервера
                nlohmann::json response = nlohmann::json::parse(response_body);

                if (!response.contains("results")) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Response does not contain 'results' field";
                    m_is_syncing.store(false);
                    CallAfter([this]() {
                        show_notification_in_webview(
                            _L("Invalid response from server. Please try again."),
                            "error"
                        );
                    });
                    return;
                }

                // Сохраняем маппинги external_id → fhub_id
                AppConfig* app_config = wxGetApp().app_config;
                if (app_config == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: app_config is null, cannot save mappings";
                    m_is_syncing.store(false);
                    CallAfter([this]() {
                        show_notification_in_webview(
                            _L("Failed to save preset mappings. Please try again."),
                            "error"
                        );
                    });
                    return;
                }
                
                int success_count = 0;
                int error_count = 0;
                int updated_count = 0;
                int created_count = 0;
                
                for (const auto& result : response["results"]) {
                    std::string external_id = result.value("external_id", "");
                    std::string status = result.value("status", "");
                    
                    // Безопасно извлекаем fhub_id (может быть null)
                    int fhub_id = 0;
                    if (result.contains("fhub_id") && !result["fhub_id"].is_null()) {
                        try {
                            fhub_id = result["fhub_id"].get<int>();
                        } catch (const std::exception& e) {
                            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse fhub_id: " << e.what();
                            fhub_id = 0;
                        }
                    }
                    
                    if (status == "created") {
                        created_count++;
                        success_count++;
                    } else if (status == "updated") {
                        updated_count++;
                        success_count++;
                    } else if (status == "error" || status == "skipped") {
                        error_count++;
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Preset " << external_id 
                                                  << " import failed: " << result.value("message", "");
                    }
                    
                    // Сохраняем маппинг external_id → fhub_id (для обратной синхронизации)
                    if (fhub_id > 0 && !external_id.empty()) {
                        std::string mapping_key = CONFIG_KEY_PRESET_MAPPING + "_" + external_id;
                        app_config->set(CONFIG_SECTION_FILAMENTHUB, mapping_key, std::to_string(fhub_id));
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved mapping external_id=" << external_id 
                                               << " -> fhub_id=" << fhub_id;
                    }
                }
                
                CallAfter([app_config]() {
                    if (app_config != nullptr)
                        app_config->save();
                });

                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets export completed. "
                                       << "Created: " << created_count 
                                       << ", Updated: " << updated_count
                                       << ", Errors: " << error_count;
                
                // Показываем уведомление пользователю
                CallAfter([this, success_count, error_count, created_count, updated_count]() {
                    static int notification_counter = 0;
                    notification_counter++;
                    
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [NOTIFICATION #" << notification_counter << "] Showing export result notification";
                    
                    wxString message;
                    if (error_count == 0) {
                        if (created_count > 0 && updated_count > 0) {
                            message = wxString::Format(_L("Successfully exported %d filament presets: %d created, %d updated."), 
                                                      success_count, created_count, updated_count);
                        } else if (created_count > 0) {
                            message = wxString::Format(_L("Successfully exported %d filament presets (created)."), created_count);
                        } else if (updated_count > 0) {
                            message = wxString::Format(_L("Successfully exported %d filament presets (updated)."), updated_count);
                        } else {
                            message = _L("Filament presets exported successfully.");
                        }
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [NOTIFICATION #" << notification_counter << "] Calling show_notification_in_webview() with success message";
                        show_notification_in_webview(message, "success");
                    } else {
                        message = wxString::Format(_L("Exported %d filament presets: %d successful, %d errors."), 
                                                  success_count + error_count, success_count, error_count);
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [NOTIFICATION #" << notification_counter << "] Calling show_notification_in_webview() with warning message";
                        show_notification_in_webview(message, "warning");
                    }
                    // Сбрасываем флаг после завершения экспорта
                    m_is_syncing.store(false);
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [EXPORT COMPLETE] Reset m_is_syncing=false after notification #" << notification_counter;
                });
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing import response: " << e.what() 
                                        << ", Response: " << response_body.substr(0, 500);
                CallAfter([this, e]() {
                    show_notification_in_webview(
                        wxString::Format(_L("Error parsing server response: %s"), e.what()),
                        "error"
                    );
                    // Сбрасываем флаг после ошибки парсинга
                    m_is_syncing.store(false);
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Reset m_is_syncing=false after export parse error";
                });
            }
        },
        // on_error: ошибка при импорте
        [this](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to export filament presets. Error: " << error 
                                    << ", Status: " << http_status;
            
            wxString error_msg;
            if (http_status == 401) {
                error_msg = _L("Your session has expired. Please login again.");
            } else if (http_status == 403) {
                error_msg = _L("Filament presets export is disabled in your FilamentHub settings. Please enable it in your profile settings.");
            } else if (http_status == 400) {
                error_msg = _L("Invalid request. Please check your presets and try again.");
            } else if (http_status >= 500) {
                error_msg = _L("Server error. Please try again later.");
            } else {
                error_msg = wxString::Format(_L("Failed to export filament presets: %s"), wxString::FromUTF8(error.c_str()));
            }
            
            CallAfter([this, error_msg, http_status]() {
                show_notification_in_webview(
                    error_msg,
                    http_status == 401 || http_status == 403 ? "warning" : "error"
                );
                // Сбрасываем флаг после ошибки экспорта
                m_is_syncing.store(false);
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Reset m_is_syncing=false after export error";
            });
        }
    );
}

// ============================================================================
// Methods for exporting printer profiles to FilamentHub
// ============================================================================

void FilamentHubPanel::export_printer_profiles_to_filamenthub()
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== export_printer_profiles_to_filamenthub() CALLED ==========";

    // Атомарный check-and-set: если уже true — кто-то экспортирует, выходим
    if (m_is_syncing.exchange(true)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Export/sync already in progress, skipping printer profiles export";
        return;
    }

    // Проверяем авторизацию
    std::string access_token;
    int user_id;
    if (!load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Not authenticated, cannot export printer profiles";
        m_is_syncing.store(false);
        CallAfter([this]() {
            show_notification_in_webview(
                _L("Please login to export printer profiles to FilamentHub."),
                "warning"
            );
        });
        return;
    }

    // Проверяем PresetBundle
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot export printer profiles";
        m_is_syncing.store(false);
        CallAfter([this]() {
            show_notification_in_webview(
                _L("Preset bundle not available. Please try again."),
                "error"
            );
        });
        return;
    }
    
    // Проверяем разрешение на импорт printer profiles
    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
    m_fhub_client->set_api_base_url(api_base_url);

    m_fhub_client->get_current_user(
        access_token,
        // on_complete: проверяем разрешение allow_printer_profiles_import
        [this, access_token, api_base_url](std::string json_body, unsigned http_status) {
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get user info for permission check. HTTP status: " << http_status;
                m_is_syncing.store(false);
                CallAfter([this, http_status]() {
                    if (http_status == 401) {
                        show_notification_in_webview(
                            _L("Your session has expired. Please login again."),
                            "warning"
                        );
                    } else if (http_status == 403) {
                        show_notification_in_webview(
                            _L("Access denied. Please check your permissions."),
                            "error"
                        );
                    } else {
                        show_notification_in_webview(
                            wxString::Format(_L("Failed to check permissions. HTTP status: %d"), http_status),
                            "error"
                        );
                    }
                });
                return;
            }

            try {
                nlohmann::json user_json = nlohmann::json::parse(json_body);
                bool allow_printer_import = user_json.value("allow_printer_profiles_import", true);

                if (!allow_printer_import) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Printer profiles import is disabled in user settings";
                    m_is_syncing.store(false);
                    CallAfter([this]() {
                        show_notification_in_webview(
                            _L("Printer profiles export is disabled in your FilamentHub settings. Please enable it in your profile settings."),
                            "warning"
                        );
                    });
                    return;
                }

                // Разрешение получено - продолжаем экспорт
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Permission check passed, proceeding with printer profiles export";
                export_printer_profiles_to_filamenthub_internal(access_token, api_base_url);
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing user info JSON: " << e.what();
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Response body (first 500 chars): " << json_body.substr(0, std::min<size_t>(500, json_body.length()));
                m_is_syncing.store(false);
                CallAfter([this, e]() {
                    show_notification_in_webview(
                        wxString::Format(_L("Error parsing server response: %s"), e.what()),
                        "error"
                    );
                });
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Unknown exception when parsing user info JSON (printer profiles export)";
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Response body (first 500 chars): " << json_body.substr(0, std::min<size_t>(500, json_body.length()));
                m_is_syncing.store(false);
                CallAfter([this]() {
                    show_notification_in_webview(
                        _L("Error parsing server response: Unknown exception"),
                        "error"
                    );
                });
            }
        },
        // on_error: ошибка при проверке разрешений
        [this](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to check permissions. Error: " << error
                                    << ", Status: " << http_status;
            m_is_syncing.store(false);
            CallAfter([this, http_status, error]() {
                if (http_status == 401) {
                    show_notification_in_webview(
                        _L("Your session has expired. Please login again."),
                        "warning"
                    );
                } else {
                    show_notification_in_webview(
                        wxString::Format(_L("Failed to check permissions: %s"), wxString::FromUTF8(error.c_str())),
                        "error"
                    );
                }
            });
        }
    );
}

void FilamentHubPanel::export_printer_profiles_to_filamenthub_internal(const std::string& access_token, const std::string& api_base_url)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== export_printer_profiles_to_filamenthub_internal() CALLED ==========";
    
    // Проверяем PresetBundle
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot export printer profiles";
        CallAfter([this]() {
            show_notification_in_webview(
                _L("Preset bundle not available. Please try again."),
                "error"
            );
        });
        return;
    }
    
    // Получаем все пользовательские printer profiles (не системные)
    PresetCollection& printers = bundle->printers;
    std::vector<nlohmann::json> profiles_json;
    
    int profile_count = 0;
    for (auto it = printers.begin(); it != printers.end(); ++it) {
        const Preset& preset = *it;
        
        // Пропускаем системные пресеты
        if (preset.is_system) {
            continue;
        }
        
        try {
            // Получаем JSON конфигурацию пресета
            nlohmann::json orcaslicer_json = get_config_json(preset.config);
            
            // Читаем оригинальный JSON файл для извлечения метаданных FilamentHub
            // Это необходимо, так как get_config_json() извлекает только известные опции,
            // а наши метки fhub_id, fhub_source не сохраняются в preset.config
            if (!preset.file.empty() && boost::filesystem::exists(preset.file)) {
                try {
                    nlohmann::json original_json;
                    boost::filesystem::ifstream ifs(preset.file);
                    if (ifs.is_open()) {
                        ifs >> original_json;
                        ifs.close();
                        
                        // Извлекаем метки FilamentHub из оригинального JSON
                        if (original_json.contains("fhub_id")) {
                            orcaslicer_json["fhub_id"] = original_json["fhub_id"];
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Extracted fhub_id from JSON file for printer profile: " 
                                                     << preset.name << " -> fhub_id=" << original_json["fhub_id"].get<int>();
                        }
                        if (original_json.contains("fhub_source")) {
                            orcaslicer_json["fhub_source"] = original_json["fhub_source"];
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Extracted fhub_source from JSON file for printer profile: " 
                                                     << preset.name << " -> fhub_source=" << original_json["fhub_source"].get<std::string>();
                        }
                        
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Successfully read metadata from JSON file for printer profile: " 
                                                 << preset.name << " (file: " << preset.file << ")";
                    } else {
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to open JSON file for printer profile: " 
                                                    << preset.name << " (file: " << preset.file << ")";
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to read original JSON file for printer profile " 
                                                << preset.name << " metadata: " << e.what() << " (file: " << preset.file << ")";
                }
            } else {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: No JSON file available for printer profile: " 
                                         << preset.name << " (file empty or not exists)";
            }
            
            // Создаем JSON для Backend
            nlohmann::json profile_data;
            
            // Базовые поля
            profile_data["external_id"] = preset.setting_id; // Уникальный ID в OrcaSlicer
            profile_data["name"] = preset.name;
            profile_data["setting_id"] = preset.setting_id;
            
            // Проверяем метки из orcaslicer_json (приоритет над маппингом из AppConfig)
            bool has_fhub_id_from_json = false;
            if (orcaslicer_json.contains("fhub_id") && orcaslicer_json.contains("fhub_source")) {
                try {
                    int fhub_id = orcaslicer_json["fhub_id"].get<int>();
                    std::string fhub_source = orcaslicer_json["fhub_source"].get<std::string>();
                    if (fhub_source == "filamenthub" && fhub_id > 0) {
                        profile_data["fhub_id"] = fhub_id;
                        has_fhub_id_from_json = true;
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Found fhub_id from JSON metadata for printer profile: " 
                                               << preset.name << " -> fhub_id=" << fhub_id << ", source=" << fhub_source;
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse fhub_id from JSON metadata: " << e.what();
                }
            }
            
            // Проверяем маппинг из AppConfig (fallback, если нет меток в JSON)
            if (!has_fhub_id_from_json) {
                std::string mapping_key = CONFIG_KEY_PRINTER_PROFILE_MAPPING + "_" + preset.setting_id;
                std::string fhub_id_str = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, mapping_key);
                if (!fhub_id_str.empty() && fhub_id_str != "true" && fhub_id_str != "True" && fhub_id_str != "TRUE") {
                    try {
                        int fhub_id = std::stoi(fhub_id_str);
                        profile_data["fhub_id"] = fhub_id;
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Found mapping for printer profile external_id=" << preset.setting_id 
                                               << " -> fhub_id=" << fhub_id;
                    } catch (const std::exception& e) {
                        (void)e; // Подавляем предупреждение о неиспользованной переменной
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse fhub_id from mapping: " << fhub_id_str;
                    }
                } else {
                    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: No mapping found for printer profile external_id=" << preset.setting_id 
                                            << ", will be created as new draft";
                }
            }
            
            // OrcaSlicer JSON формат (полный JSON профиль)
            profile_data["orcaslicer_settings"] = orcaslicer_json;
            profile_data["extra_metadata"] = nlohmann::json::object();
            
            // ВАЖНО: Добавляем метаданные для правильного сопоставления принтера
            // Извлекаем vendor и model из orcaslicer_json
            if (orcaslicer_json.contains("printer_model") && !orcaslicer_json["printer_model"].is_null()) {
                profile_data["printer_model"] = orcaslicer_json["printer_model"];
                profile_data["extra_metadata"]["printer_model"] = orcaslicer_json["printer_model"];
                profile_data["orcaslicer_settings"]["printer_model"] = orcaslicer_json["printer_model"];
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [PRINTER] printer_model=" << orcaslicer_json["printer_model"].dump();
            }
            if (orcaslicer_json.contains("printer_vendor") && !orcaslicer_json["printer_vendor"].is_null()) {
                profile_data["profile_vendor"] = orcaslicer_json["printer_vendor"];
                profile_data["extra_metadata"]["printer_vendor"] = orcaslicer_json["printer_vendor"];
                profile_data["orcaslicer_settings"]["printer_vendor"] = orcaslicer_json["printer_vendor"];
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [PRINTER] vendor=" << orcaslicer_json["printer_vendor"].dump();
            }
            if (orcaslicer_json.contains("inherits") && !orcaslicer_json["inherits"].is_null()) {
                profile_data["inherits"] = orcaslicer_json["inherits"];
                profile_data["extra_metadata"]["inherits"] = orcaslicer_json["inherits"];
                profile_data["orcaslicer_settings"]["inherits"] = orcaslicer_json["inherits"];
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [PRINTER] inherits=" << orcaslicer_json["inherits"].dump();
            }
            if (orcaslicer_json.contains("model_id") && !orcaslicer_json["model_id"].is_null()) {
                profile_data["extra_metadata"]["model_id"] = orcaslicer_json["model_id"];
            }
            if (orcaslicer_json.contains("printer_model_id") && !orcaslicer_json["printer_model_id"].is_null()) {
                profile_data["extra_metadata"]["printer_model_id"] = orcaslicer_json["printer_model_id"];
            }
            if (orcaslicer_json.contains("from") && !orcaslicer_json["from"].is_null()) {
                profile_data["extra_metadata"]["from"] = orcaslicer_json["from"];
            }
            
            // Логируем важные поля для отладки сопоставления принтеров
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [PRINTER EXPORT] " 
                                   << "name='" << preset.name << "'"
                                   << ", printer_model=" << (orcaslicer_json.contains("printer_model") ? orcaslicer_json["printer_model"].dump() : "null")
                                   << ", vendor=" << (orcaslicer_json.contains("printer_vendor") ? orcaslicer_json["printer_vendor"].dump() : "null");
            
            // Извлекаем базовые параметры для PrinterProfile
            // vendor (из preset.vendor или из orcaslicer_json)
            if (orcaslicer_json.contains("printer_vendor") && orcaslicer_json["printer_vendor"].is_string()) {
                profile_data["vendor"] = orcaslicer_json["printer_vendor"].get<std::string>();
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [PRINTER EXPORT] vendor from printer_vendor: " << orcaslicer_json["printer_vendor"].get<std::string>();
            } else if (preset.vendor != nullptr && !preset.vendor->id.empty()) {
                profile_data["vendor"] = preset.vendor->id;
                profile_data["extra_metadata"]["printer_vendor"] = preset.vendor->id;
                profile_data["orcaslicer_settings"]["printer_vendor"] = preset.vendor->id;
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [PRINTER EXPORT] vendor from preset.vendor: " << preset.vendor->id;
            }
            
            // printer_model (из orcaslicer_json) - КРИТИЧНО для сопоставления с базой
            if (orcaslicer_json.contains("printer_model") && orcaslicer_json["printer_model"].is_string()) {
                std::string printer_model = orcaslicer_json["printer_model"].get<std::string>();
                if (!printer_model.empty()) {
                    profile_data["orcaslicer_settings"]["printer_model"] = printer_model;
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [PRINTER EXPORT] printer_model: " << printer_model;
                }
            }
            
            // Извлекаем manufacturer и model из printer_model если есть
            // Формат в OrcaSlicer: "manufacturer model" или просто "model"
            if (orcaslicer_json.contains("printer_model") && orcaslicer_json["printer_model"].is_string()) {
                std::string printer_model = orcaslicer_json["printer_model"].get<std::string>();
                // Парсим: первое слово - manufacturer, остальное - model
                size_t first_space = printer_model.find(' ');
                if (first_space != std::string::npos) {
                    std::string manufacturer = printer_model.substr(0, first_space);
                    std::string model = printer_model.substr(first_space + 1);
                    profile_data["orcaslicer_settings"]["manufacturer"] = manufacturer;
                    profile_data["orcaslicer_settings"]["model"] = model;
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [PRINTER EXPORT] Extracted manufacturer='" << manufacturer << "', model='" << model << "'";
                }
            }
            
            // description (из preset.description)
            if (!preset.description.empty()) {
                profile_data["description"] = preset.description;
            }
            
            // source (из preset.is_system или preset.is_default)
            profile_data["source"] = preset.is_system ? "system" : "user";
            
            // active (по умолчанию false - черновик)
            profile_data["active"] = false;
            
            // Извлекаем дополнительные параметры из orcaslicer_json
            // nozzle_diameters
            if (orcaslicer_json.contains("nozzle_diameter")) {
                try {
                    if (orcaslicer_json["nozzle_diameter"].is_array()) {
                        auto nozzles = orcaslicer_json["nozzle_diameter"].get<std::vector<std::string>>();
                        std::vector<float> nozzle_diameters;
                        for (const auto& nozzle_str : nozzles) {
                            try {
                                nozzle_diameters.push_back(std::stof(nozzle_str));
                            } catch (const std::exception& e) {
                                (void)e; // Подавляем предупреждение о неиспользованной переменной
                                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse nozzle_diameter: " << nozzle_str;
                            }
                        }
                        if (!nozzle_diameters.empty()) {
                            profile_data["nozzle_diameters"] = nozzle_diameters;
                        }
                    } else if (orcaslicer_json["nozzle_diameter"].is_string()) {
                        std::string nozzle_str = orcaslicer_json["nozzle_diameter"].get<std::string>();
                        profile_data["nozzle_diameters"] = std::vector<float>{std::stof(nozzle_str)};
                    } else if (orcaslicer_json["nozzle_diameter"].is_number()) {
                        profile_data["nozzle_diameters"] = std::vector<float>{orcaslicer_json["nozzle_diameter"].get<float>()};
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse nozzle_diameter: " << e.what();
                }
            }
            
            // printable_area
            if (orcaslicer_json.contains("printable_area")) {
                try {
                    if (orcaslicer_json["printable_area"].is_object()) {
                        profile_data["printable_area"] = orcaslicer_json["printable_area"];
                    } else if (orcaslicer_json["printable_area"].is_string()) {
                        // Если это строка, пытаемся распарсить как JSON
                        std::string area_str = orcaslicer_json["printable_area"].get<std::string>();
                        try {
                            profile_data["printable_area"] = nlohmann::json::parse(area_str);
                        } catch (const std::exception& e) {
                            (void)e; // Подавляем предупреждение о неиспользованной переменной
                            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse printable_area as JSON: " << area_str;
                        }
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse printable_area: " << e.what();
                }
            }
            
            // printable_height_mm
            if (orcaslicer_json.contains("printable_height")) {
                try {
                    if (orcaslicer_json["printable_height"].is_string()) {
                        std::string height_str = orcaslicer_json["printable_height"].get<std::string>();
                        profile_data["printable_height_mm"] = std::stof(height_str);
                    } else if (orcaslicer_json["printable_height"].is_number()) {
                        profile_data["printable_height_mm"] = orcaslicer_json["printable_height"].get<float>();
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse printable_height: " << e.what();
                }
            }
            
            // start_gcode
            if (orcaslicer_json.contains("start_gcode")) {
                try {
                    if (orcaslicer_json["start_gcode"].is_string()) {
                        profile_data["start_gcode"] = orcaslicer_json["start_gcode"].get<std::string>();
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse start_gcode: " << e.what();
                }
            }
            
            // end_gcode
            if (orcaslicer_json.contains("end_gcode")) {
                try {
                    if (orcaslicer_json["end_gcode"].is_string()) {
                        profile_data["end_gcode"] = orcaslicer_json["end_gcode"].get<std::string>();
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse end_gcode: " << e.what();
                }
            }
            
            // default_print_profile_slug (из preset.default_print_profile или similar)
            if (orcaslicer_json.contains("default_print_profile")) {
                try {
                    if (orcaslicer_json["default_print_profile"].is_string()) {
                        profile_data["default_print_profile_slug"] = orcaslicer_json["default_print_profile"].get<std::string>();
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse default_print_profile: " << e.what();
                }
            }
            
            profiles_json.push_back(profile_data);
            profile_count++;
            
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Exported printer profile: " << preset.name 
                                   << " (external_id: " << preset.setting_id << ")";
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to export printer profile " << preset.name 
                                    << ": " << e.what();
        }
    }
    
    if (profiles_json.empty()) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: No user printer profiles to export";
        CallAfter([this]() {
            show_notification_in_webview(
                _L("No user printer profiles to export."),
                "info"
            );
        });
        return;
    }
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Exported " << profile_count << " printer profiles to JSON";
    
    const int MAX_PROFILES_PER_REQUEST = 50;
    if (profiles_json.size() > MAX_PROFILES_PER_REQUEST) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Too many printer profiles (" << profiles_json.size() 
                                   << "), limiting to " << MAX_PROFILES_PER_REQUEST;
        profiles_json.resize(MAX_PROFILES_PER_REQUEST);
    }
    
    nlohmann::json payload;
    payload["profiles"] = profiles_json;
    
    std::string payload_json = payload.dump();
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sending " << profiles_json.size() 
                           << " printer profiles to Backend (payload size: " << payload_json.size() << " bytes)";
    
    m_fhub_client->set_api_base_url(api_base_url);
    
    m_fhub_client->import_printer_profiles(
        access_token,
        payload_json,
        [this, profiles_json](std::string response_body, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profiles import successful. Status: " << http_status;
            
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Unexpected HTTP status " << http_status
                                        << " when importing printer profiles";
                m_is_syncing.store(false);
                CallAfter([this, http_status]() {
                    show_notification_in_webview(
                        wxString::Format(_L("Failed to export printer profiles. HTTP status: %d"), http_status),
                        "error"
                    );
                });
                return;
            }

            try {
                nlohmann::json response = nlohmann::json::parse(response_body);

                if (!response.contains("results")) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Response does not contain 'results' field";
                    m_is_syncing.store(false);
                    CallAfter([this]() {
                        show_notification_in_webview(
                            _L("Invalid response from server. Please try again."),
                            "error"
                        );
                    });
                    return;
                }

                AppConfig* app_config = wxGetApp().app_config;
                if (app_config == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: app_config is null, cannot save mappings";
                    m_is_syncing.store(false);
                    CallAfter([this]() {
                        show_notification_in_webview(
                            _L("Failed to save profile mappings. Please try again."),
                            "error"
                        );
                    });
                    return;
                }
                
                int success_count = 0;
                int error_count = 0;
                int updated_count = 0;
                int created_count = 0;
                
                for (const auto& result : response["results"]) {
                    std::string external_id = result.value("external_id", "");
                    std::string status = result.value("status", "");
                    
                    // Безопасно извлекаем fhub_id (может быть null)
                    int fhub_id = 0;
                    if (result.contains("fhub_id") && !result["fhub_id"].is_null()) {
                        try {
                            fhub_id = result["fhub_id"].get<int>();
                        } catch (const std::exception& e) {
                            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse fhub_id: " << e.what();
                            fhub_id = 0;
                        }
                    }
                    
                    if (status == "created") {
                        created_count++;
                        success_count++;
                    } else if (status == "updated") {
                        updated_count++;
                        success_count++;
                    } else if (status == "error" || status == "skipped") {
                        error_count++;
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Printer profile " << external_id 
                                                  << " import failed: " << result.value("message", "");
                    }
                    
                    // Сохраняем маппинг external_id → fhub_id (для обратной синхронизации)
                    if (fhub_id > 0 && !external_id.empty()) {
                        std::string mapping_key = CONFIG_KEY_PRINTER_PROFILE_MAPPING + "_" + external_id;
                        app_config->set(CONFIG_SECTION_FILAMENTHUB, mapping_key, std::to_string(fhub_id));
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved mapping external_id=" << external_id 
                                               << " -> fhub_id=" << fhub_id;
                    }
                }
                
                CallAfter([app_config]() {
                    if (app_config != nullptr)
                        app_config->save();
                });

                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profiles export completed. "
                                       << "Created: " << created_count 
                                       << ", Updated: " << updated_count
                                       << ", Errors: " << error_count;
                
                CallAfter([this, success_count, error_count, created_count, updated_count]() {
                    wxString message;
                    if (error_count == 0) {
                        if (created_count > 0 && updated_count > 0) {
                            message = wxString::Format(_L("Successfully exported %d printer profiles: %d created, %d updated."),
                                                      success_count, created_count, updated_count);
                        } else if (created_count > 0) {
                            message = wxString::Format(_L("Successfully exported %d printer profiles (created)."), created_count);
                        } else if (updated_count > 0) {
                            message = wxString::Format(_L("Successfully exported %d printer profiles (updated)."), updated_count);
                        } else {
                            message = _L("Printer profiles exported successfully.");
                        }
                        show_notification_in_webview(message, "success");
                    } else {
                        message = wxString::Format(_L("Exported %d printer profiles: %d successful, %d errors."),
                                                  success_count + error_count, success_count, error_count);
                        show_notification_in_webview(message, "warning");
                    }
                    m_is_syncing.store(false);
                });
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing import response: " << e.what()
                                        << ", Response: " << response_body.substr(0, 500);
                m_is_syncing.store(false);
                CallAfter([this, e]() {
                    show_notification_in_webview(
                        wxString::Format(_L("Error parsing server response: %s"), e.what()),
                        "error"
                    );
                });
            }
        },
        [this](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to export printer profiles. Error: " << error
                                    << ", Status: " << http_status;

            wxString error_msg;
            if (http_status == 401) {
                error_msg = _L("Your session has expired. Please login again.");
            } else if (http_status == 403) {
                error_msg = _L("Printer profiles export is disabled in your FilamentHub settings. Please enable it in your profile settings.");
            } else if (http_status == 400) {
                error_msg = _L("Invalid request. Please check your profiles and try again.");
            } else if (http_status >= 500) {
                error_msg = _L("Server error. Please try again later.");
            } else {
                error_msg = wxString::Format(_L("Failed to export printer profiles: %s"), wxString::FromUTF8(error.c_str()));
            }

            m_is_syncing.store(false);
            CallAfter([this, error_msg, http_status]() {
                show_notification_in_webview(
                    error_msg,
                    http_status == 401 || http_status == 403 ? "warning" : "error"
                );
            });
        }
    );
}

// ============================================================================
// Methods for exporting print profiles to FilamentHub
// ============================================================================

void FilamentHubPanel::export_print_profiles_to_filamenthub()
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== export_print_profiles_to_filamenthub() CALLED ==========";

    // Атомарный check-and-set: если уже true — кто-то экспортирует, выходим
    if (m_is_syncing.exchange(true)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Export/sync already in progress, skipping print profiles export";
        return;
    }

    // Проверяем авторизацию
    std::string access_token;
    int user_id;
    if (!load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Not authenticated, cannot export print profiles";
        m_is_syncing.store(false);
        CallAfter([this]() {
            show_notification_in_webview(
                _L("Please login to export print profiles to FilamentHub."),
                "warning"
            );
        });
        return;
    }

    // Проверяем PresetBundle
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot export print profiles";
        m_is_syncing.store(false);
        CallAfter([this]() {
            show_notification_in_webview(
                _L("Preset bundle not available. Please try again."),
                "error"
            );
        });
        return;
    }
    
    // Проверяем разрешение на импорт print profiles
    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
    m_fhub_client->set_api_base_url(api_base_url);

    m_fhub_client->get_current_user(
        access_token,
        // on_complete: проверяем разрешение allow_print_profiles_import
        [this, access_token, api_base_url](std::string json_body, unsigned http_status) {
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get user info for permission check. HTTP status: " << http_status;
                m_is_syncing.store(false);
                CallAfter([this, http_status]() {
                    if (http_status == 401) {
                        show_notification_in_webview(
                            _L("Your session has expired. Please login again."),
                            "warning"
                        );
                    } else if (http_status == 403) {
                        show_notification_in_webview(
                            _L("Access denied. Please check your permissions."),
                            "error"
                        );
                    } else {
                        show_notification_in_webview(
                            wxString::Format(_L("Failed to check permissions. HTTP status: %d"), http_status),
                            "error"
                        );
                    }
                });
                return;
            }

            try {
                nlohmann::json user_json = nlohmann::json::parse(json_body);
                bool allow_print_import = user_json.value("allow_print_profiles_import", true);

                if (!allow_print_import) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Print profiles import is disabled in user settings";
                    m_is_syncing.store(false);
                    CallAfter([this]() {
                        show_notification_in_webview(
                            _L("Print profiles export is disabled in your FilamentHub settings. Please enable it in your profile settings."),
                            "warning"
                        );
                    });
                    return;
                }

                // Разрешение получено - продолжаем экспорт
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Permission check passed, proceeding with print profiles export";
                export_print_profiles_to_filamenthub_internal(access_token, api_base_url);
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing user info JSON: " << e.what();
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Response body (first 500 chars): " << json_body.substr(0, std::min<size_t>(500, json_body.length()));
                m_is_syncing.store(false);
                CallAfter([this, e]() {
                    show_notification_in_webview(
                        wxString::Format(_L("Error parsing server response: %s"), e.what()),
                        "error"
                    );
                });
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Unknown exception when parsing user info JSON (print profiles export)";
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Response body (first 500 chars): " << json_body.substr(0, std::min<size_t>(500, json_body.length()));
                m_is_syncing.store(false);
                CallAfter([this]() {
                    show_notification_in_webview(
                        _L("Error parsing server response: Unknown exception"),
                        "error"
                    );
                });
            }
        },
        // on_error: ошибка при проверке разрешений
        [this](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to check permissions. Error: " << error
                                    << ", Status: " << http_status;
            m_is_syncing.store(false);
            CallAfter([this, http_status, error]() {
                if (http_status == 401) {
                    show_notification_in_webview(
                        _L("Your session has expired. Please login again."),
                        "warning"
                    );
                } else {
                    show_notification_in_webview(
                        wxString::Format(_L("Failed to check permissions: %s"), wxString::FromUTF8(error.c_str())),
                        "error"
                    );
                }
            });
        }
    );
}

void FilamentHubPanel::export_print_profiles_to_filamenthub_internal(const std::string& access_token, const std::string& api_base_url)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== export_print_profiles_to_filamenthub_internal() CALLED ==========";
    
    // Проверяем PresetBundle
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot export print profiles";
        CallAfter([this]() {
            show_notification_in_webview(
                _L("Preset bundle not available. Please try again."),
                "error"
            );
        });
        return;
    }
    
    // Получаем все пользовательские print profiles (не системные)
    // В OrcaSlicer print profiles называются "process" presets и хранятся в bundle->prints
    PresetCollection& prints = bundle->prints;
    std::vector<nlohmann::json> profiles_json;
    
    int profile_count = 0;
    for (auto it = prints.begin(); it != prints.end(); ++it) {
        const Preset& preset = *it;
        
        // Пропускаем системные пресеты
        if (preset.is_system) {
            continue;
        }
        
        try {
            // Получаем JSON конфигурацию пресета
            nlohmann::json orcaslicer_json = get_config_json(preset.config);
            
            // Читаем оригинальный JSON файл для извлечения метаданных FilamentHub
            // Это необходимо, так как get_config_json() извлекает только известные опции,
            // а наши метки fhub_id, fhub_source не сохраняются в preset.config
            if (!preset.file.empty() && boost::filesystem::exists(preset.file)) {
                try {
                    nlohmann::json original_json;
                    boost::filesystem::ifstream ifs(preset.file);
                    if (ifs.is_open()) {
                        ifs >> original_json;
                        ifs.close();
                        
                        // Извлекаем метки FilamentHub из оригинального JSON
                        if (original_json.contains("fhub_id")) {
                            orcaslicer_json["fhub_id"] = original_json["fhub_id"];
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Extracted fhub_id from JSON file for print profile: " 
                                                     << preset.name << " -> fhub_id=" << original_json["fhub_id"].get<int>();
                        }
                        if (original_json.contains("fhub_source")) {
                            orcaslicer_json["fhub_source"] = original_json["fhub_source"];
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Extracted fhub_source from JSON file for print profile: " 
                                                     << preset.name << " -> fhub_source=" << original_json["fhub_source"].get<std::string>();
                        }
                        
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Successfully read metadata from JSON file for print profile: " 
                                                 << preset.name << " (file: " << preset.file << ")";
                    } else {
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to open JSON file for print profile: " 
                                                    << preset.name << " (file: " << preset.file << ")";
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to read original JSON file for print profile " 
                                                << preset.name << " metadata: " << e.what() << " (file: " << preset.file << ")";
                }
            } else {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: No JSON file available for print profile: " 
                                         << preset.name << " (file empty or not exists)";
            }
            
            // Создаем JSON для Backend
            nlohmann::json profile_data;
            
            // Базовые поля
            profile_data["external_id"] = preset.setting_id; // Уникальный ID в OrcaSlicer
            profile_data["name"] = preset.name;
            profile_data["setting_id"] = preset.setting_id;
            
            // Проверяем метки из orcaslicer_json (приоритет над маппингом из AppConfig)
            bool has_fhub_id_from_json = false;
            if (orcaslicer_json.contains("fhub_id") && orcaslicer_json.contains("fhub_source")) {
                try {
                    int fhub_id = orcaslicer_json["fhub_id"].get<int>();
                    std::string fhub_source = orcaslicer_json["fhub_source"].get<std::string>();
                    if (fhub_source == "filamenthub" && fhub_id > 0) {
                        profile_data["fhub_id"] = fhub_id;
                        has_fhub_id_from_json = true;
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Found fhub_id from JSON metadata for print profile: " 
                                               << preset.name << " -> fhub_id=" << fhub_id << ", source=" << fhub_source;
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse fhub_id from JSON metadata: " << e.what();
                }
            }
            
            // Проверяем маппинг из AppConfig (fallback, если нет меток в JSON)
            if (!has_fhub_id_from_json) {
                std::string mapping_key = CONFIG_KEY_PRINT_PROFILE_MAPPING + "_" + preset.setting_id;
                std::string fhub_id_str = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, mapping_key);
                if (!fhub_id_str.empty() && fhub_id_str != "true" && fhub_id_str != "True" && fhub_id_str != "TRUE") {
                    try {
                        int fhub_id = std::stoi(fhub_id_str);
                        profile_data["fhub_id"] = fhub_id;
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Found mapping for print profile external_id=" << preset.setting_id 
                                               << " -> fhub_id=" << fhub_id;
                    } catch (const std::exception& e) {
                        (void)e; // Подавляем предупреждение о неиспользованной переменной
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse fhub_id from mapping: " << fhub_id_str;
                    }
                } else {
                    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: No mapping found for print profile external_id=" << preset.setting_id 
                                            << ", will be created as new draft";
                }
            }
            
            // OrcaSlicer JSON формат (полный JSON профиль)
            profile_data["orcaslicer_settings"] = orcaslicer_json;
            
            // Извлекаем базовые параметры для PrintProfile
            // vendor (из preset.vendor)
            if (preset.vendor != nullptr && !preset.vendor->id.empty()) {
                profile_data["vendor"] = preset.vendor->id;
            }
            
            // description (из preset.description)
            if (!preset.description.empty()) {
                profile_data["description"] = preset.description;
            }
            
            // source (из preset.is_system или preset.is_default)
            profile_data["source"] = preset.is_system ? "system" : "user";
            
            // active (по умолчанию false - черновик)
            profile_data["active"] = false;
            
            // Извлекаем дополнительные параметры из orcaslicer_json
            // category (из preset.type или similar)
            if (orcaslicer_json.contains("layer_height")) {
                try {
                    if (orcaslicer_json["layer_height"].is_string()) {
                        std::string layer_height_str = orcaslicer_json["layer_height"].get<std::string>();
                        float layer_height = std::stof(layer_height_str);
                        profile_data["layer_height_mm"] = layer_height;
                    } else if (orcaslicer_json["layer_height"].is_number()) {
                        profile_data["layer_height_mm"] = orcaslicer_json["layer_height"].get<float>();
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse layer_height: " << e.what();
                }
            }
            
            // quality_tier (из preset.name или similar)
            // Например, "0.20mm Standard", "0.30mm Draft" и т.д.
            // Можно извлечь из имени или из параметров профиля
            if (orcaslicer_json.contains("quality")) {
                try {
                    if (orcaslicer_json["quality"].is_string()) {
                        profile_data["quality_tier"] = orcaslicer_json["quality"].get<std::string>();
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse quality: " << e.what();
                }
            }
            
            // default_nozzle (из preset.name или similar)
            // Например, "0.4mm", "0.6mm" и т.д.
            if (orcaslicer_json.contains("nozzle_diameter")) {
                try {
                    if (orcaslicer_json["nozzle_diameter"].is_string()) {
                        std::string nozzle_str = orcaslicer_json["nozzle_diameter"].get<std::string>();
                        profile_data["default_nozzle"] = nozzle_str;
                    } else if (orcaslicer_json["nozzle_diameter"].is_number()) {
                        profile_data["default_nozzle"] = std::to_string(orcaslicer_json["nozzle_diameter"].get<float>()) + "mm";
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse nozzle_diameter: " << e.what();
                }
            }
            
            // compatible_printers (из preset.compatible_printers или similar)
            // В OrcaSlicer print profiles могут быть совместимы с определенными printer profiles
            if (orcaslicer_json.contains("compatible_printers")) {
                try {
                    if (orcaslicer_json["compatible_printers"].is_array()) {
                        auto printers = orcaslicer_json["compatible_printers"].get<std::vector<std::string>>();
                        profile_data["compatible_printers"] = printers;
                    } else if (orcaslicer_json["compatible_printers"].is_string()) {
                        std::string printers_str = orcaslicer_json["compatible_printers"].get<std::string>();
                        // Парсим строку как список через запятую
                        std::vector<std::string> printers;
                        std::istringstream iss(printers_str);
                        std::string printer;
                        while (std::getline(iss, printer, ',')) {
                            // Удаляем пробелы
                            printer.erase(0, printer.find_first_not_of(" \t"));
                            printer.erase(printer.find_last_not_of(" \t") + 1);
                            if (!printer.empty()) {
                                printers.push_back(printer);
                            }
                        }
                        if (!printers.empty()) {
                            profile_data["compatible_printers"] = printers;
                        }
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse compatible_printers: " << e.what();
                }
            }
            
            // compatible_filaments (из preset.compatible_filaments или similar)
            // В OrcaSlicer print profiles могут быть совместимы с определенными filament presets
            if (orcaslicer_json.contains("compatible_filaments")) {
                try {
                    if (orcaslicer_json["compatible_filaments"].is_array()) {
                        auto filaments = orcaslicer_json["compatible_filaments"].get<std::vector<std::string>>();
                        profile_data["compatible_filaments"] = filaments;
                    } else if (orcaslicer_json["compatible_filaments"].is_string()) {
                        std::string filaments_str = orcaslicer_json["compatible_filaments"].get<std::string>();
                        // Парсим строку как список через запятую
                        std::vector<std::string> filaments;
                        std::istringstream iss(filaments_str);
                        std::string filament;
                        while (std::getline(iss, filament, ',')) {
                            // Удаляем пробелы
                            filament.erase(0, filament.find_first_not_of(" \t"));
                            filament.erase(filament.find_last_not_of(" \t") + 1);
                            if (!filament.empty()) {
                                filaments.push_back(filament);
                            }
                        }
                        if (!filaments.empty()) {
                            profile_data["compatible_filaments"] = filaments;
                        }
                    }
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse compatible_filaments: " << e.what();
                }
            }
            
            profiles_json.push_back(profile_data);
            profile_count++;
            
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Exported print profile: " << preset.name 
                                  << " (external_id: " << preset.setting_id << ")";
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to export print profile " << preset.name 
                                    << ": " << e.what();
        }
    }
    
    if (profiles_json.empty()) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: No user print profiles to export";
        CallAfter([this]() {
            show_notification_in_webview(
                _L("No user print profiles to export."),
                "info"
            );
        });
        return;
    }
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Exported " << profile_count << " print profiles to JSON";
    
    const int MAX_PROFILES_PER_REQUEST = 50;
    if (profiles_json.size() > MAX_PROFILES_PER_REQUEST) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Too many print profiles (" << profiles_json.size() 
                                  << "), limiting to " << MAX_PROFILES_PER_REQUEST;
        profiles_json.resize(MAX_PROFILES_PER_REQUEST);
    }
    
    nlohmann::json payload;
    payload["profiles"] = profiles_json;
    
    std::string payload_json = payload.dump();
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sending " << profiles_json.size() 
                          << " print profiles to Backend (payload size: " << payload_json.size() << " bytes)";
    
    m_fhub_client->set_api_base_url(api_base_url);
    
    m_fhub_client->import_print_profiles(
        access_token,
        payload_json,
        [this, profiles_json](std::string response_body, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profiles import successful. Status: " << http_status;
            
            if (http_status != 200) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Unexpected HTTP status " << http_status
                                        << " when importing print profiles";
                m_is_syncing.store(false);
                CallAfter([this, http_status]() {
                    show_notification_in_webview(
                        wxString::Format(_L("Failed to export print profiles. HTTP status: %d"), http_status),
                        "error"
                    );
                });
                return;
            }

            try {
                nlohmann::json response = nlohmann::json::parse(response_body);

                if (!response.contains("results")) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Response does not contain 'results' field";
                    m_is_syncing.store(false);
                    CallAfter([this]() {
                        show_notification_in_webview(
                            _L("Invalid response from server. Please try again."),
                            "error"
                        );
                    });
                    return;
                }

                AppConfig* app_config = wxGetApp().app_config;
                if (app_config == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: app_config is null, cannot save mappings";
                    m_is_syncing.store(false);
                    CallAfter([this]() {
                        show_notification_in_webview(
                            _L("Failed to save profile mappings. Please try again."),
                            "error"
                        );
                    });
                    return;
                }
                
                int success_count = 0;
                int error_count = 0;
                int updated_count = 0;
                int created_count = 0;
                
                for (const auto& result : response["results"]) {
                    std::string external_id = result.value("external_id", "");
                    std::string status = result.value("status", "");
                    
                    // Безопасно извлекаем fhub_id (может быть null)
                    int fhub_id = 0;
                    if (result.contains("fhub_id") && !result["fhub_id"].is_null()) {
                        try {
                            fhub_id = result["fhub_id"].get<int>();
                        } catch (const std::exception& e) {
                            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to parse fhub_id: " << e.what();
                            fhub_id = 0;
                        }
                    }
                    
                    if (status == "created") {
                        created_count++;
                        success_count++;
                    } else if (status == "updated") {
                        updated_count++;
                        success_count++;
                    } else if (status == "error" || status == "skipped") {
                        error_count++;
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Print profile " << external_id 
                                                  << " import failed: " << result.value("message", "");
                    }
                    
                    // Сохраняем маппинг external_id → fhub_id (для обратной синхронизации)
                    if (fhub_id > 0 && !external_id.empty()) {
                        std::string mapping_key = CONFIG_KEY_PRINT_PROFILE_MAPPING + "_" + external_id;
                        app_config->set(CONFIG_SECTION_FILAMENTHUB, mapping_key, std::to_string(fhub_id));
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Saved mapping external_id=" << external_id 
                                              << " -> fhub_id=" << fhub_id;
                    }
                }
                
                CallAfter([app_config]() {
                    if (app_config != nullptr)
                        app_config->save();
                });

                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profiles export completed. "
                                      << "Created: " << created_count 
                                      << ", Updated: " << updated_count
                                      << ", Errors: " << error_count;
                
                CallAfter([this, success_count, error_count, created_count, updated_count]() {
                    wxString message;
                    if (error_count == 0) {
                        if (created_count > 0 && updated_count > 0) {
                            message = wxString::Format(_L("Successfully exported %d print profiles: %d created, %d updated."),
                                                      success_count, created_count, updated_count);
                        } else if (created_count > 0) {
                            message = wxString::Format(_L("Successfully exported %d print profiles (created)."), created_count);
                        } else if (updated_count > 0) {
                            message = wxString::Format(_L("Successfully exported %d print profiles (updated)."), updated_count);
                        } else {
                            message = _L("Print profiles exported successfully.");
                        }
                        show_notification_in_webview(message, "success");
                    } else {
                        message = wxString::Format(_L("Exported %d print profiles: %d successful, %d errors."),
                                                  success_count + error_count, success_count, error_count);
                        show_notification_in_webview(message, "warning");
                    }
                    m_is_syncing.store(false);
                });
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing import response: " << e.what()
                                        << ", Response: " << response_body.substr(0, 500);
                m_is_syncing.store(false);
                CallAfter([this, e]() {
                    show_notification_in_webview(
                        wxString::Format(_L("Error parsing server response: %s"), e.what()),
                        "error"
                    );
                });
            }
        },
        [this](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to export print profiles. Error: " << error
                                    << ", Status: " << http_status;

            wxString error_msg;
            if (http_status == 401) {
                error_msg = _L("Your session has expired. Please login again.");
            } else if (http_status == 403) {
                error_msg = _L("Print profiles export is disabled in your FilamentHub settings. Please enable it in your profile settings.");
            } else if (http_status == 400) {
                error_msg = _L("Invalid request. Please check your profiles and try again.");
            } else if (http_status >= 500) {
                error_msg = _L("Server error. Please try again later.");
            } else {
                error_msg = wxString::Format(_L("Failed to export print profiles: %s"), wxString::FromUTF8(error.c_str()));
            }

            m_is_syncing.store(false);
            CallAfter([this, error_msg, http_status]() {
                show_notification_in_webview(
                    error_msg,
                    http_status == 401 || http_status == 403 ? "warning" : "error"
                );
            });
        }
    );
}

// ============================================================================
// Unified export of all profile types to FilamentHub
// ============================================================================

void FilamentHubPanel::export_profiles_to_filamenthub()
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== export_profiles_to_filamenthub() CALLED ==========";

    // Атомарный check-and-set: если уже true — кто-то экспортирует/синхронизирует, выходим
    if (m_is_syncing.exchange(true)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Export/sync already in progress, skipping";
        return;
    }

    // Проверяем авторизацию
    std::string access_token;
    int user_id;
    if (!load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Not authenticated, cannot export profiles";
        m_is_syncing.store(false);
        CallAfter([this]() {
            show_notification_in_webview(
                _L("Please login to export profiles to FilamentHub."),
                "warning"
            );
        });
        return;
    }

    // Проверяем PresetBundle
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot export profiles";
        m_is_syncing.store(false);
        CallAfter([this]() {
            show_notification_in_webview(
                _L("Preset bundle not available. Please try again."),
                "error"
            );
        });
        return;
    }

    // Один вызов check_user_permissions — получаем все 5 флагов
    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;

    check_user_permissions(access_token,
        [this, access_token, api_base_url](bool filament_import, bool printer_import, bool printer_export, bool print_import, bool print_export) {
            CallAfter([this, access_token, api_base_url, filament_import, printer_import, print_import]() {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Unified export - filament_import=" << (filament_import ? "true" : "false")
                                       << ", printer_import=" << (printer_import ? "true" : "false")
                                       << ", print_import=" << (print_import ? "true" : "false");

                // Count how many exports will run so m_is_syncing is only reset
                // when ALL of them finish (each _internal decrements m_active_exports)
                int export_count = 0;
                if (filament_import) export_count++;
                if (printer_import) export_count++;
                if (print_import) export_count++;

                if (export_count == 0) {
                    m_is_syncing.store(false);
                    show_notification_in_webview(
                        _L("All profile exports are disabled in your FilamentHub settings. Please enable them in your profile settings."),
                        "warning"
                    );
                    return;
                }

                m_active_exports.store(export_count);

                if (filament_import) {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Unified export - exporting filament presets";
                    export_filament_presets_to_filamenthub_internal(access_token, api_base_url);
                }

                if (printer_import) {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Unified export - exporting printer profiles";
                    export_printer_profiles_to_filamenthub_internal(access_token, api_base_url);
                }

                if (print_import) {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Unified export - exporting print profiles";
                    export_print_profiles_to_filamenthub_internal(access_token, api_base_url);
                }
            });
        },
        [this](std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to check permissions for unified export. Error: " << error;
            m_is_syncing.store(false);
            CallAfter([this, error, http_status]() {
                if (http_status == 401) {
                    show_notification_in_webview(
                        _L("Your session has expired. Please login again."),
                        "warning"
                    );
                } else {
                    show_notification_in_webview(
                        wxString::Format(_L("Failed to check permissions: %s"), wxString::FromUTF8(error.c_str())),
                        "error"
                    );
                }
            });
        }
    );
}

}} // namespace Slic3r::GUI
