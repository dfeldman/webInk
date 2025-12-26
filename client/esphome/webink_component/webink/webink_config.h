/**
 * @file webink_config.h
 * @brief Configuration management for WebInk component
 * 
 * The WebInkConfig class manages runtime configuration with validation,
 * ESPHome integration, and change notification. It replaces the scattered
 * global configuration variables from the original YAML with a centralized,
 * type-safe configuration system.
 * 
 * Key features:
 * - Runtime configuration changes through ESPHome web UI
 * - Automatic validation of configuration values
 * - URL building and parsing utilities
 * - Change notification callbacks
 * - Default value management
 * 
 * @author WebInk Component Authors
 * @version 1.0.0
 */

#pragma once

#include <string>
#include <cstdint>
#include <functional>

#ifdef WEBINK_MAC_INTEGRATION_TEST
// Mac integration test mode - use local declarations
void ESP_LOGI(const char* tag, const char* format, ...);
void ESP_LOGW(const char* tag, const char* format, ...);
void ESP_LOGE(const char* tag, const char* format, ...);
void ESP_LOGD(const char* tag, const char* format, ...);
#else
// Normal ESPHome mode
#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"
#endif

#include "webink_types.h"

namespace esphome {
namespace webink {

/**
 * @class WebInkConfig
 * @brief Centralized configuration management with validation and ESPHome integration
 * 
 * This class encapsulates all configuration settings for the WebInk component,
 * providing a clean interface for runtime configuration changes and validation.
 * It integrates with ESPHome's text/number input components to allow real-time
 * configuration changes through the web UI.
 * 
 * Features:
 * - Type-safe configuration setters with validation
 * - URL building utilities for server communication
 * - Display mode parsing and validation
 * - Network mode detection (HTTP vs TCP socket)
 * - Change notification callbacks for reactive updates
 * - Default value management
 * 
 * @example Basic Usage
 * @code
 * WebInkConfig config;
 * config.set_server_url("http://my-server:8090");
 * config.set_device_id("living-room-display");
 * config.set_display_mode("800x480x1xB");
 * 
 * // Build URLs for server communication
 * std::string hash_url = config.build_hash_url();
 * ImageRequest request;
 * std::string image_url = config.build_image_url(request);
 * @endcode
 */
class WebInkConfig {
public:
    /**
     * @brief Default constructor with sensible defaults
     */
    WebInkConfig();

    /**
     * @brief Default destructor
     */
    ~WebInkConfig() = default;

    //=========================================================================
    // CONFIGURATION PROPERTIES (Fixed arrays to avoid dynamic allocation)
    //=========================================================================

    /// Server base URL (e.g., "http://192.168.68.69:8090") - fixed size array
    char base_url[64];
    
    /// Device identifier for API requests - fixed size array
    char device_id[32];
    
    /// API key for server authentication - fixed size array
    char api_key[64];
    
    /// Display mode specification (format: WIDTHxHEIGHTxBITSxCOLOR) - fixed size array
    char display_mode[16];
    
    /// Socket mode port (0 = HTTP mode, >0 = TCP socket mode)
    int socket_mode_port{8091};
    
    /// Maximum rows to fetch per request (memory optimization)
    int rows_per_slice{8};

    //=========================================================================
    // CONFIGURATION SETTERS WITH VALIDATION
    //=========================================================================

    /**
     * @brief Set server base URL with validation
     * @param url Server URL (e.g., "http://server:port")
     * @return True if URL is valid and was set
     * 
     * Validates URL format and updates configuration. Triggers change
     * notification if a callback is registered.
     */
    bool set_server_url(const char* url);

    /**
     * @brief Set device identifier with validation
     * @param id Device ID string (alphanumeric, hyphens, underscores allowed)
     * @return True if device ID is valid and was set
     */
    bool set_device_id(const char* id);

    /**
     * @brief Set API key (no validation, any string accepted)
     * @param key API key string
     */
    void set_api_key(const char* key);

    /**
     * @brief Set display mode with format validation
     * @param mode Display mode string (e.g., "800x480x1xB")
     * @return True if mode format is valid and was set
     * 
     * Validates the display mode format and parses it to ensure
     * all components are valid before setting.
     */
    bool set_display_mode(const char* mode);

    /**
     * @brief Set socket mode port with range validation
     * @param port Port number (0 = HTTP mode, 1-65535 = socket mode)
     * @return True if port is valid and was set
     */
    bool set_socket_port(int port);

    /**
     * @brief Set rows per slice with range validation
     * @param rows Number of rows (1-64, should respect memory constraints)
     * @return True if value is valid and was set
     */
    bool set_rows_per_slice(int rows);

    //=========================================================================
    // DISPLAY MODE PARSING AND VALIDATION
    //=========================================================================

    /**
     * @brief Parse display mode string into components
     * @param[out] width Display width in pixels
     * @param[out] height Display height in pixels
     * @param[out] bits Bits per pixel
     * @param[out] mode Color mode enumeration
     * @return True if parsing was successful
     * 
     * Parses display mode format "WIDTHxHEIGHTxBITSxCOLOR" where:
     * - WIDTH/HEIGHT are pixel dimensions
     * - BITS is color depth (1, 8, 2, 24)
     * - COLOR is mode indicator (B=mono, G=gray, R=RGBB, C=RGB)
     */
    bool parse_display_mode(int& width, int& height, int& bits, ColorMode& mode) const;

    /**
     * @brief Validate display mode string without parsing
     * @param mode Display mode string to validate
     * @return True if format is valid
     */
    bool validate_display_mode(const std::string& mode) const;

    /**
     * @brief Get current network mode based on socket port setting
     * @return HTTP_SLICED if socket_mode_port is 0, TCP_SOCKET otherwise
     */
    NetworkMode get_network_mode() const;

    //=========================================================================
    // URL BUILDING UTILITIES
    //=========================================================================

    /**
     * @brief Build URL for hash request
     * @return Complete URL for hash endpoint
     * 
     * Builds URL: {base_url}/get_hash?api_key={key}&device={id}&mode={mode}
     */
    std::string build_hash_url() const;

    /**
     * @brief Build URL for image request
     * @param request Image request parameters
     * @return Complete URL for image endpoint
     * 
     * Builds URL: {base_url}/get_image?api_key={key}&device={id}&mode={mode}&
     *             x={x}&y={y}&w={w}&h={h}&format={format}
     */
    std::string build_image_url(const ImageRequest& request) const;

    /**
     * @brief Build URL for log posting
     * @return Complete URL for log endpoint
     * 
     * Builds URL: {base_url}/post_log?api_key={key}&device={id}
     */
    std::string build_log_url() const;

    /**
     * @brief Build URL for sleep interval request
     * @return Complete URL for sleep endpoint
     * 
     * Builds URL: {base_url}/get_sleep?api_key={key}&device={id}
     */
    std::string build_sleep_url() const;

    /**
     * @brief Build socket request string for TCP mode
     * @param request Image request parameters
     * @return Socket protocol request string
     * 
     * Builds request: "webInkV1 {api_key} {device} {mode} {x} {y} {w} {h} {format}\n"
     */
    std::string build_socket_request(const ImageRequest& request) const;

    //=========================================================================
    // NETWORK PARSING UTILITIES
    //=========================================================================

    /**
     * @brief Parse server host and port from base URL
     * @param[out] host Hostname or IP address
     * @param[out] port Port number (HTTP port from URL or 80 default)
     * @return True if parsing was successful
     * 
     * Extracts host and port from URLs like:
     * - "http://server:8090" -> host="server", port=8090
     * - "http://192.168.1.100" -> host="192.168.1.100", port=80
     */
    bool parse_server_host(std::string& host, int& port) const;

    /**
     * @brief Extract hostname for socket connections
     * @return Hostname or IP address from base_url
     */
    std::string get_server_hostname() const;

    //=========================================================================
    // MEMORY CALCULATION UTILITIES
    //=========================================================================

    /**
     * @brief Calculate memory needed for one row of pixels
     * @return Bytes required for one row in current display mode
     */
    int calculate_bytes_per_row() const;

    /**
     * @brief Calculate optimal rows per slice for available memory
     * @param available_bytes Available memory in bytes
     * @return Maximum number of rows that fit in available memory
     */
    int calculate_optimal_rows_per_slice(int available_bytes) const;

    /**
     * @brief Calculate total image size in bytes
     * @return Total bytes required for complete image
     */
    int calculate_total_image_bytes() const;

    //=========================================================================
    // CHANGE NOTIFICATION
    //=========================================================================

    /// Callback function called when configuration changes
    std::function<void(const std::string& parameter)> on_config_changed;

    /**
     * @brief Register callback for configuration changes
     * @param callback Function to call when any configuration value changes
     */
    void set_change_callback(std::function<void(const std::string&)> callback);

    //=========================================================================
    // VALIDATION UTILITIES
    //=========================================================================

    /**
     * @brief Validate complete configuration
     * @param[out] error_buffer Buffer to store first error message (if any)
     * @param buffer_size Size of error buffer
     * @return True if all configuration is valid, false with error message in buffer
     */
    bool validate_configuration(char* error_buffer = nullptr, size_t buffer_size = 0) const;

    /**
     * @brief Get configuration summary for logging
     * @return Human-readable configuration summary
     */
    std::string get_config_summary() const;

    /**
     * @brief Reset all configuration to defaults
     */
    void reset_to_defaults();

private:
    static const char* TAG;  ///< Logging tag for this class

    /**
     * @brief Notify change callback if registered
     * @param parameter Name of parameter that changed
     */
    void notify_change(const std::string& parameter);

    /**
     * @brief Validate URL format
     * @param url URL string to validate
     * @return True if URL format is valid
     */
    bool validate_url(const std::string& url) const;

    /**
     * @brief Validate device ID format
     * @param id Device ID to validate
     * @return True if device ID format is valid
     */
    bool validate_device_id(const std::string& id) const;

    /**
     * @brief Parse color mode character to enum
     * @param mode_char Color mode character (B, G, R, C)
     * @param[out] mode Parsed color mode enum
     * @return True if character is valid
     */
    bool parse_color_mode_char(char mode_char, ColorMode& mode) const;
};

} // namespace webink
} // namespace esphome
