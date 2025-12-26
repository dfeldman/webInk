/**
 * @file webink_esphome.h
 * @brief ESPHome integration wrapper for WebInk component
 * 
 * This file provides the ESPHome component wrapper that integrates the 
 * WebInk C++ library with ESPHome's component system.
 */

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"
#include "esphome/components/deep_sleep/deep_sleep.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

#ifdef USE_ESP32
#include "esp_sleep.h"
#include "esp_system.h"
#endif
#include "esphome/components/wifi/wifi_component.h"

#include "webink.h"

namespace esphome {
namespace webink {

/**
 * @class ESPHomeWebInkDisplay
 * @brief Display manager that bridges WebInk to ESPHome display components
 */
class ESPHomeWebInkDisplay : public WebInkDisplayManager {
 public:
  ESPHomeWebInkDisplay(display::Display* display, font::Font* normal_font = nullptr, font::Font* large_font = nullptr);

  // Display interface implementation
  void clear_display() override;
  void draw_pixel(int x, int y, uint32_t color) override;
  void update_display() override;
  void get_display_size(int& width, int& height) override;

 protected:
  // Drawing primitives
  void draw_text(int x, int y, const std::string& text, bool large = false, int alignment = 1) override;
  void draw_rectangle(int x, int y, int width, int height, bool filled = false) override;
  void draw_line(int x1, int y1, int x2, int y2) override;

 private:
  display::Display* display_;
  font::Font* normal_font_;
  font::Font* large_font_;
};

/**
 * @class WebInkESPHomeComponent  
 * @brief Main ESPHome component class for WebInk integration
 */
class WebInkESPHomeComponent : public Component {
 public:
  WebInkESPHomeComponent();

  // ESPHome component lifecycle
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  // Configuration setters (called from Python codegen)
  void set_server_url(const std::string& url) { server_url_ = url; }
  void set_device_id(const std::string& id) { device_id_ = id; }
  void set_api_key(const std::string& key) { api_key_ = key; }
  void set_display_mode(const std::string& mode) { display_mode_ = mode; }
  void set_socket_port(int port) { socket_port_ = port; }
  void set_rows_per_slice(int rows) { rows_per_slice_ = rows; }

  // Component references (called from Python codegen)
  void set_display_component(display::Display* display) { display_component_ = display; }
  void set_normal_font(font::Font* font) { normal_font_ = font; }
  void set_large_font(font::Font* font) { large_font_ = font; }
  void set_deep_sleep_component(deep_sleep::DeepSleepComponent* sleep) { deep_sleep_component_ = sleep; }
  void set_boot_button(binary_sensor::BinarySensor* button) { boot_button_ = button; }

  // Public API for sensors and controls
  WebInkController* get_controller() { return controller_.get(); }
  std::string get_status_string();
  std::string get_current_state_string();
  std::string get_last_hash();
  float get_wake_counter();
  float get_boot_cycles();
  bool get_progress_info(float& progress, std::string& status);
  
  // Control methods
  bool trigger_manual_update();
  void clear_hash_force_update();
  bool trigger_deep_sleep();
  bool is_deep_sleep_enabled();
  void enable_deep_sleep(bool enabled);
  
  // Configuration accessors
  std::string get_server_url();
  std::string get_device_id();
  std::string get_display_mode();
  int get_socket_port();
  void update_server_url(const std::string& url);
  void update_device_id(const std::string& id);
  void update_display_mode(const std::string& mode);
  void update_socket_port(int port);

 private:
  void initialize_webink_controller();
  void setup_esphome_callbacks();

  // Configuration
  std::string server_url_;
  std::string device_id_;
  std::string api_key_;
  std::string display_mode_;
  int socket_port_;
  int rows_per_slice_;

  // ESPHome component references
  display::Display* display_component_;
  font::Font* normal_font_;
  font::Font* large_font_;
  deep_sleep::DeepSleepComponent* deep_sleep_component_;
  binary_sensor::BinarySensor* boot_button_;

  // WebInk components
  std::shared_ptr<WebInkConfig> config_;
  std::shared_ptr<ESPHomeWebInkDisplay> display_manager_;
  std::shared_ptr<WebInkController> controller_;

  // Deep sleep state management
  bool setup_complete_;
  bool is_wake_from_deep_sleep_;           ///< True if this boot was from deep sleep wake
  unsigned long initial_boot_time_;        ///< Time of initial boot (millis)
  bool initial_boot_no_sleep_period_;      ///< True during 5-minute no-sleep window
  bool deep_sleep_allowed_;                ///< True if deep sleep is currently allowed
  unsigned long last_error_time_;          ///< Time of last error (prevents deep sleep)
  
  static constexpr unsigned long INITIAL_BOOT_NO_SLEEP_MS = 5 * 60 * 1000;  // 5 minutes
  static constexpr unsigned long ERROR_NO_SLEEP_MS = 2 * 60 * 1000;         // 2 minutes after error

private:
  // Deep sleep helper methods
  void setup_deep_sleep_logic();
  void check_deep_sleep_trigger();
  bool can_enter_deep_sleep() const;
  void prepare_for_deep_sleep();
  
  // Server logging helper
  void post_critical_log_to_server(const std::string& message);
};

} // namespace webink
} // namespace esphome
