/**
 * @file webink_esphome.cpp
 * @brief ESPHome integration wrapper implementation
 */

#include "webink_esphome.h"
#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"

namespace esphome {
namespace webink {

static const char* TAG = "webink.esphome";

//=============================================================================
// ESPHomeWebInkDisplay Implementation
//=============================================================================

ESPHomeWebInkDisplay::ESPHomeWebInkDisplay(display::Display* display, font::Font* normal_font, font::Font* large_font)
    : display_(display), normal_font_(normal_font), large_font_(large_font) {
}

void ESPHomeWebInkDisplay::clear_display() {
  if (display_) {
    display_->fill(display::COLOR_ON);  // Clear to white for e-ink
  }
}

void ESPHomeWebInkDisplay::draw_pixel(int x, int y, uint32_t color) {
  if (display_) {
    auto esp_color = (color == 0) ? display::COLOR_ON : display::COLOR_OFF;
    display_->draw_pixel_at(x, y, esp_color);
  }
}

void ESPHomeWebInkDisplay::update_display() {
  if (display_) {
    display_->update();
  }
}

void ESPHomeWebInkDisplay::get_display_size(int& width, int& height) {
  if (display_) {
    width = display_->get_width();
    height = display_->get_height();
  } else {
    width = 800;  // Default fallback
    height = 480;
  }
}

void ESPHomeWebInkDisplay::draw_text(int x, int y, const std::string& text, bool large, int alignment) {
  if (!display_) return;
  
  font::Font* font = large ? large_font_ : normal_font_;
  if (!font) return;
  
  display::TextAlign align = display::TextAlign::CENTER;
  if (alignment == 0) align = display::TextAlign::TOP_LEFT;
  else if (alignment == 2) align = display::TextAlign::TOP_RIGHT;
  
  display_->printf(x, y, font, display::COLOR_OFF, align, "%s", text.c_str());
}

void ESPHomeWebInkDisplay::draw_rectangle(int x, int y, int width, int height, bool filled) {
  if (!display_) return;
  
  if (filled) {
    display_->filled_rectangle(x, y, width, height, display::COLOR_OFF);
  } else {
    display_->rectangle(x, y, width, height, display::COLOR_OFF);
  }
}

void ESPHomeWebInkDisplay::draw_line(int x1, int y1, int x2, int y2) {
  if (display_) {
    display_->line(x1, y1, x2, y2, display::COLOR_OFF);
  }
}

//=============================================================================
// WebInkESPHomeComponent Implementation  
//=============================================================================

WebInkESPHomeComponent::WebInkESPHomeComponent()
    : server_url_("http://192.168.68.69:8090")
    , device_id_("webink-esphome") 
    , api_key_("myapikey")
    , display_mode_("800x480x1xB")
    , socket_port_(8091)
    , rows_per_slice_(8)
    , display_component_(nullptr)
    , normal_font_(nullptr)
    , large_font_(nullptr)
    , deep_sleep_component_(nullptr)
    , boot_button_(nullptr)
    , setup_complete_(false)
    , is_wake_from_deep_sleep_(false)
    , initial_boot_time_(0)
    , initial_boot_no_sleep_period_(true)
    , deep_sleep_allowed_(false)
    , last_error_time_(0) {
}

void WebInkESPHomeComponent::setup() {
  ESP_LOGI(TAG, "Setting up WebInk component...");
  
  // Initialize deep sleep logic first (before WebInk controller)
  setup_deep_sleep_logic();
  
  initialize_webink_controller();
  setup_esphome_callbacks();
  
  setup_complete_ = true;
  ESP_LOGI(TAG, "WebInk component setup complete");
  
  // 🚀 CRITICAL LOG: Component initialized
  // Use a delay to ensure WiFi has time to connect
  set_timeout("post_startup_log", 3000, [this]() {
    std::string startup_log = "STARTUP: Component initialized - Boot type: " + 
                             (is_wake_from_deep_sleep_ ? "Deep sleep wake" : "Cold boot") +
                             ", Wake #" + std::to_string(controller_ ? controller_->get_state().wake_counter : 0);
    post_critical_log_to_server(startup_log);
  });
}

void WebInkESPHomeComponent::loop() {
  if (!setup_complete_ || !controller_) {
    return;
  }
  
  // Let the WebInk controller handle its state machine
  controller_->loop();
  
  // Check deep sleep trigger occasionally (not every loop)
  check_deep_sleep_trigger();
}

void WebInkESPHomeComponent::initialize_webink_controller() {
  // Create configuration
  config_ = std::make_shared<WebInkConfig>();
  config_->set_server_url(server_url_.c_str());
  config_->set_device_id(device_id_.c_str());
  config_->set_api_key(api_key_.c_str());
  config_->set_display_mode(display_mode_.c_str());
  config_->set_socket_port(socket_port_);
  
  // Create display manager
  display_manager_ = std::make_shared<ESPHomeWebInkDisplay>(display_component_, normal_font_, large_font_);
  
  // Create controller
  controller_ = create_webink_controller();
  if (!controller_) {
    ESP_LOGE(TAG, "Failed to create WebInk controller!");
    // Cannot post to server yet since controller is needed
    return;
  }
  
  controller_->set_config(config_);
  controller_->set_display(display_manager_);
  
  // Deep sleep integration is handled by setup_deep_sleep_logic() in setup()
  if (deep_sleep_component_) {
    ESP_LOGD(TAG, "Deep sleep component will be managed by WebInk logic");
  } else {
    ESP_LOGW(TAG, "No deep sleep component configured - device will stay awake");
  }
  
  ESP_LOGI(TAG, "WebInk controller initialized with server: %s", server_url_.c_str());
}

void WebInkESPHomeComponent::setup_esphome_callbacks() {
  if (!controller_) return;
  
  // WiFi status callback
  controller_->get_wifi_status = []() {
    return wifi::global_wifi_component->is_connected();
  };
  
  // Boot button status callback  
  controller_->get_boot_button_status = [this]() {
    if (boot_button_) {
      return boot_button_->state;
    }
    return false;
  };
  
  // Logging callback
  controller_->on_log_message = [](const std::string& msg) {
    ESP_LOGI(TAG, "%s", msg.c_str());
  };
  
  // State change callback
  controller_->on_state_change = [](UpdateState from, UpdateState to) {
    ESP_LOGI(TAG, "State transition: %s -> %s", 
             update_state_to_string(from),
             update_state_to_string(to));
  };
  
  // Error callback
  controller_->on_error_occurred = [](ErrorType error, const std::string& details) {
    ESP_LOGE(TAG, "WebInk Error [%s]: %s", 
             error_type_to_string(error),
             details.c_str());
  };
}

//=============================================================================
// Public API Implementation
//=============================================================================

std::string WebInkESPHomeComponent::get_status_string() {
  if (controller_) {
    return controller_->get_status_string();
  }
  return "Not initialized";
}

std::string WebInkESPHomeComponent::get_current_state_string() {
  if (controller_) {
    return std::string(update_state_to_string(controller_->get_current_state()));
  }
  return "UNKNOWN";
}

std::string WebInkESPHomeComponent::get_last_hash() {
  if (controller_) {
    return std::string(controller_->get_state().get_hash());
  }
  return "00000000";
}

float WebInkESPHomeComponent::get_wake_counter() {
  if (controller_) {
    return (float)controller_->get_state().wake_counter;
  }
  return 0.0f;
}

float WebInkESPHomeComponent::get_boot_cycles() {
  if (controller_) {
    return (float)controller_->get_state().cycles_since_boot;
  }
  return 0.0f;
}

bool WebInkESPHomeComponent::get_progress_info(float& progress, std::string& status) {
  if (controller_) {
    return controller_->get_progress_info(progress, status);
  }
  progress = 0.0f;
  status = "Not initialized";
  return false;
}

bool WebInkESPHomeComponent::trigger_manual_update() {
  if (controller_) {
    return controller_->trigger_manual_update();
  }
  return false;
}

void WebInkESPHomeComponent::clear_hash_force_update() {
  if (controller_) {
    controller_->clear_hash_force_update();
  }
}

bool WebInkESPHomeComponent::trigger_deep_sleep() {
  if (controller_) {
    return controller_->trigger_deep_sleep();
  }
  return false;
}

bool WebInkESPHomeComponent::is_deep_sleep_enabled() {
  if (controller_) {
    return controller_->get_state().deep_sleep_enabled;
  }
  return true;
}

void WebInkESPHomeComponent::enable_deep_sleep(bool enabled) {
  if (controller_) {
    controller_->enable_deep_sleep(enabled);
  }
}

//=============================================================================
// Configuration Methods
//=============================================================================

std::string WebInkESPHomeComponent::get_server_url() {
  if (config_) {
    return std::string(config_->base_url);
  }
  return server_url_;
}

std::string WebInkESPHomeComponent::get_device_id() {
  if (config_) {
    return std::string(config_->device_id);
  }
  return device_id_;
}

std::string WebInkESPHomeComponent::get_display_mode() {
  if (config_) {
    return std::string(config_->display_mode);
  }
  return display_mode_;
}

int WebInkESPHomeComponent::get_socket_port() {
  if (config_) {
    return config_->socket_mode_port;
  }
  return socket_port_;
}

void WebInkESPHomeComponent::update_server_url(const std::string& url) {
  if (controller_) {
    controller_->set_server_url(url);
  }
}

void WebInkESPHomeComponent::update_device_id(const std::string& id) {
  if (controller_) {
    controller_->set_device_id(id);
  }
}

void WebInkESPHomeComponent::update_display_mode(const std::string& mode) {
  if (controller_) {
    controller_->set_display_mode(mode);
  }
}

void WebInkESPHomeComponent::update_socket_port(int port) {
  if (controller_) {
    controller_->set_socket_port(port);
  }
}

//=============================================================================
// Deep Sleep Logic Implementation
//=============================================================================

void WebInkESPHomeComponent::setup_deep_sleep_logic() {
  initial_boot_time_ = millis();
  
#ifdef USE_ESP32
  // Detect if we woke from deep sleep
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_TIMER:
    case ESP_SLEEP_WAKEUP_GPIO:
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
    case ESP_SLEEP_WAKEUP_ULP:
      is_wake_from_deep_sleep_ = true;
      initial_boot_no_sleep_period_ = false;  // Skip 5-minute rule for wake
      ESP_LOGI(TAG, "Woke from deep sleep (cause: %d)", wakeup_reason);
      break;
      
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
      is_wake_from_deep_sleep_ = false;
      initial_boot_no_sleep_period_ = true;   // Enforce 5-minute rule for cold boot
      ESP_LOGI(TAG, "Cold boot detected - 5-minute no-sleep period active");
      break;
  }
#else
  // Non-ESP32 platforms - always treat as cold boot
  is_wake_from_deep_sleep_ = false;
  initial_boot_no_sleep_period_ = true;
  ESP_LOGI(TAG, "Non-ESP32 platform - deep sleep disabled");
#endif

  deep_sleep_allowed_ = !initial_boot_no_sleep_period_;
  
  ESP_LOGI(TAG, "Deep sleep setup: wake=%s, no_sleep_period=%s, allowed=%s",
           is_wake_from_deep_sleep_ ? "true" : "false",
           initial_boot_no_sleep_period_ ? "true" : "false", 
           deep_sleep_allowed_ ? "true" : "false");
}

void WebInkESPHomeComponent::check_deep_sleep_trigger() {
  if (!deep_sleep_component_) {
    return; // No deep sleep component configured
  }
  
  // Only check periodically (not every loop cycle)
  static unsigned long last_check_time = 0;
  unsigned long now = millis();
  if (now - last_check_time < 10000) { // Check every 10 seconds
    return;
  }
  last_check_time = now;
  
  bool prev_allowed = deep_sleep_allowed_;
  deep_sleep_allowed_ = can_enter_deep_sleep();
  
  if (deep_sleep_allowed_ != prev_allowed) {
    ESP_LOGI(TAG, "Deep sleep state changed: %s -> %s", 
             prev_allowed ? "ALLOWED" : "BLOCKED",
             deep_sleep_allowed_ ? "ALLOWED" : "BLOCKED");
  }
  
  // Trigger deep sleep if conditions are met
  if (deep_sleep_allowed_) {
    // Check if WebInk operations have completed
    if (controller_) {
      UpdateState state = controller_->get_current_state();
      if (state == UpdateState::IDLE || state == UpdateState::COMPLETE) {
        // Get sleep duration from server (stored in WebInk state)
        unsigned long sleep_duration_ms = controller_->get_state().get_sleep_duration_ms();
        int sleep_duration_sec = controller_->get_state().sleep_duration_seconds;
        
        ESP_LOGI(TAG, "WebInk operations complete - entering deep sleep for %d seconds", sleep_duration_sec);
        
        // 🚀 CRITICAL LOG: Entering deep sleep
        std::string sleep_log = "DEEP_SLEEP: Entering " + std::to_string(sleep_duration_sec) + 
                               "s sleep after wake #" + std::to_string(controller_->get_state().wake_counter) +
                               " (state: " + std::string(update_state_to_string(state)) + ")";
        post_critical_log_to_server(sleep_log);
        
        prepare_for_deep_sleep();
        
        // Enter deep sleep with server-provided duration
        // Note: ESPHome deep sleep API may vary - this uses the most common pattern
        #ifdef USE_ESP32
        deep_sleep_component_->begin_sleep(sleep_duration_ms);
        #else
        // For non-ESP32 or if the above API doesn't exist, fall back to default
        deep_sleep_component_->begin_sleep();
        #endif
        // Execution stops here - device enters deep sleep
      }
    }
  } else {
    // 🚨 CRITICAL LOG: Deep sleep blocked
    static unsigned long last_blocked_log_time = 0;
    unsigned long now = millis();
    
    // Only log every 30 seconds to avoid spam
    if (now - last_blocked_log_time > 30000) {
      std::string reason = "Unknown";
      
      if (!setup_complete_ || !controller_) {
        reason = "Component not initialized";
      } else if (initial_boot_no_sleep_period_ && (now - initial_boot_time_) < INITIAL_BOOT_NO_SLEEP_MS) {
        unsigned long remaining_sec = (INITIAL_BOOT_NO_SLEEP_MS - (now - initial_boot_time_)) / 1000;
        reason = "Boot protection (" + std::to_string(remaining_sec) + "s remaining)";
      } else if (last_error_time_ > 0 && (now - last_error_time_) < ERROR_NO_SLEEP_MS) {
        unsigned long error_sec = (now - last_error_time_) / 1000;
        reason = "Error recovery (" + std::to_string(error_sec) + "s since error)";
      } else if (controller_) {
        UpdateState state = controller_->get_current_state();
        reason = "Active operation (" + std::string(update_state_to_string(state)) + ")";
      }
      
      std::string blocked_log = "DEEP_SLEEP: BLOCKED - " + reason;
      post_critical_log_to_server(blocked_log);
      last_blocked_log_time = now;
    }
  }
}

void WebInkESPHomeComponent::post_critical_log_to_server(const std::string& message) {
  // Only post if WiFi is connected and controller is ready
  if (!controller_) return;
  
  // Check if WiFi is available (use the controller's callback)
  if (controller_->get_wifi_status && !controller_->get_wifi_status()) {
    ESP_LOGD(TAG, "Skipping server log - WiFi not connected: %s", message.c_str());
    return;
  }
  
  ESP_LOGI(TAG, "Posting to server: %s", message.c_str());
  controller_->post_status_to_server(message);
}

bool WebInkESPHomeComponent::can_enter_deep_sleep() const {
  unsigned long now = millis();
  
  // 1. Never allow deep sleep if component not initialized
  if (!setup_complete_ || !controller_) {
    return false;
  }
  
  // 2. Never allow deep sleep during the initial 5-minute boot period (cold boot only)
  if (initial_boot_no_sleep_period_ && (now - initial_boot_time_) < INITIAL_BOOT_NO_SLEEP_MS) {
    return false;
  }
  
  // 3. Never allow deep sleep if there was a recent error
  if (last_error_time_ > 0 && (now - last_error_time_) < ERROR_NO_SLEEP_MS) {
    return false;
  }
  
  // 4. Never allow deep sleep if WebInk controller is in an active/busy state
  if (controller_) {
    UpdateState state = controller_->get_current_state();
    switch (state) {
      case UpdateState::CONNECTING:
      case UpdateState::CHECKING_HASH:
      case UpdateState::DOWNLOADING_IMAGE:
      case UpdateState::PROCESSING_IMAGE:
      case UpdateState::UPDATING_DISPLAY:
        return false; // Active operations in progress
        
      case UpdateState::IDLE:
      case UpdateState::COMPLETE:
        break; // These states allow deep sleep
        
      case UpdateState::ERROR:
        // Record error time to prevent deep sleep
        const_cast<WebInkESPHomeComponent*>(this)->last_error_time_ = millis();
        return false;
        
      default:
        return false; // Unknown state - be conservative
    }
  }
  
  // // 5. Check if WiFi is connected (might want to stay awake for debugging)
  // if (!wifi::global_wifi_component || !wifi::global_wifi_component->is_connected()) {
  //   // Allow deep sleep even without WiFi - it will reconnect on wake
  //   // You might want to change this behavior based on your needs
  // }
  
  return true; // All checks passed - deep sleep is allowed
}

void WebInkESPHomeComponent::prepare_for_deep_sleep() {
  if (!deep_sleep_component_ || !can_enter_deep_sleep()) {
    return;
  }
  
  ESP_LOGI(TAG, "Preparing for deep sleep...");
  
  // Perform any necessary cleanup before deep sleep
  if (controller_) {
    // The WebInk controller will automatically save state
    // No explicit prepare_for_sleep() method needed
    ESP_LOGD(TAG, "WebInk controller state preserved");
  }
  
  // Additional cleanup can be added here
  ESP_LOGI(TAG, "Ready for deep sleep");
}

} // namespace webink
} // namespace esphome
