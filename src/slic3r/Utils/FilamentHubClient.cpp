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
 * Source: https://github.com/lizardjazz1/OrcaSlicer
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
std::string FilamentHubClient::s_api_base_url = "http://localhost:8000";

FilamentHubClient::FilamentHubClient()
    : m_access_token("")
{
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
) const
{
    try {
        // Try /health first (simple health check endpoint)
        std::string url = s_api_base_url + "/health";
        
        Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .timeout_connect(5)
            .timeout_max(10)
            .on_complete([on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Connection test successful. Status: " << status;
                on_complete(body, status);
            })
            .on_error([on_error, on_complete](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(warning) << "FilamentHub: Health check failed. Error: " << error << ", Status: " << status;
                // If health endpoint doesn't exist, try root endpoint as fallback
                if (status == 404) {
                    std::string root_url = FilamentHubClient::get_api_base_url() + "/";
                    Http::get(root_url)
                        .header("Content-Type", "application/json")
                        .header("Accept", "application/json")
                        .timeout_connect(5)
                        .timeout_max(10)
                        .on_complete([=](std::string body, unsigned status) {
                            BOOST_LOG_TRIVIAL(info) << "FilamentHub: Connection test successful (root endpoint). Status: " << status;
                            on_complete(body, status);
                        })
                        .on_error([=](std::string body, std::string error, unsigned status) {
                            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Connection test failed. Error: " << error << ", Status: " << status;
                            on_error(body, error, status);
                        })
                        .perform_sync();
                } else {
                    on_error(body, error, status);
                }
            })
            .perform_sync();
        
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
) const
{
    try {
        std::string url = s_api_base_url + "/api/v1/auth/login";
        
        nlohmann::json payload;
        payload["email_or_username"] = email_or_username;
        payload["password"] = password;
        
        Http::post(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .set_post_body(payload.dump())
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Login successful. Status: " << status;
                on_complete(body, status);
            })
            .on_error([on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Login failed. Error: " << error << ", Status: " << status;
                on_error(body, error, status);
            })
            .perform_sync();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in login: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::get_current_user(
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
) const
{
    try {
        std::string url = s_api_base_url + "/api/v1/auth/me";
        
        Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Get current user successful. Status: " << status;
                on_complete(body, status);
            })
            .on_error([on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Get current user failed. Error: " << error << ", Status: " << status;
                on_error(body, error, status);
            })
            .perform_sync();
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
) const
{
    try {
        std::string url = s_api_base_url + "/api/v1/presets/" + std::to_string(preset_id) + "/export/orcaslicer.json";
        
        Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Profile download successful. Status: " << status;
                on_complete(body, status);
            })
            .on_error([on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Profile download failed. Error: " << error << ", Status: " << status;
                on_error(body, error, status);
            })
            .perform_sync();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in download_profile: " << e.what();
        on_error("", std::string("Exception: ") + e.what(), 0);
    }
}

void FilamentHubClient::get_my_presets(
    const std::string& access_token,
    const std::string& updated_since,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
) const
{
    try {
        std::string url = s_api_base_url + "/api/v1/auth/my-presets";
        
        // Добавляем query параметр updated_since если указан
        if (!updated_since.empty()) {
            url += "?updated_since=" + Http::url_encode(updated_since);
        }
        
        Http::get(url)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("Authorization", "Bearer " + access_token)
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([on_complete](std::string body, unsigned status) {
                BOOST_LOG_TRIVIAL(info) << "FilamentHub: Get my presets successful. Status: " << status;
                if (on_complete) {
                    on_complete(body, status);
                }
            })
            .on_error([on_error](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "FilamentHub: Get my presets failed. Error: " << error << ", Status: " << status;
                if (on_error) {
                    on_error(body, error, status);
                }
            })
            .perform_sync();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in get_my_presets: " << e.what();
        if (on_error) {
            on_error("", std::string("Exception: ") + e.what(), 0);
        }
    }
}

} // namespace Slic3r

