/**
 * @file webink_state.h
 * @brief State persistence manager for WebInk component
 * 
 * The WebInkState class manages all state information that needs to persist
 * across deep sleep cycles and power loss events. It handles both persistent
 * variables (that survive deep sleep) and session variables (reset on boot).
 * 
 * This class replaces the global variables from the original YAML configuration
 * and provides a clean interface for state management with deep sleep safety.
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
unsigned long millis(); // Add millis() declaration for Mac
#else
// Normal ESPHome mode
#include "esphome/core/log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#endif

#include "webink_types.h"

#ifndef WEBINK_MAC_INTEGRATION_TEST
// ESP32 specific headers (only for actual ESP32 build)
#include <esp_system.h>
#include <esp_sleep.h>
#endif

namespace esphome {
namespace webink {

/**
 * @class WebInkState
 * @brief Manages persistent and session state for WebInk operations
 * 
 * This class encapsulates all state management for the WebInk component,
 * including variables that need to persist across deep sleep cycles and
 * those that are reset on each boot.
 * 
 * Key features:
 * - Deep sleep state persistence using ESPHome restore_value mechanism
 * - Boot time tracking for 5-minute OTA protection window
 * - Wake counter and cycle management
 * - Hash-based change detection
 * - Error state tracking
 * - Sleep duration management
 * 
 * @example Basic Usage
 * @code
 * WebInkState state;
 * state.increment_wake_counter();
 * if (state.can_deep_sleep(boot_button_pressed, millis())) {
 *     // Safe to enter deep sleep
 * }
 * @endcode
 */
class WebInkState {
public:
    /**
     * @brief Default constructor initializes all state variables
     */
    WebInkState();

    /**
     * @brief Default destructor
     */
    ~WebInkState() = default;

    //=========================================================================
    // PERSISTENT STATE (survives deep sleep cycles)
    //=========================================================================
    
    /// Hash of last displayed content (used for change detection) - fixed size to avoid allocation
    char last_hash[16];         // Enough for typical hash (8-12 chars + null terminator)
    
    /// All-time wake counter (increments on every wake, persists forever)
    int wake_counter{0};
    
    /// Sleep duration in seconds (fetched from server, default 60)
    int sleep_duration_seconds{60};
    
    /// Global flag to enable/disable deep sleep (controllable via web UI)
    bool deep_sleep_enabled{true};
    
    /// Error message from last cycle (for debugging) - fixed size to avoid allocation
    char error_message[128];    // Fixed size for error messages
    
    /// Flag to track if error screen is currently displayed
    bool error_screen_displayed{false};

    //=========================================================================
    // SESSION STATE (reset on power-on, persists across deep sleep)
    //=========================================================================
    
    /// Cycles since boot counter (resets on power loss, persists across deep sleep)
    int cycles_since_boot{0};
    
    /// Boot time in milliseconds (when device was powered on)
    unsigned long boot_time{0};
    
    /// Last update time in milliseconds (when last update cycle started)
    unsigned long last_update_time{0};
    
    /// Flag to track if last cycle had WiFi or server error
    bool last_cycle_had_error{false};
    
    /// Current slice being processed (for sliced image downloads)
    int current_slice{0};
    
    /// Flag to indicate if we should sleep after this cycle
    bool should_sleep{false};
    
    /// Current error type (if any)
    ErrorType current_error{ErrorType::NONE};

    //=========================================================================
    // STATE MANAGEMENT METHODS
    //=========================================================================

    /**
     * @brief Increment wake counter and cycles since boot
     * 
     * Call this at the start of each update cycle to track device usage
     * and cycle counts. This is used for logging and diagnostics.
     */
    void increment_wake_counter();

    /**
     * @brief Record boot time for safety calculations
     * @param time Current time in milliseconds from millis()
     * 
     * Boot time is used to enforce the 5-minute safety window after
     * power-on to prevent immediate deep sleep (OTA safety feature).
     */
    void record_boot_time(unsigned long time);

    /**
     * @brief Record when the current update cycle started
     * @param time Current time in milliseconds from millis()
     * 
     * Used to track intervals between updates and for timeout calculations.
     */
    void record_update_time(unsigned long time);

    /**
     * @brief Clear all error flags at start of new cycle
     * 
     * Resets error state to prepare for a new update attempt.
     * Called at the beginning of each update cycle.
     */
    void clear_error_flags();

    /**
     * @brief Set error state with specific error type
     * @param error The type of error that occurred
     * @param message Human-readable error description
     */
    void set_error(ErrorType error, const char* message);

    //=========================================================================
    // DEEP SLEEP SAFETY METHODS
    //=========================================================================

    /**
     * @brief Determine if it's safe to enter deep sleep
     * @param boot_button_pressed True if BOOT button is currently pressed
     * @param current_time Current time in milliseconds from millis()
     * @return True if all safety conditions are met for deep sleep
     * 
     * This method implements comprehensive safety checks to prevent
     * deep sleep in unsafe conditions:
     * - Deep sleep must be enabled in configuration
     * - Sleep duration must be > 0 (server can disable by setting to 0)
     * - BOOT button must not be held down (emergency override)
     * - Previous cycle must not have had errors (for debugging)
     * - Must be outside 5-minute boot protection window (OTA safety)
     */
    bool can_deep_sleep(bool boot_button_pressed, unsigned long current_time) const;

    /**
     * @brief Calculate time elapsed since boot
     * @param current_time Current time in milliseconds from millis()
     * @return Time since boot in milliseconds
     */
    unsigned long time_since_boot(unsigned long current_time) const;

    /**
     * @brief Check if we're within the 5-minute boot protection period
     * @param current_time Current time in milliseconds from millis()
     * @return True if within protection period
     * 
     * The boot protection period prevents deep sleep for 5 minutes after
     * power-on (not after deep sleep wake) to ensure OTA updates can be
     * applied even if the device has a bug that causes immediate sleep.
     */
    bool within_boot_protection_period(unsigned long current_time) const;

    /**
     * @brief Get reset reason and determine if this was a deep sleep wake
     * @return True if device woke from deep sleep, false if power-on/reset
     */
    bool is_deep_sleep_wake() const;

    //=========================================================================
    // UPDATE CYCLE MANAGEMENT
    //=========================================================================

    /**
     * @brief Check if enough time has elapsed for the next update
     * @param current_time Current time in milliseconds from millis()
     * @return True if it's time for the next update cycle
     */
    bool should_start_update_cycle(unsigned long current_time) const;

    /**
     * @brief Get formatted status string for logging
     * @return Human-readable status string with key metrics
     */
    std::string get_status_string() const;

    /**
     * @brief Get sleep duration in milliseconds for deep sleep component
     * @return Sleep duration converted to milliseconds
     */
    unsigned long get_sleep_duration_ms() const;

    //=========================================================================
    // HASH MANAGEMENT
    //=========================================================================

    /**
     * @brief Check if hash has changed since last update
     * @param new_hash Hash received from server
     * @return True if hash is different from stored hash
     */
    bool has_hash_changed(const char* new_hash) const;

    /**
     * @brief Update stored hash to new value
     * @param new_hash Hash value to store (will be safely copied to fixed array)
     */
    void update_hash(const char* new_hash);

    /**
     * @brief Force hash change by clearing stored hash
     * 
     * This forces the next update cycle to refresh the display
     * regardless of server hash. Used for manual refresh requests.
     */
    void clear_hash_force_update();
    
    /**
     * @brief Get hash as null-terminated string
     * @return Pointer to hash character array
     */
    const char* get_hash() const { return last_hash; }

private:
    static const char* TAG;  ///< Logging tag for this class
    static const unsigned long BOOT_PROTECTION_MS = 5 * 60 * 1000;  ///< 5 minutes
};

} // namespace webink
} // namespace esphome
