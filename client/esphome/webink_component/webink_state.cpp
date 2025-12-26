/**
 * @file webink_state.cpp
 * @brief Implementation of WebInkState class for state persistence management
 * 
 * @author WebInk Component Authors
 * @version 1.0.0
 */

#include "webink_state.h"

namespace esphome {
namespace webink {

const char* WebInkState::TAG = "webink.state";

//=============================================================================
// CONSTRUCTOR AND DESTRUCTOR
//=============================================================================

WebInkState::WebInkState() {
    // Initialize fixed char arrays
    strcpy(last_hash, "00000000");  // Default hash value
    error_message[0] = '\0';        // Empty error message
    
    ESP_LOGD(TAG, "WebInkState initialized with fixed arrays (no dynamic allocation)");
}

//=============================================================================
// STATE MANAGEMENT METHODS
//=============================================================================

void WebInkState::increment_wake_counter() {
    wake_counter++;
    cycles_since_boot++;
    
    ESP_LOGI(TAG, "Wake counter: %d, Cycles since boot: %d", 
             wake_counter, cycles_since_boot);
}

void WebInkState::record_boot_time(unsigned long time) {
    // Only update boot time on actual power-on (not deep sleep wake)
    if (!is_deep_sleep_wake()) {
        boot_time = time;
        ESP_LOGI(TAG, "Boot time recorded: %lu ms (power-on detected)", boot_time);
    } else {
        ESP_LOGI(TAG, "Deep sleep wake detected, keeping existing boot time: %lu ms", boot_time);
    }
}

void WebInkState::record_update_time(unsigned long time) {
    last_update_time = time;
    ESP_LOGD(TAG, "Update time recorded: %lu ms", last_update_time);
}

void WebInkState::clear_error_flags() {
    last_cycle_had_error = false;
    error_screen_displayed = false;
    current_error = ErrorType::NONE;
    error_message[0] = '\0';  // Clear error message array
    
    ESP_LOGD(TAG, "Error flags cleared");
}

void WebInkState::set_error(ErrorType error, const char* message) {
    current_error = error;
    
    // Safe copy to fixed array
    if (message) {
        strncpy(error_message, message, sizeof(error_message) - 1);
        error_message[sizeof(error_message) - 1] = '\0';  // Ensure null termination
    } else {
        error_message[0] = '\0';
    }
    
    last_cycle_had_error = true;
    
    ESP_LOGE(TAG, "Error set: %s - %s", 
             error_type_to_string(error), error_message);
}

//=============================================================================
// DEEP SLEEP SAFETY METHODS
//=============================================================================

bool WebInkState::can_deep_sleep(bool boot_button_pressed, unsigned long current_time) const {
    // Check deep sleep enabled flag
    if (!deep_sleep_enabled) {
        ESP_LOGW(TAG, "[SLEEP] Deep sleep disabled via configuration");
        return false;
    }
    
    // Check sleep duration (server can disable by setting to 0)
    if (sleep_duration_seconds == 0) {
        ESP_LOGW(TAG, "[SLEEP] Sleep interval is 0 - server signal to disable sleep");
        return false;
    }
    
    // Check BOOT button (emergency override)
    if (boot_button_pressed) {
        ESP_LOGW(TAG, "[SLEEP] BOOT button held - safety override");
        return false;
    }
    
    // Check for recent errors
    if (last_cycle_had_error) {
        ESP_LOGW(TAG, "[SLEEP] Last cycle had error - staying awake for troubleshooting");
        return false;
    }
    
    // Check boot protection period
    if (within_boot_protection_period(current_time)) {
        unsigned long remaining_ms = BOOT_PROTECTION_MS - time_since_boot(current_time);
        ESP_LOGW(TAG, "[SLEEP] Within 5-minute boot protection period - %lu seconds remaining", 
                 remaining_ms / 1000);
        return false;
    }
    
    ESP_LOGI(TAG, "[SLEEP] All safety checks passed - can enter deep sleep for %d seconds", 
             sleep_duration_seconds);
    return true;
}

unsigned long WebInkState::time_since_boot(unsigned long current_time) const {
    if (boot_time == 0 || current_time < boot_time) {
        return 0;
    }
    return current_time - boot_time;
}

bool WebInkState::within_boot_protection_period(unsigned long current_time) const {
    // Only enforce protection period for power-on (not deep sleep wake)
    if (is_deep_sleep_wake()) {
        return false;
    }
    
    return time_since_boot(current_time) < BOOT_PROTECTION_MS;
}

bool WebInkState::is_deep_sleep_wake() const {
#ifdef WEBINK_MAC_INTEGRATION_TEST
    // Mac integration test - simulate not coming from deep sleep
    return false;
#else
    esp_reset_reason_t reset_reason = esp_reset_reason();
    return (reset_reason == ESP_RST_DEEPSLEEP);
#endif
}

//=============================================================================
// UPDATE CYCLE MANAGEMENT
//=============================================================================

bool WebInkState::should_start_update_cycle(unsigned long current_time) const {
    // Always start on first boot
    if (wake_counter == 0) {
        return true;
    }
    
    // Check if enough time has elapsed
    unsigned long elapsed_ms = current_time - last_update_time;
    unsigned long interval_ms = sleep_duration_seconds * 1000;
    
    return elapsed_ms >= interval_ms;
}

std::string WebInkState::get_status_string() const {
    // Use smaller buffer and avoid std::string if possible in critical paths
    static char buffer[256];  // Static to avoid stack allocation each call
    
    unsigned long time_since_boot_sec = time_since_boot(millis()) / 1000;
    
    snprintf(buffer, sizeof(buffer),
             "[STATUS] Wake #%d, Boot cycle #%d, %lu sec since boot, "
             "Hash: %s, Sleep: %ds, Errors: %s",
             wake_counter,
             cycles_since_boot, 
             time_since_boot_sec,
             last_hash,  // Now a char array, no .c_str() needed
             sleep_duration_seconds,
             last_cycle_had_error ? "YES" : "NO");
    
    return std::string(buffer);
}

unsigned long WebInkState::get_sleep_duration_ms() const {
    return static_cast<unsigned long>(sleep_duration_seconds) * 1000;
}

//=============================================================================
// HASH MANAGEMENT
//=============================================================================

bool WebInkState::has_hash_changed(const char* new_hash) const {
    if (!new_hash) return false;
    
    bool changed = (strcmp(new_hash, last_hash) != 0);
    
    if (changed) {
        ESP_LOGI(TAG, "[HASH] Hash changed - Old: %s, New: %s", 
                 last_hash, new_hash);
    } else {
        ESP_LOGI(TAG, "[HASH] Hash unchanged: %s", last_hash);
    }
    
    return changed;
}

void WebInkState::update_hash(const char* new_hash) {
    if (!new_hash) return;
    
    char old_hash[sizeof(last_hash)];
    strcpy(old_hash, last_hash);  // Save old value for logging
    
    // Safe copy to fixed array
    strncpy(last_hash, new_hash, sizeof(last_hash) - 1);
    last_hash[sizeof(last_hash) - 1] = '\0';  // Ensure null termination
    
    ESP_LOGI(TAG, "[HASH] Updated - Old: %s, New: %s", old_hash, last_hash);
}

void WebInkState::clear_hash_force_update() {
    char old_hash[sizeof(last_hash)];
    strcpy(old_hash, last_hash);  // Save old value for logging
    strcpy(last_hash, "00000000");  // Reset to default value
    
    ESP_LOGI(TAG, "[HASH] Cleared for forced update - Old: %s, New: %s", 
             old_hash, last_hash);
}

} // namespace webink
} // namespace esphome
