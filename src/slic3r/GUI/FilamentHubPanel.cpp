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
#include <map>
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
    , m_frontend_url(DEFAULT_FRONTEND_URL)
    , m_api_base_url(FilamentHubClient::DEFAULT_API_BASE_URL)
{
    SetBackgroundColour(*wxWHITE);
    init();
}

FilamentHubPanel::~FilamentHubPanel()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " Start";
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

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " End";
}

void FilamentHubPanel::init()
{
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: ========== FilamentHubPanel::init() CALLED ==========";
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Initializing FilamentHubPanel...";
    
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
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Creating sync button...";
    m_sync_button = new Button(m_info_panel, _L("Synchronize"));
    set_button_square_style(m_sync_button, ButtonStyle::Confirm);
    
    // Проверяем, что кнопка создана
    if (m_sync_button == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to create m_sync_button!";
    } else {
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Sync button created successfully";
    }
    
    // Привязываем обработчик события
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Binding sync button click handler...";
    m_sync_button->Bind(wxEVT_BUTTON, &FilamentHubPanel::on_sync_button_click, this);
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Sync button click handler bound successfully";
    
    m_sync_button->Hide(); // Hidden by default (shown when logged in)
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Sync button hidden by default (will be shown when logged in)";
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
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Settings button shown (Developer Mode enabled)";
    } else {
        m_settings_button->Hide();
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Settings button hidden (Developer Mode disabled)";
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
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Script message handler added successfully on init";
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
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Auto-sync disabled on panel load (user must click 'Synchronize' button)";
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
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Loading URL: " << url.ToUTF8();
    } else {
        m_url_deferred = url;
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Deferring URL load: " << url.ToUTF8();
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
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Loading deferred URL";
    }
    return wxPanel::Show(show);
}

void FilamentHubPanel::OnError(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: WebView error: " << evt.GetString().ToUTF8();
}

void FilamentHubPanel::OnLoaded(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: WebView loaded: " << evt.GetURL().ToUTF8();

    // CRASH-3 fix: проверяем m_browser перед любым использованием
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: OnLoaded called but m_browser is null";
        return;
    }

    // Попробуем добавить script message handler еще раз (если не получилось при инициализации)
    // Но не логируем как ошибку, если уже добавлен - это нормально
    bool handler_added = m_browser->AddScriptMessageHandler("wx");
    if (handler_added) {
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Script message handler added successfully after load";
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
            },
            // Scan for orphaned presets (on-demand)
            scanOrphanedPresets: function() {
                return new Promise(function(resolve, reject) {
                    const sequenceId = 'scan_orphaned_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);

                    const message = JSON.stringify({
                        command: 'scan_orphaned_presets',
                        sequence_id: sequenceId,
                        data: {}
                    });

                    const handleResponse = function(event) {
                        try {
                            const data = typeof event.data === 'string' ? JSON.parse(event.data) : event.data;
                            if (data.command === 'scan_orphaned_presets' && data.sequence_id === sequenceId) {
                                window.removeEventListener('message', handleResponse);
                                if (data.status === 'success') {
                                    resolve({ message: data.message || '' });
                                } else {
                                    reject(new Error(data.message || 'Scan failed'));
                                }
                            }
                        } catch (e) {}
                    };

                    window.addEventListener('message', handleResponse);

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
        // FIX: очищаем предыдущий interval при повторной загрузке (SPA navigation / reload)
        if (window._fhTokenMonitorId) {
            clearInterval(window._fhTokenMonitorId);
            window._fhTokenMonitorId = null;
        }
        if (window._fhTokenMonitorTimeoutId) {
            clearTimeout(window._fhTokenMonitorTimeoutId);
            window._fhTokenMonitorTimeoutId = null;
        }
        window._fhTokenMonitorTimeoutId = setTimeout(() => {
            var lastToken = localStorage.getItem('access_token');
            window._fhTokenMonitorId = setInterval(() => {
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
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Injected auth token into WebView localStorage (user_id=" << user_id << ")";
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
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: No token in AppConfig, cleared WebView localStorage";
    }
}

void FilamentHubPanel::OnScriptMessage(wxWebViewEvent& evt)
{
    wxString str_input = evt.GetString();
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Script message received: " << str_input.ToUTF8();
    
    try {
        nlohmann::json j = nlohmann::json::parse(str_input.ToUTF8().data());

        if (!j.contains("command") || !j["command"].is_string()) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Script message missing 'command' key";
            return;
        }
        wxString command = j["command"].get<std::string>();
        wxString sequence_id = j.value("sequence_id", "");

        if (command == "import_profile") {
            if (!j.contains("data") || !j["data"].contains("preset_id")) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: import_profile missing data.preset_id";
                send_response("import_profile", "error", "Missing preset_id in message", sequence_id);
                return;
            }
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
            if (!j.contains("data") || !j["data"].contains("access_token")) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: login_success missing data.access_token";
                send_response("login_success", "error", "Missing access_token in message", sequence_id);
                return;
            }
            std::string access_token = j["data"]["access_token"].get<std::string>();

            // Сохраняем refresh_token если передан
            std::string refresh_token;
            if (j["data"].contains("refresh_token") && !j["data"]["refresh_token"].is_null()) {
                refresh_token = j["data"]["refresh_token"].get<std::string>();
            }

            // user_id может быть null, string или boolean в JSON — парсим безопасно
            int user_id = 0;
            if (!j["data"]["user_id"].is_null()) {
                if (j["data"]["user_id"].is_number()) {
                    user_id = j["data"]["user_id"].get<int>();
                } else if (j["data"]["user_id"].is_string()) {
                    try { user_id = std::stoi(j["data"]["user_id"].get<std::string>()); }
                    catch (...) { BOOST_LOG_TRIVIAL(warning) << "FilamentHub: user_id is not numeric: " << j["data"]["user_id"].dump(); }
                } else {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: unexpected user_id type: " << j["data"]["user_id"].dump();
                }
            } else {
                // Если user_id не передан, получаем его через API
                m_fhub_client->set_api_base_url(m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url);
                m_fhub_client->get_current_user(
                    access_token,
                    [this, access_token, refresh_token](std::string json_body, unsigned http_status) {
                        try {
                            nlohmann::json user_json = nlohmann::json::parse(json_body);
                            if (!user_json.contains("id")) {
                                BOOST_LOG_TRIVIAL(error) << "FilamentHub: User response missing 'id' field";
                                return;
                            }
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
        } else if (command == "scan_orphaned_presets") {
            // User wants to scan for orphaned filament presets (broken inherits)
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Scan orphaned presets command received from Frontend";

            CallAfter([this]() {
                scan_orphaned_presets();
            });

            send_response("scan_orphaned_presets", "success", "", sequence_id);
        } else if (command == "logout") {
            // Frontend сообщает о logout (401 без refresh, или refresh failed)
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Logout command received from Frontend (token expired/revoked)";
            CallAfter([this]() {
                // Очищаем все токены в AppConfig
                if (wxGetApp().app_config != nullptr) {
                    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_ACCESS_TOKEN, std::string());
                    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_REFRESH_TOKEN, std::string());
                    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_USER_ID, std::string());
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
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Sent response: " << response.dump();
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
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Sent notification to WebView: type=" << type.ToUTF8() 
                            << ", message=" << message.ToUTF8();
}

void FilamentHubPanel::send_command_to_webview(const std::string& command)
{
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Cannot send command '" << command << "', WebView is null";
        return;
    }

    nlohmann::json msg;
    msg["command"] = command;

    wxString json_wx = wxString::FromUTF8(msg.dump().c_str());
    wxString js_code = wxString(R"(
            (function() {
                try {
                    window.postMessage()") + json_wx + wxString(R"(, '*');
                } catch (e) {
                    console.error('FilamentHub: Error sending command:', e);
                }
            })();
        )");

    WebView::RunScript(m_browser, js_code);
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Sent command to WebView: " << command;
}

void FilamentHubPanel::OnClose(wxCloseEvent& evt)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Panel closing";
    evt.Skip();
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
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Received user info. Status: " << http_status;
            
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

            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: User info JSON: " << json_body;

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

                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Parsed user data - username: '" << username
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
                    display_name = wxString::Format(_L("User %d"), user_id);
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: No name/email found, using default: " << display_name.ToUTF8();
                }

                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Setting display name: " << display_name.ToUTF8();

                // Все UI-обновления через CallAfter (callback вызывается из background thread)
                CallAfter([this, display_name]() {
                    m_user_name_label->SetLabel(display_name);

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
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Received presets stats. Status: " << http_status;

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
                                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Total presets: " << total_presets << ", Synced: " << synced_presets;
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
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: update_ui_for_login_state called. is_logged_in=" << (is_logged_in ? "true" : "false");
    
    if (is_logged_in) {
        // Show logged-in UI elements
        m_profile_button->Show();
        m_preset_count_label->Show();
        m_sync_button->Show();
        m_logout_button->Show();
        
        // Update unread notifications count
        update_unread_notifications_count();
        
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Showing sync button (user is logged in)";
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Sync button is shown: " << (m_sync_button->IsShown() ? "true" : "false");
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Sync button is enabled: " << (m_sync_button->IsEnabled() ? "true" : "false");
        
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
        
        // Reset unread notifications count
        m_unread_notifications_count = 0;
        
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Hiding sync button (user is not logged in)";
        
        // Update labels
        m_user_name_label->SetLabel(_L("Sign in to unlock full functionality"));
        m_preset_count_label->SetLabel(_L("Presets: 0"));
    }
    
    m_info_panel->Layout();
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: UI updated for login state";
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

    // Set active button to Confirm style (green) - keep square corners
    if (m_active_page == "catalog" && m_catalog_button) {
        set_button_square_style(m_catalog_button, ButtonStyle::Confirm);
    } else if (m_active_page == "profile" && m_profile_button) {
        set_button_square_style(m_profile_button, ButtonStyle::Confirm);
    } else if (m_active_page == "wiki" && m_wiki_button) {
        set_button_square_style(m_wiki_button, ButtonStyle::Confirm);
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
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Navigating to " << path.ToUTF8() << " without page reload";
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
        // IMPORTANT: use std::string() not "" — const char* "" resolves to bool overload and writes "true"!
        wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_ACCESS_TOKEN, std::string());
        wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_REFRESH_TOKEN, std::string());
        wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_USER_ID, std::string());
        CallAfter([]() {
            if (wxGetApp().app_config != nullptr)
                wxGetApp().app_config->save();
        });
    }
    
    // Reset sync state so next login triggers auto-sync again
    m_initial_sync_done.store(false);
    m_full_sync_attempted.store(false);

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
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Async job started: " << job_name;
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

        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Async job finished: " << job_name;
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
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Sent update_notifications_count to WebView: " << unread_count;
                        }
                        
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Unread notifications count updated: " << unread_count;
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
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Triggered notifications dropdown via JavaScript";
}

}} // namespace Slic3r::GUI
