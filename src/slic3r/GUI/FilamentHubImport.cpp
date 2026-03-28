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
#include <nlohmann/json.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <libslic3r/AppConfig.hpp>
#include <fstream>
#include <sstream>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <wx/msgdlg.h>
#include <boost/algorithm/string.hpp>
#include <thread>
#include <algorithm>
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
        boost::filesystem::path temp_file; // Declared before try for cleanup in catch
        try {
            nlohmann::json profile_json = nlohmann::json::parse(profile_payload);

            std::string original_name = profile_json.value("name", std::string());
                std::string new_name = ensure_filamenthub_postfix(original_name);
                profile_json["name"] = new_name;

                ensure_parent_preset_exists(profile_json);

                boost::filesystem::path temp_dir = boost::filesystem::temp_directory_path();
            temp_file = temp_dir / ("filamenthub_preset_" + std::to_string(preset_id) + "_" + std::to_string(std::time(nullptr)) + ".json");

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
            if (!temp_file.empty()) {
                try { boost::filesystem::remove(temp_file); } catch (...) {}
            }
            wxString message = wxString::Format(_L("Error importing profile: %s"), wxString::FromUTF8(e.what()));
            send_response("import_profile", "error", message.ToUTF8().data(), sequence_id);
            wxMessageBox(message, _L("FilamentHub Import Error"), wxOK | wxICON_ERROR);
        }
    });
}

// ============================================================================
// Helper methods
// ============================================================================

std::string FilamentHubPanel::ensure_filamenthub_postfix(const std::string& preset_name)
{
    std::string postfix = " [fh]";
    
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
    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Looking for parent preset '" << inherits << "' in system presets...";
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
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Parent preset '" << inherits << "' found via find_preset2 -> '" << actual_name << "'";
        // Обновляем inherits на реальное имя найденного пресета
        if (actual_name != inherits) {
            profile_json["inherits"] = actual_name;
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Updated inherits from '" << inherits << "' to '" << actual_name << "'";
        }
    }
    
    // Логируем все системные пресеты для диагностики (всегда, не только если не найден)
    if (!system_preset_names.empty()) {
        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Available system presets (" << system_preset_names.size() << " total):";
        for (size_t i = 0; i < std::min(system_preset_names.size(), size_t(20)); ++i) {
            BOOST_LOG_TRIVIAL(debug) << "  - " << system_preset_names[i];
        }
        if (system_preset_names.size() > 20) {
            BOOST_LOG_TRIVIAL(debug) << "  ... and " << (system_preset_names.size() - 20) << " more";
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
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Using '" << fallback_preset << "' as fallback";
                break;
            }
        }
        
        // Если не нашли подходящий, используем первый системный пресет
        if (fallback_preset.empty() && !system_preset_names.empty()) {
            fallback_preset = system_preset_names[0];
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Using first system preset '" << fallback_preset << "' as fallback";
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
                
                // Добавляем постфикс [fh] к имени пресета
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
                            // ВАЖНО: Используем preset_name_to_save (имя пресета с постфиксом [fh]),
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

// ---------------------------------------------------------------------------
// Batch import: process all profiles from a single batch-export API response
// ---------------------------------------------------------------------------
void FilamentHubPanel::process_batch_export_response(
    const std::string& batch_json,
    const std::map<int, std::string>& presets_meta,
    int user_id,
    const std::string& access_token)
{
    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [BATCH] ========== process_batch_export_response START ==========";

    // ---------- 1. Parse the batch response (background thread) ----------
    nlohmann::json response;
    try {
        response = nlohmann::json::parse(batch_json);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [BATCH] Failed to parse batch response: " << e.what();
        CallAfter([this]() {
            m_active_syncs--;
            m_is_syncing.store(false);
            if (m_sync_progress) { m_sync_progress->Hide(); m_sync_progress->SetValue(0); }
            if (m_sync_status_label) m_sync_status_label->Hide();
            if (m_active_syncs <= 0) { update_sync_button_state(false); m_active_syncs = 0; }
            m_info_panel->Layout();
            show_notification_in_webview(_L("Failed to parse batch download response."), "error");
        });
        return;
    }

    if (!response.contains("profiles") || !response["profiles"].is_array()) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [BATCH] Response missing 'profiles' array";
        CallAfter([this]() {
            m_active_syncs--;
            m_is_syncing.store(false);
            if (m_sync_progress) { m_sync_progress->Hide(); m_sync_progress->SetValue(0); }
            if (m_sync_status_label) m_sync_status_label->Hide();
            if (m_active_syncs <= 0) { update_sync_button_state(false); m_active_syncs = 0; }
            m_info_panel->Layout();
            show_notification_in_webview(_L("Invalid batch download response."), "error");
        });
        return;
    }

    // ---------- 2. Parse profiles on background thread (NO wxWidgets access!) ----------
    // IMPORTANT: ensure_parent_preset_exists() accesses wxGetApp().preset_bundle
    // which is NOT thread-safe. All preset_bundle access must happen on UI thread.
    struct PreparedPreset {
        int preset_id;
        std::string new_name;
        nlohmann::json profile_json; // stored here, temp file written on UI thread
        std::string info_content;
        bool ok { false };
        std::string error_msg;
    };

    std::vector<PreparedPreset> prepared;
    prepared.reserve(response["profiles"].size());

    for (const auto& item : response["profiles"]) {
        int pid = item.value("preset_id", 0);
        std::string item_status = json_string_value_or(item, "status", "error");
        std::string item_error = json_string_value_or(item, "error", "");

        PreparedPreset pp;
        pp.preset_id = pid;

        if (item_status != "ok" || !item.contains("config") || item["config"].is_null()) {
            pp.ok = false;
            pp.error_msg = item_error.empty() ? "Export failed on server" : item_error;
            pp.new_name = presets_meta.count(pid) ? presets_meta.at(pid) : std::to_string(pid);
            prepared.push_back(std::move(pp));
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [BATCH] Preset " << pid << " skipped: " << pp.error_msg;
            continue;
        }

        try {
            pp.profile_json = item["config"];
            std::string original_name = json_string_value_or(pp.profile_json, "name", "");
            if (original_name.empty() && presets_meta.count(pid))
                original_name = presets_meta.at(pid);
            pp.new_name = ensure_filamenthub_postfix(original_name);
            pp.profile_json["name"] = pp.new_name;

            // Store .info content from batch response (no separate HTTP needed)
            if (item.contains("info") && item["info"].is_string())
                pp.info_content = item["info"].get<std::string>();

            pp.ok = true;
        } catch (const std::exception& e) {
            pp.ok = false;
            pp.error_msg = std::string("Prepare failed: ") + e.what();
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: [BATCH] Preset " << pid << ": " << pp.error_msg;
        }

        prepared.push_back(std::move(pp));
    }

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [BATCH] Parsed " << prepared.size()
                            << " presets. Posting to UI thread for import...";

    // ---------- 3. Import all on UI thread in one CallAfter ----------
    CallAfter([this, prepared = std::move(prepared), user_id]() mutable {
        bool sync_ui_released = false;

        auto release_sync_ui = [this, &sync_ui_released]() {
            if (sync_ui_released)
                return;
            sync_ui_released = true;
            m_active_syncs--;
            m_is_syncing.store(false);
            if (m_sync_progress) { m_sync_progress->Hide(); m_sync_progress->SetValue(0); }
            if (m_sync_status_label) m_sync_status_label->Hide();
            if (m_active_syncs <= 0) { update_sync_button_state(false); m_active_syncs = 0; }
            if (m_info_panel) m_info_panel->Layout();
        };

        auto fail_batch_sync = [this, &release_sync_ui](const wxString& message) {
            release_sync_ui();
            show_notification_in_webview(message, "error");
        };

        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [BATCH] UI thread import callback entered";

        try {
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [BATCH] ========== UI thread import START ==========";

            PresetBundle* bundle = wxGetApp().preset_bundle;
            if (bundle == nullptr) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: [BATCH] preset_bundle is null";
                fail_batch_sync(_L("Preset bundle not available."));
                return;
            }

            int synced = 0;
            int errors = 0;
            std::vector<std::string> detail_lines;

            for (auto& pp : prepared) {
                if (!pp.ok) {
                    errors++;
                    detail_lines.push_back(pp.new_name + " — ERROR: " + pp.error_msg);
                    continue;
                }

                bool import_ok = false;
                boost::filesystem::path temp_file;
                try {
                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [BATCH] Preparing UI-thread import for preset " << pp.preset_id;
                    ensure_parent_preset_exists(pp.profile_json);

                    boost::filesystem::path temp_dir = boost::filesystem::temp_directory_path();
                    temp_file = temp_dir / ("fhub_batch_" + std::to_string(pp.preset_id) + "_" +
                                           std::to_string(std::time(nullptr)) + ".json");
                    {
                        std::ofstream ofs(temp_file.string());
                        if (!ofs.is_open()) {
                            errors++;
                            detail_lines.push_back(pp.new_name + " — ERROR: Failed to create temp file");
                            continue;
                        }
                        ofs << pp.profile_json.dump(2);
                        ofs.close();
                    }

                    PresetsConfigSubstitutions substitutions;
                    int overwrite = 1;
                    std::vector<std::string> import_result_vec;
                    auto override_confirm = [](std::string const&) -> int { return 1; };
                    std::string file_path = temp_file.string();

                    import_ok = bundle->import_json_presets(
                        substitutions, file_path, override_confirm,
                        ForwardCompatibilitySubstitutionRule::Enable,
                        overwrite, import_result_vec
                    );

                    try {
                        if (!temp_file.empty())
                            boost::filesystem::remove(temp_file);
                    } catch (...) {}

                    if (import_ok || !import_result_vec.empty()) {
                        save_preset_mapping(pp.preset_id, pp.new_name);

                        if (!pp.info_content.empty()) {
                            Preset* imported = bundle->filaments.find_preset2(pp.new_name, true);
                            if (imported && !imported->file.empty()) {
                                boost::filesystem::path info_path(imported->file);
                                info_path.replace_extension(".info");
                                try {
                                    boost::filesystem::ofstream info_ofs(info_path);
                                    if (info_ofs.is_open()) {
                                        info_ofs << pp.info_content;
                                        info_ofs.close();
                                    }
                                } catch (const std::exception& e) {
                                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [BATCH] .info write failed: " << e.what();
                                }

                                if (imported) {
                                    std::istringstream info_stream(pp.info_content);
                                    std::string line;
                                    while (std::getline(info_stream, line)) {
                                        if (line.empty() || line[0] == '#') continue;
                                        size_t eq = line.find('=');
                                        if (eq == std::string::npos) continue;
                                        std::string key = line.substr(0, eq);
                                        std::string val = line.substr(eq + 1);
                                        boost::algorithm::trim(key);
                                        boost::algorithm::trim(val);
                                        if (key == "setting_id") imported->setting_id = val;
                                        else if (key == "base_id") imported->base_id = val;
                                        else if (key == "sync_info") imported->sync_info = val;
                                        else if (key == "updated_time") {
                                            try { imported->updated_time = std::stoll(val); } catch (...) {}
                                        }
                                    }
                                }
                            }
                        }

                        synced++;
                        detail_lines.push_back(pp.new_name + " — OK");
                        BOOST_LOG_TRIVIAL(info) << "FilamentHub: [BATCH] Preset " << pp.preset_id << " imported OK";
                    } else {
                        errors++;
                        detail_lines.push_back(pp.new_name + " — ERROR: import_json_presets failed");
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: [BATCH] import_json_presets failed for " << pp.preset_id;
                    }
                } catch (const std::exception& e) {
                    try {
                        if (!temp_file.empty())
                            boost::filesystem::remove(temp_file);
                    } catch (...) {}
                    errors++;
                    detail_lines.push_back(pp.new_name + " — ERROR: " + e.what());
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [BATCH] Exception importing " << pp.preset_id << ": " << e.what();
                }
            }

            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [BATCH] Before load_current_presets";
            wxGetApp().load_current_presets();
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [BATCH] After load_current_presets";

            bool no_errors = (errors == 0);
            if (no_errors && user_id > 0) {
                std::time_t now = std::time(nullptr);
                std::stringstream ss;
                ss << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%S.000000");
                std::string ts = ss.str();
                save_last_sync_time(user_id, ts, SyncTimestampType::Filament);
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: [BATCH] Saved last_sync_time=" << ts;
            } else if (!no_errors) {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [BATCH] Sync had " << errors
                                           << " errors — last_sync_time NOT updated";
            }

            m_full_sync_attempted.store(false);
            send_command_to_webview("sync_complete");

            release_sync_ui();
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [BATCH] Sync completed. Synced: " << synced << ", Errors: " << errors;

            {
                bool dev_mode = wxGetApp().app_config && wxGetApp().app_config->get("developer_mode") == "true";
                wxString message;
                if (no_errors)
                    message = wxString::Format(_L("Synced %d filament presets."), synced);
                else
                    message = wxString::Format(_L("Synced %d filament presets: %d successful, %d errors."),
                                               synced + errors, synced, errors);
                if (dev_mode && !detail_lines.empty()) {
                    message += "\n";
                    for (const auto& l : detail_lines)
                        message += "\n" + wxString::FromUTF8(l.c_str());
                }
                show_notification_in_webview(message, no_errors ? "success" : "warning");
            }

            update_user_info();
            update_unread_notifications_count();

            if (!no_errors) {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [BATCH] Skipping printer/print export due to errors";
                return;
            }

            // Auto-export printer/print profiles silently (don't re-grab m_is_syncing — UI already released)
            BOOST_LOG_TRIVIAL(info) << "FilamentHub: [BATCH] Scheduling silent auto-export...";
            std::string token;
            int uid = 0;
            if (load_auth_token(token, uid)) {
                check_user_permissions(token,
                    [this, token](bool filament_import, bool printer_import, bool printer_export, bool print_import, bool print_export) {
                        // Defer each export to its own CallAfter so UI stays responsive between them
                        if (printer_import) {
                            CallAfter([this, token]() {
                                try {
                                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [BATCH] Silent auto-export: printer profiles";
                                    std::string api_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
                                    m_active_exports.fetch_add(1);
                                    export_printer_profiles_to_filamenthub_internal(token, api_url);
                                } catch (const std::exception& e) {
                                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [BATCH] Silent printer export exception: " << e.what();
                                }
                            });
                        }
                        if (print_import) {
                            CallAfter([this, token]() {
                                try {
                                    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [BATCH] Silent auto-export: print profiles";
                                    std::string api_url = m_api_base_url.empty() ? FilamentHubClient::DEFAULT_API_BASE_URL : m_api_base_url;
                                    m_active_exports.fetch_add(1);
                                    export_print_profiles_to_filamenthub_internal(token, api_url);
                                } catch (const std::exception& e) {
                                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: [BATCH] Silent print export exception: " << e.what();
                                }
                            });
                        }
                    },
                    [](std::string error, unsigned status) {
                        BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Failed to check permissions: " << error;
                    }
                );
            }
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: [BATCH] Outer UI import exception: " << e.what();
            fail_batch_sync(_L("Batch preset import failed due to an internal error."));
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: [BATCH] Unknown outer UI import exception";
            fail_batch_sync(_L("Batch preset import failed due to an internal error."));
        }
    });

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: [BATCH] ========== process_batch_export_response END (posted to UI) ==========";
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
                
                // Добавляем постфикс [fh] к имени профиля
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
                    // ВАЖНО: Используем new_name (имя профиля с постфиксом [fh]),
                    // а НЕ import_result, так как import_result может содержать пути к файлам
                    // new_name уже содержит правильное имя профиля: original_name + " [fh]"
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
                
                // Добавляем постфикс [fh] к имени профиля
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
                    // ВАЖНО: Используем new_name (имя профиля с постфиксом [fh]),
                    // а НЕ import_result, так как import_result может содержать пути к файлам
                    // new_name уже содержит правильное имя профиля: original_name + " [fh]"
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

} // namespace GUI
} // namespace Slic3r
