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
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "I18N.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "../Utils/FilamentHubClient.hpp"
#include "Widgets/WebView.hpp"
#include "Widgets/Button.hpp"
#include <nlohmann/json.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <libslic3r/AppConfig.hpp>
#include <map>
#include <set>
#include <vector>
#include <wx/msgdlg.h>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace Slic3r {
namespace GUI {

static std::string json_string_value_or(const nlohmann::json& object, const char* key, std::string default_value = {})
{
    auto it = object.find(key);
    if (it == object.end() || it->is_null())
        return default_value;
    if (!it->is_string())
        return default_value;
    return it->get<std::string>();
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
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Token saved. User ID: " << user_id << ", token length: " << access_token.length();

    // Update UI to show logged-in state
    CallAfter([this]() {
        update_user_info();
        // Автоматически синхронизируем пресеты после логина — ОДНОКРАТНО
        if (!m_is_syncing && !m_initial_sync_done.load()) {
            m_initial_sync_done.store(true);
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Auto-syncing presets after login (one-time)...";
            synchronize_presets(true); // force_full_sync = true для первого раза
        } else {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Skipping auto-sync after login (already done or in progress)";
        }
    });
}

void FilamentHubPanel::synchronize_presets(bool force_full_sync)
{
    static unsigned int sync_call_counter = 0;
    if (sync_call_counter < UINT_MAX) sync_call_counter++;
    
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== [SYNC START] synchronize_presets() CALLED (call #" << sync_call_counter << ") ==========";
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 1] force_full_sync=" << (force_full_sync ? "true" : "false");
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC TRACE] m_is_syncing=" << (m_is_syncing ? "true" : "false")
                            << ", m_full_sync_attempted=" << (m_full_sync_attempted.load() ? "true" : "false");
    
    // Атомарный check-and-set с таймаутом: предотвращает deadlock при зависших операциях
    if (!try_acquire_sync_lock()) {
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

// ============================================================================
// Decomposed sync helpers
// ============================================================================

void FilamentHubPanel::handle_sync_token_expired()
{
    // Called on UI thread (from CallAfter). Handles 401 response during sync.
    m_is_syncing.store(false);
    if (m_sync_progress) m_sync_progress->Hide();
    if (m_sync_status_label) m_sync_status_label->Hide();
    update_sync_button_state(false);
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (401). Active syncs: " << m_active_syncs;
    if (m_active_syncs < 0) m_active_syncs = 0;
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
}

std::vector<nlohmann::json> FilamentHubPanel::detect_deleted_presets(
    const std::vector<nlohmann::json>& server_presets,
    bool force_full_sync)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 13] Detecting deleted presets...";

    std::vector<nlohmann::json> deleted_presets_list;
    std::set<int> server_preset_ids;

    for (const auto& preset_json : server_presets) {
        if (!preset_json.contains("id")) continue;
        server_preset_ids.insert(preset_json["id"].get<int>());
    }

    // Check each server preset against local PresetBundle
    for (const auto& preset_json : server_presets) {
        if (!preset_json.contains("id") || !preset_json.contains("name")) continue;
        int preset_id = preset_json["id"];
        std::string preset_name = preset_json["name"];

        std::string bundle_preset_name = load_preset_mapping(preset_id);

        if (!bundle_preset_name.empty()) {
            bool preset_exists = preset_exists_in_bundle(bundle_preset_name);

            if (!preset_exists) {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC STEP 13.1] Preset " << preset_id
                                           << " (" << preset_name
                                           << ") is mapped to '" << bundle_preset_name
                                           << "' but preset not found in bundle (deleted locally)";

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

    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 13.2] Found " << deleted_presets_list.size()
                             << " deleted presets (deleted locally, but exist on server)";
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 13.2] Found " << deleted_presets_list.size()
                            << " deleted presets (deleted locally, but exist on server)";

    // Clean up orphaned mappings (only during full sync)
    if (force_full_sync) {
        std::vector<int> all_mapped_ids = get_all_mapped_preset_ids();
        int orphaned_count = 0;
        for (int mapped_id : all_mapped_ids) {
            if (server_preset_ids.find(mapped_id) == server_preset_ids.end()) {
                remove_preset_mapping(mapped_id);
                orphaned_count++;
            }
        }
        if (orphaned_count > 0) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 13.3] Cleaned up " << orphaned_count
                                   << " orphaned preset mappings";
        }
    }

    return deleted_presets_list;
}

void FilamentHubPanel::report_deleted_presets_to_backend(
    const std::vector<nlohmann::json>& deleted_list,
    const std::string& access_token,
    const std::string& api_base_url)
{
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 15] Found " << deleted_list.size()
                             << " deleted presets. Reporting to backend...";
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 15] Found " << deleted_list.size()
                            << " deleted presets. Reporting to backend...";

    std::string access_token_for_report = access_token;
    std::string api_base_url_for_report = api_base_url;
    std::vector<nlohmann::json> deleted_presets_for_report = deleted_list;
    size_t deleted_presets_count = deleted_list.size();

    CallAfter([this, access_token_for_report, api_base_url_for_report, deleted_presets_for_report, deleted_presets_count]() {
        nlohmann::json deleted_presets_request;
        deleted_presets_request["deleted_presets"] = deleted_presets_for_report;
        std::string deleted_presets_json = deleted_presets_request.dump();

        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [SYNC STEP 15.1] Deleted presets JSON: " << deleted_presets_json;

        FilamentHubClient report_client;
        report_client.set_api_base_url(api_base_url_for_report);

        report_client.report_deleted_presets(
            access_token_for_report,
            deleted_presets_json,
            [this, deleted_presets_count](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 15.2] Deleted presets reported successfully. Status: " << status
                                       << ", Count: " << deleted_presets_count;
                if (status == 200) {
                    try {
                        nlohmann::json response = nlohmann::json::parse(body);
                        std::string message = response.value("message", "");
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 15.2.1] Backend response: " << message;
                        if (response.contains("notification_id")) {
                            int notification_id = response.value("notification_id", 0);
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 15.2.2] Notification created with ID: " << notification_id;
                        }
                    } catch (const std::exception& e) {
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC STEP 15.2.1] Error parsing backend response: " << e.what();
                    }
                }
            },
            [this](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 15.2] Failed to report deleted presets. Error: " << error
                                        << ", Status: " << status;
            }
        );
    });
}

void FilamentHubPanel::trigger_silent_profile_export()
{
    // Called on UI thread. Auto-exports printer/print profiles after empty filament sync.
    std::string token;
    int uid = 0;
    if (!load_auth_token(token, uid)) return;

    check_user_permissions(token,
        [this, token](bool filament_import, bool printer_import, bool printer_export, bool print_import, bool print_export) {
            if (printer_import) {
                CallAfter([this, token]() {
                    try {
                        std::string api_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
                        m_active_exports.fetch_add(1);
                        export_printer_profiles_to_filamenthub_internal(token, api_url);
                    } catch (const std::exception& e) {
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [EMPTY] Silent printer export exception: " << e.what();
                    }
                });
            }
            if (print_import) {
                CallAfter([this, token]() {
                    try {
                        std::string api_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
                        m_active_exports.fetch_add(1);
                        export_print_profiles_to_filamenthub_internal(token, api_url);
                    } catch (const std::exception& e) {
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [EMPTY] Silent print export exception: " << e.what();
                    }
                });
            }
        },
        [](std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to check permissions, skipping printer/print export: " << error;
        }
    );
}

void FilamentHubPanel::handle_presets_list_response(
    std::string json_body, unsigned http_status,
    int user_id, bool force_full_sync,
    const std::string& updated_since,
    const std::string& api_base_url,
    const std::string& access_token)
{
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 9] ========== on_complete CALLBACK (get_my_presets) ==========";
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 9.0] Lambda function called from FilamentHubPanel!";
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 9.1] HTTP status: " << http_status
                             << ", Body size: " << json_body.size() << " bytes";

    // Handle 401 token expired
    if (http_status == 401) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC ERROR] Token expired (401) in on_complete, "
                                   << "waiting for frontend auto-refresh before retry...";
        CallAfter([this]() { handle_sync_token_expired(); });
        return;
    }

    // Handle 403 access denied
    if (http_status == 403) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Access denied (403) during presets sync. Body: " << json_body;
        try {
            nlohmann::json error_json = nlohmann::json::parse(json_body);
            std::string error_detail = error_json.value("detail", "Access denied");
            CallAfter([this, error_detail]() {
                m_is_syncing.store(false);
                if (m_sync_progress) m_sync_progress->Hide();
                if (m_sync_status_label) m_sync_status_label->Hide();
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (403). Active syncs: " << m_active_syncs;
                if (m_active_syncs <= 0) {
                    update_sync_button_state(false);
                    m_active_syncs = 0;
                }
                m_info_panel->Layout();
                show_notification_in_webview(
                    wxString::Format(_L("Access denied: %s"), wxString::FromUTF8(error_detail.c_str())),
                    "warning"
                );
            });
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing 403 response: " << e.what();
            CallAfter([this]() {
                m_is_syncing.store(false);
                if (m_sync_progress) m_sync_progress->Hide();
                if (m_sync_status_label) m_sync_status_label->Hide();
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (403, parse error). Active syncs: " << m_active_syncs;
                if (m_active_syncs <= 0) {
                    update_sync_button_state(false);
                    m_active_syncs = 0;
                }
                m_info_panel->Layout();
                show_notification_in_webview(
                    _L("Access denied. Please check your permissions in FilamentHub settings."),
                    "warning"
                );
            });
        }
        return;
    }

    // Handle other non-200 errors
    if (http_status != 200) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC ERROR] Failed to get presets list. Status: " << http_status;
        CallAfter([this, http_status]() {
            m_is_syncing.store(false);
            if (m_sync_progress) m_sync_progress->Hide();
            if (m_sync_status_label) m_sync_status_label->Hide();
            update_sync_button_state(false);
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (status " << http_status << "). Active syncs: " << m_active_syncs;
            if (m_active_syncs < 0) m_active_syncs = 0;
            m_info_panel->Layout();
            show_notification_in_webview(
                wxString::Format(_L("Failed to get presets list. Status: %d"), http_status),
                "error"
            );
        });
        return;
    }

    // 200 OK — process presets list
    process_successful_presets_list(json_body, user_id, force_full_sync, updated_since, api_base_url, access_token);
}

void FilamentHubPanel::handle_presets_list_error(
    std::string body, std::string error, unsigned http_status)
{
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: ========== on_error CALLBACK (get_my_presets) ==========";
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get presets list. Error: '" << error << "'"
                             << ", Status: " << http_status << ", Body size: " << body.size() << " bytes";
    if (body.size() < 500) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error body: " << body;
    } else {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error body (first 500 chars): " << body.substr(0, 500);
    }

    // Handle 401 token expired
    if (http_status == 401) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC ERROR] Token expired (401) in on_error callback, "
                                   << "waiting for frontend auto-refresh before retry...";
        CallAfter([this]() { handle_sync_token_expired(); });
        return;
    }

    // Build error message based on status
    wxString error_msg;
    if (http_status == 403) {
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
        m_is_syncing.store(false);
        if (m_sync_progress) m_sync_progress->Hide();
        if (m_sync_status_label) m_sync_status_label->Hide();
            update_sync_button_state(false);
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Filament presets sync failed (error: " << error_msg.ToUTF8()
                                 << ", status: " << http_status << "). Active syncs: " << m_active_syncs;
            if (m_active_syncs < 0) {
                m_active_syncs = 0;
            }
            m_info_panel->Layout();
            show_notification_in_webview(
                error_msg,
                http_status == 403 ? "warning" : "error"
            );
        });
}

void FilamentHubPanel::process_successful_presets_list(
    const std::string& json_body,
    int user_id, bool force_full_sync,
    const std::string& updated_since,
    const std::string& api_base_url,
    const std::string& access_token)
{
    // Increment active syncs only after 200 OK
    m_active_syncs++;
    m_sync_retry_attempted.store(false);
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 9.2] Incremented m_active_syncs for filament presets (after 200 OK). Active syncs: " << m_active_syncs;

    try {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 10] Parsing JSON response...";
        nlohmann::json response = nlohmann::json::parse(json_body);
        if (!response.contains("items") || !response["items"].is_array()) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 10] Response missing 'items' array";
            m_active_syncs--;
            return;
        }
        std::vector<nlohmann::json> presets = response["items"];
        int total = response.value("total", 0);

        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 11] Received " << total << " presets (items: " << presets.size() << ")";

        // Check if full sync retry is needed (empty result with incremental sync)
        if (presets.empty() && !force_full_sync && !updated_since.empty() && !m_full_sync_attempted.load()) {
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC STEP 12] API returned empty list, but last_sync_time exists. "
                                       << "This might indicate locally deleted presets. Performing full sync to restore all presets...";
            m_full_sync_attempted.store(true);
            save_last_sync_time(user_id, "", SyncTimestampType::Filament);
            m_active_syncs--;
            CallAfter([this]() {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Restarting sync with force_full_sync=true to restore deleted presets";
                m_is_syncing.store(false);
                if (m_sync_progress) m_sync_progress->Hide();
                if (m_sync_status_label) m_sync_status_label->Hide();
                m_info_panel->Layout();
                synchronize_presets(true);
            });
            return;
        } else if (presets.empty() && !force_full_sync && !updated_since.empty() && m_full_sync_attempted.load()) {
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC STEP 12] Full sync already attempted, skipping to prevent infinite loop";
            m_active_syncs--;
            CallAfter([this]() {
                m_is_syncing.store(false);
                if (m_sync_progress) m_sync_progress->Hide();
                if (m_sync_status_label) m_sync_status_label->Hide();
                update_sync_button_state(false);
                m_info_panel->Layout();
                show_notification_in_webview(
                    _L("No presets to sync. All presets may have sync disabled."),
                    "info"
                );
            });
            return;
        }

        // Handle empty presets list (normal case)
        if (presets.empty()) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 12] No presets to sync (empty list)";
            CallAfter([this]() {
                m_active_syncs--;
                m_is_syncing.store(false);
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync completed (empty list). Active syncs: " << m_active_syncs;
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [EMPTY] Scheduling silent auto-export...";
                trigger_silent_profile_export();
                if (m_sync_progress) m_sync_progress->Hide();
                if (m_sync_status_label) m_sync_status_label->Hide();
                if (m_active_syncs <= 0) {
                    update_sync_button_state(false);
                    m_active_syncs = 0;
                }
                m_info_panel->Layout();
            });
            return;
        }

        // Detect deleted presets
        auto deleted_presets_list = detect_deleted_presets(presets, force_full_sync);

        // Batch-download all preset configs in ONE HTTP request
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 14] Batch-downloading " << presets.size() << " preset configs...";

        std::vector<int> preset_ids;
        std::map<int, std::string> presets_meta;
        for (const auto& preset_json : presets) {
            if (!preset_json.contains("id")) continue;
            int pid = preset_json["id"];
            preset_ids.push_back(pid);
            presets_meta[pid] = json_string_value_or(preset_json, "name", "");
        }

        {
            std::lock_guard<std::mutex> lock(m_preset_queue_mutex);
            m_total_presets_to_sync = presets.size();
            m_synced_count = 0;
            m_error_count = 0;
            m_sync_detail_lines.clear();
        }

        int user_id_for_batch = user_id;
        std::string access_token_for_batch = access_token;

        m_fhub_client->batch_download_profiles(
            preset_ids,
            access_token,
            // on_complete: all profiles downloaded in one response
            [this, presets_meta, user_id_for_batch, access_token_for_batch](std::string batch_body, unsigned batch_status) {
                if (batch_status != 200) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC ERROR] Batch download failed. Status: " << batch_status;
                    CallAfter([this, batch_status]() {
                        m_active_syncs--;
                        m_is_syncing.store(false);
                        if (m_active_syncs <= 0) {
                            update_sync_button_state(false);
                            m_active_syncs = 0;
                        }
                        m_info_panel->Layout();
                        show_notification_in_webview(
                            wxString::Format(_L("Failed to download presets. Status: %d"), batch_status),
                            "error"
                        );
                    });
                    return;
                }
                try {
                    process_batch_export_response(batch_body, presets_meta, user_id_for_batch, access_token_for_batch);
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC ERROR] process_batch_export_response threw: " << e.what();
                    CallAfter([this]() {
                        m_active_syncs--;
                        m_is_syncing.store(false);
                        if (m_sync_progress) { m_sync_progress->Hide(); m_sync_progress->SetValue(0); }
                        if (m_sync_status_label) m_sync_status_label->Hide();
                        if (m_active_syncs <= 0) {
                            update_sync_button_state(false);
                            m_active_syncs = 0;
                        }
                        if (m_info_panel) m_info_panel->Layout();
                        show_notification_in_webview(_L("Failed to process batch preset response."), "error");
                    });
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC ERROR] process_batch_export_response threw unknown exception";
                    CallAfter([this]() {
                        m_active_syncs--;
                        m_is_syncing.store(false);
                        if (m_sync_progress) { m_sync_progress->Hide(); m_sync_progress->SetValue(0); }
                        if (m_sync_status_label) m_sync_status_label->Hide();
                        if (m_active_syncs <= 0) {
                            update_sync_button_state(false);
                            m_active_syncs = 0;
                        }
                        if (m_info_panel) m_info_panel->Layout();
                        show_notification_in_webview(_L("Failed to process batch preset response."), "error");
                    });
                }
            },
            // on_error: batch download network failure
            [this](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC ERROR] Batch download error: " << error;
                CallAfter([this, error]() {
                    m_active_syncs--;
                    m_is_syncing.store(false);
                    if (m_active_syncs <= 0) {
                        update_sync_button_state(false);
                        m_active_syncs = 0;
                    }
                    m_info_panel->Layout();
                    show_notification_in_webview(
                        wxString::Format(_L("Failed to download presets: %s"), wxString::FromUTF8(error.c_str())),
                        "error"
                    );
                });
            }
        );

        // Report deleted presets to backend (if any)
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [SYNC STEP 15.0] Checking deleted_presets_list. Size: " << deleted_presets_list.size();
        if (!deleted_presets_list.empty()) {
            report_deleted_presets_to_backend(deleted_presets_list, access_token, api_base_url);
        } else {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 15] No deleted presets found";
        }

        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 16] Presets added to queue. last_sync_time will be updated after all presets are imported.";
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 17] ========== Synchronization started ==========";
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 17.1] Summary - Presets to import: " << presets.size()
                               << ", Deleted presets detected: " << deleted_presets_list.size();
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Presets added to queue, processing will continue in UI thread";

    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing presets list: " << e.what();
        CallAfter([this, e]() {
            m_active_syncs--;
            m_is_syncing.store(false);
            if (m_sync_progress) m_sync_progress->Hide();
            if (m_sync_status_label) m_sync_status_label->Hide();
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync failed (parse error). Active syncs: " << m_active_syncs;
            if (m_active_syncs <= 0) {
                update_sync_button_state(false);
                m_active_syncs = 0;
            }
            m_info_panel->Layout();
            show_notification_in_webview(
                wxString::Format(_L("Error parsing presets list: %s"), e.what()),
                "error"
            );
        });
    }
}

void FilamentHubPanel::continue_sync_after_token_validation(int user_id, bool force_full_sync, const std::string& api_base_url, const std::string& access_token)
{
    // Load last_sync_time for incremental sync
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 5] Loading last_sync_time from AppConfig...";
    std::string updated_since;
    if (!force_full_sync) {
        updated_since = load_last_sync_time(user_id, SyncTimestampType::Filament);
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 6] Incremental sync, last_sync_time=" << updated_since;
    } else {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 6] Full sync requested, updated_since=''";
    }

    // Call get_my_presets API — callbacks delegate to extracted methods
    m_fhub_client->set_api_base_url(api_base_url);

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [SYNC STEP 8] Calling get_my_presets API: " << api_base_url
                            << ", updated_since='" << updated_since << "'"
                            << ", token_length=" << access_token.length();

    m_fhub_client->get_my_presets(
        access_token,
        updated_since,
        [this, user_id, force_full_sync, updated_since, api_base_url, access_token](std::string json_body, unsigned http_status) {
            handle_presets_list_response(std::move(json_body), http_status,
                user_id, force_full_sync, updated_since, api_base_url, access_token);
        },
        [this](std::string body, std::string error, unsigned http_status) {
            handle_presets_list_error(std::move(body), std::move(error), http_status);
        }
    );
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: get_my_presets() called, waiting for callback";
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
        int final_synced_count = m_synced_count;
        int final_error_count = m_error_count;
        bool sync_completed_without_errors = final_error_count == 0;
        
        CallAfter([this, final_user_id, final_synced_count, final_error_count, sync_completed_without_errors]() {
            // ВАЖНО: Вызываем load_current_presets() только один раз после завершения импорта всех пресетов
            // Это обновит UI и предотвратит множественные перезагрузки всех пресетов (включая не-FilamentHub)
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE FINISH] All presets imported. Calling load_current_presets() once...";
            wxGetApp().load_current_presets();
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE FINISH] load_current_presets() completed.";
            
            // КРИТИЧНО: Обновляем last_sync_time только после ПОЛНОСТЬЮ успешного импорта без ошибок.
            // Иначе следующий incremental sync пропустит пресеты, которые не импортировались.
            if (sync_completed_without_errors && final_user_id > 0) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE FINISH] Updating last_sync_time after successful import...";
                std::time_t now = std::time(nullptr);
                std::stringstream ss;
                ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%S.000000");
                std::string current_time = ss.str();
                save_last_sync_time(final_user_id, current_time, SyncTimestampType::Filament);
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE FINISH] Saved last_sync_time=" << current_time;
            } else if (!sync_completed_without_errors) {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [QUEUE FINISH] Filament sync completed with "
                                           << final_error_count
                                           << " errors. last_sync_time was NOT updated so failed presets can retry.";
            }
            
            // Сбрасываем флаг защиты от зацикливания после успешной синхронизации
            m_full_sync_attempted.store(false);
            
            // Оповещаем WebView фронтенд что данные изменились после импорта
            send_command_to_webview("sync_complete");

            m_active_syncs--;
            m_is_syncing.store(false);
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets sync completed. Active syncs: " << m_active_syncs;
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Sync summary - Synced: " << final_synced_count << ", Errors: " << final_error_count;

            // Уведомление о результатах импорта
            {
                bool dev_mode = wxGetApp().app_config && wxGetApp().app_config->get("developer_mode") == "true";
                wxString message;
                if (sync_completed_without_errors) {
                    message = wxString::Format(_L("Synced %d filament presets."), final_synced_count);
                } else {
                    message = wxString::Format(_L("Synced %d filament presets: %d successful, %d errors."),
                                              final_synced_count + final_error_count, final_synced_count, final_error_count);
                }
                if (dev_mode && !m_sync_detail_lines.empty()) {
                    message += "\n";
                    for (const auto& line : m_sync_detail_lines) {
                        message += "\n" + wxString::FromUTF8(line.c_str());
                    }
                }
                show_notification_in_webview(message, sync_completed_without_errors ? "success" : "warning");
            }

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
            
            if (!sync_completed_without_errors) {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Skipping printer/print auto-export because filament sync had import errors.";
                return;
            }

            // Silent auto-export printer/print profiles (don't re-grab m_is_syncing — UI already released)
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE] Scheduling silent auto-export...";
            std::string token;
            int uid = 0;
            if (load_auth_token(token, uid)) {
                check_user_permissions(token,
                    [this, token](bool filament_import, bool printer_import, bool printer_export, bool print_import, bool print_export) {
                        if (printer_import) {
                            CallAfter([this, token]() {
                                try {
                                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE] Silent auto-export: printer profiles";
                                    std::string api_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
                                    m_active_exports.fetch_add(1);
                                    export_printer_profiles_to_filamenthub_internal(token, api_url);
                                } catch (const std::exception& e) {
                                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [QUEUE] Silent printer export exception: " << e.what();
                                }
                            });
                        }
                        if (print_import) {
                            CallAfter([this, token]() {
                                try {
                                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE] Silent auto-export: print profiles";
                                    std::string api_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
                                    m_active_exports.fetch_add(1);
                                    export_print_profiles_to_filamenthub_internal(token, api_url);
                                } catch (const std::exception& e) {
                                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [QUEUE] Silent print export exception: " << e.what();
                                }
                            });
                        }
                    },
                    [](std::string error, unsigned status) {
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to check permissions, skipping printer/print export: " << error;
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
                        m_sync_detail_lines.push_back(task.preset_name + " — OK");
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [QUEUE] Preset " << task.preset_id << " imported successfully.";
                    } else {
                        m_error_count++;
                        m_sync_detail_lines.push_back(task.preset_name + " — ERROR");
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
// Helper: try to acquire sync lock with deadlock timeout (60 seconds)
// Returns true if lock was acquired, false if sync is genuinely in progress.
// If sync has been stuck for >60s, force-resets and acquires the lock.
// ============================================================================

bool FilamentHubPanel::try_acquire_sync_lock()
{
    static constexpr int SYNC_TIMEOUT_SECONDS = 60;

    if (m_is_syncing.exchange(true)) {
        // Already locked — check if it's been stuck too long
        auto elapsed = std::chrono::steady_clock::now() - m_sync_started_at;
        auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        if (elapsed_sec > SYNC_TIMEOUT_SECONDS) {
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Sync lock has been held for " << elapsed_sec
                                       << "s (timeout=" << SYNC_TIMEOUT_SECONDS << "s), force-resetting deadlocked state";
            m_active_exports.store(0);
            m_active_syncs = 0;
            // Lock is already true from our exchange, so we now own it
        } else {
            // Genuinely in progress
            m_is_syncing.store(true); // restore (exchange already set it)
            return false;
        }
    }

    m_sync_started_at = std::chrono::steady_clock::now();
    return true;
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

} // namespace GUI
} // namespace Slic3r
