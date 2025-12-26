/**
 * @file webink_display.h
 * @brief Display abstraction and error rendering for WebInk component
 * 
 * The WebInkDisplayManager class provides an abstract interface for display
 * operations with built-in error message rendering, progress indicators, and
 * WiFi setup screens. It bridges the gap between the WebInk component and
 * various ESPHome display drivers.
 * 
 * Key features:
 * - Abstract display interface for multiple display types
 * - Professional error message rendering with icons
 * - WiFi setup instruction screens
 * - Progress indicators for long operations
 * - Pixel-level drawing abstraction
 * - Font and layout management
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
#else
// Normal ESPHome mode
#include "esphome/core/log.h"
#endif

#include "webink_types.h"

#ifndef WEBINK_MAC_INTEGRATION_TEST
// ESPHome component includes (only for actual ESPHome build)
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/font/font.h"
#endif

namespace esphome {
namespace webink {

/**
 * @class WebInkDisplayManager
 * @brief Abstract display manager with error rendering and progress indication
 * 
 * This class provides a unified interface for display operations across different
 * e-ink display types, with built-in support for rendering error messages,
 * setup instructions, and progress indicators. It abstracts away display-specific
 * details and provides consistent error handling and user feedback.
 * 
 * The class is designed to be subclassed for specific display implementations,
 * with the base class providing common functionality like error screens and
 * progress indicators.
 * 
 * Features:
 * - Abstract interface for multiple display types
 * - Built-in error message templates with icons
 * - WiFi setup instruction screens
 * - Progress indicators with percentage display
 * - Pixel-level and block-level drawing operations
 * - Font management and text rendering
 * - Layout calculations and centering
 * 
 * @example Basic Implementation
 * @code
 * class ESPHomeDisplayManager : public WebInkDisplayManager {
 * public:
 *     ESPHomeDisplayManager(display::DisplayComponent* display) : display_(display) {}
 *     
 *     void clear_display() override {
 *         display_->fill(Color::WHITE);
 *     }
 *     
 *     void draw_pixel(int x, int y, uint32_t color) override {
 *         display_->draw_pixel_at(x, y, Color(color));
 *     }
 *     
 *     void update_display() override {
 *         display_->update();
 *     }
 * };
 * @endcode
 * 
 * @example Error Display
 * @code
 * display_manager->draw_error_message(ErrorType::WIFI_TIMEOUT, 
 *     "WiFi connection failed after 10 seconds. Check your network settings.");
 * @endcode
 */
class WebInkDisplayManager {
public:
    /**
     * @brief Constructor with optional logging callback
     * @param log_callback Function for logging messages (optional)
     */
    WebInkDisplayManager(std::function<void(const std::string&)> log_callback = nullptr);

    /**
     * @brief Virtual destructor for proper inheritance
     */
    virtual ~WebInkDisplayManager() = default;

    //=========================================================================
    // PURE VIRTUAL INTERFACE (must be implemented by subclasses)
    //=========================================================================

    /**
     * @brief Clear the entire display to background color
     * 
     * Implementation should fill the display with the background color
     * (typically white for e-ink displays).
     */
    virtual void clear_display() = 0;

    /**
     * @brief Draw a single pixel at specified coordinates
     * @param x X coordinate (0 = left edge)
     * @param y Y coordinate (0 = top edge)  
     * @param color Color value (format depends on display type)
     */
    virtual void draw_pixel(int x, int y, uint32_t color) = 0;

    /**
     * @brief Update the physical display with current buffer contents
     * 
     * This typically triggers the e-ink refresh cycle which can take
     * several seconds to complete.
     */
    virtual void update_display() = 0;

    /**
     * @brief Get display dimensions
     * @param[out] width Display width in pixels
     * @param[out] height Display height in pixels
     */
    virtual void get_display_size(int& width, int& height) = 0;

    //=========================================================================
    // VIRTUAL INTERFACE (can be overridden for customization)
    //=========================================================================

    /**
     * @brief Draw text at specified position
     * @param x X coordinate for text position
     * @param y Y coordinate for text position
     * @param text Text string to draw
     * @param large True for large font, false for normal font
     * @param alignment Text alignment (0=left, 1=center, 2=right)
     */
    virtual void draw_text(int x, int y, const std::string& text, 
                          bool large = false, int alignment = 1);

    /**
     * @brief Draw rectangle (outline or filled)
     * @param x Left edge X coordinate
     * @param y Top edge Y coordinate
     * @param width Rectangle width
     * @param height Rectangle height
     * @param filled True for filled rectangle, false for outline
     */
    virtual void draw_rectangle(int x, int y, int width, int height, bool filled = false);

    /**
     * @brief Draw circle (outline or filled)
     * @param center_x Center X coordinate
     * @param center_y Center Y coordinate
     * @param radius Circle radius
     * @param filled True for filled circle, false for outline
     */
    virtual void draw_circle(int center_x, int center_y, int radius, bool filled = false);

    /**
     * @brief Draw line between two points
     * @param x1 Start X coordinate
     * @param y1 Start Y coordinate
     * @param x2 End X coordinate
     * @param y2 End Y coordinate
     */
    virtual void draw_line(int x1, int y1, int x2, int y2);

    //=========================================================================
    // HIGH-LEVEL DRAWING OPERATIONS
    //=========================================================================

    /**
     * @brief Draw block of pixels from pixel data
     * @param start_x Starting X coordinate
     * @param start_y Starting Y coordinate
     * @param pixels Pixel data structure
     * 
     * Draws a rectangular block of pixels from the PixelData structure.
     * Handles color mode conversion as needed.
     */
    virtual void draw_pixel_block(int start_x, int start_y, const PixelData& pixels);

    /**
     * @brief Draw pixels row by row for progressive display updates
     * @param start_x Starting X coordinate
     * @param start_y Starting Y coordinate  
     * @param width Width of pixel block
     * @param height Height of pixel block
     * @param pixel_data Raw pixel data
     * @param color_mode Color mode of pixel data
     */
    virtual void draw_progressive_pixels(int start_x, int start_y, int width, int height,
                                       const uint8_t* pixel_data, ColorMode color_mode);

    //=========================================================================
    // ERROR AND STATUS DISPLAYS
    //=========================================================================

    /**
     * @brief Display comprehensive error message with icon and details
     * @param error_type Type of error that occurred
     * @param details Detailed error description
     * @param show_network_info True to show IP address and server info
     * 
     * Renders a professional error screen with:
     * - Error icon (X symbol)
     * - Error type title
     * - Detailed error message
     * - Network information (optional)
     * - Retry interval information
     * - Troubleshooting hints
     */
    virtual void draw_error_message(ErrorType error_type, const std::string& details,
                                   bool show_network_info = true);

    /**
     * @brief Display WiFi setup instructions for captive portal
     * 
     * Shows step-by-step instructions for connecting to the device's
     * access point and configuring WiFi credentials.
     */
    virtual void draw_wifi_setup_message();

    /**
     * @brief Display progress indicator with percentage and status
     * @param percentage Progress percentage (0-100)
     * @param status Current operation status message
     * @param show_details True to show additional progress details
     * 
     * Renders a progress screen with:
     * - Progress bar
     * - Percentage display
     * - Current operation status
     * - Estimated time remaining (if available)
     */
    virtual void draw_progress_indicator(float percentage, const std::string& status,
                                       bool show_details = false);

    /**
     * @brief Display system status information
     * @param status Status information to display
     * 
     * Shows current system status including:
     * - Wake counter and cycle information
     * - Current hash
     * - Network status
     * - Memory usage
     * - Last update time
     */
    virtual void draw_status_screen(const std::string& status);

    //=========================================================================
    // ICON AND SYMBOL DRAWING
    //=========================================================================

    /**
     * @brief Draw WiFi icon with signal strength indication
     * @param x Center X coordinate
     * @param y Center Y coordinate
     * @param size Icon size (radius)
     * @param signal_strength Signal strength (0-100, -1 for no signal)
     */
    virtual void draw_wifi_icon(int x, int y, int size = 30, int signal_strength = -1);

    /**
     * @brief Draw error icon (X symbol)
     * @param x Center X coordinate
     * @param y Center Y coordinate
     * @param size Icon size (radius)
     */
    virtual void draw_error_icon(int x, int y, int size = 40);

    /**
     * @brief Draw progress/loading icon (spinner or hourglass)
     * @param x Center X coordinate
     * @param y Center Y coordinate
     * @param size Icon size (radius)
     * @param progress Progress value (0-100) for animated icons
     */
    virtual void draw_progress_icon(int x, int y, int size = 30, float progress = 0);

    /**
     * @brief Draw network icon (server/connection symbol)
     * @param x Center X coordinate
     * @param y Center Y coordinate
     * @param size Icon size (radius)
     * @param connected True if connection is active
     */
    virtual void draw_network_icon(int x, int y, int size = 30, bool connected = false);

    //=========================================================================
    // LAYOUT AND MEASUREMENT UTILITIES
    //=========================================================================

    /**
     * @brief Calculate text dimensions for layout planning
     * @param text Text string to measure
     * @param large True for large font, false for normal font
     * @param[out] width Text width in pixels
     * @param[out] height Text height in pixels
     * @return True if measurement was successful
     */
    virtual bool get_text_dimensions(const std::string& text, bool large,
                                   int& width, int& height);

    /**
     * @brief Get centered X coordinate for text
     * @param text Text string
     * @param large True for large font
     * @param container_width Width of container for centering
     * @return X coordinate for centered text
     */
    virtual int get_centered_x(const std::string& text, bool large, int container_width);

    /**
     * @brief Get recommended line spacing for multi-line text
     * @param large True for large font
     * @return Line spacing in pixels
     */
    virtual int get_line_spacing(bool large);

    //=========================================================================
    // COLOR AND PIXEL UTILITIES
    //=========================================================================

    /**
     * @brief Convert color mode pixel to display color
     * @param pixel_value Raw pixel value
     * @param color_mode Source color mode
     * @return Display color value
     */
    virtual uint32_t convert_pixel_color(uint32_t pixel_value, ColorMode color_mode);

    /**
     * @brief Get foreground color (typically black)
     * @return Foreground color value
     */
    virtual uint32_t get_foreground_color() { return 0x000000; }

    /**
     * @brief Get background color (typically white)  
     * @return Background color value
     */
    virtual uint32_t get_background_color() { return 0xFFFFFF; }

    /**
     * @brief Get accent color for highlights
     * @return Accent color value
     */
    virtual uint32_t get_accent_color() { return 0x808080; }

    //=========================================================================
    // CONFIGURATION AND STATE
    //=========================================================================

    /**
     * @brief Set server information for error displays
     * @param server_url Server URL
     * @param device_ip Device IP address
     */
    void set_network_info(const std::string& server_url, const std::string& device_ip);

    /**
     * @brief Set font components for text rendering
     * @param normal_font Normal size font component
     * @param large_font Large size font component
     */
#ifndef WEBINK_MAC_INTEGRATION_TEST
    void set_fonts(font::Font* normal_font, font::Font* large_font);
#endif

    /**
     * @brief Check if error screen is currently displayed
     * @return True if error screen is active
     */
    bool is_error_screen_displayed() const { return error_screen_displayed_; }

    /**
     * @brief Mark error screen as displayed/cleared
     * @param displayed True if error screen is now displayed
     */
    void set_error_screen_displayed(bool displayed) { error_screen_displayed_ = displayed; }

protected:
    //=========================================================================
    // PROTECTED HELPER METHODS
    //=========================================================================

    /**
     * @brief Log message through callback or ESP_LOG
     * @param message Message to log
     */
    void log_message(const std::string& message);

    /**
     * @brief Get error type title string
     * @param error_type Error type enum
     * @return Human-readable error title
     */
    std::string get_error_title(ErrorType error_type);

    /**
     * @brief Get error type description
     * @param error_type Error type enum
     * @return Human-readable error description
     */
    std::string get_error_description(ErrorType error_type);

    /**
     * @brief Draw text with automatic line wrapping
     * @param x Starting X coordinate
     * @param y Starting Y coordinate
     * @param text Text to draw (may contain \n)
     * @param large True for large font
     * @param max_width Maximum width before wrapping
     * @return Height of drawn text block
     */
    int draw_wrapped_text(int x, int y, const std::string& text, 
                         bool large, int max_width);

private:
    static const char* TAG;                                     ///< Logging tag
    
    std::function<void(const std::string&)> log_callback_;      ///< Logging callback
    std::string server_url_;                                    ///< Server URL for error displays
    std::string device_ip_;                                     ///< Device IP for error displays
    bool error_screen_displayed_;                               ///< Error screen state flag
    
#ifndef WEBINK_MAC_INTEGRATION_TEST
    font::Font* normal_font_;                                   ///< Normal size font
    font::Font* large_font_;                                    ///< Large size font
#endif
};

} // namespace webink
} // namespace esphome
