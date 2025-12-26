/**
 * @file webink_controller.cpp
 * @brief Implementation of WebInkController class - main state machine orchestrator
 * 
 * @author WebInk Component Authors
 * @version 1.0.0
 */

#include "webink_controller.h"
#include <esp_system.h>
#include <esp_sleep.h>

namespace esphome {
namespace webink {

const char* WebInkController::TAG = "webink.controller";

//=============================================================================
// CONSTRUCTOR AND DESTRUCTOR
//=============================================================================

WebInkController::WebInkController()
    : config_(std::make_shared<WebInkConfig>()),
      deep_sleep_(nullptr),
      current_state_(UpdateState::IDLE),
      state_start_time_(0),
      last_yield_time_(0),
      manual_update_requested_(false),
      total_image_rows_(0),
      rows_completed_(0),
      current_progress_(0.0f) {
    
    ESP_LOGI(TAG, "WebInkController initializing...");
    
    // Initialize sub-components
    initialize_components();
    
    ESP_LOGI(TAG, "WebInkController initialized");
}

WebInkController::~WebInkController() {
    ESP_LOGD(TAG, "WebInkController destructor");
}

//=============================================================================
// ESPHOME COMPONENT INTERFACE
//=============================================================================

void WebInkController::setup() {
    ESP_LOGI(TAG, "[SETUP] WebInk Controller starting setup...");
    
    if (!validate_configuration()) {
        ESP_LOGE(TAG, "[SETUP] Configuration validation failed");
        return;
    }
    
    // Initialize state
    state_.record_boot_time(millis());
    state_.clear_error_flags();
    
    // Log boot information
    if (state_.is_deep_sleep_wake()) {
        ESP_LOGI(TAG, "[SETUP] Woke from deep sleep");
    } else {
        ESP_LOGI(TAG, "[SETUP] Power-on boot detected");
    }
    
    ESP_LOGI(TAG, "[SETUP] Boot time recorded: %lu ms", state_.boot_time);
    ESP_LOGI(TAG, "[SETUP] Configuration: %s", config_->get_config_summary().c_str());
    
    // Set up network info for display
    if (display_) {
        std::string host;
        int port;
        if (config_->parse_server_host(host, port)) {
            display_->set_network_info(config_->base_url, ""); // IP will be set later
        }
    }
    
    ESP_LOGI(TAG, "[SETUP] WebInk Controller setup complete");
}

void WebInkController::loop() {
    // Yield control regularly to ESPHome main loop
    if (should_yield_control()) {
        return;
    }
    
    // Update network operations
    if (network_) {
        network_->update();
    }
    
    // Check for state timeout (skip IDLE and COMPLETE - they are waiting states)
    if (current_state_ != UpdateState::IDLE && 
        current_state_ != UpdateState::COMPLETE &&
        has_state_timed_out()) {
        ESP_LOGW(TAG, "[TIMEOUT] State %s timed out after %lu ms",
                 update_state_to_string(current_state_), STATE_TIMEOUT_MS);
        handle_error(ErrorType::SERVER_UNREACHABLE, "State machine timeout");
        return;
    }
    
    // Execute current state
    switch (current_state_) {
        case UpdateState::IDLE:
            handle_idle_state();
            break;
        case UpdateState::WIFI_WAIT:
            handle_wifi_wait_state();
            break;
        case UpdateState::HASH_CHECK:
            handle_hash_check_state();
            break;
        case UpdateState::HASH_REQUEST:
            handle_hash_request_state();
            break;
        case UpdateState::HASH_PARSE:
            handle_hash_parse_state();
            break;
        case UpdateState::IMAGE_REQUEST:
            handle_image_request_state();
            break;
        case UpdateState::IMAGE_DOWNLOAD:
            handle_image_download_state();
            break;
        case UpdateState::IMAGE_PARSE:
            handle_image_parse_state();
            break;
        case UpdateState::IMAGE_DISPLAY:
            handle_image_display_state();
            break;
        case UpdateState::DISPLAY_UPDATE:
            handle_display_update_state();
            break;
        case UpdateState::ERROR_DISPLAY:
            handle_error_display_state();
            break;
        case UpdateState::SLEEP_PREPARE:
            handle_sleep_prepare_state();
            break;
        case UpdateState::COMPLETE:
            handle_complete_state();
            break;
    }
}

//=============================================================================
// CONFIGURATION INTERFACE
//=============================================================================

void WebInkController::set_config(std::shared_ptr<WebInkConfig> config) {
    config_ = config;
    
    if (config_) {
        config_->set_change_callback([this](const std::string& param) {
            ESP_LOGI(TAG, "[CONFIG] Parameter changed: %s", param.c_str());
            if (on_log_message) {
                on_log_message("Configuration updated: " + param);
            }
        });
    }
    
    ESP_LOGD(TAG, "Configuration manager set");
}

void WebInkController::set_display(std::shared_ptr<WebInkDisplayManager> display) {
    display_ = display;
    ESP_LOGD(TAG, "Display manager set");
}

void WebInkController::set_deep_sleep_component(deep_sleep::DeepSleepComponent* deep_sleep) {
    deep_sleep_ = deep_sleep;
    ESP_LOGD(TAG, "Deep sleep component set");
}

void WebInkController::set_network_client(std::shared_ptr<WebInkNetworkClient> network) {
    network_ = network;
    ESP_LOGD(TAG, "Network client set");
}

void WebInkController::set_image_processor(std::shared_ptr<WebInkImageProcessor> image_processor) {
    image_processor_ = image_processor;
    ESP_LOGD(TAG, "Image processor set");
}

//=============================================================================
// MANUAL CONTROL INTERFACE
//=============================================================================

bool WebInkController::trigger_manual_update() {
    if (current_state_ != UpdateState::IDLE) {
        ESP_LOGW(TAG, "[MANUAL] Update already in progress: %s", 
                 update_state_to_string(current_state_));
        return false;
    }
    
    ESP_LOGI(TAG, "[MANUAL] Manual update triggered");
    manual_update_requested_ = true;
    transition_to_state(UpdateState::WIFI_WAIT);
    
    if (on_log_message) {
        on_log_message("Manual update started");
    }
    
    return true;
}

bool WebInkController::trigger_deep_sleep() {
    if (!should_enter_deep_sleep()) {
        ESP_LOGW(TAG, "[MANUAL] Deep sleep conditions not met");
        return false;
    }
    
    ESP_LOGI(TAG, "[MANUAL] Manual deep sleep triggered");
    transition_to_state(UpdateState::SLEEP_PREPARE);
    
    return true;
}

void WebInkController::clear_hash_force_update() {
    state_.clear_hash_force_update();
    ESP_LOGI(TAG, "[MANUAL] Hash cleared - next update will refresh display");
    
    if (on_log_message) {
        on_log_message("Hash cleared for forced refresh");
    }
}

void WebInkController::enable_deep_sleep(bool enabled) {
    state_.deep_sleep_enabled = enabled;
    ESP_LOGI(TAG, "[CONFIG] Deep sleep %s", enabled ? "ENABLED" : "DISABLED");
    
    if (on_log_message) {
        on_log_message(std::string("Deep sleep ") + (enabled ? "enabled" : "disabled"));
    }
}

bool WebInkController::cancel_current_operation() {
    if (current_state_ == UpdateState::IDLE) {
        return false;
    }
    
    ESP_LOGI(TAG, "[CANCEL] Cancelling current operation: %s", 
             update_state_to_string(current_state_));
    
    if (network_) {
        network_->cancel_all_operations();
    }
    
    transition_to_state(UpdateState::IDLE);
    reset_operation_state();
    
    return true;
}

//=============================================================================
// STATUS AND MONITORING INTERFACE
//=============================================================================

UpdateState WebInkController::get_current_state() const {
    return current_state_;
}

bool WebInkController::is_update_in_progress() const {
    return current_state_ != UpdateState::IDLE;
}

const WebInkState& WebInkController::get_state() const {
    return state_;
}

const WebInkConfig& WebInkController::get_config() const {
    return *config_;
}

bool WebInkController::get_progress_info(float& percentage, std::string& status) const {
    if (!is_update_in_progress()) {
        return false;
    }
    
    percentage = current_progress_;
    status = current_status_;
    
    return true;
}

std::string WebInkController::get_status_string() const {
    static char buffer[160];  // Much smaller static buffer instead of 512 byte stack allocation!
    snprintf(buffer, sizeof(buffer),
             "[STATUS] State: %s, Wake #%d, Boot #%d, Progress: %.1f%%, Hash: %s",
             update_state_to_string(current_state_),
             state_.wake_counter,
             state_.cycles_since_boot,
             current_progress_,
             state_.last_hash);  // Now a char array, no .c_str() needed
    
    return std::string(buffer);
}

unsigned long WebInkController::get_time_in_current_state() const {
    return millis() - state_start_time_;
}

//=============================================================================
// ESPHOME INTEGRATION HELPERS
//=============================================================================

void WebInkController::set_server_url(const std::string& url) {
    if (config_->set_server_url(url.c_str())) {
        ESP_LOGI(TAG, "[CONFIG] Server URL updated to: %s", url.c_str());
    }
}

void WebInkController::set_device_id(const std::string& device_id) {
    if (config_->set_device_id(device_id.c_str())) {
        ESP_LOGI(TAG, "[CONFIG] Device ID updated to: %s", device_id.c_str());
    }
}

void WebInkController::set_api_key(const std::string& api_key) {
    config_->set_api_key(api_key.c_str());
    ESP_LOGI(TAG, "[CONFIG] API key updated");
}

void WebInkController::set_display_mode(const std::string& display_mode) {
    if (config_->set_display_mode(display_mode.c_str())) {
        ESP_LOGI(TAG, "[CONFIG] Display mode updated to: %s", display_mode.c_str());
    }
}

void WebInkController::set_socket_port(int port) {
    if (config_->set_socket_port(port)) {
        ESP_LOGI(TAG, "[CONFIG] Socket port updated to: %d", port);
    }
}

//=============================================================================
// STATE MACHINE IMPLEMENTATION
//=============================================================================

void WebInkController::transition_to_state(UpdateState new_state) {
    if (current_state_ != new_state) {
        UpdateState old_state = current_state_;
        current_state_ = new_state;
        state_start_time_ = millis();
        
        log_state_transition(old_state, new_state);
        
        // DISABLED: std::function callback causes stack overflow on ESP32C3
        // if (on_state_change) {
        //     on_state_change(old_state, new_state);
        // }
    }
}

bool WebInkController::should_yield_control() {
    unsigned long now = millis();
    
    if (now - last_yield_time_ >= YIELD_INTERVAL_MS) {
        last_yield_time_ = now;
        return true;
    }
    
    return false;
}

bool WebInkController::has_state_timed_out() {
    return (millis() - state_start_time_) > STATE_TIMEOUT_MS;
}

//=============================================================================
// STATE HANDLERS
//=============================================================================

void WebInkController::handle_idle_state() {
    // Check if we should start an update cycle
    bool should_start_update = false;
    
    if (manual_update_requested_) {
        should_start_update = true;
        manual_update_requested_ = false;
        ESP_LOGI(TAG, "[IDLE] Starting manual update cycle");
    } else if (state_.should_start_update_cycle(millis())) {
        should_start_update = true;
        ESP_LOGI(TAG, "[IDLE] Starting scheduled update cycle");
    }
    
    if (should_start_update) {
        state_.increment_wake_counter();
        state_.record_update_time(millis());
        transition_to_state(UpdateState::WIFI_WAIT);
        
        update_progress(0.0f, "Starting update cycle");
        // post_status_to_server disabled - string concat causes stack overflow
    }
}

void WebInkController::handle_wifi_wait_state() {
    // Check WiFi connection status
    bool wifi_connected = false;
    
    if (get_wifi_status) {
        wifi_connected = get_wifi_status();
    }
    
    // Debug: Log WiFi status periodically (every 2 seconds)
    static unsigned long last_wifi_log = 0;
    unsigned long now = millis();
    if (now - last_wifi_log > 2000) {
        ESP_LOGD(TAG, "[WIFI] Status check: connected=%s, time_in_state=%lu ms", 
                 wifi_connected ? "true" : "false", get_time_in_current_state());
        last_wifi_log = now;
    }
    
    if (wifi_connected) {
        ESP_LOGI(TAG, "[WIFI] WiFi connected, proceeding to hash check");
        transition_to_state(UpdateState::HASH_REQUEST);
        update_progress(10.0f, "WiFi connected");
    } else {
        // Check for timeout - increased to 30 seconds for slow WiFi connections
        if (get_time_in_current_state() > 30000) {  // 30 second timeout
            ESP_LOGW(TAG, "[WIFI] WiFi connection timeout after 30 seconds");
            handle_error(ErrorType::WIFI_TIMEOUT, "WiFi connection timeout after 30 seconds");
        }
    }
}

void WebInkController::handle_hash_request_state() {
    if (!network_) {
        handle_error(ErrorType::SERVER_UNREACHABLE, "Network client not available");
        return;
    }
    
    std::string hash_url = config_->build_hash_url();
    ESP_LOGI(TAG, "[HASH] Requesting hash from: %s", hash_url.c_str());
    
    // Note: http_get_async is actually blocking - callback fires immediately
    // The callback handles state transitions, so we don't transition here
    bool request_started = network_->http_get_async(hash_url, 
        [this](NetworkResult result) {
            this->on_hash_response(result);
        }, NETWORK_TIMEOUT_MS);
    
    if (!request_started) {
        handle_error(ErrorType::SERVER_UNREACHABLE, "Failed to start hash request");
    }
    // State transition happens in on_hash_response callback
}

void WebInkController::handle_hash_check_state() {
    // Transition to hash request - this is just a passthrough state
    transition_to_state(UpdateState::HASH_REQUEST);
    update_progress(15.0f, "Checking hash");
}

void WebInkController::handle_hash_parse_state() {
    // This state waits for the hash response callback
    update_progress(25.0f, "Waiting for hash response");
    
    // The actual work is done in on_hash_response callback
}

void WebInkController::handle_image_request_state() {
    ESP_LOGI(TAG, "[IMAGE] Starting image request, socket_port=%d", config_->socket_mode_port);
    
    // Calculate image parameters
    calculate_image_parameters();
    
    // Initialize slice tracking
    rows_completed_ = 0;
    
    if (config_->get_network_mode() == NetworkMode::TCP_SOCKET) {
        // Use TCP socket mode for full image download
        std::string host = config_->get_server_hostname();
        int port = config_->socket_mode_port;
        
        ESP_LOGI(TAG, "[IMAGE] Using socket mode: %s:%d", host.c_str(), port);
        
        if (network_->socket_connect_async(host, port)) {
            transition_to_state(UpdateState::IMAGE_DOWNLOAD);
        } else {
            handle_error(ErrorType::SOCKET_ERROR, "Failed to connect to image server");
        }
    } else {
        // Use HTTP sliced mode - transition to IMAGE_DOWNLOAD which handles the loop
        ESP_LOGI(TAG, "[IMAGE] Using HTTP sliced mode");
        transition_to_state(UpdateState::IMAGE_DOWNLOAD);
    }
}

void WebInkController::handle_image_download_state() {
    // HTTP sliced mode - request current slice
    if (config_->get_network_mode() == NetworkMode::HTTP_SLICED) {
        // Check if all slices received
        if (rows_completed_ >= total_image_rows_) {
            ESP_LOGI(TAG, "[IMAGE] All %d rows received", rows_completed_);
            transition_to_state(UpdateState::DISPLAY_UPDATE);
            return;
        }
        
        // Request next slice
        int remaining_rows = total_image_rows_ - rows_completed_;
        int rows_to_request = std::min(config_->rows_per_slice, remaining_rows);
        
        current_image_request_.rect = DisplayRect(0, rows_completed_, 800, rows_to_request);
        current_image_request_.start_row = rows_completed_;
        current_image_request_.num_rows = rows_to_request;
        current_image_request_.format = "pbm";
        
        std::string image_url = config_->build_image_url(current_image_request_);
        ESP_LOGD(TAG, "[IMAGE] Requesting rows %d-%d of %d", rows_completed_, 
                 rows_completed_ + rows_to_request, total_image_rows_);
        
        bool request_started = network_->http_get_async(image_url,
            [this](NetworkResult result) {
                this->on_image_response(result);
            }, NETWORK_TIMEOUT_MS);
        
        if (!request_started) {
            handle_error(ErrorType::SERVER_UNREACHABLE, "Failed to request image slice");
        }
        // on_image_response will increment rows_completed_ and we'll be called again
        return;
    }
    
    // TCP Socket mode - send request and receive image
    static bool socket_request_sent = false;
    static bool socket_receive_started = false;
    
    // Check if socket is connected
    if (!network_->socket_is_connected()) {
        ESP_LOGD(TAG, "[SOCKET] Waiting for connection...");
        return;
    }
    
    // Send request once connected
    if (!socket_request_sent) {
        // Build and send socket request
        ImageRequest req;
        req.rect = DisplayRect(0, 0, 800, total_image_rows_);
        req.start_row = 0;
        req.num_rows = total_image_rows_;
        req.format = "pbm";
        
        std::string request = config_->build_socket_request(req);
        ESP_LOGI(TAG, "[SOCKET] Sending request: %s", request.c_str());
        
        if (!network_->socket_send(request)) {
            handle_error(ErrorType::SOCKET_ERROR, "Failed to send socket request");
            socket_request_sent = false;
            socket_receive_started = false;
            return;
        }
        socket_request_sent = true;
        ESP_LOGI(TAG, "[SOCKET] Request sent, waiting for image data");
        return;
    }
    
    // Start receive stream (only once)
    if (!socket_receive_started) {
        // Use static variables inside lambda for row buffering
        bool started = network_->socket_receive_stream(
            [this](const uint8_t* data, int length) {
                // Static buffer to accumulate partial rows (persists across calls)
                static const int BYTES_PER_ROW = 800 / 8;  // 100 bytes per row
                static uint8_t row_buffer[BYTES_PER_ROW];
                static int buffer_pos = 0;
                
                // Reset buffer at start of new image (when rows_completed_ is 0)
                if (rows_completed_ == 0) {
                    buffer_pos = 0;
                }
                
                ESP_LOGD(TAG, "[SOCKET] Received %d bytes, buffer_pos=%d, rows=%d", 
                         length, buffer_pos, rows_completed_);
                
                if (!display_ || length <= 0) return;
                
                int data_pos = 0;
                
                while (data_pos < length) {
                    // Fill buffer with incoming data
                    int bytes_needed = BYTES_PER_ROW - buffer_pos;
                    int bytes_available = length - data_pos;
                    int bytes_to_copy = std::min(bytes_needed, bytes_available);
                    
                    memcpy(row_buffer + buffer_pos, data + data_pos, bytes_to_copy);
                    buffer_pos += bytes_to_copy;
                    data_pos += bytes_to_copy;
                    
                    // If we have a complete row, draw it
                    if (buffer_pos >= BYTES_PER_ROW) {
                        display_->draw_progressive_pixels(0, rows_completed_, 800, 1,
                                                         row_buffer, ColorMode::MONO_BLACK_WHITE);
                        rows_completed_++;
                        buffer_pos = 0;
                    }
                }
            },
            800 * total_image_rows_ / 8,  // Max bytes for full image
            NETWORK_TIMEOUT_MS
        );
        
        if (started) {
            socket_receive_started = true;
            ESP_LOGI(TAG, "[SOCKET] Receive stream started");
        } else {
            handle_error(ErrorType::SOCKET_ERROR, "Failed to start socket receive");
            socket_request_sent = false;
            socket_receive_started = false;
        }
        return;
    }
    
    // Let network update() handle the receive - check if operation completed
    if (!network_->is_operation_pending()) {
        ESP_LOGI(TAG, "[SOCKET] Image transfer complete");
        socket_request_sent = false;
        socket_receive_started = false;
        network_->socket_close();
        transition_to_state(UpdateState::DISPLAY_UPDATE);
    }
    
    // Update progress
    if (total_image_rows_ > 0) {
        float progress = 50.0f + (rows_completed_ * 30.0f) / total_image_rows_;
        current_progress_ = progress;
    }
}

void WebInkController::handle_image_parse_state() {
    ESP_LOGI(TAG, "[IMAGE] Parsing image data");
    update_progress(75.0f, "Processing image data");
    
    // In a full implementation, this would parse received image data
    // For now, just transition to display
    transition_to_state(UpdateState::IMAGE_DISPLAY);
}

void WebInkController::handle_image_display_state() {
    ESP_LOGI(TAG, "[IMAGE] Drawing image to display buffer");
    update_progress(85.0f, "Drawing image to buffer");
    
    // In a full implementation, this would draw pixels to display buffer
    // For now, just transition to display update
    transition_to_state(UpdateState::DISPLAY_UPDATE);
}

void WebInkController::handle_display_update_state() {
    ESP_LOGI(TAG, "[DISPLAY] Updating physical display");
    
    if (display_) {
        display_->update_display();
    }
    
    update_progress(95.0f, "Refreshing display");
    
    // Display update is typically slow (several seconds for e-ink)
    // In a full implementation, this would be non-blocking
    transition_to_state(UpdateState::SLEEP_PREPARE);
}

void WebInkController::handle_sleep_prepare_state() {
    static bool sleep_interval_requested = false;
    
    // Phase 1: Request sleep interval from server
    if (!sleep_interval_requested) {
        ESP_LOGI(TAG, "[SLEEP] Requesting sleep interval from server");
        
        if (!network_) {
            ESP_LOGW(TAG, "[SLEEP] Network client not available - using default sleep duration");
            sleep_interval_requested = true;  // Skip to phase 2
            return;
        }
        
        std::string sleep_url = config_->build_sleep_url();
        ESP_LOGI(TAG, "[SLEEP] Sleep URL: %s", sleep_url.c_str());
        
        bool request_started = network_->http_get_async(sleep_url,
            [this](NetworkResult result) {
                this->on_sleep_response(result);
            }, NETWORK_TIMEOUT_MS);
        
        if (request_started) {
            sleep_interval_requested = true;
            update_progress(95.0f, "Getting sleep interval");
            ESP_LOGD(TAG, "[SLEEP] Sleep interval request started");
        } else {
            ESP_LOGW(TAG, "[SLEEP] Failed to request sleep interval - using default");
            sleep_interval_requested = true;  // Skip to phase 2
        }
        return;
    }
    
    // Phase 2: Prepare for deep sleep (after sleep interval received)
    ESP_LOGI(TAG, "[SLEEP] Preparing for deep sleep");
    
    update_progress(100.0f, "Update complete");
    
    post_status_to_server("Update complete - entering deep sleep for " + 
                         std::to_string(state_.sleep_duration_seconds) + " seconds");
    
    if (should_enter_deep_sleep()) {
        prepare_and_enter_deep_sleep();
    } else {
        ESP_LOGI(TAG, "[SLEEP] Skipping deep sleep - conditions not met");
        transition_to_state(UpdateState::COMPLETE);
    }
    
    // Reset for next cycle
    sleep_interval_requested = false;
}

void WebInkController::handle_complete_state() {
    ESP_LOGI(TAG, "[COMPLETE] Update cycle complete");
    
    reset_operation_state();
    transition_to_state(UpdateState::IDLE);
}

void WebInkController::handle_error_display_state() {
    // Error display is handled in the error handler
    // After a delay, transition to sleep preparation
    
    if (get_time_in_current_state() > 2000) {  // Show error for 2 seconds
        transition_to_state(UpdateState::SLEEP_PREPARE);
    }
}

//=============================================================================
// NETWORK CALLBACK HANDLERS
//=============================================================================

void WebInkController::on_hash_response(NetworkResult result) {
    if (!result.success) {
        handle_error(ErrorType::SERVER_UNREACHABLE, "Hash request failed: " + result.error_message);
        return;
    }
    
    ESP_LOGI(TAG, "[HASH] Received response: %s", result.data.c_str());
    
    // Parse JSON response: {"hash":"abcd1234"}
    std::string hash_response = result.data;
    size_t hash_pos = hash_response.find("\"hash\":\"");
    size_t value_offset = 8;  // length of "hash":"
    if (hash_pos == std::string::npos) {
        hash_pos = hash_response.find("\"hash\": \"");
        value_offset = 9;  // length of "hash": " (with space)
    }
    
    if (hash_pos != std::string::npos) {
        size_t start = hash_pos + value_offset;  // First char of hash value
        size_t end = hash_response.find("\"", start);
        
        if (end != std::string::npos && end > start) {
            current_hash_ = hash_response.substr(start, end - start);
            ESP_LOGI(TAG, "[HASH] Parsed hash: %s", current_hash_.c_str());
            
            if (state_.has_hash_changed(current_hash_.c_str())) {
                ESP_LOGI(TAG, "[HASH] Hash changed - starting image download");
                state_.update_hash(current_hash_.c_str());
                transition_to_state(UpdateState::IMAGE_REQUEST);
            } else {
                ESP_LOGI(TAG, "[HASH] Hash unchanged - skipping update");
                transition_to_state(UpdateState::SLEEP_PREPARE);
            }
        } else {
            handle_error(ErrorType::PARSE_ERROR, "Failed to extract hash from response");
        }
    } else {
        handle_error(ErrorType::PARSE_ERROR, "Hash not found in server response");
    }
}

void WebInkController::on_image_response(NetworkResult result) {
    if (!result.success) {
        handle_error(ErrorType::SERVER_UNREACHABLE, "Image request failed: " + result.error_message);
        return;
    }
    
    ESP_LOGI(TAG, "[IMAGE] Received %d bytes of image data (rows %d-%d)", 
             result.bytes_received, rows_completed_, 
             rows_completed_ + current_image_request_.num_rows);
    
    if (result.bytes_received > 0) {
        // Parse PBM and render to display
        const uint8_t* data = reinterpret_cast<const uint8_t*>(result.data.data());
        size_t data_size = result.data.size();
        
        // Skip PBM header to find pixel data
        // PBM P4 format: "P4\n<width> <height>\n<binary data>"
        size_t pixel_start = 0;
        int newline_count = 0;
        for (size_t i = 0; i < data_size && newline_count < 2; i++) {
            if (data[i] == '\n') {
                newline_count++;
                if (newline_count == 2) {
                    pixel_start = i + 1;
                    break;
                }
            }
        }
        
        // Render pixels to display if we have display manager and pixel data
        if (display_ && pixel_start < data_size) {
            int width = 800;  // TODO: get from config
            int height = current_image_request_.num_rows;
            const uint8_t* pixel_data = data + pixel_start;
            
            // Draw this slice to the display buffer
            display_->draw_progressive_pixels(0, rows_completed_, width, height,
                                             pixel_data, ColorMode::MONO_BLACK_WHITE);
            
            ESP_LOGD(TAG, "[IMAGE] Rendered rows %d-%d to display buffer", 
                     rows_completed_, rows_completed_ + height);
        }
        
        // Increment rows completed
        rows_completed_ += current_image_request_.num_rows;
        
        ESP_LOGD(TAG, "[IMAGE] Progress: %d/%d rows complete", 
                 rows_completed_, total_image_rows_);
    } else {
        handle_error(ErrorType::PARSE_ERROR, "Empty image data received");
    }
}

void WebInkController::on_sleep_response(NetworkResult result) {
    if (!result.success) {
        ESP_LOGW(TAG, "[SLEEP] Sleep interval request failed: %s - using default", result.error_message.c_str());
        ESP_LOGW(TAG, "[SLEEP] Using default sleep duration: %d seconds", state_.sleep_duration_seconds);
        return;
    }
    
    ESP_LOGI(TAG, "[SLEEP] Received sleep interval response: %s", result.data.c_str());
    
    // Parse JSON response: {"sleep": 1800} or {"sleep_duration": 1800} or {"sleep_seconds": 1800}
    std::string sleep_response = result.data;
    size_t sleep_pos = sleep_response.find("\"sleep_seconds\":");
    if (sleep_pos == std::string::npos) {
        sleep_pos = sleep_response.find("\"sleep\":");
    }
    if (sleep_pos == std::string::npos) {
        sleep_pos = sleep_response.find("\"sleep_duration\":");
    }
    
    if (sleep_pos != std::string::npos) {
        // Find the colon and skip whitespace
        size_t colon_pos = sleep_response.find(":", sleep_pos);
        if (colon_pos != std::string::npos) {
            size_t start = colon_pos + 1;
            
            // Skip whitespace
            while (start < sleep_response.length() && 
                   (sleep_response[start] == ' ' || sleep_response[start] == '\t')) {
                start++;
            }
            
            // Extract number (until non-digit character)
            size_t end = start;
            while (end < sleep_response.length() && isdigit(sleep_response[end])) {
                end++;
            }
            
            if (end > start) {
                std::string sleep_str = sleep_response.substr(start, end - start);
                int new_sleep_duration = std::atoi(sleep_str.c_str());
                
                if (new_sleep_duration > 0) {
                    ESP_LOGI(TAG, "[SLEEP] Server set sleep duration: %d seconds", new_sleep_duration);
                    state_.sleep_duration_seconds = new_sleep_duration;
                } else {
                    ESP_LOGW(TAG, "[SLEEP] Invalid sleep duration from server: %d - using default", 
                             new_sleep_duration);
                }
            } else {
                ESP_LOGW(TAG, "[SLEEP] Failed to parse sleep duration number");
            }
        } else {
            ESP_LOGW(TAG, "[SLEEP] Failed to find colon in sleep response");
        }
    } else {
        ESP_LOGW(TAG, "[SLEEP] Sleep duration not found in server response");
    }
}


void WebInkController::on_socket_data(const uint8_t* data, int length) {
    ESP_LOGD(TAG, "[SOCKET] Received %d bytes of data", length);
    
    // Process socket data for full image download mode
    // This would be used in TCP socket mode for streaming image data
    // For now, just log the data receipt
    if (current_state_ == UpdateState::IMAGE_DOWNLOAD) {
        ESP_LOGD(TAG, "[SOCKET] Processing image data chunk");
        
        // In a full implementation, this would:
        // 1. Accumulate the received data
        // 2. Check if we have received the complete image
        // 3. Transition to IMAGE_PARSE state when complete
    }
}

//=============================================================================
// ERROR HANDLING
//=============================================================================

void WebInkController::handle_error(ErrorType error_type, const std::string& details) {
    ESP_LOGE(TAG, "[ERROR] %s: %s", error_type_to_string(error_type), details.c_str());
    
    state_.set_error(error_type, details.c_str());
    
    if (on_error_occurred) {
        on_error_occurred(error_type, details);
    }
    
    display_error_and_sleep(error_type, details);
}

void WebInkController::display_error_and_sleep(ErrorType error_type, const std::string& details) {
    if (display_) {
        display_->draw_error_message(error_type, details);
    }
    
    // DISABLED: String concatenation causes stack overflow on ESP32C3
    // post_status_to_server("ERROR: " + std::string(error_type_to_string(error_type)) + " - " + details);
    
    transition_to_state(UpdateState::ERROR_DISPLAY);
}

//=============================================================================
// UTILITY METHODS
//=============================================================================

void WebInkController::initialize_components() {
    // Create network client if not provided
    if (!network_) {
        network_ = std::make_shared<WebInkNetworkClient>(config_.get(),
            [this](const std::string& msg) {
                if (on_log_message) on_log_message(msg);
            });
    }
    
    // Create image processor if not provided
    if (!image_processor_) {
        image_processor_ = std::make_shared<WebInkImageProcessor>(
            [this](const std::string& msg) {
                if (on_log_message) on_log_message(msg);
            });
    }
    
    ESP_LOGD(TAG, "Components initialized");
}

bool WebInkController::validate_configuration() {
    // Use char buffer for config validation (matches header signature)
    char error_buffer[512];
    error_buffer[0] = '\0';  // Initialize as empty
    
    if (!config_->validate_configuration(error_buffer, sizeof(error_buffer))) {
        ESP_LOGE(TAG, "[CONFIG] Configuration validation failed: %s", error_buffer);
        return false;
    }
    
    if (!display_) {
        ESP_LOGE(TAG, "[CONFIG] Display manager not configured");
        return false;
    }
    
    return true;
}

void WebInkController::calculate_image_parameters() {
    int width, height, bits;
    ColorMode mode;
    
    if (config_->parse_display_mode(width, height, bits, mode)) {
        total_image_rows_ = height;
        ESP_LOGD(TAG, "[IMAGE] Calculated parameters: %dx%d, %d total rows",
                 width, height, total_image_rows_);
    } else {
        ESP_LOGW(TAG, "[IMAGE] Failed to parse display mode");
        total_image_rows_ = 480; // Default fallback
    }
}

bool WebInkController::validate_image_data(const uint8_t* data, int size) {
    // Basic validation - check for PBM header
    if (size < 10 || !data) {
        return false;
    }
    
    // Check for PBM magic number
    return (data[0] == 'P' && (data[1] == '1' || data[1] == '4'));
}

void WebInkController::update_progress(float percentage, const std::string& status) {
    current_progress_ = percentage;
    current_status_ = status;
    
    if (on_progress_update) {
        on_progress_update(percentage, status);
    }
    
    ESP_LOGD(TAG, "[PROGRESS] %.1f%% - %s", percentage, status.c_str());
}

bool WebInkController::should_enter_deep_sleep() {
    bool boot_button_pressed = false;
    if (get_boot_button_status) {
        boot_button_pressed = get_boot_button_status();
    }
    
    return state_.can_deep_sleep(boot_button_pressed, millis());
}

void WebInkController::prepare_and_enter_deep_sleep() {
    ESP_LOGI(TAG, "[SLEEP] Entering deep sleep for %d seconds", state_.sleep_duration_seconds);
    
    if (deep_sleep_) {
        deep_sleep_->set_sleep_duration(state_.get_sleep_duration_ms());
        deep_sleep_->begin_sleep();
    } else {
        ESP_LOGW(TAG, "[SLEEP] Deep sleep component not configured");
        transition_to_state(UpdateState::COMPLETE);
    }
}

void WebInkController::post_status_to_server(const std::string& message) {
    if (!network_) return;
    
    std::string log_url = config_->build_log_url();
    network_->http_post_async(log_url, message, 
        [this](NetworkResult result) {
            this->on_log_response(result);
        });
}

void WebInkController::on_log_response(NetworkResult result) {
    // Log response handling - typically just debug logging
    if (result.success) {
        ESP_LOGD(TAG, "[LOG] Status posted to server successfully");
    } else {
        ESP_LOGW(TAG, "[LOG] Failed to post status to server: %s", result.error_message.c_str());
    }
}

void WebInkController::log_state_transition(UpdateState from_state, UpdateState to_state) {
    // Simplified to avoid stack overflow - split into separate log calls
    const char* from_str = update_state_to_string(from_state);
    const char* to_str = update_state_to_string(to_state);
    ESP_LOGI(TAG, "[STATE] %s -> %s", from_str, to_str);
}

void WebInkController::reset_operation_state() {
    current_hash_ = "";
    rows_completed_ = 0;
    total_image_rows_ = 0;
    current_progress_ = 0.0f;
    current_status_ = "";
}

//=============================================================================
// FACTORY FUNCTION
//=============================================================================

std::shared_ptr<WebInkController> create_webink_controller() {
    return std::make_shared<WebInkController>();
}

} // namespace webink
} // namespace esphome
