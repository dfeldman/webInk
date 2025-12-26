/**
 * @file webink_display.cpp
 * @brief Implementation of WebInkDisplayManager class for display abstraction and error rendering
 * 
 * @author WebInk Component Authors
 * @version 1.0.0
 */

#include "webink_display.h"
#include <algorithm>
#include <sstream>

namespace esphome {
namespace webink {

const char* WebInkDisplayManager::TAG = "webink.display";

//=============================================================================
// CONSTRUCTOR
//=============================================================================

WebInkDisplayManager::WebInkDisplayManager(std::function<void(const std::string&)> log_callback)
    : log_callback_(log_callback),
      error_screen_displayed_(false),
      normal_font_(nullptr),
      large_font_(nullptr) {
    
    ESP_LOGD(TAG, "WebInkDisplayManager initialized");
}

//=============================================================================
// VIRTUAL INTERFACE IMPLEMENTATIONS
//=============================================================================

void WebInkDisplayManager::draw_text(int x, int y, const std::string& text, bool large, int alignment) {
    // Default implementation - subclasses should override with actual font rendering
    log_message("draw_text called: '" + text + "' at (" + std::to_string(x) + "," + std::to_string(y) + ")");
    
    // This is a placeholder - actual implementation would use ESPHome font components
    // For now, just draw a rectangle where text would be
    int text_width = text.length() * (large ? 12 : 8);
    int text_height = large ? 24 : 16;
    
    // Apply alignment
    int adjusted_x = x;
    if (alignment == 1) {  // Center
        adjusted_x = x - text_width / 2;
    } else if (alignment == 2) {  // Right
        adjusted_x = x - text_width;
    }
    
    draw_rectangle(adjusted_x, y, text_width, text_height, false);
}

void WebInkDisplayManager::draw_rectangle(int x, int y, int width, int height, bool filled) {
    // Default implementation using pixel drawing
    uint32_t color = get_foreground_color();
    
    if (filled) {
        for (int py = y; py < y + height; py++) {
            for (int px = x; px < x + width; px++) {
                draw_pixel(px, py, color);
            }
        }
    } else {
        // Draw outline
        for (int px = x; px < x + width; px++) {
            draw_pixel(px, y, color);                    // Top edge
            draw_pixel(px, y + height - 1, color);       // Bottom edge
        }
        for (int py = y; py < y + height; py++) {
            draw_pixel(x, py, color);                    // Left edge
            draw_pixel(x + width - 1, py, color);        // Right edge
        }
    }
}

void WebInkDisplayManager::draw_circle(int center_x, int center_y, int radius, bool filled) {
    uint32_t color = get_foreground_color();
    
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int distance_squared = x * x + y * y;
            
            if (filled) {
                if (distance_squared <= radius * radius) {
                    draw_pixel(center_x + x, center_y + y, color);
                }
            } else {
                // Draw outline - check if point is on the circle edge
                if (distance_squared >= (radius - 1) * (radius - 1) && 
                    distance_squared <= radius * radius) {
                    draw_pixel(center_x + x, center_y + y, color);
                }
            }
        }
    }
}

void WebInkDisplayManager::draw_line(int x1, int y1, int x2, int y2) {
    uint32_t color = get_foreground_color();
    
    // Bresenham's line algorithm
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    
    while (true) {
        draw_pixel(x1, y1, color);
        
        if (x1 == x2 && y1 == y2) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

//=============================================================================
// HIGH-LEVEL DRAWING OPERATIONS
//=============================================================================

void WebInkDisplayManager::draw_pixel_block(int start_x, int start_y, const PixelData& pixels) {
    if (!pixels.data) {
        log_message("draw_pixel_block: null pixel data");
        return;
    }
    
    ESP_LOGD(TAG, "Drawing pixel block: %dx%d at (%d,%d), mode=%s, stride=%d, offset=%d",
             pixels.width, pixels.height, start_x, start_y, 
             color_mode_to_string(pixels.mode), pixels.data_stride, pixels.start_offset);
    
    for (int y = 0; y < pixels.height; y++) {
        // Get pointer to start of this row using the new helper method
        const uint8_t* row_data = pixels.get_row_ptr(y);
        if (!row_data) continue;
        
        for (int x = 0; x < pixels.width; x++) {
            uint32_t pixel_value = 0;
            
            // Extract pixel value based on color mode using direct row pointer
            switch (pixels.mode) {
                case ColorMode::MONO_BLACK_WHITE: {
                    // For bit-packed data, calculate bit position within the row
                    int byte_index = x / 8;
                    int bit_index = 7 - (x % 8);
                    pixel_value = (row_data[byte_index] >> bit_index) & 1;
                    break;
                }
                case ColorMode::GRAYSCALE_8BIT: {
                    pixel_value = row_data[x * pixels.bytes_per_pixel];
                    break;
                }
                case ColorMode::RGB_FULL_COLOR: {
                    int pixel_offset = x * pixels.bytes_per_pixel;
                    uint8_t r = row_data[pixel_offset + 0];
                    uint8_t g = row_data[pixel_offset + 1]; 
                    uint8_t b = row_data[pixel_offset + 2];
                    pixel_value = (r << 16) | (g << 8) | b;
                    break;
                }
                default:
                    pixel_value = 0;
                    break;
            }
            
            uint32_t display_color = convert_pixel_color(pixel_value, pixels.mode);
            draw_pixel(start_x + x, start_y + y, display_color);
        }
    }
    
    log_message("Pixel block drawn successfully (zero-copy)");
}

void WebInkDisplayManager::draw_progressive_pixels(int start_x, int start_y, int width, int height,
                                                   const uint8_t* pixel_data, ColorMode color_mode) {
    if (!pixel_data) {
        log_message("draw_progressive_pixels: null pixel data");
        return;
    }
    
    ESP_LOGD(TAG, "Drawing progressive pixels: %dx%d at (%d,%d)", width, height, start_x, start_y);
    
    // Calculate proper bytes per pixel and stride based on color mode
    int bytes_per_pixel = 1;
    int stride = width;
    
    switch (color_mode) {
        case ColorMode::MONO_BLACK_WHITE:
            bytes_per_pixel = 1; // Approximate for bit-packed
            stride = (width + 7) / 8; // Actual bytes per row for bit-packed
            break;
        case ColorMode::GRAYSCALE_8BIT:
            bytes_per_pixel = 1;
            stride = width;
            break;
        case ColorMode::RGB_FULL_COLOR:
            bytes_per_pixel = 3;
            stride = width * 3;
            break;
        case ColorMode::RGBB_4COLOR:
            bytes_per_pixel = 1; // Approximate for 2-bit packed
            stride = (width + 3) / 4; // Actual bytes per row for 2-bit packed
            break;
    }
    
    // Create temporary PixelData structure pointing directly to input data
    PixelData pixels(pixel_data, width, height, bytes_per_pixel, stride, color_mode, 0);
    
    draw_pixel_block(start_x, start_y, pixels);
}

//=============================================================================
// ERROR AND STATUS DISPLAYS
//=============================================================================

void WebInkDisplayManager::draw_error_message(ErrorType error_type, const std::string& details,
                                             bool show_network_info) {
    log_message("Displaying error message: " + std::string(error_type_to_string(error_type)));
    
    int width, height;
    get_display_size(width, height);
    
    // Clear screen
    clear_display();
    
    // Draw border
    draw_rectangle(10, 10, width - 20, height - 20, false);
    draw_rectangle(12, 12, width - 24, height - 24, false);
    
    // Draw error icon at top
    int icon_y = 80;
    draw_error_icon(width / 2, icon_y, 50);
    
    // Draw error title
    std::string title = get_error_title(error_type);
    draw_text(width / 2, icon_y + 80, title, true, 1);
    
    // Draw error details
    int details_y = icon_y + 120;
    draw_wrapped_text(50, details_y, details, false, width - 100);
    
    // Draw network information if requested
    if (show_network_info) {
        int info_y = height - 120;
        
        if (!device_ip_.empty()) {
            draw_text(width / 2, info_y, "Device IP: " + device_ip_, false, 1);
            info_y += 30;
        }
        
        if (!server_url_.empty()) {
            draw_text(width / 2, info_y, "Server: " + server_url_, false, 1);
            info_y += 30;
        }
    }
    
    // Draw retry information
    draw_text(width / 2, height - 60, "Will retry every 30 seconds", false, 1);
    
    // Update display
    update_display();
    error_screen_displayed_ = true;
    
    log_message("Error message displayed");
}

void WebInkDisplayManager::draw_wifi_setup_message() {
    log_message("Displaying WiFi setup message");
    
    int width, height;
    get_display_size(width, height);
    
    // Clear screen
    clear_display();
    
    // Draw border
    draw_rectangle(10, 10, width - 20, height - 20, false);
    draw_rectangle(12, 12, width - 24, height - 24, false);
    
    // Draw WiFi icon at top
    int icon_y = 100;
    draw_wifi_icon(width / 2, icon_y, 40, -1);
    
    // Draw title
    draw_text(width / 2, icon_y + 80, "WiFi Setup Required", true, 1);
    
    // Draw instructions
    int text_y = icon_y + 130;
    draw_text(width / 2, text_y, "1. Connect to WiFi network:", false, 1);
    text_y += 30;
    draw_text(width / 2, text_y, "E-Ink Display Setup", true, 1);
    text_y += 40;
    draw_text(width / 2, text_y, "Password: einksetup123", false, 1);
    text_y += 50;
    
    draw_text(width / 2, text_y, "2. Open browser (portal should auto-open)", false, 1);
    text_y += 25;
    draw_text(width / 2, text_y, "or go to: http://192.168.4.1", false, 1);
    text_y += 40;
    
    draw_text(width / 2, text_y, "3. Configure your WiFi credentials and server address", false, 1);
    
    // Draw retry info
    draw_text(width / 2, height - 40, "Will retry every 30 seconds", false, 1);
    
    // Update display
    update_display();
    error_screen_displayed_ = true;
    
    log_message("WiFi setup message displayed");
}

void WebInkDisplayManager::draw_progress_indicator(float percentage, const std::string& status,
                                                  bool show_details) {
    log_message("Displaying progress: " + std::to_string((int)percentage) + "% - " + status);
    
    int width, height;
    get_display_size(width, height);
    
    // Clear screen
    clear_display();
    
    // Draw progress icon
    draw_progress_icon(width / 2, height / 2 - 80, 30, percentage);
    
    // Draw percentage
    std::string percent_text = std::to_string((int)percentage) + "%";
    draw_text(width / 2, height / 2 - 20, percent_text, true, 1);
    
    // Draw status message
    draw_text(width / 2, height / 2 + 20, status, false, 1);
    
    // Draw progress bar
    int bar_width = 300;
    int bar_height = 20;
    int bar_x = (width - bar_width) / 2;
    int bar_y = height / 2 + 50;
    
    draw_rectangle(bar_x, bar_y, bar_width, bar_height, false);
    
    int fill_width = (bar_width - 4) * percentage / 100.0f;
    if (fill_width > 0) {
        draw_rectangle(bar_x + 2, bar_y + 2, fill_width, bar_height - 4, true);
    }
    
    if (show_details) {
        // Add additional progress details
        draw_text(width / 2, height / 2 + 90, "Processing image data...", false, 1);
    }
    
    // Update display
    update_display();
    
    log_message("Progress indicator displayed");
}

void WebInkDisplayManager::draw_status_screen(const std::string& status) {
    log_message("Displaying status screen");
    
    int width, height;
    get_display_size(width, height);
    
    // Clear screen
    clear_display();
    
    // Draw title
    draw_text(width / 2, 40, "WebInk Status", true, 1);
    
    // Draw status information
    int text_y = 100;
    draw_wrapped_text(50, text_y, status, false, width - 100);
    
    // Update display
    update_display();
    
    log_message("Status screen displayed");
}

//=============================================================================
// ICON AND SYMBOL DRAWING
//=============================================================================

void WebInkDisplayManager::draw_wifi_icon(int x, int y, int size, int signal_strength) {
    // Draw WiFi signal arcs
    for (int i = 1; i <= 3; i++) {
        int radius = size * i / 3;
        
        // Draw arc segments (simplified as quarter circles)
        for (int angle = 0; angle < 90; angle += 5) {
            int px = x + (radius * cos(angle * M_PI / 180)) / 2;
            int py = y - (radius * sin(angle * M_PI / 180)) / 2;
            
            // Only draw if signal is strong enough
            if (signal_strength < 0 || i <= (signal_strength * 3 / 100 + 1)) {
                draw_pixel(px, py, get_foreground_color());
            }
        }
    }
    
    // Draw base dot
    draw_circle(x, y + size / 4, 4, true);
}

void WebInkDisplayManager::draw_error_icon(int x, int y, int size) {
    // Draw thick X
    for (int i = -2; i <= 2; i++) {
        draw_line(x - size + i, y - size, x + size + i, y + size);
        draw_line(x + size - i, y - size, x - size - i, y + size);
    }
}

void WebInkDisplayManager::draw_progress_icon(int x, int y, int size, float progress) {
    // Draw simple spinner based on progress
    int num_spokes = 8;
    int spoke_length = size;
    
    for (int i = 0; i < num_spokes; i++) {
        float angle = (2 * M_PI * i) / num_spokes;
        int x1 = x + (spoke_length / 2) * cos(angle);
        int y1 = y + (spoke_length / 2) * sin(angle);
        int x2 = x + spoke_length * cos(angle);
        int y2 = y + spoke_length * sin(angle);
        
        // Fade spokes based on progress
        float spoke_progress = fmod(progress / 12.5f, num_spokes);
        if (i <= spoke_progress) {
            draw_line(x1, y1, x2, y2);
        }
    }
}

void WebInkDisplayManager::draw_network_icon(int x, int y, int size, bool connected) {
    // Draw simple server/network icon
    draw_rectangle(x - size/2, y - size/2, size, size/2, false);
    
    // Draw connection indicator
    if (connected) {
        draw_circle(x, y + size/4, size/6, true);
    } else {
        draw_error_icon(x, y + size/4, size/6);
    }
}

//=============================================================================
// LAYOUT AND MEASUREMENT UTILITIES
//=============================================================================

bool WebInkDisplayManager::get_text_dimensions(const std::string& text, bool large,
                                              int& width, int& height) {
    // Default implementation - estimate based on character count
    int char_width = large ? 12 : 8;
    int char_height = large ? 24 : 16;
    
    width = text.length() * char_width;
    height = char_height;
    
    return true;
}

int WebInkDisplayManager::get_centered_x(const std::string& text, bool large, int container_width) {
    int text_width, text_height;
    get_text_dimensions(text, large, text_width, text_height);
    
    return (container_width - text_width) / 2;
}

int WebInkDisplayManager::get_line_spacing(bool large) {
    return large ? 30 : 20;
}

//=============================================================================
// COLOR AND PIXEL UTILITIES
//=============================================================================

uint32_t WebInkDisplayManager::convert_pixel_color(uint32_t pixel_value, ColorMode color_mode) {
    switch (color_mode) {
        case ColorMode::MONO_BLACK_WHITE:
            // 0 = black, 1 = white (inverted for some displays)
            return (pixel_value == 0) ? get_foreground_color() : get_background_color();
            
        case ColorMode::GRAYSCALE_8BIT:
            // Convert grayscale to monochrome with threshold
            return (pixel_value < 128) ? get_foreground_color() : get_background_color();
            
        case ColorMode::RGB_FULL_COLOR:
            // For monochrome displays, convert RGB to grayscale then threshold
            uint8_t r = (pixel_value >> 16) & 0xFF;
            uint8_t g = (pixel_value >> 8) & 0xFF;
            uint8_t b = pixel_value & 0xFF;
            uint8_t gray = (r * 299 + g * 587 + b * 114) / 1000; // ITU-R BT.601
            return (gray < 128) ? get_foreground_color() : get_background_color();
            
        default:
            return get_background_color();
    }
}

//=============================================================================
// CONFIGURATION AND STATE
//=============================================================================

void WebInkDisplayManager::set_network_info(const std::string& server_url, const std::string& device_ip) {
    server_url_ = server_url;
    device_ip_ = device_ip;
    
    ESP_LOGD(TAG, "Network info set - Server: %s, IP: %s", server_url.c_str(), device_ip.c_str());
}

void WebInkDisplayManager::set_fonts(font::Font* normal_font, font::Font* large_font) {
    normal_font_ = normal_font;
    large_font_ = large_font;
    
    ESP_LOGD(TAG, "Fonts configured");
}

//=============================================================================
// PROTECTED HELPER METHODS
//=============================================================================

void WebInkDisplayManager::log_message(const std::string& message) {
    if (log_callback_) {
        log_callback_(message);
    }
    ESP_LOGD(TAG, "%s", message.c_str());
}

std::string WebInkDisplayManager::get_error_title(ErrorType error_type) {
    switch (error_type) {
        case ErrorType::WIFI_TIMEOUT:       return "WiFi Connection Failed";
        case ErrorType::SERVER_UNREACHABLE: return "Server Unreachable";
        case ErrorType::INVALID_RESPONSE:   return "Invalid Server Response";
        case ErrorType::PARSE_ERROR:        return "Image Parse Error";
        case ErrorType::MEMORY_ERROR:       return "Insufficient Memory";
        case ErrorType::SOCKET_ERROR:       return "Network Socket Error";
        case ErrorType::DISPLAY_ERROR:      return "Display Error";
        default:                            return "Unknown Error";
    }
}

std::string WebInkDisplayManager::get_error_description(ErrorType error_type) {
    switch (error_type) {
        case ErrorType::WIFI_TIMEOUT:
            return "Check your WiFi network settings and signal strength.";
        case ErrorType::SERVER_UNREACHABLE:
            return "Verify server address and network connectivity.";
        case ErrorType::INVALID_RESPONSE:
            return "Server returned malformed or unexpected data.";
        case ErrorType::PARSE_ERROR:
            return "Unable to parse image data from server.";
        case ErrorType::MEMORY_ERROR:
            return "Insufficient memory to process image data.";
        case ErrorType::SOCKET_ERROR:
            return "Network socket connection or data transfer failed.";
        case ErrorType::DISPLAY_ERROR:
            return "Display hardware communication error.";
        default:
            return "An unexpected error occurred.";
    }
}

int WebInkDisplayManager::draw_wrapped_text(int x, int y, const std::string& text, 
                                           bool large, int max_width) {
    int line_height = get_line_spacing(large);
    int current_y = y;
    
    std::istringstream iss(text);
    std::string line;
    
    while (std::getline(iss, line)) {
        // Simple line wrapping - could be enhanced to break on words
        if (!line.empty()) {
            int text_width, text_height;
            get_text_dimensions(line, large, text_width, text_height);
            
            if (text_width <= max_width) {
                draw_text(x + max_width / 2, current_y, line, large, 1);
                current_y += line_height;
            } else {
                // Line is too long - break into smaller pieces
                int chars_per_line = max_width / (large ? 12 : 8);
                for (size_t i = 0; i < line.length(); i += chars_per_line) {
                    std::string sub_line = line.substr(i, chars_per_line);
                    draw_text(x + max_width / 2, current_y, sub_line, large, 1);
                    current_y += line_height;
                }
            }
        } else {
            current_y += line_height;
        }
    }
    
    return current_y - y;
}

} // namespace webink
} // namespace esphome
