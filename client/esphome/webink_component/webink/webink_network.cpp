/**
 * @file webink_network.cpp
 * @brief Implementation of WebInkNetworkClient class for defensive network communication
 * 
 * @author WebInk Component Authors
 * @version 1.0.0
 */

#include "webink_network.h"

#ifdef WEBINK_MAC_INTEGRATION_TEST
// Mac integration test mode - use system curl and POSIX sockets
#include <cstdlib>
#include <unistd.h>
#include <sstream>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <cstdarg>
#include <cstdio>
#include <chrono>

// Mock ESPHome logging for Mac
namespace esphome {
#else
// Normal ESPHome mode  
#include "esphome.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/components/socket/socket.h"
#include "esp_http_client.h"
#include <sys/socket.h>
#include <netdb.h>

// ESPHome millis() is available via helpers
using namespace esphome;
#endif

#include <regex>

#ifndef WEBINK_MAC_INTEGRATION_TEST
// Static pointer for event handler to access response buffer (declared early for visibility)
static std::string* s_http_response_buffer = nullptr;

// Event handler for ESP-IDF HTTP client - captures response body
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // Append received data to response buffer
            if (s_http_response_buffer != nullptr && evt->data_len > 0) {
                s_http_response_buffer->append((char*)evt->data, evt->data_len);
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}
#endif

#ifdef WEBINK_MAC_INTEGRATION_TEST
//=============================================================================
// MAC SOCKET WRAPPER (POSIX implementation for testing)
//=============================================================================

class MacSocket {
private:
    int sockfd_;
    bool connected_;
    
public:
    MacSocket() : sockfd_(-1), connected_(false) {}
    
    ~MacSocket() {
        close();
    }
    
    bool create_tcp() {
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        return sockfd_ != -1;
    }
    
    bool setblocking(bool blocking) {
        int flags = fcntl(sockfd_, F_GETFL, 0);
        if (flags == -1) return false;
        
        if (blocking) {
            flags &= ~O_NONBLOCK;
        } else {
            flags |= O_NONBLOCK;
        }
        
        return fcntl(sockfd_, F_SETFL, flags) == 0;
    }
    
    int connect(const std::string& host, int port) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        // Try to convert IP address directly
        if (inet_aton(host.c_str(), &addr.sin_addr) == 0) {
            // Not an IP, resolve hostname
            struct hostent* he = gethostbyname(host.c_str());
            if (!he) return -1;
            memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
        }
        
        int result = ::connect(sockfd_, (struct sockaddr*)&addr, sizeof(addr));
        if (result == 0) {
            connected_ = true;
        } else if (errno == EINPROGRESS) {
            // Non-blocking connect in progress
            return -2; // Special return for in-progress
        }
        
        return result;
    }
    
    ssize_t write(const void* data, size_t len) {
        if (!connected_) return -1;
        return ::write(sockfd_, data, len);
    }
    
    ssize_t read(void* buffer, size_t len) {
        if (!connected_) return -1;
        return ::read(sockfd_, buffer, len);
    }
    
    bool ready() {
        if (!connected_) return false;
        
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd_, &readfds);
        
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        
        int result = select(sockfd_ + 1, &readfds, nullptr, nullptr, &timeout);
        return result > 0 && FD_ISSET(sockfd_, &readfds);
    }
    
    void close() {
        if (sockfd_ != -1) {
            ::close(sockfd_);
            sockfd_ = -1;
            connected_ = false;
        }
    }
    
    bool is_connected() const { return connected_; }
    int get_socket_fd() const { return sockfd_; }
};

#endif // WEBINK_MAC_INTEGRATION_TEST

namespace esphome {
namespace webink {

const char* WebInkNetworkClient::TAG = "webink.network";

//=============================================================================
// CONSTRUCTOR AND DESTRUCTOR
//=============================================================================

WebInkNetworkClient::WebInkNetworkClient(WebInkConfig* config, 
                                        std::function<void(const std::string&)> log_callback)
    : config_(config), 
      log_callback_(log_callback),
      pending_operation_(false),
      operation_start_time_(0),
      default_http_timeout_ms_(10000),      // 10 seconds
      default_socket_timeout_ms_(30000),    // 30 seconds  
      current_timeout_ms_(0),
      http_operation_pending_(false),
      socket_operation_pending_(false),
      socket_connected_(false),
      socket_bytes_remaining_(0),
      http_requests_sent_(0),
      http_requests_successful_(0),
      socket_connections_made_(0),
      socket_bytes_sent_(0),
      socket_bytes_received_(0)
#ifndef WEBINK_MAC_INTEGRATION_TEST
      , esp_http_client_(nullptr),
      http_request_in_progress_(false)
#endif
      {
    
    init_http_client();
    
    ESP_LOGD(TAG, "WebInkNetworkClient initialized");
    ESP_LOGD(TAG, "HTTP timeout: %lu ms, Socket timeout: %lu ms", 
             default_http_timeout_ms_, default_socket_timeout_ms_);
}

WebInkNetworkClient::~WebInkNetworkClient() {
    cancel_all_operations();
    socket_close();
    
#ifndef WEBINK_MAC_INTEGRATION_TEST
    // Clean up ESP32 HTTP client
    if (esp_http_client_ != nullptr) {
        esp_http_client_cleanup(esp_http_client_);
        esp_http_client_ = nullptr;
    }
#endif
    
    ESP_LOGD(TAG, "WebInkNetworkClient destroyed");
}

//=============================================================================
// HTTP INTERFACE
//=============================================================================

bool WebInkNetworkClient::http_get_async(const std::string& url,
                                         std::function<void(NetworkResult)> callback,
                                         unsigned long timeout_ms) {
    if (!validate_url(url)) {
        ESP_LOGW(TAG, "Invalid URL format");
        callback(create_error_result(ErrorType::INVALID_RESPONSE, "Invalid URL format"));
        return false;
    }
    
    if (pending_operation_) {
        ESP_LOGW(TAG, "HTTP operation already pending");
        callback(create_error_result(ErrorType::SERVER_UNREACHABLE, "Operation already pending"));
        return false;
    }
    
    // Set up operation state
    pending_operation_ = true;
    http_operation_pending_ = true;
    operation_start_time_ = millis();
    current_timeout_ms_ = (timeout_ms > 0) ? timeout_ms : default_http_timeout_ms_;
    http_callback_ = callback;
    
    ESP_LOGI(TAG, "[HTTP] GET %s (timeout: %lu ms)", url.c_str(), current_timeout_ms_);
    
#ifdef WEBINK_MAC_INTEGRATION_TEST
    // Mac integration test - use system curl immediately
    NetworkResult result = perform_curl_request(url);
    pending_operation_ = false;
    http_operation_pending_ = false;
    callback(result);
#else
    // ESP32 implementation using ESP-IDF HTTP client
    // Reinitialize client if needed (it may have been cleaned up after previous request)
    if (esp_http_client_ == nullptr) {
        init_http_client();
        if (esp_http_client_ == nullptr) {
            ESP_LOGE(TAG, "Failed to reinitialize HTTP client");
            pending_operation_ = false;
            http_operation_pending_ = false;
            callback(create_error_result(ErrorType::SERVER_UNREACHABLE, "HTTP client init failed"));
            return false;
        }
    }
    
    // Configure the HTTP client for this request
    esp_http_client_set_url(esp_http_client_, url.c_str());
    esp_http_client_set_method(esp_http_client_, HTTP_METHOD_GET);
    esp_http_client_set_timeout_ms(esp_http_client_, current_timeout_ms_);
    
    // Clear response buffer and set up static pointer for event handler
    http_response_buffer_.clear();
    s_http_response_buffer = &http_response_buffer_;  // Event handler will append data here
    
    // Perform the HTTP request (this is BLOCKING despite the "async" name)
    // Response body is captured by the event handler during perform()
    esp_err_t err = esp_http_client_perform(esp_http_client_);
    
    // Clear static pointer after perform completes
    s_http_response_buffer = nullptr;
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP client perform failed: %s", esp_err_to_name(err));
        pending_operation_ = false;
        http_operation_pending_ = false;
        callback(create_error_result(ErrorType::SERVER_UNREACHABLE, 
                                   "HTTP request failed"));
        return false;
    }
    
    // Get response metadata
    int content_length = esp_http_client_get_content_length(esp_http_client_);
    int status_code = esp_http_client_get_status_code(esp_http_client_);
    
    ESP_LOGD(TAG, "HTTP response: status=%d, content_length=%d, received=%zu bytes", 
             status_code, content_length, http_response_buffer_.length());
    
    // Build result and call callback immediately (since perform() is blocking)
    NetworkResult result;
    result.success = (status_code >= 200 && status_code < 300);
    result.status_code = status_code;
    result.data = http_response_buffer_;
    result.content = http_response_buffer_;
    result.bytes_received = http_response_buffer_.length();
    
    if (!result.success) {
        result.error_type = ErrorType::INVALID_RESPONSE;
        result.error_message = "HTTP error";
    }
    
    // Clean up HTTP client to avoid stale connection state on next request
    pending_operation_ = false;
    http_operation_pending_ = false;
    http_response_buffer_.clear();
    esp_http_client_cleanup(esp_http_client_);
    esp_http_client_ = nullptr;  // Will be reinitialized on next request
    
    // Call callback with result
    http_requests_sent_++;
    callback(result);
    
    return true;
#endif
}

bool WebInkNetworkClient::http_post_async(const std::string& url,
                                          const std::string& body,
                                          std::function<void(NetworkResult)> callback,
                                          const std::string& content_type,
                                          unsigned long timeout_ms) {
    if (!validate_url(url)) {
        log_message("Invalid URL format: " + url);
        callback(create_error_result(ErrorType::INVALID_RESPONSE, "Invalid URL format"));
        return false;
    }
    
    if (pending_operation_) {
        log_message("HTTP operation already pending");
        callback(create_error_result(ErrorType::SERVER_UNREACHABLE, "Operation already pending"));
        return false;
    }
    
    // Set up operation state
    pending_operation_ = true;
    http_operation_pending_ = true;
    operation_start_time_ = millis();
    current_timeout_ms_ = (timeout_ms > 0) ? timeout_ms : default_http_timeout_ms_;
    http_callback_ = callback;
    
    ESP_LOGI(TAG, "[HTTP] POST %s (%zu bytes, %s, timeout: %lu ms)", 
             url.c_str(), body.length(), content_type.c_str(), current_timeout_ms_);
    log_message("HTTP POST: " + url + " (" + std::to_string(body.length()) + " bytes)");
    
#ifdef WEBINK_MAC_INTEGRATION_TEST
    // Mac integration test - use system curl immediately  
    NetworkResult result = perform_curl_post_request(url, body, content_type);
    pending_operation_ = false;
    http_operation_pending_ = false;
    callback(result);
#else
    // ESP32 implementation using ESP-IDF HTTP client
    // Reinitialize client if needed (it may have been cleaned up after previous request)
    if (esp_http_client_ == nullptr) {
        init_http_client();
        if (esp_http_client_ == nullptr) {
            ESP_LOGE(TAG, "Failed to reinitialize HTTP client for POST");
            pending_operation_ = false;
            http_operation_pending_ = false;
            callback(create_error_result(ErrorType::SERVER_UNREACHABLE, "HTTP client init failed"));
            return false;
        }
    }
    
    // Configure the HTTP client for POST request
    esp_http_client_set_url(esp_http_client_, url.c_str());
    esp_http_client_set_method(esp_http_client_, HTTP_METHOD_POST);
    esp_http_client_set_timeout_ms(esp_http_client_, current_timeout_ms_);
    esp_http_client_set_post_field(esp_http_client_, body.c_str(), body.length());
    esp_http_client_set_header(esp_http_client_, "Content-Type", content_type.c_str());
    
    // Clear response buffer and set up static pointer for event handler
    http_response_buffer_.clear();
    s_http_response_buffer = &http_response_buffer_;
    
    // Perform the HTTP POST request (blocking)
    esp_err_t err = esp_http_client_perform(esp_http_client_);
    
    // Clear static pointer after perform completes
    s_http_response_buffer = nullptr;
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST perform failed: %s", esp_err_to_name(err));
        pending_operation_ = false;
        http_operation_pending_ = false;
        esp_http_client_cleanup(esp_http_client_);
        esp_http_client_ = nullptr;
        callback(create_error_result(ErrorType::SERVER_UNREACHABLE, 
                                   "HTTP POST failed"));
        return false;
    }
    
    // Get response metadata
    int status_code = esp_http_client_get_status_code(esp_http_client_);
    
    ESP_LOGD(TAG, "HTTP POST response: status=%d, received=%zu bytes", 
             status_code, http_response_buffer_.length());
    
    // Build result
    NetworkResult result;
    result.success = (status_code >= 200 && status_code < 300);
    result.status_code = status_code;
    result.data = http_response_buffer_;
    result.content = http_response_buffer_;
    result.bytes_received = http_response_buffer_.length();
    
    if (!result.success) {
        result.error_type = ErrorType::INVALID_RESPONSE;
        result.error_message = "HTTP POST error";
    }
    
    // Clean up HTTP client
    pending_operation_ = false;
    http_operation_pending_ = false;
    http_response_buffer_.clear();
    esp_http_client_cleanup(esp_http_client_);
    esp_http_client_ = nullptr;
    
    // Call callback with result
    http_requests_sent_++;
    callback(result);
    
    return true;
#endif
}

//=============================================================================
// TCP SOCKET INTERFACE
//=============================================================================

bool WebInkNetworkClient::socket_connect_async(const std::string& host, int port,
                                               unsigned long timeout_ms) {
    if (!validate_host(host)) {
        log_message("Invalid hostname: " + host);
        return false;
    }
    
    if (port <= 0 || port > 65535) {
        log_message("Invalid port: " + std::to_string(port));
        return false;
    }
    
    if (pending_operation_) {
        log_message("Socket operation already pending");
        return false;
    }
    
    ESP_LOGI(TAG, "[SOCKET] Connecting to %s:%d", host.c_str(), port);
    
    // Note: Don't set pending_operation_ here - connect is optimistic
    // The pending operation will be set when we start socket_receive_stream
    operation_start_time_ = millis();
    current_timeout_ms_ = default_socket_timeout_ms_;
    
    try {
#ifdef WEBINK_MAC_INTEGRATION_TEST
        // Mac integration test - use POSIX socket
        socket_ = std::make_unique<MacSocket>();
        
        if (!socket_->create_tcp()) {
            log_message("Failed to create Mac socket");
            reset_operation_state();
            return false;
        }
        
        socket_->setblocking(false);
        
        // Attempt connection  
        int result = socket_->connect(host, port);
#else
        // ESPHome mode - use ESPHome socket
        socket_ = socket::socket_ip(SOCK_STREAM, 0);
        if (!socket_) {
            log_message("Failed to create ESPHome socket");
            reset_operation_state();
            return false;
        }
        
        // Set up address
        struct sockaddr_storage addr{};
        socklen_t addrlen = socket::set_sockaddr(
            reinterpret_cast<sockaddr *>(&addr),
            sizeof(addr), host.c_str(), port);
            
        if (addrlen == 0) {
            log_message("Failed to set socket address");
            reset_operation_state();
            return false;
        }
        
        socket_->setblocking(false);
        
        // Attempt connection
        int result = socket_->connect(reinterpret_cast<sockaddr *>(&addr), addrlen);
#endif
        if (result == 0) {
            // Connected immediately
            socket_connected_ = true;
            ESP_LOGI(TAG, "[SOCKET] Connected successfully");
            return true;
#ifdef WEBINK_MAC_INTEGRATION_TEST
        } else if (result == -2) {
            // Mac socket: connection in progress (EINPROGRESS)
            ESP_LOGI(TAG, "[SOCKET] Mac connection in progress");
            socket_connections_made_++;
            return true;
        } else {
            // Mac socket: connection failed
            log_message("Mac socket connection failed");
            socket_close();
            reset_operation_state();
            return false;
#endif
        }
        
        // ESPHome: Connection in progress - mark as connected optimistically
        // The socket will fail on send/receive if not actually connected
        socket_connected_ = true;
        socket_connections_made_++;
        ESP_LOGI(TAG, "[SOCKET] Connection initiated (async)");
        return true;
        
    } catch (const std::exception& e) {
        log_message("Socket connection error: " + std::string(e.what()));
        log_message("Socket connection failed");
        socket_close();
        reset_operation_state();
        return false;
    }
}

bool WebInkNetworkClient::socket_send(const std::string& data) {
    if (!socket_connected_ || !socket_) {
        log_message("Socket not connected for send");
        return false;
    }
    
    try {
        ssize_t sent = socket_->write(data.c_str(), data.length());
        if (sent != static_cast<ssize_t>(data.length())) {
            log_message("Socket send incomplete: " + std::to_string(sent) + "/" + std::to_string(data.length()));
            return false;
        }
        
        socket_bytes_sent_ += sent;
        ESP_LOGD(TAG, "[SOCKET] Sent %zu bytes", data.length());
        
        return true;
        
    } catch (const std::exception& e) {
        log_message("Socket send exception: " + std::string(e.what()));
        return false;
    }
}

bool WebInkNetworkClient::socket_receive_stream(std::function<void(const uint8_t*, int)> callback,
                                                int max_bytes,
                                                unsigned long timeout_ms) {
    if (!socket_connected_ || !socket_) {
        log_message("Socket not connected for receive");
        return false;
    }
    
    if (pending_operation_) {
        log_message("Socket operation already pending");
        return false;
    }
    
    // Set up streaming operation
    pending_operation_ = true;
    socket_operation_pending_ = true;
    operation_start_time_ = millis();
    current_timeout_ms_ = (timeout_ms > 0) ? timeout_ms : default_socket_timeout_ms_;
    socket_stream_callback_ = callback;
    socket_bytes_remaining_ = max_bytes;
    
    ESP_LOGI(TAG, "[SOCKET] Starting stream receive (max: %d bytes, timeout: %lu ms)", 
             max_bytes, current_timeout_ms_);
    
    return true;
}

void WebInkNetworkClient::socket_close() {
    if (socket_) {
        try {
            socket_->close();
        } catch (const std::exception& e) {
            ESP_LOGW(TAG, "[SOCKET] Close exception: %s", e.what());
        }
        socket_.reset();
    }
    
    socket_connected_ = false;
    ESP_LOGD(TAG, "[SOCKET] Closed");
}

bool WebInkNetworkClient::socket_is_connected() const {
    return socket_connected_ && socket_ != nullptr;
}

//=============================================================================
// OPERATION MANAGEMENT
//=============================================================================

void WebInkNetworkClient::update() {
    if (!pending_operation_) {
        return;
    }
    
    // Check for timeout
    if (has_operation_timed_out()) {
        if (http_operation_pending_) {
            handle_http_timeout();
        } else if (socket_operation_pending_) {
            handle_socket_timeout();
        }
        return;
    }
    
    // Process operations
    if (http_operation_pending_) {
        process_http_operations();
    } else if (socket_operation_pending_) {
        process_socket_operations();
    }
}

bool WebInkNetworkClient::is_operation_pending() const {
    return pending_operation_;
}

void WebInkNetworkClient::cancel_all_operations() {
    if (http_operation_pending_) {
        complete_http_operation(create_error_result(ErrorType::SERVER_UNREACHABLE, "Operation cancelled"));
    }
    
    if (socket_operation_pending_) {
        complete_socket_operation();
    }
    
    reset_operation_state();
    ESP_LOGD(TAG, "All operations cancelled");
}

void WebInkNetworkClient::set_http_timeout(unsigned long timeout_ms) {
    default_http_timeout_ms_ = timeout_ms;
    ESP_LOGD(TAG, "HTTP timeout set to %lu ms", timeout_ms);
}

void WebInkNetworkClient::set_socket_timeout(unsigned long timeout_ms) {
    default_socket_timeout_ms_ = timeout_ms;
    ESP_LOGD(TAG, "Socket timeout set to %lu ms", timeout_ms);
}

//=============================================================================
// STATISTICS AND MONITORING
//=============================================================================

std::string WebInkNetworkClient::get_statistics() const {
    static char buffer[128];  // Much smaller static buffer instead of 512 byte stack allocation!
    snprintf(buffer, sizeof(buffer),
             "[STATS] HTTP: %d sent, %d successful; Socket: %d connections, %d sent, %d received bytes",
             http_requests_sent_,
             http_requests_successful_,
             socket_connections_made_,
             socket_bytes_sent_,
             socket_bytes_received_);
    
    return std::string(buffer);
}

void WebInkNetworkClient::reset_statistics() {
    http_requests_sent_ = 0;
    http_requests_successful_ = 0;
    socket_connections_made_ = 0;
    socket_bytes_sent_ = 0;
    socket_bytes_received_ = 0;
    
    ESP_LOGD(TAG, "Statistics reset");
}

std::string WebInkNetworkClient::get_last_error() const {
    return last_error_message_;
}

//=============================================================================
// INTERNAL HTTP METHODS
//=============================================================================

void WebInkNetworkClient::init_http_client() {
#ifndef WEBINK_MAC_INTEGRATION_TEST
    // Initialize ESP32 HTTP client
    // ESP-IDF requires a URL at init time - use a placeholder that will be overwritten
    esp_http_client_config_t config = {};
    config.url = "http://localhost/";  // Placeholder - will be set per-request
    config.timeout_ms = default_http_timeout_ms_;
    config.buffer_size = 4096;
    config.buffer_size_tx = 1024;
    config.event_handler = http_event_handler;  // Set event handler to capture response body
    
    esp_http_client_ = esp_http_client_init(&config);
    http_request_in_progress_ = false;
    http_response_buffer_.clear();
    
    if (esp_http_client_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize ESP32 HTTP client");
        return;
    }
    
    ESP_LOGD(TAG, "ESP32 HTTP client initialized successfully");
#else
    // Mac integration test mode - HTTP client uses curl directly
    ESP_LOGD(TAG, "HTTP client initialized (Mac integration test mode)");
#endif
}

void WebInkNetworkClient::process_http_operations() {
#ifndef WEBINK_MAC_INTEGRATION_TEST
    // Real ESP32 HTTP client implementation
    if (!http_request_in_progress_ || esp_http_client_ == nullptr) {
        return; // No operation in progress
    }
    
    // Check if HTTP client has finished
    if (esp_http_client_is_complete_data_received(esp_http_client_)) {
        // Operation completed - collect results
        int status_code = esp_http_client_get_status_code(esp_http_client_);
        int content_length = esp_http_client_get_content_length(esp_http_client_);
        
        NetworkResult result;
        result.success = (status_code >= 200 && status_code < 300);
        result.status_code = status_code;
        result.data = http_response_buffer_;
        result.content = http_response_buffer_;  // Alias for compatibility
        result.bytes_received = http_response_buffer_.length();
        
        if (!result.success) {
            result.error_type = ErrorType::INVALID_RESPONSE;
            result.error_message = "HTTP request failed with status " + std::to_string(status_code);
            ESP_LOGW(TAG, "HTTP request failed: %d", status_code);
        } else {
            ESP_LOGD(TAG, "HTTP request completed: %d bytes, status %d", 
                    result.bytes_received, status_code);
        }
        
        // Clean up and complete operation
        http_request_in_progress_ = false;
        http_response_buffer_.clear();
        esp_http_client_cleanup(esp_http_client_);
        esp_http_client_ = nullptr;
        
        complete_http_operation(result);
        return;
    }
    
    // Check for timeout
    if (millis() - operation_start_time_ > current_timeout_ms_) {
        handle_http_timeout();
        return;
    }
    
    // Continue processing - let the HTTP client do its work
    esp_http_client_perform(esp_http_client_);
    
#else
    // Mac integration test mode - this function is not used
    // (Mac mode uses direct curl in get_url_content)
#endif
}

void WebInkNetworkClient::handle_http_timeout() {
    ESP_LOGW(TAG, "[HTTP] Operation timeout after %lu ms", current_timeout_ms_);
    
#ifndef WEBINK_MAC_INTEGRATION_TEST
    // Clean up ESP32 HTTP client on timeout
    if (esp_http_client_ != nullptr) {
        esp_http_client_cleanup(esp_http_client_);
        esp_http_client_ = nullptr;
    }
    http_request_in_progress_ = false;
    http_response_buffer_.clear();
#endif
    
    NetworkResult result = create_error_result(ErrorType::SERVER_UNREACHABLE, 
                                              "HTTP request timeout");
    complete_http_operation(result);
}

void WebInkNetworkClient::complete_http_operation(const NetworkResult& result) {
    if (http_callback_) {
        if (result.success) {
            http_requests_successful_++;
            ESP_LOGD(TAG, "[HTTP] Operation completed successfully (%d bytes)", 
                     result.bytes_received);
        } else {
            last_error_message_ = result.error_message;
            ESP_LOGW(TAG, "[HTTP] Operation failed: %s", result.error_message.c_str());
        }
        
        http_callback_(result);
        http_callback_ = nullptr;
    }
    
    http_operation_pending_ = false;
    pending_operation_ = false;
}

//=============================================================================
// INTERNAL SOCKET METHODS
//=============================================================================

void WebInkNetworkClient::process_socket_operations() {
    if (!socket_stream_callback_ || !socket_) {
        return;
    }
    
    // Check for socket errors
    if (check_socket_errors()) {
        handle_socket_timeout();
        return;
    }
    
    // Try to read data - use smaller static buffer to avoid stack allocation
    static const int BUFFER_SIZE = 512;  // Reduced from 1024 to 512 bytes
    static uint8_t buffer[BUFFER_SIZE];   // Static to avoid stack allocation each call
    
    try {
        if (socket_->ready()) {
            ssize_t bytes_read = socket_->read(buffer, BUFFER_SIZE);
            if (bytes_read > 0) {
                socket_bytes_received_ += bytes_read;
                
                // Call stream callback with received data
                socket_stream_callback_(buffer, static_cast<int>(bytes_read));
                
                // Update remaining bytes if limit is set
                if (socket_bytes_remaining_ > 0) {
                    socket_bytes_remaining_ -= bytes_read;
                    if (socket_bytes_remaining_ <= 0) {
                        ESP_LOGI(TAG, "[SOCKET] Receive limit reached");
                        complete_socket_operation();
                    }
                }
                
                ESP_LOGD(TAG, "[SOCKET] Received %zd bytes", bytes_read);
            } else if (bytes_read == 0) {
                // Connection closed by peer
                ESP_LOGI(TAG, "[SOCKET] Connection closed by peer");
                complete_socket_operation();
            }
        }
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "[SOCKET] Receive exception: %s", e.what());
        handle_socket_timeout();
    }
}

void WebInkNetworkClient::handle_socket_timeout() {
    ESP_LOGW(TAG, "[SOCKET] Operation timeout after %lu ms", current_timeout_ms_);
    last_error_message_ = "Socket operation timeout";
    complete_socket_operation();
}

void WebInkNetworkClient::complete_socket_operation() {
    ESP_LOGD(TAG, "[SOCKET] Operation completed (%d bytes received)", socket_bytes_received_);
    
    socket_stream_callback_ = nullptr;
    socket_operation_pending_ = false;
    pending_operation_ = false;
}

bool WebInkNetworkClient::check_socket_errors() {
    if (!socket_) {
        return true;
    }
    
    // Check socket for errors using appropriate system calls
#ifdef WEBINK_MAC_INTEGRATION_TEST
    // Mac integration test - check POSIX socket status
    MacSocket* mac_socket = static_cast<MacSocket*>(socket_.get());
    if (!mac_socket || !mac_socket->is_connected()) {
        return true; // Error detected
    }
    
    // Use SO_ERROR to check for socket errors
    int sock_fd = mac_socket->get_socket_fd();
    if (sock_fd < 0) {
        return true; // Invalid socket
    }
    
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        ESP_LOGW(TAG, "[SOCKET] Failed to check socket status");
        return true; // Error checking failed
    }
    
    if (error != 0) {
        ESP_LOGW(TAG, "[SOCKET] Socket error detected: %d (%s)", error, strerror(error));
        return true; // Socket has error
    }
    
#else
    // ESP32 - check ESPHome socket status
    try {
        // ESPHome sockets have error checking methods
        if (!socket_connected_) {
            return true; // Disconnected
        }
        
        // Additional ESP32-specific socket error checks could be added here
        
    } catch (const std::exception& e) {
        ESP_LOGW(TAG, "[SOCKET] Exception checking socket: %s", e.what());
        return true; // Exception indicates error
    }
#endif
    
    return false; // No errors detected
}

//=============================================================================
// UTILITY METHODS
//=============================================================================

bool WebInkNetworkClient::has_operation_timed_out() const {
    return (millis() - operation_start_time_) > current_timeout_ms_;
}

void WebInkNetworkClient::log_message(const std::string& message) {
    if (log_callback_) {
        log_callback_(message);
    }
    ESP_LOGD(TAG, "%s", message.c_str());
}

NetworkResult WebInkNetworkClient::create_error_result(ErrorType error_type, 
                                                       const std::string& message) {
    NetworkResult result;
    result.success = false;
    result.error_type = error_type;
    result.error_message = message;
    
    return result;
}

void WebInkNetworkClient::reset_operation_state() {
    pending_operation_ = false;
    http_operation_pending_ = false;
    socket_operation_pending_ = false;
    operation_start_time_ = 0;
    current_timeout_ms_ = 0;
    socket_bytes_remaining_ = 0;
    
    http_callback_ = nullptr;
    socket_stream_callback_ = nullptr;
}

bool WebInkNetworkClient::validate_url(const std::string& url) const {
    // Basic URL validation
    std::regex url_pattern(R"(^https?://[a-zA-Z0-9.-]+(?::\d+)?(?:/.*)?$)");
    return std::regex_match(url, url_pattern);
}

bool WebInkNetworkClient::validate_host(const std::string& host) const {
    if (host.empty() || host.length() > 253) {
        return false;
    }
    
    // Allow IP addresses and hostnames
    std::regex ip_pattern(R"(^(\d{1,3}\.){3}\d{1,3}$)");
    std::regex hostname_pattern(R"(^[a-zA-Z0-9.-]+$)");
    
    return std::regex_match(host, ip_pattern) || std::regex_match(host, hostname_pattern);
}

#ifdef WEBINK_MAC_INTEGRATION_TEST
//=============================================================================
// MAC INTEGRATION TEST METHODS (using system curl)
//=============================================================================

// HTTPResponse is now declared in header for Mac integration tests

NetworkResult WebInkNetworkClient::perform_curl_request(const std::string& url) {
    NetworkResult result;
    
    // Create temporary file for curl output
    std::string temp_file = "/tmp/webink_net_" + std::to_string(rand());
    std::string cmd = "curl -s -w '%{http_code}' -o '" + temp_file + "' '" + url + "' 2>/dev/null";
    
    // Execute curl command
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.success = false;
        result.error_type = ErrorType::SERVER_UNREACHABLE;
        result.error_message = "Failed to execute curl";
        return result;
    }
    
    // Read status code
    char buffer[16];
    int status_code = 0;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        status_code = atoi(buffer);
    }
    pclose(pipe);
    
    // Read content from temp file
    std::ifstream file(temp_file);
    if (file.is_open()) {
        std::stringstream ss;
        ss << file.rdbuf();
        result.content = ss.str();
        file.close();
        unlink(temp_file.c_str()); // Delete temp file
    }
    
    result.success = (status_code == 200);
    result.status_code = status_code;
    
    if (!result.success) {
        result.error_type = (status_code == 0) ? ErrorType::SERVER_UNREACHABLE : ErrorType::INVALID_RESPONSE;
        result.error_message = "HTTP " + std::to_string(status_code);
    }
    
    return result;
}

NetworkResult WebInkNetworkClient::perform_curl_post_request(const std::string& url, const std::string& body, const std::string& content_type) {
    NetworkResult result;
    
    // Create temporary files for post data and response
    std::string temp_data = "/tmp/webink_post_" + std::to_string(rand());
    std::string temp_response = "/tmp/webink_resp_" + std::to_string(rand());
    
    // Write POST data to temp file
    std::ofstream data_file(temp_data);
    if (data_file.is_open()) {
        data_file << body;
        data_file.close();
    } else {
        result.success = false;
        result.error_type = ErrorType::SERVER_UNREACHABLE;
        result.error_message = "Failed to write POST data";
        return result;
    }
    
    // Build curl POST command
    std::string cmd = "curl -s -w '%{http_code}' -X POST -H 'Content-Type: " + content_type + 
                     "' --data-binary '@" + temp_data + "' -o '" + temp_response + "' '" + url + "' 2>/dev/null";
    
    // Execute curl command
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        unlink(temp_data.c_str());
        result.success = false;
        result.error_type = ErrorType::SERVER_UNREACHABLE;
        result.error_message = "Failed to execute curl POST";
        return result;
    }
    
    // Read status code
    char buffer[16];
    int status_code = 0;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        status_code = atoi(buffer);
    }
    pclose(pipe);
    
    // Read response from temp file
    std::ifstream response_file(temp_response);
    if (response_file.is_open()) {
        std::stringstream ss;
        ss << response_file.rdbuf();
        result.content = ss.str();
        response_file.close();
    }
    
    // Cleanup temp files
    unlink(temp_data.c_str());
    unlink(temp_response.c_str());
    
    result.success = (status_code == 200);
    result.status_code = status_code;
    
    if (!result.success) {
        result.error_type = (status_code == 0) ? ErrorType::SERVER_UNREACHABLE : ErrorType::INVALID_RESPONSE;
        result.error_message = "HTTP " + std::to_string(status_code);
    }
    
    return result;
}

// Method used by integration test
bool WebInkNetworkClient::get_url_content(const std::string& url, HTTPResponse& response) {
    NetworkResult result = perform_curl_request(url);
    
    response.status_code = result.status_code;
    response.content = result.content;
    response.success = result.success;
    
    return result.success;
}

#endif // WEBINK_MAC_INTEGRATION_TEST

} // namespace webink
} // namespace esphome
