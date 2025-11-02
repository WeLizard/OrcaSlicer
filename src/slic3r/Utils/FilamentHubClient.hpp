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

#ifndef __FILAMENTHUB_CLIENT_HPP__
#define __FILAMENTHUB_CLIENT_HPP__

#include <string>
#include <functional>
#include <memory>
#include "Http.hpp"

namespace Slic3r {

// Forward declarations
class DynamicPrintConfig;

/**
 * \brief Client for FilamentHub API integration
 * 
 * This class provides methods to interact with FilamentHub REST API:
 * - Authentication (login/logout)
 * - Fetching filament profiles
 * - Syncing presets
 * 
 * All HTTP requests use the Http class (libcurl-based) already available in OrcaSlicer.
 */
class FilamentHubClient
{
public:
    // Constructor
    FilamentHubClient();
    ~FilamentHubClient() = default;

    // API base URL (configurable, defaults to localhost:8000 for development)
    static std::string get_api_base_url();
    static void set_api_base_url(const std::string& url);

    /**
     * \brief Test connection to FilamentHub API
     * 
     * Sends a simple GET request to /api/v1/health or /api/v1/ to verify connectivity.
     * 
     * \param on_complete Called when request succeeds. Parameters: (response_body, http_status)
     * \param on_error Called when request fails. Parameters: (response_body, error_message, http_status)
     * \return true if request was initiated, false otherwise
     */
    bool test_connection(
        std::function<void(std::string /* body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    ) const;

    /**
     * \brief Login to FilamentHub
     * 
     * Authenticates user with email/username and password.
     * Returns JWT token for subsequent API calls.
     * 
     * \param email_or_username User email or username
     * \param password User password
     * \param on_complete Called when login succeeds. Parameters: (response_body, http_status)
     * \param on_error Called when login fails. Parameters: (response_body, error_message, http_status)
     */
    void login(
        const std::string& email_or_username,
        const std::string& password,
        std::function<void(std::string /* body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    ) const;

    /**
     * \brief Get current user info
     * 
     * Retrieves information about the currently authenticated user.
     * 
     * \param access_token JWT access token
     * \param on_complete Called when request succeeds. Parameters: (response_body, http_status)
     * \param on_error Called when request fails. Parameters: (response_body, error_message, http_status)
     */
    void get_current_user(
        const std::string& access_token,
        std::function<void(std::string /* body */, unsigned /* http_status */)> on_complete,
        std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> on_error
    ) const;

    /**
     * \brief Check if user is authenticated
     * 
     * \return true if access token is set, false otherwise
     */
    bool is_authenticated() const;

    /**
     * \brief Get stored access token
     * 
     * \return Access token string or empty string if not authenticated
     */
    std::string get_access_token() const;

    /**
     * \brief Set access token
     * 
     * \param token JWT access token
     */
    void set_access_token(const std::string& token);

    /**
     * \brief Clear access token (logout)
     */
    void clear_access_token();

private:
    std::string m_access_token;
    static std::string s_api_base_url;
};

} // namespace Slic3r

#endif // __FILAMENTHUB_CLIENT_HPP__

