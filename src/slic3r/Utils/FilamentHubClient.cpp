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
 * New file for FilamentHub API integration.
 * Original copyright (C) SoftFever/OrcaSlicer.
 *
 * Licensed under AGPL-3.0 (same as original OrcaSlicer)
 * Source: https://github.com/WeLizard/OrcaSlicer
 * Branch: filamenthub-integration
 * =============================================================================
 */

#include "FilamentHubClient.hpp"
#include "Http.hpp"
#include "nlohmann/json.hpp"
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
#include <utility>

namespace Slic3r {

namespace {

template <typename Callback, typename... Args>
void invoke_callback_safe(const char* tag, const Callback& callback, Args&&... args)
{
    if (!callback)
        return;

    try {
        callback(std::forward<Args>(args)...);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: callback exception in " << tag << ": " << e.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: unknown callback exception in " << tag;
    }
}

} // namespace

// Static member initialization
const std::string FilamentHubClient::DEFAULT_API_BASE_URL = "https://filamenthub.ru";
std::string FilamentHubClient::s_api_base_url = FilamentHubClient::DEFAULT_API_BASE_URL;

FilamentHubClient::FilamentHubClient()
    : m_access_token("")
{
}

FilamentHubClient::~FilamentHubClient()
{
    cancel_all();
}

void FilamentHubClient::cancel_all()
{
    std::lock_guard<std::mutex> lock(m_requests_mutex);
    for (auto& req : m_active_requests) {
        if (req) {
            req->cancel();
        }
    }
    m_active_requests.clear();
}

void FilamentHubClient::store_request(Http::Ptr request)
{
    std::lock_guard<std::mutex> lock(m_requests_mutex);
    // Clean up completed requests first (use_count == 1 means only we hold the reference)
    m_active_requests.erase(
        std::remove_if(m_active_requests.begin(), m_active_requests.end(),
            [](const Http::Ptr& req) { return !req || req.use_count() <= 1; }),
        m_active_requests.end()
    );
    m_active_requests.push_back(request);
}

void FilamentHubClient::cleanup_completed_requests()
{
    std::lock_guard<std::mutex> lock(m_requests_mutex);
    m_active_requests.erase(
        std::remove_if(m_active_requests.begin(), m_active_requests.end(),
            [](const Http::Ptr& req) { return !req || req.use_count() <= 1; }),
        m_active_requests.end()
    );
}

std::string FilamentHubClient::get_api_base_url()
{
    return s_api_base_url;
}

void FilamentHubClient::set_api_base_url(const std::string& url)
{
    s_api_base_url = url;
}

bool FilamentHubClient::test_connection(
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        // Try /health first (simple health check endpoint)
        std::string url = s_api_base_url + API_HEALTH;

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .timeout_connect(TIMEOUT_CONNECT_HEALTH)
            .timeout_max(TIMEOUT_MAX_HEALTH)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Connection test successful. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error, on_complete](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Health check failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                // If health endpoint doesn't exist, try root endpoint as fallback
                if (status == 404) {
                    std::string root_url = FilamentHubClient::get_api_base_url() + "/";
                    auto fallback_request = Http::get(root_url)
                        .header("Content-Type", "application/json")
                        .header("Accept", "application/json")
                        .timeout_connect(TIMEOUT_CONNECT_HEALTH)
                        .timeout_max(TIMEOUT_MAX_HEALTH)
                        .on_complete([this, on_complete](std::string body, unsigned status) {
                            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Connection test successful (root endpoint). Status: " << status;
                            cleanup_completed_requests();
                            on_complete(body, status);
                        })
                        .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Connection test failed. Error: " << error << ", Status: " << status;
                            cleanup_completed_requests();
                            on_error(body, error, status);
                        })
                        .perform();
                    store_request(fallback_request);
                } else {
                    on_error(body, error, status);
                }
            })
            .perform();

        store_request(request);
        return true;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in test_connection: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
        return false;
    }
}

void FilamentHubClient::login(
    const std::string& email_or_username,
    const std::string& password,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_AUTH_LOGIN;

        nlohmann::json payload;
        payload["email_or_username"] = email_or_username;
        payload["password"] = password;

        auto request = Http::post(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .set_post_body(payload.dump())
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Login successful. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Login failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                on_error(body, error, status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in login: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::get_current_user(
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_AUTH_ME;

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Get current user successful. Status: " << status;
                cleanup_completed_requests();
                invoke_callback_safe("get_current_user.on_complete", on_complete, std::move(body), status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Get current user failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                invoke_callback_safe("get_current_user.on_error", on_error, std::move(body), std::move(error), status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in get_current_user: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

bool FilamentHubClient::is_authenticated() const
{
    return !m_access_token.empty();
}

std::string FilamentHubClient::get_access_token() const
{
    return m_access_token;
}

void FilamentHubClient::set_access_token(const std::string& token)
{
    m_access_token = token;
}

void FilamentHubClient::clear_access_token()
{
    m_access_token.clear();
}

void FilamentHubClient::download_profile(
    int preset_id,
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_PRESETS_BASE + std::to_string(preset_id) + API_EXPORT_JSON_SUFFIX;

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Profile download successful. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Profile download failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                on_error(body, error, status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in download_profile: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::download_profile_info(
    int preset_id,
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_PRESETS_BASE + std::to_string(preset_id) + API_EXPORT_INFO_SUFFIX;

        auto request = Http::get(url)
            .header("Content-Type", "text/plain")
            .header("Accept", "text/plain")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: .info file download successful. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: .info file download failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                on_error(body, error, status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in download_profile_info: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::batch_download_profiles(
    const std::vector<int>& preset_ids,
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_BATCH_EXPORT;

        // Build JSON body: {"preset_ids": [1, 2, 3, ...]}
        nlohmann::json payload;
        payload["preset_ids"] = preset_ids;
        std::string body = payload.dump();

        BOOST_LOG_TRIVIAL(info) << "FilamentHub: Batch download " << preset_ids.size()
                                << " profiles (" << body.size() << " bytes)";

        auto request = Http::post(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .set_post_body(body)
            .timeout_connect(TIMEOUT_CONNECT_BATCH)
            .timeout_max(TIMEOUT_MAX_BATCH)
            .on_complete([this, on_complete, count = preset_ids.size()](std::string resp, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Batch download successful (" << count
                                        << " profiles). Status: " << status;
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [BATCH] batch_download_profiles on_complete callback enter. Status: " << status;
                cleanup_completed_requests();
                invoke_callback_safe("batch_download_profiles.on_complete", on_complete, std::move(resp), status);
            })
            .on_error([this, on_error](std::string resp, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Batch download failed. Error: " << error
                                         << ", Status: " << status;
                cleanup_completed_requests();
                invoke_callback_safe("batch_download_profiles.on_error", on_error, std::move(resp), std::move(error), status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in batch_download_profiles: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::get_my_presets(
    const std::string& access_token,
    const std::string& updated_since,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_AUTH_MY_PRESETS;

        // Добавляем query параметр updated_since если указан
        if (!updated_since.empty()) {
            url += "?updated_since=" + Http::url_encode(updated_since);
        }

        BOOST_LOG_TRIVIAL(debug) << "FilamentHub: get_my_presets() called. URL: " << url
                                << ", updated_since: " << (updated_since.empty() ? "(empty)" : updated_since);

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Get my presets successful. Status: " << status << ", Body size: " << body.size() << " bytes";
                cleanup_completed_requests();
                invoke_callback_safe("get_my_presets.on_complete", on_complete, std::move(body), status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Get my presets failed. Error: " << error << ", Status: " << status;
                if (body.size() < 500) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error body: " << body;
                }
                cleanup_completed_requests();
                invoke_callback_safe("get_my_presets.on_error", on_error, std::move(body), std::move(error), status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in get_my_presets: " << e.what();
        if (on_error) {
            on_error("", std::string("Exception: ") + e.what(), 0);
        }
    }
}

void FilamentHubClient::get_my_printer_profiles(
    const std::string& access_token,
    const std::string& updated_since,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_PRINTER_PROFILES;

        // Добавляем query параметры
        bool has_query = false;
        if (!updated_since.empty()) {
            url += "?updated_since=" + Http::url_encode(updated_since);
            has_query = true;
        }
        // Включаем официальные профили по умолчанию
        url += (has_query ? "&" : "?") + std::string("include_official=true");

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Get my printer profiles successful. Status: " << status;
                cleanup_completed_requests();
                if (on_complete) {
                    on_complete(body, status);
                }
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Get my printer profiles failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                if (on_error) {
                    on_error(body, error, status);
                }
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in get_my_printer_profiles: " << e.what();
        if (on_error) {
            on_error("", std::string("Exception: ") + e.what(), 0);
        }
    }
}

void FilamentHubClient::get_my_print_profiles(
    const std::string& access_token,
    const std::string& updated_since,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_PRINT_PROFILES;

        // Добавляем query параметры
        bool has_query = false;
        if (!updated_since.empty()) {
            url += "?updated_since=" + Http::url_encode(updated_since);
            has_query = true;
        }
        // Включаем официальные профили по умолчанию
        url += (has_query ? "&" : "?") + std::string("include_official=true");

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Get my print profiles successful. Status: " << status;
                cleanup_completed_requests();
                if (on_complete) {
                    on_complete(body, status);
                }
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Get my print profiles failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                if (on_error) {
                    on_error(body, error, status);
                }
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in get_my_print_profiles: " << e.what();
        if (on_error) {
            on_error("", std::string("Exception: ") + e.what(), 0);
        }
    }
}

void FilamentHubClient::download_printer_profile(
    int profile_id,
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_PRINTER_PROFILES_BASE + std::to_string(profile_id) + API_EXPORT_JSON_SUFFIX;

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Printer profile download successful. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Printer profile download failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                on_error(body, error, status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in download_printer_profile: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::download_print_profile(
    int profile_id,
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_PRINT_PROFILES_BASE + std::to_string(profile_id) + API_EXPORT_JSON_SUFFIX;

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Print profile download successful. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Print profile download failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                on_error(body, error, status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in download_print_profile: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::import_printer_profiles(
    const std::string& access_token,
    const std::string& profiles_json,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_PRINTER_PROFILES_IMPORT;

        auto request = Http::post(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .set_post_body(profiles_json)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profiles import successful. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Printer profiles import failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                on_error(body, error, status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in import_printer_profiles: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::import_print_profiles(
    const std::string& access_token,
    const std::string& profiles_json,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_PRINT_PROFILES_IMPORT;

        auto request = Http::post(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .set_post_body(profiles_json)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profiles import successful. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Print profiles import failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                on_error(body, error, status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in import_print_profiles: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::import_filament_presets(
    const std::string& access_token,
    const std::string& presets_json,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_FILAMENTS_IMPORT;

        auto request = Http::post(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .set_post_body(presets_json)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Filament presets import successful. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Filament presets import failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                on_error(body, error, status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in import_filament_presets: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::delete_preset(
    int preset_id,
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_PRESETS_BASE + std::to_string(preset_id);

        auto request = Http::del(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Preset deletion successful. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Preset deletion failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                on_error(body, error, status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in delete_preset: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::report_deleted_presets(
    const std::string& access_token,
    const std::string& deleted_presets_json,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_DELETED_PRESETS;

        auto request = Http::post(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .set_post_body(deleted_presets_json)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Deleted presets report successful. Status: " << status;
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: [SYNC] report_deleted_presets on_complete callback enter. Status: " << status;
                cleanup_completed_requests();
                invoke_callback_safe("report_deleted_presets.on_complete", on_complete, std::move(body), status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Deleted presets report failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                invoke_callback_safe("report_deleted_presets.on_error", on_error, std::move(body), std::move(error), status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in report_deleted_presets: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::get_unread_notifications_count(
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_NOTIFICATIONS_UNREAD_COUNT;

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Unread notifications count retrieved. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get unread notifications count. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                on_error(body, error, status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in get_unread_notifications_count: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::get_presets_stats(
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + API_AUTH_PRESETS_STATS;

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
            .timeout_max(TIMEOUT_MAX_DEFAULT)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: Presets stats retrieved. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Failed to get presets stats. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                on_error(body, error, status);
            })
            .perform();
        store_request(request);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in get_presets_stats: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

std::map<int, int> FilamentHubClient::resolve_spool_presets_sync(
    const std::string& access_token,
    const std::string& spool_ids)
{
    std::map<int, int> result;
    if (access_token.empty() || spool_ids.empty())
        return result;

    std::string url = get_api_base_url() + API_SPOOL_PRESET_MAPPING + std::string("?spool_ids=") + spool_ids;
    std::string response_body;
    bool ok = false;

    Http::get(url)
        .header("Content-Type", "application/json")
        .header("Accept", "application/json")
        .header("Authorization", "Bearer " + access_token)
        .timeout_connect(TIMEOUT_CONNECT_SPOOL)
        .timeout_max(TIMEOUT_MAX_SPOOL)
        .on_complete([&](std::string body, unsigned status) {
            if (status == 200) {
                response_body = std::move(body);
                ok = true;
            } else {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: resolve_spool_presets_sync HTTP " << status;
            }
        })
        .on_error([](std::string, std::string error, unsigned) {
            BOOST_LOG_TRIVIAL(debug) << "FilamentHub: resolve_spool_presets_sync error: " << error;
        })
        .perform_sync();

    if (!ok)
        return result;

    auto json = nlohmann::json::parse(response_body, nullptr, false);
    if (json.is_discarded() || !json.contains("mapping"))
        return result;

    for (auto& [spool_id_str, val] : json["mapping"].items()) {
        if (val.is_null() || !val.contains("preset_id"))
            continue;
        int spool_id = std::stoi(spool_id_str);
        int preset_id = val["preset_id"].get<int>();
        result[spool_id] = preset_id;
    }

    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: resolve_spool_presets_sync resolved " << result.size() << " spools";
    return result;
}

} // namespace Slic3r
