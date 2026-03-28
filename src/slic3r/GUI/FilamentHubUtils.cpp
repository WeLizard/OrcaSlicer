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
#include "../Utils/FilamentHubClient.hpp"
#include <nlohmann/json.hpp>
#include <boost/log/trivial.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <libslic3r/AppConfig.hpp>
#include <wx/msgdlg.h>
#include <cctype>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>

namespace Slic3r {
namespace GUI {

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
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Saved auth token for user_id=" << user_id;
    });
}

bool FilamentHubPanel::load_auth_token(std::string& access_token, int& user_id)
{
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: ========== load_auth_token() CALLED ==========";

    if (wxGetApp().app_config == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: app_config is null, cannot load token";
        return false;
    }

    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: app_config is not null. Loading token...";

    std::string token = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_ACCESS_TOKEN);
    std::string user_id_str = wxGetApp().app_config->get(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_USER_ID);

    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Loading auth token - token length: " << token.length()
                            << ", user_id_str: '" << user_id_str << "'";

    if (token.empty()) {
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: No auth token found in config (token is empty)";
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: User is not logged in.";
        return false;
    }
    
    // user_id может быть пустым, это не критично
    if (user_id_str.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: user_id_str is empty, setting user_id to 0";
        user_id = 0;
        access_token = token;
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Loaded auth token (without user_id) - token length: " << access_token.length();
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
            // IMPORTANT: use std::string() not "" — const char* "" resolves to bool overload and writes "true"!
            wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_ACCESS_TOKEN, std::string());
            wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_REFRESH_TOKEN, std::string());
            wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, CONFIG_KEY_USER_ID, std::string());
            CallAfter([]() {
                if (wxGetApp().app_config != nullptr)
                    wxGetApp().app_config->save();
            });
            return false;
        }
        
        user_id = std::stoi(user_id_str);
        access_token = token;
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Loaded auth token for user_id=" << user_id
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

    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Saved mapping preset_id=" << preset_id
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
    // IMPORTANT: use std::string() not "" — const char* "" resolves to bool overload and writes "true"!
    wxGetApp().app_config->set(CONFIG_SECTION_FILAMENTHUB, key, std::string());
    CallAfter([]() {
        if (wxGetApp().app_config != nullptr)
            wxGetApp().app_config->save();
    });

    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Removed mapping for preset_id=" << preset_id;
}

std::vector<int> FilamentHubPanel::get_all_mapped_preset_ids()
{
    std::vector<int> preset_ids;

    if (wxGetApp().app_config == nullptr) {
        return preset_ids;
    }

    const std::string prefix = CONFIG_KEY_PRESET_MAPPING + "_";
    const auto& section = wxGetApp().app_config->get_section(CONFIG_SECTION_FILAMENTHUB);

    for (const auto& [key, value] : section) {
        if (key.rfind(prefix, 0) == 0 && !value.empty()) {
            try {
                int preset_id = std::stoi(key.substr(prefix.size()));
                preset_ids.push_back(preset_id);
            } catch (...) {
                // Skip malformed keys
            }
        }
    }

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

    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Saved scoped last_sync_time (" << sync_scope
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
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: load_last_sync_time(" << sync_scope
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

    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Set deleted preset action to: " << action;
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
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Removed mapping for deleted preset " << preset_id;
    
    return true;
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

    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Saved printer profile mapping profile_id=" << profile_id
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

    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Saved print profile mapping profile_id=" << profile_id
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

} // namespace GUI
} // namespace Slic3r
