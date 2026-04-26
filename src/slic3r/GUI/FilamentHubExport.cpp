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
#include <boost/filesystem/fstream.hpp>
#include <libslic3r/AppConfig.hpp>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <algorithm>

namespace Slic3r {
namespace GUI {

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

// Helper: safely parse fhub_id from JSON value that can be int, string, or FHUB-prefixed string
// Returns 0 on failure (invalid format, "true", empty, etc.)
static int parse_fhub_id(const nlohmann::json& val) {
    try {
        if (val.is_number_integer()) {
            return val.get<int>();
        }
        if (val.is_string()) {
            std::string s = val.get<std::string>();
            if (s.empty() || s == "true" || s == "True" || s == "TRUE")
                return 0;
            // FHUB-prefix: "FHUB000013" → 13
            if (s.size() > 4 && (s.substr(0, 4) == "FHUB" || s.substr(0, 4) == "fhub"))
                return std::stoi(s.substr(4));
            return std::stoi(s);
        }
    } catch (...) {}
    return 0;
}

// ============================================================================
// Shared helpers (CODE-1 decomposition + CODE-2 CallAfter reduction)
// ============================================================================

void FilamentHubPanel::notify_webview(const wxString& message, const wxString& type)
{
    CallAfter([this, message, type]() {
        show_notification_in_webview(message, type);
    });
}

void FilamentHubPanel::save_app_config_async()
{
    CallAfter([]() {
        AppConfig* cfg = wxGetApp().app_config;
        if (cfg != nullptr)
            cfg->save();
    });
}

void FilamentHubPanel::process_export_response(
    const std::string& profile_type_label,
    const std::string& mapping_key_prefix,
    const std::vector<nlohmann::json>& profiles_json,
    const std::string& response_body,
    unsigned http_status)
{
    if (http_status != 200) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Unexpected HTTP status " << http_status
                                 << " when exporting " << profile_type_label;
        finish_export_operation();
        notify_webview(
            wxString::Format(_L("Failed to export %s. HTTP status: %d"),
                             wxString::FromUTF8(profile_type_label.c_str()), http_status),
            "error");
        return;
    }

    try {
        nlohmann::json response = nlohmann::json::parse(response_body);

        if (!response.contains("results")) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Response does not contain 'results' field";
            finish_export_operation();
            notify_webview(_L("Invalid response from server. Please try again."), "error");
            return;
        }

        AppConfig* app_config = wxGetApp().app_config;
        if (app_config == nullptr) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: app_config is null, cannot save mappings";
            finish_export_operation();
            notify_webview(_L("Failed to save profile mappings. Please try again."), "error");
            return;
        }

        int success_count = 0, error_count = 0, updated_count = 0, created_count = 0;

        // Build ext_id → name lookup
        std::map<std::string, std::string> ext_id_to_name;
        for (const auto& p : profiles_json) {
            std::string eid = p.value("external_id", p.value("setting_id", ""));
            std::string nm = p.value("name", "");
            if (!eid.empty() && !nm.empty()) ext_id_to_name[eid] = nm;
        }
        std::vector<std::string> detail_lines;

        for (const auto& result : response["results"]) {
            std::string external_id = result.value("external_id", "");
            std::string status = result.value("status", "");
            std::string message = result.value("message", "");
            std::string name = ext_id_to_name.count(external_id) ? ext_id_to_name[external_id] : external_id;

            int fhub_id = 0;
            if (result.contains("fhub_id") && !result["fhub_id"].is_null())
                fhub_id = parse_fhub_id(result["fhub_id"]);

            if (status == "created") {
                created_count++; success_count++;
                detail_lines.push_back(name + " — created");
            } else if (status == "updated") {
                updated_count++; success_count++;
                detail_lines.push_back(name + " — updated");
            } else if (status == "error" || status == "skipped") {
                error_count++;
                detail_lines.push_back(name + " — ERROR: " + message);
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: " << profile_type_label << " "
                                           << external_id << " import failed: " << message;
            }

            if (fhub_id > 0 && !external_id.empty()) {
                std::string mapping_key = mapping_key_prefix + "_" + external_id;
                app_config->set(CONFIG_SECTION_FILAMENTHUB, mapping_key, std::to_string(fhub_id));
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Saved mapping external_id=" << external_id
                                         << " -> fhub_id=" << fhub_id;
            }
        }

        save_app_config_async();

        BOOST_LOG_TRIVIAL(info) << "FilamentHub: " << profile_type_label << " export completed. "
                                << "Created: " << created_count
                                << ", Updated: " << updated_count
                                << ", Errors: " << error_count;

        CallAfter([this, profile_type_label, success_count, error_count, created_count, updated_count, detail_lines]() {
            bool dev_mode = wxGetApp().app_config && wxGetApp().app_config->get("developer_mode") == "true";
            wxString label = wxString::FromUTF8(profile_type_label.c_str());
            wxString message;
            if (error_count == 0) {
                if (created_count > 0 && updated_count > 0) {
                    message = wxString::Format(_L("Successfully exported %d %s: %d created, %d updated."),
                                              success_count, label, created_count, updated_count);
                } else if (created_count > 0) {
                    message = wxString::Format(_L("Successfully exported %d %s (created)."), created_count, label);
                } else if (updated_count > 0) {
                    message = wxString::Format(_L("Successfully exported %d %s (updated)."), updated_count, label);
                } else {
                    message = wxString::Format(_L("%s exported successfully."), label);
                }
            } else {
                message = wxString::Format(_L("Exported %d %s: %d successful, %d errors."),
                                          success_count + error_count, label, success_count, error_count);
            }
            if (dev_mode && !detail_lines.empty()) {
                message += "\n";
                for (const auto& line : detail_lines)
                    message += "\n" + wxString::FromUTF8(line.c_str());
            }
            show_notification_in_webview(message, error_count > 0 ? "warning" : "success");
            send_command_to_webview("sync_complete");
            finish_export_operation();
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [EXPORT COMPLETE] " << profile_type_label
                                    << " synced=" << success_count << " errors=" << error_count;
        });
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error parsing import response: " << e.what()
                                 << ", Response: " << response_body.substr(0, 500);
        finish_export_operation();
        notify_webview(wxString::Format(_L("Error parsing server response: %s"), e.what()), "error");
    }
}

void FilamentHubPanel::handle_export_error(
    const std::string& profile_type_label,
    const std::string& error,
    unsigned http_status)
{
    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to export " << profile_type_label
                             << ". Error: " << error << ", Status: " << http_status;

    wxString error_msg;
    if (http_status == 401) {
        error_msg = _L("Your session has expired. Please login again.");
    } else if (http_status == 403) {
        error_msg = wxString::Format(_L("%s export is disabled in your FilamentHub settings. Please enable it in your profile settings."),
                                     wxString::FromUTF8(profile_type_label.c_str()));
    } else if (http_status == 400) {
        error_msg = _L("Invalid request. Please check your presets and try again.");
    } else if (http_status >= 500) {
        error_msg = _L("Server error. Please try again later.");
    } else {
        error_msg = wxString::Format(_L("Failed to export %s: %s"),
                                     wxString::FromUTF8(profile_type_label.c_str()),
                                     wxString::FromUTF8(error.c_str()));
    }

    finish_export_operation();
    notify_webview(error_msg, (http_status == 401 || http_status == 403) ? "warning" : "error");
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
    
    // Атомарный check-and-set с таймаутом: если уже true — кто-то экспортирует, выходим (с deadlock recovery после 60с)
    if (!try_acquire_sync_lock()) {
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
        finish_export_operation(); // Сбрасываем флаг при ошибке
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
        
        // Пропускаем пресеты с постфиксом [fh] — они пришли с сервера,
        // сервер является источником истины для них. Обратная отправка портит имена.
        if (preset.name.find(" [fh]") != std::string::npos) {
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Skipping [fh] preset from export: " << preset.name;
            continue;
        }

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
                                                     << preset.name << " -> fhub_id=" << parse_fhub_id(original_json["fhub_id"]);
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
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Added .info file content to payload for preset: " << preset.name;
            }
            
            // Базовые поля
            preset_data["external_id"] = preset.setting_id; // Уникальный ID в OrcaSlicer
            preset_data["name"] = preset.name;
            
            // Проверяем метки из orcaslicer_json (приоритет над маппингом из AppConfig)
            bool has_fhub_id_from_json = false;
            if (orcaslicer_json.contains("fhub_id") && orcaslicer_json.contains("fhub_source")) {
                int fhub_id = parse_fhub_id(orcaslicer_json["fhub_id"]);
                std::string fhub_source = orcaslicer_json["fhub_source"].get<std::string>();
                if (fhub_source == "filamenthub" && fhub_id > 0) {
                    preset_data["fhub_id"] = fhub_id;
                    has_fhub_id_from_json = true;
                    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Found fhub_id from JSON metadata for filament preset: "
                                           << preset.name << " -> fhub_id=" << fhub_id << ", source=" << fhub_source;
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
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Found mapping for preset external_id=" << preset.setting_id 
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
            
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Exported preset: " << preset.name 
                                   << " (external_id: " << preset.setting_id << ")";
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to export preset " << preset.name 
                                    << ": " << e.what();
        }
    }
    
    // Orphaned preset scanning is NOT done here — it runs on-demand via scan_orphaned_presets()

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
        [this, presets_json](std::string response_body, unsigned http_status) {
            process_export_response("filament presets", CONFIG_KEY_PRESET_MAPPING,
                                    presets_json, response_body, http_status);
        },
        [this](std::string body, std::string error, unsigned http_status) {
            handle_export_error("filament presets", error, http_status);
        }
    );
}

// ============================================================================
// Methods for exporting printer profiles to FilamentHub
// ============================================================================

void FilamentHubPanel::export_printer_profiles_to_filamenthub()
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== export_printer_profiles_to_filamenthub() CALLED ==========";

    if (!try_acquire_sync_lock()) {
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
                bool allow_print_import = user_json.value("allow_print_profiles_import", true);

                if (!allow_printer_import && !allow_print_import) {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Both printer and print profiles export disabled in user settings (user preference)";
                    m_is_syncing.store(false);
                    if (!m_export_disabled_notified) {
                        m_export_disabled_notified = true;
                        CallAfter([this]() {
                            show_notification_in_webview(
                                _L("Printer and print profiles export is disabled in your FilamentHub settings."),
                                "info"
                            );
                        });
                    }
                    return;
                }

                // Count how many exports to run (printer + print)
                int export_count = 0;
                if (allow_printer_import) export_count++;
                if (allow_print_import) export_count++;
                m_active_exports.store(export_count);

                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Permission check passed, exporting profiles (printer="
                                       << (allow_printer_import ? "yes" : "no") << ", print="
                                       << (allow_print_import ? "yes" : "no") << ")";

                if (allow_printer_import) {
                    export_printer_profiles_to_filamenthub_internal(access_token, api_base_url);
                }
                if (allow_print_import) {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Also exporting print profiles (chained with printer profiles)";
                    export_print_profiles_to_filamenthub_internal(access_token, api_base_url);
                }
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
        finish_export_operation();
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

        // Пропускаем пресеты [fh] — сервер является источником истины
        if (preset.name.find(" [fh]") != std::string::npos) {
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Skipping [fh] printer preset from export: " << preset.name;
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
                                                     << preset.name << " -> fhub_id=" << parse_fhub_id(original_json["fhub_id"]);
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
                int fhub_id = parse_fhub_id(orcaslicer_json["fhub_id"]);
                std::string fhub_source = orcaslicer_json["fhub_source"].get<std::string>();
                if (fhub_source == "filamenthub" && fhub_id > 0) {
                    profile_data["fhub_id"] = fhub_id;
                    has_fhub_id_from_json = true;
                    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Found fhub_id from JSON metadata for printer profile: "
                                           << preset.name << " -> fhub_id=" << fhub_id << ", source=" << fhub_source;
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
                        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Found mapping for printer profile external_id=" << preset.setting_id 
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
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [PRINTER EXPORT] " 
                                   << "name='" << preset.name << "'"
                                   << ", printer_model=" << (orcaslicer_json.contains("printer_model") ? orcaslicer_json["printer_model"].dump() : "null")
                                   << ", vendor=" << (orcaslicer_json.contains("printer_vendor") ? orcaslicer_json["printer_vendor"].dump() : "null");
            
            // Извлекаем базовые параметры для PrinterProfile
            // vendor (из preset.vendor или из orcaslicer_json)
            if (orcaslicer_json.contains("printer_vendor") && orcaslicer_json["printer_vendor"].is_string()) {
                profile_data["vendor"] = orcaslicer_json["printer_vendor"].get<std::string>();
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [PRINTER EXPORT] vendor from printer_vendor: " << orcaslicer_json["printer_vendor"].get<std::string>();
            } else if (preset.vendor != nullptr && !preset.vendor->id.empty()) {
                profile_data["vendor"] = preset.vendor->id;
                profile_data["extra_metadata"]["printer_vendor"] = preset.vendor->id;
                profile_data["orcaslicer_settings"]["printer_vendor"] = preset.vendor->id;
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [PRINTER EXPORT] vendor from preset.vendor: " << preset.vendor->id;
            }
            
            // printer_model (из orcaslicer_json) - КРИТИЧНО для сопоставления с базой
            if (orcaslicer_json.contains("printer_model") && orcaslicer_json["printer_model"].is_string()) {
                std::string printer_model = orcaslicer_json["printer_model"].get<std::string>();
                if (!printer_model.empty()) {
                    profile_data["orcaslicer_settings"]["printer_model"] = printer_model;
                    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [PRINTER EXPORT] printer_model: " << printer_model;
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
                    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: [PRINTER EXPORT] Extracted manufacturer='" << manufacturer << "', model='" << model << "'";
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
            
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Exported printer profile: " << preset.name 
                                   << " (external_id: " << preset.setting_id << ")";
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to export printer profile " << preset.name 
                                    << ": " << e.what();
        }
    }
    
    if (profiles_json.empty()) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: No user printer profiles to export";
        finish_export_operation();
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
            process_export_response("printer profiles", CONFIG_KEY_PRINTER_PROFILE_MAPPING,
                                    profiles_json, response_body, http_status);
        },
        [this](std::string body, std::string error, unsigned http_status) {
            handle_export_error("printer profiles", error, http_status);
        }
    );
}

// ============================================================================
// Methods for exporting print profiles to FilamentHub
// ============================================================================

void FilamentHubPanel::export_print_profiles_to_filamenthub()
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== export_print_profiles_to_filamenthub() CALLED ==========";

    if (!try_acquire_sync_lock()) {
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
        finish_export_operation();
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

        // Пропускаем пресеты [fh] — сервер является источником истины
        if (preset.name.find(" [fh]") != std::string::npos) {
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Skipping [fh] print preset from export: " << preset.name;
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
                                                     << preset.name << " -> fhub_id=" << parse_fhub_id(original_json["fhub_id"]);
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
                int fhub_id = parse_fhub_id(orcaslicer_json["fhub_id"]);
                std::string fhub_source = orcaslicer_json["fhub_source"].get<std::string>();
                if (fhub_source == "filamenthub" && fhub_id > 0) {
                    profile_data["fhub_id"] = fhub_id;
                    has_fhub_id_from_json = true;
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Found fhub_id from JSON metadata for print profile: "
                                           << preset.name << " -> fhub_id=" << fhub_id << ", source=" << fhub_source;
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
        finish_export_operation();
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
            process_export_response("print profiles", CONFIG_KEY_PRINT_PROFILE_MAPPING,
                                    profiles_json, response_body, http_status);
        },
        [this](std::string body, std::string error, unsigned http_status) {
            handle_export_error("print profiles", error, http_status);
        }
    );
}

// ============================================================================
// Unified export of all profile types to FilamentHub
// ============================================================================

void FilamentHubPanel::export_profiles_to_filamenthub()
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: ========== export_profiles_to_filamenthub() CALLED ==========";

    if (!try_acquire_sync_lock()) {
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

// ========== ORPHANED PRESET SCANNER (on-demand) ==========

void FilamentHubPanel::scan_orphaned_presets()
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: scan_orphaned_presets() called";

    if (!try_acquire_sync_lock()) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Sync already in progress, skipping orphaned scan";
        return;
    }

    std::string access_token;
    int user_id;
    if (!load_auth_token(access_token, user_id)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Not authenticated, cannot scan orphaned presets";
        m_is_syncing.store(false);
        CallAfter([this]() {
            show_notification_in_webview(
                _L("Please login to scan for orphaned presets."),
                "warning"
            );
        });
        return;
    }

    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: preset_bundle is null, cannot scan orphaned presets";
        m_is_syncing.store(false);
        return;
    }

    std::string api_base_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
    scan_orphaned_presets_internal(access_token, api_base_url);
}

void FilamentHubPanel::scan_orphaned_presets_internal(const std::string& access_token, const std::string& api_base_url)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: scan_orphaned_presets_internal() starting";

    PresetBundle* bundle = wxGetApp().preset_bundle;
    auto& filaments = bundle->filaments;

    // Build set of loaded preset names for comparison
    std::set<std::string> loaded_names;
    for (auto it = filaments.begin(); it != filaments.end(); ++it) {
        loaded_names.insert(it->name);
    }
    for (size_t i = 0; i < filaments.num_default_presets(); ++i) {
        loaded_names.insert(filaments.default_preset(i).name);
    }

    // Get user filament directory path
    std::string filament_dir;
    for (auto it = filaments.begin(); it != filaments.end(); ++it) {
        if (!it->file.empty()) {
            filament_dir = boost::filesystem::path(it->file).parent_path().string();
            break;
        }
    }

    if (filament_dir.empty() || !boost::filesystem::exists(filament_dir)) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Cannot determine filament directory for orphaned scan";
        finish_export_operation();
        CallAfter([this]() {
            show_notification_in_webview(
                _L("Cannot determine filament directory."),
                "warning"
            );
        });
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Scanning for orphaned presets in: " << filament_dir << " (recursive)";

    std::vector<nlohmann::json> orphaned_json;
    int orphaned_count = 0;

    for (auto& dir_entry : boost::filesystem::recursive_directory_iterator(filament_dir)) {
        if (!boost::filesystem::is_regular_file(dir_entry)) continue;

        std::string filename = dir_entry.path().filename().string();
        if (filename.size() < 6 || filename.substr(filename.size() - 5) != ".json") continue;

        std::string stem = dir_entry.path().stem().string();

        if (loaded_names.count(stem) > 0) continue;
        if (stem.find(" [fh]") != std::string::npos) continue;

        try {
            nlohmann::json orphan_json;
            boost::filesystem::ifstream ifs(dir_entry.path());
            if (!ifs.is_open()) continue;
            ifs >> orphan_json;
            ifs.close();

            nlohmann::json preset_data;
            preset_data["name"] = stem;
            preset_data["source"] = "orcaslicer";
            preset_data["active"] = false;
            preset_data["orphaned"] = true;
            preset_data["orphaned_reason"] = "parent_not_found";

            if (orphan_json.contains("inherits") && orphan_json["inherits"].is_string()) {
                preset_data["original_inherits"] = orphan_json["inherits"].get<std::string>();
            }

            preset_data["orcaslicer_settings"] = orphan_json;

            if (orphan_json.contains("filament_settings_id")) {
                auto& fsi = orphan_json["filament_settings_id"];
                if (fsi.is_array() && !fsi.empty())
                    preset_data["external_id"] = fsi[0].get<std::string>();
                else if (fsi.is_string())
                    preset_data["external_id"] = fsi.get<std::string>();
            }

            if (orphan_json.contains("fhub_id"))
                preset_data["fhub_id"] = parse_fhub_id(orphan_json["fhub_id"]);
            if (orphan_json.contains("fhub_draft_id"))
                preset_data["orcaslicer_settings"]["fhub_draft_id"] = orphan_json["fhub_draft_id"];

            preset_data["filament_name"] = stem;

            boost::filesystem::path info_path = dir_entry.path();
            info_path.replace_extension(".info");
            if (boost::filesystem::exists(info_path)) {
                try {
                    boost::filesystem::ifstream info_ifs(info_path);
                    if (info_ifs.is_open()) {
                        std::stringstream buf;
                        buf << info_ifs.rdbuf();
                        preset_data["info_content"] = buf.str();
                        info_ifs.close();
                    }
                } catch (...) {}
            }

            orphaned_json.push_back(preset_data);
            orphaned_count++;

            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Found orphaned preset: " << stem
                                   << " (inherits: "
                                   << (preset_data.contains("original_inherits")
                                           ? preset_data["original_inherits"].get<std::string>()
                                           : "unknown")
                                   << ", path: " << dir_entry.path().string()
                                   << ")";
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to read orphaned preset file "
                                      << dir_entry.path().string() << ": " << e.what();
        }
    }

    if (orphaned_count == 0) {
        BOOST_LOG_TRIVIAL(info) << "FilamentHub: No orphaned presets found";
        finish_export_operation();
        CallAfter([this]() {
            show_notification_in_webview(
                _L("No orphaned presets found. All your presets are loaded correctly."),
                "info"
            );
        });
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Found " << orphaned_count << " orphaned presets, sending to server";

    // Send orphaned presets through the same import endpoint
    nlohmann::json payload;
    payload["profiles"] = orphaned_json;

    std::string payload_json = payload.dump();

    m_fhub_client->set_api_base_url(api_base_url);
    m_fhub_client->import_filament_presets(
        access_token,
        payload_json,
        [this, orphaned_count](std::string response_body, unsigned http_status) {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Orphaned presets import response. Status: " << http_status;

            finish_export_operation();

            if (http_status != 200) {
                CallAfter([this, http_status]() {
                    show_notification_in_webview(
                        wxString::Format(_L("Failed to send orphaned presets. HTTP status: %d"), http_status),
                        "error"
                    );
                });
                return;
            }

            CallAfter([this, orphaned_count]() {
                show_notification_in_webview(
                    wxString::Format(_L("Found and recovered %d orphaned presets. Check your profile on FilamentHub."), orphaned_count),
                    "success"
                );
                send_command_to_webview("sync_complete");
            });
        },
        [this](std::string body, std::string error, unsigned http_status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to send orphaned presets. Error: " << error;
            finish_export_operation();
            CallAfter([this, error]() {
                show_notification_in_webview(
                    wxString::Format(_L("Failed to send orphaned presets: %s"), wxString::FromUTF8(error.c_str())),
                    "error"
                );
            });
        }
    );
}

} // namespace GUI
} // namespace Slic3r
