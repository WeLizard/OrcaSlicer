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

namespace Slic3r {

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
        std::string url = s_api_base_url + "/health";

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .timeout_connect(5)
            .timeout_max(10)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Connection test successful. Status: " << status;
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
                        .timeout_connect(5)
                        .timeout_max(10)
                        .on_complete([this, on_complete](std::string body, unsigned status) {
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Connection test successful (root endpoint). Status: " << status;
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
        std::string url = s_api_base_url + "/api/v1/auth/login";

        nlohmann::json payload;
        payload["email_or_username"] = email_or_username;
        payload["password"] = password;

        auto request = Http::post(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .set_post_body(payload.dump())
            .timeout_connect(10)
            .timeout_max(30)
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
        std::string url = s_api_base_url + "/api/v1/auth/me";

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Get current user successful. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Get current user failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                on_error(body, error, status);
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
        std::string url = s_api_base_url + "/api/v1/presets/" + std::to_string(preset_id) + "/export/orcaslicer.json";

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Profile download successful. Status: " << status;
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
        std::string url = s_api_base_url + "/api/v1/presets/" + std::to_string(preset_id) + "/export/orcaslicer.info";

        auto request = Http::get(url)
            .header("Content-Type", "text/plain")
            .header("Accept", "text/plain")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: .info file download successful. Status: " << status;
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

void FilamentHubClient::get_my_presets(
    const std::string& access_token,
    const std::string& updated_since,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    try {
        std::string url = s_api_base_url + "/api/v1/auth/my-presets";

        // Добавляем query параметр updated_since если указан
        if (!updated_since.empty()) {
            url += "?updated_since=" + Http::url_encode(updated_since);
        }

        BOOST_LOG_TRIVIAL(info) << "FilamentHub: get_my_presets() called. URL: " << url
                                << ", updated_since: " << (updated_since.empty() ? "(empty)" : updated_since);

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Get my presets successful. Status: " << status << ", Body size: " << body.size() << " bytes";
                cleanup_completed_requests();
                if (on_complete) {
                    try {
                        on_complete(body, status);
                    } catch (const std::exception& e) {
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in on_complete callback: " << e.what();
                    }
                }
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Get my presets failed. Error: " << error << ", Status: " << status;
                if (body.size() < 500) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentHub: Error body: " << body;
                }
                cleanup_completed_requests();
                if (on_error) {
                    on_error(body, error, status);
                }
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
        std::string url = s_api_base_url + "/api/v1/orcaslicer/printer-profiles";

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
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Get my printer profiles successful. Status: " << status;
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
        std::string url = s_api_base_url + "/api/v1/orcaslicer/print-profiles";

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
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Get my print profiles successful. Status: " << status;
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
        std::string url = s_api_base_url + "/api/v1/printer-profiles/" + std::to_string(profile_id) + "/export/orcaslicer.json";

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Printer profile download successful. Status: " << status;
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
        std::string url = s_api_base_url + "/api/v1/print-profiles/" + std::to_string(profile_id) + "/export/orcaslicer.json";

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Print profile download successful. Status: " << status;
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
        std::string url = s_api_base_url + "/api/v1/orcaslicer/printer-profiles/import";

        auto request = Http::post(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .set_post_body(profiles_json)
            .timeout_connect(10)
            .timeout_max(30)
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
        std::string url = s_api_base_url + "/api/v1/orcaslicer/print-profiles/import";

        auto request = Http::post(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .set_post_body(profiles_json)
            .timeout_connect(10)
            .timeout_max(30)
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
        std::string url = s_api_base_url + "/api/v1/orcaslicer/filaments/import";

        auto request = Http::post(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .set_post_body(presets_json)
            .timeout_connect(10)
            .timeout_max(30)
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
        std::string url = s_api_base_url + "/api/v1/presets/" + std::to_string(preset_id);

        auto request = Http::del(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(10)
            .timeout_max(30)
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
        std::string url = s_api_base_url + "/api/v1/orcaslicer/deleted-presets";

        auto request = Http::post(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(10)
            .timeout_max(30)
            .set_post_body(deleted_presets_json)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Deleted presets report successful. Status: " << status;
                cleanup_completed_requests();
                on_complete(body, status);
            })
            .on_error([this, on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Deleted presets report failed. Error: " << error << ", Status: " << status;
                cleanup_completed_requests();
                on_error(body, error, status);
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
        std::string url = s_api_base_url + "/api/v1/notifications/unread-count";

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Unread notifications count retrieved. Status: " << status;
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
        std::string url = s_api_base_url + "/api/v1/auth/me/presets-stats";

        auto request = Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([this, on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Presets stats retrieved. Status: " << status;
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

} // namespace Slic3r
