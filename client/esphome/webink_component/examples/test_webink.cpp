/**
 * @file test_webink.cpp
 * @brief Simple test program for WebInk component (for development/testing)
 * 
 * This file demonstrates how to use the WebInk component programmatically
 * and serves as a test harness for development and debugging.
 * 
 * Note: This is for testing purposes only and is not part of the normal
 * ESPHome integration. It shows the C++ API usage patterns.
 */

#include "webink.h"
#include <iostream>
#include <chrono>
#include <thread>

using namespace esphome::webink;

/**
 * Mock display manager for testing
 */
class MockDisplayManager : public WebInkDisplayManager {
public:
    MockDisplayManager() : width_(800), height_(480) {
        std::cout << "[DISPLAY] Mock display manager created (800x480)" << std::endl;
    }
    
    void clear_display() override {
        std::cout << "[DISPLAY] Screen cleared" << std::endl;
    }
    
    void draw_pixel(int x, int y, uint32_t color) override {
        // Only log occasionally to avoid spam
        static int pixel_count = 0;
        if (pixel_count++ % 1000 == 0) {
            std::cout << "[DISPLAY] Drawing pixels... (" << pixel_count << " total)" << std::endl;
        }
    }
    
    void update_display() override {
        std::cout << "[DISPLAY] Physical display update triggered (simulated 3s e-ink refresh)" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        std::cout << "[DISPLAY] Display update complete" << std::endl;
    }
    
    void get_display_size(int& width, int& height) override {
        width = width_;
        height = height_;
    }
    
protected:
    void draw_text(int x, int y, const std::string& text, bool large = false, int alignment = 1) override {
        std::cout << "[DISPLAY] Text at (" << x << "," << y << "): \"" << text << "\"" 
                  << (large ? " [LARGE]" : " [normal]") << std::endl;
    }
    
private:
    int width_, height_;
};

/**
 * Mock WiFi status function
 */
bool mock_wifi_connected = false;
bool get_mock_wifi_status() {
    return mock_wifi_connected;
}

/**
 * Mock boot button function
 */
bool get_mock_boot_button() {
    return false; // Never pressed in test
}

/**
 * Test the WebInk component
 */
int main() {
    std::cout << "=== WebInk Component Test Program ===" << std::endl;
    
    try {
        // Create configuration
        auto config = std::make_shared<WebInkConfig>();
        config->set_server_url("http://localhost:8090");
        config->set_device_id("test-device");
        config->set_api_key("test-key");
        config->set_display_mode("800x480x1xB");
        config->set_socket_port(0); // Use HTTP mode for testing
        
        std::cout << "[CONFIG] " << config->get_config_summary() << std::endl;
        
        // Create display manager
        auto display = std::make_shared<MockDisplayManager>();
        
        // Create controller
        auto controller = create_webink_controller();
        controller->set_config(config);
        controller->set_display(display);
        
        // Set up callbacks
        controller->get_wifi_status = get_mock_wifi_status;
        controller->get_boot_button_status = get_mock_boot_button;
        
        controller->on_log_message = [](const std::string& msg) {
            std::cout << "[LOG] " << msg << std::endl;
        };
        
        controller->on_state_change = [](UpdateState from, UpdateState to) {
            std::cout << "[STATE] " << update_state_to_string(from) 
                      << " -> " << update_state_to_string(to) << std::endl;
        };
        
        controller->on_progress_update = [](float progress, const std::string& status) {
            std::cout << "[PROGRESS] " << (int)progress << "% - " << status << std::endl;
        };
        
        controller->on_error_occurred = [](ErrorType error, const std::string& details) {
            std::cout << "[ERROR] " << error_type_to_string(error) << ": " << details << std::endl;
        };
        
        // Initialize component
        std::cout << "\n=== Component Setup ===" << std::endl;
        controller->setup();
        
        // Test configuration changes
        std::cout << "\n=== Configuration Test ===" << std::endl;
        controller->set_server_url("http://new-server:8090");
        controller->set_device_id("updated-device");
        
        // Test status queries
        std::cout << "\n=== Status Queries ===" << std::endl;
        std::cout << "Current state: " << update_state_to_string(controller->get_current_state()) << std::endl;
        std::cout << "Update in progress: " << (controller->is_update_in_progress() ? "Yes" : "No") << std::endl;
        std::cout << "Status: " << controller->get_status_string() << std::endl;
        
        // Simulate WiFi connection and run update cycle
        std::cout << "\n=== Simulated Update Cycle ===" << std::endl;
        mock_wifi_connected = false;
        
        // Trigger update without WiFi (should show error)
        controller->trigger_manual_update();
        
        // Run some loop iterations
        for (int i = 0; i < 50; i++) {
            controller->loop();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Connect WiFi after a delay
            if (i == 10) {
                std::cout << "[TEST] Simulating WiFi connection..." << std::endl;
                mock_wifi_connected = true;
            }
        }
        
        // Test error display
        std::cout << "\n=== Error Display Test ===" << std::endl;
        display->draw_error_message(ErrorType::SERVER_UNREACHABLE, 
                                   "Test error message for demonstration");
        
        // Test WiFi setup display
        std::cout << "\n=== WiFi Setup Display Test ===" << std::endl;
        display->draw_wifi_setup_message();
        
        // Test progress display
        std::cout << "\n=== Progress Display Test ===" << std::endl;
        for (int p = 0; p <= 100; p += 25) {
            display->draw_progress_indicator(p, "Processing step " + std::to_string(p/25 + 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        // Test image processor
        std::cout << "\n=== Image Processor Test ===" << std::endl;
        
        // Test memory calculations
        int bytes_per_row = WebInkImageProcessor::calculate_bytes_per_row(800, ColorMode::MONO_BLACK_WHITE);
        int max_rows = WebInkImageProcessor::calculate_max_rows_for_memory(800, ColorMode::MONO_BLACK_WHITE, 700);
        
        std::cout << "Bytes per row (800px B&W): " << bytes_per_row << std::endl;
        std::cout << "Max rows in 700 bytes: " << max_rows << std::endl;
        
        // Test configuration validation
        std::cout << "\n=== Configuration Validation Test ===" << std::endl;
        char error_buffer[128];
        if (config->validate_configuration(error_buffer, sizeof(error_buffer))) {
            std::cout << "Configuration is valid" << std::endl;
        } else {
            std::cout << "Configuration error: " << error_buffer << std::endl;
        }
        
        // Test state persistence
        std::cout << "\n=== State Management Test ===" << std::endl;
        auto& state = controller->get_state();
        state.increment_wake_counter();
        state.update_hash("test-hash-12345");
        
        std::cout << "Wake counter: " << state.wake_counter << std::endl;
        std::cout << "Current hash: " << state.get_hash() << std::endl;
        std::cout << "Can deep sleep: " << (state.can_deep_sleep(false, 0) ? "Yes" : "No") << std::endl;
        
        // Test force refresh
        std::cout << "\n=== Force Refresh Test ===" << std::endl;
        controller->clear_hash_force_update();
        std::cout << "Hash after clear: " << state.get_hash() << std::endl;
        
        std::cout << "\n=== Test Complete ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

/**
 * Compile instructions:
 * 
 * g++ -std=c++17 -I. -I../include \
 *     test_webink.cpp \
 *     webink_types.cpp \
 *     webink_state.cpp \
 *     webink_config.cpp \
 *     webink_network.cpp \
 *     webink_image.cpp \
 *     webink_display.cpp \
 *     webink_controller.cpp \
 *     -o test_webink
 * 
 * Note: This requires ESPHome headers and may need adaptation for 
 *       standalone compilation. It's primarily for development testing.
 */
