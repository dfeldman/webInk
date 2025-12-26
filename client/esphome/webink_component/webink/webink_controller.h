/**
 * @file webink_controller.h
 * @brief Main state machine controller for WebInk component
 * 
 * The WebInkController class is the central orchestrator for the WebInk component,
 * implementing a non-blocking state machine that coordinates network communication,
 * image processing, display updates, and power management. It integrates all
 * WebInk components into a cohesive system.
 * 
 * Key features:
 * - Non-blocking state machine with regular yield points
 * - Complete update cycle orchestration
 * - Deep sleep integration with safety checks
 * - Error recovery and retry logic
 * - ESPHome component lifecycle integration
 * - Memory-efficient operation coordination
 * 
 * @author WebInk Component Authors
 * @version 1.0.0
 */

#pragma once

#include "webink_types.h"
#include "webink_state.h"
#include "webink_config.h"
#include "webink_network.h"
#include "webink_image.h"
#include "webink_display.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/deep_sleep/deep_sleep_component.h"
#include <memory>
#include <functional>

namespace esphome {
namespace webink {

/**
 * @class WebInkController
 * @brief Main state machine controller orchestrating complete WebInk operations
 * 
 * This class serves as the central coordinator for all WebInk operations,
 * implementing a sophisticated state machine that manages the complete
 * image update cycle from hash checking through display refresh to deep sleep.
 * 
 * The controller operates as a non-blocking state machine that regularly yields
 * control back to the ESPHome main loop, ensuring the device remains responsive
 * to other operations while processing images.
 * 
 * State Machine Flow:
 * IDLE → WIFI_WAIT → HASH_REQUEST → HASH_PARSE → [IMAGE_REQUEST → 
 * IMAGE_DOWNLOAD → IMAGE_PARSE → IMAGE_DISPLAY → DISPLAY_UPDATE] → 
 * SLEEP_PREPARE → COMPLETE
 * 
 * Error states can interrupt this flow and lead to ERROR_DISPLAY state,
 * which provides user feedback and then transitions to SLEEP_PREPARE.
 * 
 * Features:
 * - Non-blocking execution with configurable yield intervals
 * - Comprehensive error handling and recovery
 * - Memory-efficient image processing coordination
 * - Deep sleep integration with safety checks
 * - Progress reporting and status monitoring
 * - Network protocol switching (HTTP vs TCP socket)
 * - Configuration management and runtime updates
 * 
 * @example Basic Usage
 * @code
 * auto webink = new WebInkController();
 * webink->set_config(config);
 * webink->set_display(display_manager);
 * webink->set_deep_sleep_component(deep_sleep_component);
 * 
 * // In ESPHome YAML:
 * // custom_component:
 * //   - lambda: |-
 * //       return {webink};
 * @endcode
 * 
 * @example Manual Control
 * @code
 * // Trigger manual update
 * webink->trigger_manual_update();
 * 
 * // Check status
 * if (webink->is_update_in_progress()) {
 *     ESP_LOGI("app", "Update in progress: %s", 
 *              update_state_to_string(webink->get_current_state()));
 * }
 * 
 * // Force refresh
 * webink->clear_hash_force_update();
 * @endcode
 */
class WebInkController : public Component {
public:
    /**
     * @brief Default constructor
     */
    WebInkController();

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~WebInkController();

    //=========================================================================
    // ESPHOME COMPONENT INTERFACE
    //=========================================================================

    /**
     * @brief ESPHome component setup phase
     * 
     * Called once during ESP32 boot to initialize the component.
     * Sets up all sub-components and initial state.
     */
    void setup() override;

    /**
     * @brief ESPHome component main loop
     * 
     * Called continuously from ESPHome main loop. Executes the state machine
     * with regular yield points to maintain system responsiveness.
     * 
     * This method is non-blocking and will yield control back to ESPHome
     * regularly to allow other components to operate.
     */
    void loop() override;

    /**
     * @brief Get component name for ESPHome logging
     * @return Component name string
     */
    const char* get_component_name() override { return "webink_controller"; }

    //=========================================================================
    // CONFIGURATION INTERFACE
    //=========================================================================

    /**
     * @brief Set configuration manager
     * @param config Shared pointer to configuration manager
     */
    void set_config(std::shared_ptr<WebInkConfig> config);

    /**
     * @brief Set display manager
     * @param display Shared pointer to display manager
     */
    void set_display(std::shared_ptr<WebInkDisplayManager> display);

    /**
     * @brief Set deep sleep component for power management
     * @param deep_sleep ESPHome deep sleep component
     */
    void set_deep_sleep_component(deep_sleep::DeepSleepComponent* deep_sleep);

    /**
     * @brief Set network client (optional, will create default if not set)
     * @param network Shared pointer to network client
     */
    void set_network_client(std::shared_ptr<WebInkNetworkClient> network);

    /**
     * @brief Set image processor (optional, will create default if not set)
     * @param image_processor Shared pointer to image processor
     */
    void set_image_processor(std::shared_ptr<WebInkImageProcessor> image_processor);

    //=========================================================================
    // MANUAL CONTROL INTERFACE
    //=========================================================================

    /**
     * @brief Trigger manual update cycle
     * @return True if update was started, false if already in progress
     * 
     * Initiates a complete update cycle regardless of hash status.
     * Useful for testing and manual refresh operations.
     */
    bool trigger_manual_update();

    /**
     * @brief Trigger deep sleep immediately
     * @return True if deep sleep was initiated
     * 
     * Forces immediate deep sleep if all safety conditions are met.
     * Bypasses normal sleep preparation sequence.
     */
    bool trigger_deep_sleep();

    /**
     * @brief Clear hash to force next update
     * 
     * Resets the stored content hash, causing the next update cycle
     * to refresh the display regardless of server hash.
     */
    void clear_hash_force_update();

    /**
     * @brief Enable or disable deep sleep
     * @param enabled True to enable deep sleep, false to disable
     */
    void enable_deep_sleep(bool enabled);

    /**
     * @brief Cancel current operation
     * @return True if operation was cancelled
     * 
     * Attempts to cancel any current network or processing operation
     * and return to IDLE state.
     */
    bool cancel_current_operation();

    //=========================================================================
    // STATUS AND MONITORING INTERFACE
    //=========================================================================

    /**
     * @brief Get current state machine state
     * @return Current UpdateState enumeration value
     */
    UpdateState get_current_state() const;

    /**
     * @brief Check if update cycle is currently in progress
     * @return True if state machine is not in IDLE state
     */
    bool is_update_in_progress() const;

    /**
     * @brief Get persistent state information
     * @return Reference to current state object
     */
    const WebInkState& get_state() const;

    /**
     * @brief Get current configuration
     * @return Reference to configuration object
     */
    const WebInkConfig& get_config() const;

    /**
     * @brief Get current progress information
     * @param[out] percentage Progress percentage (0-100)
     * @param[out] status Human-readable status message
     * @return True if progress information is available
     */
    bool get_progress_info(float& percentage, std::string& status) const;

    /**
     * @brief Get formatted status string for logging
     * @return Human-readable status string
     */
    std::string get_status_string() const;

    /**
     * @brief Get time elapsed in current state
     * @return Milliseconds in current state
     */
    unsigned long get_time_in_current_state() const;

    //=========================================================================
    // CALLBACK CONFIGURATION
    //=========================================================================

    /// Callback for log messages (called for important events)
    std::function<void(const std::string&)> on_log_message;

    /// Callback for state changes (called on each state transition)
    std::function<void(UpdateState, UpdateState)> on_state_change;

    /// Callback for progress updates (called during long operations)
    std::function<void(float, const std::string&)> on_progress_update;

    /// Callback for error events (called when errors occur)
    std::function<void(ErrorType, const std::string&)> on_error_occurred;

    /// Function to get WiFi connection status
    std::function<bool()> get_wifi_status;

    /// Function to get BOOT button status
    std::function<bool()> get_boot_button_status;

    //=========================================================================
    // ESPHOME INTEGRATION HELPERS
    //=========================================================================

    /**
     * @brief Set server URL from ESPHome text input
     * @param url New server URL
     */
    void set_server_url(const std::string& url);

    /**
     * @brief Set device ID from ESPHome text input
     * @param device_id New device identifier
     */
    void set_device_id(const std::string& device_id);

    /**
     * @brief Set API key from ESPHome text input
     * @param api_key New API key
     */
    void set_api_key(const std::string& api_key);

    /**
     * @brief Set display mode from ESPHome text input
     * @param display_mode New display mode string
     */
    void set_display_mode(const std::string& display_mode);

    /**
     * @brief Set socket port from ESPHome number input
     * @param port Socket port (0 = HTTP mode)
     */
    void set_socket_port(int port);

private:
    //=========================================================================
    // COMPONENT INSTANCES
    //=========================================================================

    std::shared_ptr<WebInkConfig> config_;                      ///< Configuration manager
    std::shared_ptr<WebInkDisplayManager> display_;             ///< Display manager
    std::shared_ptr<WebInkNetworkClient> network_;              ///< Network client
    std::shared_ptr<WebInkImageProcessor> image_processor_;     ///< Image processor
    deep_sleep::DeepSleepComponent* deep_sleep_;                ///< ESPHome deep sleep component

    //=========================================================================
    // STATE MACHINE STATE
    //=========================================================================

    WebInkState state_;                                         ///< Persistent state manager
    UpdateState current_state_;                                 ///< Current state machine state
    unsigned long state_start_time_;                            ///< Time when current state started
    unsigned long last_yield_time_;                             ///< Last time control was yielded
    bool manual_update_requested_;                              ///< Manual update flag

    //=========================================================================
    // CURRENT OPERATION CONTEXT
    //=========================================================================

    std::string current_hash_;                                  ///< Hash from current request
    ImageRequest current_image_request_;                        ///< Current image request parameters
    int total_image_rows_;                                      ///< Total rows in current image
    int rows_completed_;                                        ///< Rows completed in current operation
    float current_progress_;                                    ///< Current operation progress (0-100)
    std::string current_status_;                                ///< Current operation status message

    //=========================================================================
    // TIMING AND CONTROL
    //=========================================================================

    static const unsigned long YIELD_INTERVAL_MS = 50;         ///< Yield every 50ms
    static const unsigned long STATE_TIMEOUT_MS = 30000;       ///< 30 second state timeout
    static const unsigned long NETWORK_TIMEOUT_MS = 10000;     ///< 10 second network timeout

    static const char* TAG;                                     ///< Logging tag

    //=========================================================================
    // STATE MACHINE IMPLEMENTATION
    //=========================================================================

    /**
     * @brief Transition to new state with logging and callbacks
     * @param new_state State to transition to
     */
    void transition_to_state(UpdateState new_state);

    /**
     * @brief Check if control should be yielded to ESPHome main loop
     * @return True if control should be yielded
     */
    bool should_yield_control();

    /**
     * @brief Check if current state has timed out
     * @return True if state has exceeded timeout
     */
    bool has_state_timed_out();

    //=========================================================================
    // STATE HANDLERS
    //=========================================================================

    void handle_idle_state();                                  ///< Handle IDLE state
    void handle_wifi_wait_state();                             ///< Handle WIFI_WAIT state
    void handle_hash_check_state();                            ///< Handle HASH_CHECK state
    void handle_hash_request_state();                          ///< Handle HASH_REQUEST state
    void handle_hash_parse_state();                            ///< Handle HASH_PARSE state
    void handle_image_request_state();                         ///< Handle IMAGE_REQUEST state
    void handle_image_download_state();                        ///< Handle IMAGE_DOWNLOAD state
    void handle_image_parse_state();                           ///< Handle IMAGE_PARSE state
    void handle_image_display_state();                         ///< Handle IMAGE_DISPLAY state
    void handle_display_update_state();                        ///< Handle DISPLAY_UPDATE state
    void handle_error_display_state();                         ///< Handle ERROR_DISPLAY state
    void handle_sleep_prepare_state();                         ///< Handle SLEEP_PREPARE state
    void handle_complete_state();                              ///< Handle COMPLETE state

    //=========================================================================
    // NETWORK CALLBACK HANDLERS
    //=========================================================================

    /**
     * @brief Handle hash request response
     * @param result Network operation result
     */
    void on_hash_response(NetworkResult result);

    /**
     * @brief Handle image request response
     * @param result Network operation result
     */
    void on_image_response(NetworkResult result);

    /**
     * @brief Handle log post response
     * @param result Network operation result
     */
    void on_log_response(NetworkResult result);

    /**
     * @brief Handle sleep interval response
     * @param result Network operation result
     */
    void on_sleep_response(NetworkResult result);

    /**
     * @brief Handle socket data stream
     * @param data Received data buffer
     * @param length Length of received data
     */
    void on_socket_data(const uint8_t* data, int length);

    //=========================================================================
    // ERROR HANDLING
    //=========================================================================

    /**
     * @brief Handle error condition with logging and state transition
     * @param error_type Type of error that occurred
     * @param details Detailed error description
     */
    void handle_error(ErrorType error_type, const std::string& details);

    /**
     * @brief Display error on screen and prepare for sleep
     * @param error_type Error type to display
     * @param details Error details
     */
    void display_error_and_sleep(ErrorType error_type, const std::string& details);

    //=========================================================================
    // IMAGE PROCESSING HELPERS
    //=========================================================================

    /**
     * @brief Calculate image processing parameters
     */
    void calculate_image_parameters();

    /**
     * @brief Validate received image data
     * @param data Image data buffer
     * @param size Data buffer size
     * @return True if data appears valid
     */
    bool validate_image_data(const uint8_t* data, int size);

    /**
     * @brief Process image data in memory-efficient chunks
     * @param data Complete image data
     * @param size Data size
     * @return True if processing was successful
     */
    bool process_image_in_chunks(const uint8_t* data, int size);

    //=========================================================================
    // DEEP SLEEP MANAGEMENT
    //=========================================================================

    /**
     * @brief Check if device should enter deep sleep
     * @return True if all conditions are met for deep sleep
     */
    bool should_enter_deep_sleep();

    /**
     * @brief Prepare for deep sleep and enter sleep mode
     */
    void prepare_and_enter_deep_sleep();

    /**
     * @brief Post status message to server
     * @param message Status message to log
     */
    void post_status_to_server(const std::string& message);

    //=========================================================================
    // UTILITY METHODS
    //=========================================================================

    /**
     * @brief Initialize all sub-components
     */
    void initialize_components();

    /**
     * @brief Update progress information
     * @param percentage Progress percentage
     * @param status Status message
     */
    void update_progress(float percentage, const std::string& status);

    /**
     * @brief Log state transition with timing information
     * @param from_state Previous state
     * @param to_state New state
     */
    void log_state_transition(UpdateState from_state, UpdateState to_state);

    /**
     * @brief Log message through callback and ESP_LOG
     * @param level Log level (LOGD, LOGI, LOGW, LOGE)
     * @param message Message to log
     */
    void log_message(int level, const std::string& message);

    /**
     * @brief Validate component configuration
     * @return True if all required components are configured
     */
    bool validate_configuration();

    /**
     * @brief Reset operation state for new cycle
     */
    void reset_operation_state();
};

/**
 * @brief Factory function to create WebInk controller with default configuration
 * @return Shared pointer to configured WebInkController
 */
std::shared_ptr<WebInkController> create_webink_controller();

} // namespace webink
} // namespace esphome
