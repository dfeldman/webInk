/**
 * @file webink_config.cpp
 * @brief Implementation of WebInkConfig class for configuration management
 * 
 * @author WebInk Component Authors
 * @version 1.0.0
 */

#include "webink_config.h"
#include <algorithm>
#include <cstdlib>

namespace esphome {
namespace webink {

const char* WebInkConfig::TAG = "webink.config";

//=============================================================================
// CONSTRUCTOR
//=============================================================================

WebInkConfig::WebInkConfig() {
    // Initialize fixed char arrays with defaults (avoid dynamic allocation)
    strcpy(base_url, "http://192.168.68.69:8090");
    strcpy(device_id, "default");
    strcpy(api_key, "myapikey");
    strcpy(display_mode, "800x480x1xB");
    
    ESP_LOGD(TAG, "WebInkConfig initialized with fixed arrays (no dynamic allocation)");
    ESP_LOGD(TAG, "Server URL: %s", base_url);
    ESP_LOGD(TAG, "Device ID: %s", device_id);
    ESP_LOGD(TAG, "Display mode: %s", display_mode);
    ESP_LOGD(TAG, "Socket port: %d", socket_mode_port);
}

//=============================================================================
// CONFIGURATION SETTERS WITH VALIDATION
//=============================================================================

bool WebInkConfig::set_server_url(const char* url) {
    if (!url || !validate_url(url)) {
        ESP_LOGW(TAG, "Invalid server URL format: %s", url ? url : "NULL");
        return false;
    }
    
    if (strlen(url) >= sizeof(base_url)) {
        ESP_LOGW(TAG, "Server URL too long (max %zu chars): %s", sizeof(base_url)-1, url);
        return false;
    }
    
    char old_url[sizeof(base_url)];
    strcpy(old_url, base_url);
    strcpy(base_url, url);
    
    ESP_LOGI(TAG, "Server URL updated: %s -> %s", old_url, base_url);
    notify_change("server_url");
    
    return true;
}

bool WebInkConfig::set_device_id(const char* id) {
    if (!id || !validate_device_id(id)) {
        ESP_LOGW(TAG, "Invalid device ID format: %s", id ? id : "NULL");
        return false;
    }
    
    if (strlen(id) >= sizeof(device_id)) {
        ESP_LOGW(TAG, "Device ID too long (max %zu chars): %s", sizeof(device_id)-1, id);
        return false;
    }
    
    char old_id[sizeof(device_id)];
    strcpy(old_id, device_id);
    strcpy(device_id, id);
    
    ESP_LOGI(TAG, "Device ID updated: %s -> %s", old_id, device_id);
    notify_change("device_id");
    
    return true;
}

void WebInkConfig::set_api_key(const char* key) {
    if (!key) {
        api_key[0] = '\0';
        ESP_LOGI(TAG, "API key cleared");
        return;
    }
    
    if (strlen(key) >= sizeof(api_key)) {
        ESP_LOGW(TAG, "API key too long (max %zu chars), truncating", sizeof(api_key)-1);
        strncpy(api_key, key, sizeof(api_key) - 1);
        api_key[sizeof(api_key) - 1] = '\0';
    } else {
        strcpy(api_key, key);
    }
    
    ESP_LOGI(TAG, "API key updated (length: %zu characters)", strlen(api_key));
    notify_change("api_key");
}

bool WebInkConfig::set_display_mode(const char* mode) {
    if (!mode || !validate_display_mode(mode)) {
        ESP_LOGW(TAG, "Invalid display mode format: %s", mode ? mode : "NULL");
        return false;
    }
    
    if (strlen(mode) >= sizeof(display_mode)) {
        ESP_LOGW(TAG, "Display mode too long (max %zu chars): %s", sizeof(display_mode)-1, mode);
        return false;
    }
    
    char old_mode[sizeof(display_mode)];
    strcpy(old_mode, display_mode);
    strcpy(display_mode, mode);
    
    ESP_LOGI(TAG, "Display mode updated: %s -> %s", old_mode, display_mode);
    notify_change("display_mode");
    
    return true;
}

bool WebInkConfig::set_socket_port(int port) {
    if (port < 0 || port > 65535) {
        ESP_LOGW(TAG, "Invalid socket port: %d (must be 0-65535)", port);
        return false;
    }
    
    int old_port = socket_mode_port;
    socket_mode_port = port;
    
    if (port == 0) {
        ESP_LOGI(TAG, "Socket mode DISABLED - using HTTP mode");
    } else {
        ESP_LOGI(TAG, "Socket port updated: %d -> %d", old_port, port);
    }
    
    notify_change("socket_port");
    return true;
}

bool WebInkConfig::set_rows_per_slice(int rows) {
    if (rows < 1 || rows > 64) {
        ESP_LOGW(TAG, "Invalid rows per slice: %d (must be 1-64)", rows);
        return false;
    }
    
    int old_rows = rows_per_slice;
    rows_per_slice = rows;
    
    ESP_LOGI(TAG, "Rows per slice updated: %d -> %d", old_rows, rows);
    notify_change("rows_per_slice");
    
    return true;
}

//=============================================================================
// DISPLAY MODE PARSING AND VALIDATION
//=============================================================================

bool WebInkConfig::parse_display_mode(int& width, int& height, int& bits, ColorMode& mode) const {
    // Parse format: "800x480x1xB" using manual parsing (no regex - avoids stack overflow)
    const char* str = display_mode;
    char* endptr;
    
    // Parse width
    width = strtol(str, &endptr, 10);
    if (str == endptr || *endptr != 'x') {
        ESP_LOGW(TAG, "Display mode parse failed at width: %s", display_mode);
        return false;
    }
    str = endptr + 1;  // Skip 'x'
    
    // Parse height  
    height = strtol(str, &endptr, 10);
    if (str == endptr || *endptr != 'x') {
        ESP_LOGW(TAG, "Display mode parse failed at height: %s", display_mode);
        return false;
    }
    str = endptr + 1;  // Skip 'x'
    
    // Parse bits
    bits = strtol(str, &endptr, 10);
    if (str == endptr || *endptr != 'x') {
        ESP_LOGW(TAG, "Display mode parse failed at bits: %s", display_mode);
        return false;
    }
    str = endptr + 1;  // Skip 'x'
    
    // Parse color mode character
    char mode_char = *str;
    if (mode_char == '\0' || *(str + 1) != '\0') {
        ESP_LOGW(TAG, "Display mode parse failed at color mode: %s", display_mode);
        return false;
    }
    
    if (!parse_color_mode_char(mode_char, mode)) {
        return false;
    }
    
    // Validate parsed values
    if (width <= 0 || height <= 0 || 
        (bits != 1 && bits != 2 && bits != 8 && bits != 24)) {
        ESP_LOGW(TAG, "Invalid display mode values: %dx%dx%d", width, height, bits);
        return false;
    }
    
    ESP_LOGD(TAG, "Parsed display mode: %dx%d, %d bits, mode=%s",
             width, height, bits, color_mode_to_string(mode));
    
    return true;
}

bool WebInkConfig::validate_display_mode(const std::string& mode) const {
    int width, height, bits;
    ColorMode color_mode;
    return parse_display_mode(width, height, bits, color_mode);
}

NetworkMode WebInkConfig::get_network_mode() const {
    return socket_mode_port > 0 ? NetworkMode::TCP_SOCKET : NetworkMode::HTTP_SLICED;
}

//=============================================================================
// URL BUILDING UTILITIES
//=============================================================================

std::string WebInkConfig::build_hash_url() const {
    // Use static buffer to avoid stack allocation (thread-safe in single-threaded ESP32 context)
    static char buffer[256];  // Much smaller, still sufficient for URLs
    snprintf(buffer, sizeof(buffer), 
             "%s/get_hash?api_key=%s&device=%s&mode=%s",
             base_url,     // Now char arrays, no .c_str() needed
             api_key, 
             device_id, 
             display_mode);
    
    return std::string(buffer);
}

std::string WebInkConfig::build_image_url(const ImageRequest& request) const {
    // Use static buffer to avoid stack allocation - reduced from 1024 to 384 bytes!
    static char buffer[384];  // Still enough for longest URLs but much more memory-efficient
    
    // Use slice-based parameters if available, otherwise use rect parameters
    int x = request.rect.x;
    int y = request.num_rows > 0 ? request.start_row : request.rect.y;  // Use start_row if num_rows is specified
    int w = request.rect.width;
    int h = request.num_rows > 0 ? request.num_rows : request.rect.height;  // Use num_rows if specified
    
    snprintf(buffer, sizeof(buffer),
             "%s/get_image?api_key=%s&device=%s&mode=%s&x=%d&y=%d&w=%d&h=%d&format=%s",
             base_url,
             api_key,
             device_id,
             display_mode,
             x, y, w, h,
             request.format.c_str());
    
    return std::string(buffer);
}

std::string WebInkConfig::build_log_url() const {
    // Use static buffer to avoid stack allocation
    static char buffer[192];  // Reduced from 512 to 192 bytes
    snprintf(buffer, sizeof(buffer),
             "%s/post_log?api_key=%s&device=%s",
             base_url,
             api_key,
             device_id);
    
    return std::string(buffer);
}

std::string WebInkConfig::build_sleep_url() const {
    // Use static buffer to avoid stack allocation
    static char buffer[192];  // Reduced from 512 to 192 bytes
    snprintf(buffer, sizeof(buffer),
             "%s/get_sleep?api_key=%s&device=%s",
             base_url,
             api_key,
             device_id);
    
    return std::string(buffer);
}

std::string WebInkConfig::build_socket_request(const ImageRequest& request) const {
    // Use static buffer to avoid stack allocation
    static char buffer[128];  // Reduced from 256 to 128 bytes
    snprintf(buffer, sizeof(buffer),
             "webInkV1 %s %s %s %d %d %d %d %s\n",
             api_key,
             device_id,
             display_mode,
             request.rect.x,
             request.rect.y,
             request.rect.width,
             request.rect.height,
             request.format.c_str());
    
    return std::string(buffer);
}

//=============================================================================
// NETWORK PARSING UTILITIES
//=============================================================================

bool WebInkConfig::parse_server_host(std::string& host, int& port) const {
    std::string url = base_url;
    
    // Remove protocol if present
    size_t proto_pos = url.find("://");
    if (proto_pos != std::string::npos) {
        url = url.substr(proto_pos + 3);
    }
    
    // Find port delimiter
    size_t port_pos = url.find(":");
    if (port_pos != std::string::npos) {
        host = url.substr(0, port_pos);
        
        // Extract port number
        size_t path_pos = url.find("/", port_pos);
        std::string port_str;
        if (path_pos != std::string::npos) {
            port_str = url.substr(port_pos + 1, path_pos - port_pos - 1);
        } else {
            port_str = url.substr(port_pos + 1);
        }
        
        try {
            port = std::stoi(port_str);
        } catch (const std::exception& e) {
            ESP_LOGW(TAG, "Invalid port in URL: %s", port_str.c_str());
            return false;
        }
    } else {
        // No port specified, find end of hostname
        size_t path_pos = url.find("/");
        if (path_pos != std::string::npos) {
            host = url.substr(0, path_pos);
        } else {
            host = url;
        }
        port = 80; // Default HTTP port
    }
    
    return !host.empty() && port > 0 && port <= 65535;
}

std::string WebInkConfig::get_server_hostname() const {
    std::string host;
    int port;
    
    if (parse_server_host(host, port)) {
        return host;
    }
    
    // Fallback: return base_url as-is
    return base_url;
}

//=============================================================================
// MEMORY CALCULATION UTILITIES
//=============================================================================

int WebInkConfig::calculate_bytes_per_row() const {
    int width, height, bits;
    ColorMode mode;
    
    if (!parse_display_mode(width, height, bits, mode)) {
        ESP_LOGW(TAG, "Cannot calculate bytes per row - invalid display mode");
        return 0;
    }
    
    switch (mode) {
        case ColorMode::MONO_BLACK_WHITE:
            return (width + 7) / 8;  // 1-bit packed
        case ColorMode::GRAYSCALE_8BIT:
            return width;            // 1 byte per pixel
        case ColorMode::RGBB_4COLOR:
            return (width + 3) / 4;  // 2-bits per pixel
        case ColorMode::RGB_FULL_COLOR:
            return width * 3;        // 3 bytes per pixel
        default:
            return width;            // Conservative fallback
    }
}

int WebInkConfig::calculate_optimal_rows_per_slice(int available_bytes) const {
    int bytes_per_row = calculate_bytes_per_row();
    if (bytes_per_row == 0) {
        return rows_per_slice; // Fallback to configured value
    }
    
    int max_rows = available_bytes / bytes_per_row;
    max_rows = std::max(1, max_rows); // Always allow at least 1 row
    max_rows = std::min(max_rows, 64); // Cap at reasonable maximum
    
    ESP_LOGD(TAG, "Optimal rows for %d bytes: %d (bytes_per_row=%d)",
             available_bytes, max_rows, bytes_per_row);
    
    return max_rows;
}

int WebInkConfig::calculate_total_image_bytes() const {
    int width, height, bits;
    ColorMode mode;
    
    if (!parse_display_mode(width, height, bits, mode)) {
        return 0;
    }
    
    int bytes_per_row = calculate_bytes_per_row();
    return bytes_per_row * height;
}

//=============================================================================
// CHANGE NOTIFICATION
//=============================================================================

void WebInkConfig::set_change_callback(std::function<void(const std::string&)> callback) {
    on_config_changed = callback;
    ESP_LOGD(TAG, "Change callback registered");
}

//=============================================================================
// VALIDATION UTILITIES
//=============================================================================

bool WebInkConfig::validate_configuration(char* error_buffer, size_t buffer_size) const {
    // Validate server URL
    if (!validate_url(base_url)) {
        if (error_buffer && buffer_size > 0) {
            snprintf(error_buffer, buffer_size, "Invalid server URL format: %.32s", base_url);
        }
        return false;
    }
    
    // Validate device ID
    if (!validate_device_id(device_id)) {
        if (error_buffer && buffer_size > 0) {
            snprintf(error_buffer, buffer_size, "Invalid device ID format: %.16s", device_id);
        }
        return false;
    }
    
    // Validate API key (just check not empty)
    if (api_key[0] == '\0') {
        if (error_buffer && buffer_size > 0) {
            snprintf(error_buffer, buffer_size, "API key cannot be empty");
        }
        return false;
    }
    
    // Validate display mode
    if (!validate_display_mode(display_mode)) {
        if (error_buffer && buffer_size > 0) {
            snprintf(error_buffer, buffer_size, "Invalid display mode format: %.16s", display_mode);
        }
        return false;
    }
    
    // Validate socket port
    if (socket_mode_port < 0 || socket_mode_port > 65535) {
        if (error_buffer && buffer_size > 0) {
            snprintf(error_buffer, buffer_size, "Socket port out of range: %d", socket_mode_port);
        }
        return false;
    }
    
    // Validate rows per slice
    if (rows_per_slice < 1 || rows_per_slice > 64) {
        if (error_buffer && buffer_size > 0) {
            snprintf(error_buffer, buffer_size, "Rows per slice out of range: %d", rows_per_slice);
        }
        return false;
    }
    
    return true;  // All validation passed
}

std::string WebInkConfig::get_config_summary() const {
    static char buffer[192];  // Much smaller static buffer instead of 1024 byte stack allocation!
    snprintf(buffer, sizeof(buffer),
             "[CONFIG] URL: %s, Device: %s, Mode: %s, Socket: %d, Rows: %d",
             base_url,          // Now char arrays, no .c_str() needed
             device_id,
             display_mode,
             socket_mode_port,
             rows_per_slice);
    
    return std::string(buffer);
}

void WebInkConfig::reset_to_defaults() {
    strcpy(base_url, "http://192.168.68.69:8090");
    strcpy(device_id, "default");
    strcpy(api_key, "myapikey");
    strcpy(display_mode, "800x480x1xB");
    socket_mode_port = 8091;
    rows_per_slice = 8;
    
    ESP_LOGI(TAG, "Configuration reset to defaults");
    notify_change("reset_to_defaults");
}

//=============================================================================
// PRIVATE HELPER METHODS
//=============================================================================

void WebInkConfig::notify_change(const std::string& parameter) {
    if (on_config_changed) {
        on_config_changed(parameter);
    }
}

bool WebInkConfig::validate_url(const std::string& url) const {
    if (url.empty()) return false;
    
    // Basic URL validation - should start with http:// or https://
    if (url.length() < 10) {
        return false;
    }
    
    if (url.substr(0, 7) != "http://" && url.substr(0, 8) != "https://") {
        return false;
    }
    
    return true;
}

bool WebInkConfig::validate_device_id(const std::string& id) const {
    if (id.empty()) return false;
    
    if (id.length() == 0 || id.length() > 64) {
        return false;
    }
    
    // Allow alphanumeric characters, hyphens, and underscores
    for (size_t i = 0; i < id.length(); i++) {
        char c = id[i];
        if (!std::isalnum(c) && c != '-' && c != '_') {
            return false;
        }
    }
    
    return true;
}

bool WebInkConfig::parse_color_mode_char(char mode_char, ColorMode& mode) const {
    switch (mode_char) {
        case 'B':
            mode = ColorMode::MONO_BLACK_WHITE;
            return true;
        case 'G':
            mode = ColorMode::GRAYSCALE_8BIT;
            return true;
        case 'R':
            mode = ColorMode::RGBB_4COLOR;
            return true;
        case 'C':
            mode = ColorMode::RGB_FULL_COLOR;
            return true;
        default:
            ESP_LOGW(TAG, "Unknown color mode character: %c", mode_char);
            return false;
    }
}

} // namespace webink
} // namespace esphome
