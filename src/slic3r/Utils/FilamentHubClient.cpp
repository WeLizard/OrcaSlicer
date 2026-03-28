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
#include <thread>
#include <chrono>

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

bool is_retryable_error(unsigned http_status)
{
    // Status 0: connection failure, DNS error, timeout
    if (http_status == 0)   return true;
    // 429: rate limited
    if (http_status == 429)  return true;
    // 5xx: server errors
    if (http_status >= 500 && http_status < 600) return true;
    return false;
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

void FilamentHubClient::perform_with_retry(
    const char* tag,
    RequestExecutor execute_request,
    Http::CompleteFn on_complete,
    Http::ErrorFn on_error,
    int max_retries)
{
    auto attempt = std::make_shared<int>(0);
    auto tag_str = std::make_shared<std::string>(tag);
    auto try_request = std::make_shared<std::function<void()>>();

    *try_request = [this, execute_request, on_complete, on_error, max_retries, attempt, tag_str, try_request]() {
        (*attempt)++;
        try {
            Http::CompleteFn wrapped_complete = [this, on_complete, tag_str](std::string body, unsigned status) {
                cleanup_completed_requests();
                invoke_callback_safe(tag_str->c_str(), on_complete, std::move(body), status);
            };

            Http::ErrorFn wrapped_error = [this, on_error, max_retries, attempt, tag_str, try_request](std::string body, std::string error, unsigned status) {
                cleanup_completed_requests();

                if (*attempt < max_retries && is_retryable_error(status)) {
                    int delay_ms = RETRY_INITIAL_DELAY_MS * (1 << (*attempt - 1));
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: " << *tag_str
                        << " failed (attempt " << *attempt << "/" << max_retries
                        << ", status=" << status << "), retrying in " << delay_ms << "ms...";
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                    (*try_request)();
                } else {
                    if (*attempt > 1) {
                        BOOST_LOG_TRIVIAL(error) << "FilamentHub: " << *tag_str
                            << " failed after " << *attempt << " attempts";
                    }
                    invoke_callback_safe((*tag_str + ".on_error").c_str(), on_error,
                        std::move(body), std::move(error), status);
                }
            };

            execute_request(std::move(wrapped_complete), std::move(wrapped_error));
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "FilamentHub: Exception in " << *tag_str << ": " << e.what();
            invoke_callback_safe((*tag_str + ".on_error").c_str(), on_error,
                std::string(), std::string("Exception: ") + e.what(), 0u);
        }
    };

    (*try_request)();
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
    std::string url = s_api_base_url + API_AUTH_ME;

    perform_with_retry("get_current_user",
        [this, url, access_token](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::get(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_DEFAULT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
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
    std::string url = s_api_base_url + API_PRESETS_BASE + std::to_string(preset_id) + API_EXPORT_JSON_SUFFIX;

    perform_with_retry("download_profile",
        [this, url, access_token](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::get(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_DEFAULT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
}

void FilamentHubClient::download_profile_info(
    int preset_id,
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    std::string url = s_api_base_url + API_PRESETS_BASE + std::to_string(preset_id) + API_EXPORT_INFO_SUFFIX;

    perform_with_retry("download_profile_info",
        [this, url, access_token](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::get(url)
                .header("Content-Type", "text/plain")
                .header("Accept", "text/plain")
                .header("Authorization", "Bearer " + access_token)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_DEFAULT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
}

void FilamentHubClient::batch_download_profiles(
    const std::vector<int>& preset_ids,
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    std::string url = s_api_base_url + API_BATCH_EXPORT;

    nlohmann::json payload;
    payload["preset_ids"] = preset_ids;
    std::string body = payload.dump();

    BOOST_LOG_TRIVIAL(info) << "FilamentHub: Batch download " << preset_ids.size()
                            << " profiles (" << body.size() << " bytes)";

    perform_with_retry("batch_download_profiles",
        [this, url, access_token, body](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::post(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .set_post_body(body)
                .timeout_connect(TIMEOUT_CONNECT_BATCH)
                .timeout_max(TIMEOUT_MAX_BATCH)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
}

void FilamentHubClient::get_my_presets(
    const std::string& access_token,
    const std::string& updated_since,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    std::string url = s_api_base_url + API_AUTH_MY_PRESETS;
    if (!updated_since.empty()) {
        url += "?updated_since=" + Http::url_encode(updated_since);
    }

    BOOST_LOG_TRIVIAL(debug) << "FilamentHub: get_my_presets() URL: " << url;

    perform_with_retry("get_my_presets",
        [this, url, access_token](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::get(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_DEFAULT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
}

void FilamentHubClient::get_my_printer_profiles(
    const std::string& access_token,
    const std::string& updated_since,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    std::string url = s_api_base_url + API_PRINTER_PROFILES;
    bool has_query = false;
    if (!updated_since.empty()) {
        url += "?updated_since=" + Http::url_encode(updated_since);
        has_query = true;
    }
    url += (has_query ? "&" : "?") + std::string("include_official=true");

    perform_with_retry("get_my_printer_profiles",
        [this, url, access_token](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::get(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_DEFAULT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
}

void FilamentHubClient::get_my_print_profiles(
    const std::string& access_token,
    const std::string& updated_since,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    std::string url = s_api_base_url + API_PRINT_PROFILES;
    bool has_query = false;
    if (!updated_since.empty()) {
        url += "?updated_since=" + Http::url_encode(updated_since);
        has_query = true;
    }
    url += (has_query ? "&" : "?") + std::string("include_official=true");

    perform_with_retry("get_my_print_profiles",
        [this, url, access_token](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::get(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_DEFAULT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
}

void FilamentHubClient::download_printer_profile(
    int profile_id,
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    std::string url = s_api_base_url + API_PRINTER_PROFILES_BASE + std::to_string(profile_id) + API_EXPORT_JSON_SUFFIX;

    perform_with_retry("download_printer_profile",
        [this, url, access_token](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::get(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_DEFAULT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
}

void FilamentHubClient::download_print_profile(
    int profile_id,
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    std::string url = s_api_base_url + API_PRINT_PROFILES_BASE + std::to_string(profile_id) + API_EXPORT_JSON_SUFFIX;

    perform_with_retry("download_print_profile",
        [this, url, access_token](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::get(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_DEFAULT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
}

void FilamentHubClient::import_printer_profiles(
    const std::string& access_token,
    const std::string& profiles_json,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    std::string url = s_api_base_url + API_PRINTER_PROFILES_IMPORT;

    perform_with_retry("import_printer_profiles",
        [this, url, access_token, profiles_json](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::post(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .set_post_body(profiles_json)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_IMPORT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
}

void FilamentHubClient::import_print_profiles(
    const std::string& access_token,
    const std::string& profiles_json,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    std::string url = s_api_base_url + API_PRINT_PROFILES_IMPORT;

    perform_with_retry("import_print_profiles",
        [this, url, access_token, profiles_json](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::post(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .set_post_body(profiles_json)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_IMPORT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
}

void FilamentHubClient::import_filament_presets(
    const std::string& access_token,
    const std::string& presets_json,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    std::string url = s_api_base_url + API_FILAMENTS_IMPORT;

    perform_with_retry("import_filament_presets",
        [this, url, access_token, presets_json](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::post(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .set_post_body(presets_json)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_IMPORT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
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
    std::string url = s_api_base_url + API_DELETED_PRESETS;

    perform_with_retry("report_deleted_presets",
        [this, url, access_token, deleted_presets_json](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::post(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .set_post_body(deleted_presets_json)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_DEFAULT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
}

void FilamentHubClient::get_unread_notifications_count(
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    std::string url = s_api_base_url + API_NOTIFICATIONS_UNREAD_COUNT;

    perform_with_retry("get_unread_notifications_count",
        [this, url, access_token](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::get(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_DEFAULT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
}

void FilamentHubClient::get_presets_stats(
    const std::string& access_token,
    std::function<void(std::string, unsigned)> on_complete,
    std::function<void(std::string, std::string, unsigned)> on_error
)
{
    std::string url = s_api_base_url + API_AUTH_PRESETS_STATS;

    perform_with_retry("get_presets_stats",
        [this, url, access_token](Http::CompleteFn complete, Http::ErrorFn error) {
            auto request = Http::get(url)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + access_token)
                .timeout_connect(TIMEOUT_CONNECT_DEFAULT)
                .timeout_max(TIMEOUT_MAX_DEFAULT)
                .on_complete(std::move(complete))
                .on_error(std::move(error))
                .perform();
            store_request(request);
        },
        on_complete, on_error);
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

    for (int attempt = 1; attempt <= RETRY_MAX_ATTEMPTS; ++attempt) {
        response_body.clear();
        ok = false;

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
                } else if (is_retryable_error(status)) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentHub: resolve_spool_presets_sync HTTP " << status;
                }
            })
            .on_error([&](std::string, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(debug) << "FilamentHub: resolve_spool_presets_sync error: " << error;
                // ok remains false, will retry
            })
            .perform_sync();

        if (ok)
            break;

        if (attempt < RETRY_MAX_ATTEMPTS) {
            int delay_ms = RETRY_INITIAL_DELAY_MS * (1 << (attempt - 1));
            BOOST_LOG_TRIVIAL(warning) << "FilamentHub: resolve_spool_presets_sync retry "
                << attempt << "/" << RETRY_MAX_ATTEMPTS << " in " << delay_ms << "ms";
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }

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
