/**
 * @file webink_network.h
 * @brief Defensive network client for WebInk component
 * 
 * The WebInkNetworkClient class provides robust network communication
 * with comprehensive timeout handling, error recovery, and support for
 * both HTTP and raw TCP socket protocols. It implements defensive
 * programming practices to handle unreliable network conditions.
 * 
 * Key features:
 * - Async HTTP requests with timeout handling
 * - Raw TCP socket support for efficient image downloads
 * - Comprehensive error reporting and recovery
 * - Non-blocking operations with callback-based design
 * - Automatic retry logic with exponential backoff
 * - Memory-efficient streaming for large transfers
 * 
 * @author WebInk Component Authors
 * @version 1.0.0
 */

#pragma once

#include <string>
#include <cstdint>
#include <functional>
#include <chrono>

#ifdef WEBINK_MAC_INTEGRATION_TEST
// Mac integration test mode - use local declarations
void ESP_LOGI(const char* tag, const char* format, ...);
void ESP_LOGW(const char* tag, const char* format, ...);
void ESP_LOGE(const char* tag, const char* format, ...);
void ESP_LOGD(const char* tag, const char* format, ...);

// Mock ESPHome time function
unsigned long millis();

// Mock ESPHome component types (empty for Mac)
namespace http_request {
    class HttpRequestComponent {};
}
namespace esphome_socket {
    class Socket {};
}

// Forward declare MacSocket for Mac builds
class MacSocket;
#else
// Normal ESPHome mode
#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/http_request/http_request.h"
// ESP32 HTTP client for real implementation
#include "esp_http_client.h"
#include "esp_tls.h"
#endif

#include "webink_config.h"
#include "webink_types.h"

namespace esphome {
namespace webink {

/**
 * @class WebInkNetworkClient
 * @brief Defensive network client with timeout handling and error recovery
 * 
 * This class provides robust network communication for the WebInk component,
 * supporting both HTTP and raw TCP socket protocols. It implements comprehensive
 * error handling, timeout management, and defensive programming practices to
 * handle unreliable network conditions common in IoT environments.
 * 
 * Features:
 * - Non-blocking async operations with callbacks
 * - Configurable timeouts and retry logic
 * - Support for both HTTP REST API and raw socket protocols
 * - Memory-efficient streaming for large image downloads
 * - Comprehensive error reporting with categorized error types
 * - Automatic cleanup and resource management
 * 
 * @example HTTP Usage
 * @code
 * WebInkNetworkClient client(config);
 * client.http_get_async("http://server/api/hash", [](NetworkResult result) {
 *     if (result.success) {
 *         ESP_LOGI("app", "Received: %s", result.data.c_str());
 *     } else {
 *         ESP_LOGE("app", "Error: %s", result.error_message.c_str());
 *     }
 * });
 * @endcode
 * 
 * @example Socket Usage
 * @code
 * WebInkNetworkClient client(config);
 * if (client.socket_connect_async("server", 8091)) {
 *     client.socket_send("webInkV1 key device mode 0 0 800 480 pbm\n");
 *     client.socket_receive_stream([](uint8_t* data, int length) {
 *         // Process received data
 *     });
 * }
 * @endcode
 */
class WebInkNetworkClient {
public:
    /**
     * @brief Constructor with configuration reference
     * @param config Configuration manager for URLs and settings
     * @param log_callback Optional callback for logging messages
     */
    WebInkNetworkClient(WebInkConfig* config, 
                       std::function<void(const std::string&)> log_callback = nullptr);

    /**
     * @brief Destructor ensures cleanup of resources
     */
    ~WebInkNetworkClient();

    //=========================================================================
    // HTTP INTERFACE
    //=========================================================================

    /**
     * @brief Perform async HTTP GET request
     * @param url Complete URL to request
     * @param callback Function called when request completes or times out
     * @param timeout_ms Timeout in milliseconds (0 = use default)
     * @return True if request was initiated successfully
     * 
     * Performs non-blocking HTTP GET request with timeout handling.
     * The callback is called exactly once with the result.
     */
    bool http_get_async(const std::string& url, 
                       std::function<void(NetworkResult)> callback,
                       unsigned long timeout_ms = 0);

    /**
     * @brief Perform async HTTP POST request
     * @param url Complete URL to post to
     * @param body Request body data
     * @param callback Function called when request completes or times out
     * @param content_type Content-Type header value
     * @param timeout_ms Timeout in milliseconds (0 = use default)
     * @return True if request was initiated successfully
     */
    bool http_post_async(const std::string& url,
                        const std::string& body,
                        std::function<void(NetworkResult)> callback,
                        const std::string& content_type = "text/plain",
                        unsigned long timeout_ms = 0);

    //=========================================================================
    // TCP SOCKET INTERFACE
    //=========================================================================

    /**
     * @brief Connect to TCP socket asynchronously
     * @param host Hostname or IP address
     * @param port Port number
     * @param timeout_ms Connection timeout in milliseconds (0 = use default)
     * @return True if connection attempt was initiated
     * 
     * Initiates non-blocking socket connection. Use socket_is_connected()
     * to check connection status.
     */
    bool socket_connect_async(const std::string& host, int port, 
                             unsigned long timeout_ms = 0);

    /**
     * @brief Send data over connected socket
     * @param data Data to send
     * @return True if data was sent successfully
     * 
     * Sends data over established socket connection. Returns false
     * if socket is not connected or send fails.
     */
    bool socket_send(const std::string& data);

    /**
     * @brief Receive data from socket with streaming callback
     * @param callback Function called for each chunk of received data
     * @param max_bytes Maximum bytes to receive (0 = unlimited)
     * @param timeout_ms Receive timeout in milliseconds (0 = use default)
     * @return True if receive operation was initiated
     * 
     * Receives data in chunks and calls callback for each chunk.
     * This allows for memory-efficient processing of large responses.
     */
    bool socket_receive_stream(std::function<void(const uint8_t*, int)> callback,
                              int max_bytes = 0,
                              unsigned long timeout_ms = 0);

    /**
     * @brief Close socket connection
     */
    void socket_close();

    /**
     * @brief Check if socket is connected
     * @return True if socket connection is active
     */
    bool socket_is_connected() const;

    //=========================================================================
    // OPERATION MANAGEMENT
    //=========================================================================

    /**
     * @brief Update network operations (call from main loop)
     * 
     * This method must be called regularly from the main loop to process
     * pending network operations, handle timeouts, and trigger callbacks.
     */
    void update();

    /**
     * @brief Check if any network operation is pending
     * @return True if HTTP or socket operation is in progress
     */
    bool is_operation_pending() const;

    /**
     * @brief Cancel all pending operations
     * 
     * Cancels any pending HTTP requests or socket operations and
     * calls their callbacks with timeout errors.
     */
    void cancel_all_operations();

    /**
     * @brief Set default timeout for HTTP operations
     * @param timeout_ms Timeout in milliseconds
     */
    void set_http_timeout(unsigned long timeout_ms);

    /**
     * @brief Set default timeout for socket operations
     * @param timeout_ms Timeout in milliseconds
     */
    void set_socket_timeout(unsigned long timeout_ms);

    //=========================================================================
    // STATISTICS AND MONITORING
    //=========================================================================

    /**
     * @brief Get network statistics
     * @return Human-readable statistics string
     */
    std::string get_statistics() const;

    /**
     * @brief Reset network statistics
     */
    void reset_statistics();

    /**
     * @brief Get last error message
     * @return Description of last network error
     */
    std::string get_last_error() const;

private:
    //=========================================================================
    // INTERNAL STATE
    //=========================================================================

    WebInkConfig* config_;                           ///< Configuration reference
    std::function<void(const std::string&)> log_callback_;  ///< Logging callback

    // Operation state
    bool pending_operation_;                         ///< Any operation pending
    unsigned long operation_start_time_;             ///< Start time of current operation
    
    // Timeouts
    unsigned long default_http_timeout_ms_;          ///< Default HTTP timeout
    unsigned long default_socket_timeout_ms_;        ///< Default socket timeout
    unsigned long current_timeout_ms_;               ///< Current operation timeout

    // HTTP state
    std::unique_ptr<http_request::HttpRequestComponent> http_client_;
    std::function<void(NetworkResult)> http_callback_;
    bool http_operation_pending_;

    // Socket state
#ifdef WEBINK_MAC_INTEGRATION_TEST
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <curl/curl.h>
#include <cstring>

class MacSocket {
public:
    MacSocket() : fd_(-1) {}
    ~MacSocket() { if (fd_ != -1) close(fd_); }
    
    bool connect(const std::string& host, int port);
    ssize_t write(const void* data, size_t len);
    bool ready();
    bool is_connected() const { return fd_ != -1; }
    void close_socket() { if (fd_ != -1) { close(fd_); fd_ = -1; } }
    int get_socket_fd() const { return fd_; }
    
private:
    int fd_;
};

struct HTTPResponse {
    int status_code;
    std::string data;
    std::string get_url_content(const std::string& url);
};
#else
// ESP32/ESPHome includes
#include "esphome.h"
#include "esphome/core/log.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/components/socket/socket.h"
#endif
    // ... rest of the code remains the same ...
    std::string http_response_buffer_;               ///< Response data accumulator
    bool http_request_in_progress_;                  ///< HTTP operation active
#endif

    static const char* TAG;                          ///< Logging tag

    //=========================================================================
    // INTERNAL HTTP METHODS
    //=========================================================================

    /**
     * @brief Initialize HTTP client component
     */
    void init_http_client();

    /**
     * @brief Process pending HTTP operations
     */
    void process_http_operations();

    /**
     * @brief Handle HTTP operation timeout
     */
    void handle_http_timeout();

    /**
     * @brief Complete HTTP operation with result
     * @param result Network result to pass to callback
     */
    void complete_http_operation(const NetworkResult& result);

    //=========================================================================
    // INTERNAL SOCKET METHODS
    //=========================================================================

    /**
     * @brief Process pending socket operations
     */
    void process_socket_operations();

    /**
     * @brief Handle socket operation timeout
     */
    void handle_socket_timeout();

    /**
     * @brief Complete socket operation
     */
    void complete_socket_operation();

    /**
     * @brief Check for socket errors
     * @return True if socket has errors
     */
    bool check_socket_errors();

    //=========================================================================
    // UTILITY METHODS
    //=========================================================================

    /**
     * @brief Check if operation has timed out
     * @return True if current operation has exceeded timeout
     */
    bool has_operation_timed_out() const;

    /**
     * @brief Log network operation
     * @param message Log message
     */
    void log_message(const std::string& message);

    /**
     * @brief Create error result
     * @param error_type Type of error
     * @param message Error description
     * @return NetworkResult with error information
     */
    NetworkResult create_error_result(ErrorType error_type, 
                                     const std::string& message);

    /**
     * @brief Reset operation state
     */
    void reset_operation_state();

    /**
     * @brief Validate URL format
     * @param url URL to validate
     * @return True if URL is valid
     */
    bool validate_url(const std::string& url) const;

    /**
     * @brief Validate hostname/IP address
     * @param host Hostname or IP to validate
     * @return True if host is valid
     */
    bool validate_host(const std::string& host) const;

#ifdef WEBINK_MAC_INTEGRATION_TEST
public:
    //=========================================================================
    // MAC INTEGRATION TEST METHODS
    //=========================================================================
    
    // HTTPResponse structure for integration test compatibility
    struct HTTPResponse {
        int status_code = 0;
        std::string content;
        bool success = false;
    };
    
    /**
     * @brief Get URL content using system curl (Mac integration test only)
     * @param url URL to request
     * @param response Response structure to fill
     * @return True if request succeeded
     */
    bool get_url_content(const std::string& url, HTTPResponse& response);

private:
    /**
     * @brief Perform HTTP GET using system curl
     * @param url URL to request
     * @return NetworkResult with response
     */
    NetworkResult perform_curl_request(const std::string& url);
    
    /**
     * @brief Perform HTTP POST using system curl  
     * @param url URL to post to
     * @param body Request body
     * @param content_type Content type header
     * @return NetworkResult with response
     */
    NetworkResult perform_curl_post_request(const std::string& url, const std::string& body, const std::string& content_type);
#endif // WEBINK_MAC_INTEGRATION_TEST
};

} // namespace webink
} // namespace esphome
