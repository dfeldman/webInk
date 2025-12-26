/**
 * @file webink_types.h
 * @brief Common types, enums, and structures used throughout WebInk component
 * 
 * This file defines all shared data types, enumerations, and structures
 * used by different WebInk component classes. It serves as the foundation
 * for type safety and consistent interfaces across the component.
 * 
 * @author WebInk Component Authors
 * @version 1.0.0
 */

#pragma once

#include <string>
#include <cstdint>
#include <cstring>

namespace esphome {
namespace webink {

/**
 * @enum UpdateState
 * @brief State machine states for the main WebInk controller
 * 
 * The WebInk controller operates as a non-blocking state machine,
 * transitioning through these states during an update cycle.
 * Each state has a specific responsibility and timeout.
 */
enum class UpdateState {
    IDLE,              ///< Waiting for next update trigger
    WIFI_WAIT,         ///< Waiting for WiFi connection
    HASH_CHECK,        ///< Initiating hash check with server
    HASH_REQUEST,      ///< Requesting content hash from server
    HASH_PARSE,        ///< Parsing and comparing hash response
    IMAGE_REQUEST,     ///< Requesting image data from server
    IMAGE_DOWNLOAD,    ///< Downloading image data
    IMAGE_PARSE,       ///< Parsing received image data
    IMAGE_DISPLAY,     ///< Drawing pixels to display buffer
    DISPLAY_UPDATE,    ///< Updating physical display
    ERROR_DISPLAY,     ///< Displaying error message
    SLEEP_PREPARE,     ///< Preparing for deep sleep
    COMPLETE          ///< Update cycle complete
};

/**
 * @enum ColorMode
 * @brief Supported color modes for display and image processing
 * 
 * Different display types support different color depths and formats.
 * The image processor automatically handles conversion between formats.
 */
enum class ColorMode {
    MONO_BLACK_WHITE,  ///< 1-bit monochrome (black/white only)
    GRAYSCALE_8BIT,    ///< 8-bit grayscale (256 levels)
    RGBB_4COLOR,       ///< 4-color RGBB (Red/Green/Blue/Black)
    RGB_FULL_COLOR     ///< 24-bit full color RGB
};

/**
 * @enum NetworkMode
 * @brief Network communication modes supported by WebInk
 * 
 * WebInk supports two different network protocols for image delivery:
 * HTTP mode fetches images in small chunks, while socket mode downloads
 * the complete image in one TCP connection.
 */
enum class NetworkMode {
    HTTP_SLICED,       ///< HTTP requests for image slices (memory efficient)
    TCP_SOCKET         ///< Direct TCP socket for full image download (faster)
};

/**
 * @enum ErrorType
 * @brief Categorized error types for structured error handling
 * 
 * Each error type has specific recovery strategies and user messages.
 * Errors are logged to both local console and remote server.
 */
enum class ErrorType {
    NONE,                  ///< No error
    WIFI_TIMEOUT,         ///< WiFi connection timeout
    SERVER_UNREACHABLE,   ///< HTTP/socket connection failed
    INVALID_RESPONSE,     ///< Server returned malformed data
    PARSE_ERROR,          ///< Image format parsing failed
    MEMORY_ERROR,         ///< Insufficient memory for operation
    SOCKET_ERROR,         ///< TCP socket operation failed
    DISPLAY_ERROR         ///< Display hardware error
};

/**
 * @struct DisplayRect
 * @brief Rectangle coordinates for image requests
 * 
 * Used to specify which portion of an image to request from the server.
 * Coordinates are in pixels relative to the full image.
 */
struct DisplayRect {
    int x;        ///< Left edge X coordinate
    int y;        ///< Top edge Y coordinate  
    int width;    ///< Rectangle width in pixels
    int height;   ///< Rectangle height in pixels
    
    DisplayRect() : x(0), y(0), width(0), height(0) {}
    DisplayRect(int x_, int y_, int w_, int h_) : x(x_), y(y_), width(w_), height(h_) {}
};

/**
 * @struct ImageRequest
 * @brief Complete specification for an image data request
 * 
 * Contains all parameters needed to request image data from the server,
 * including format, color mode, and target rectangle.
 */
struct ImageRequest {
    DisplayRect rect;      ///< Target rectangle to fetch
    ColorMode mode;        ///< Requested color mode
    std::string format;    ///< Image format ("pbm", "pgm", "ppm")
    int start_row;         ///< Starting row for sliced requests
    int num_rows;          ///< Number of rows for sliced requests
    
    ImageRequest() : mode(ColorMode::MONO_BLACK_WHITE), format("pbm"), start_row(0), num_rows(0) {}
};

/**
 * @struct ImageHeader
 * @brief Parsed image header information
 * 
 * Contains metadata extracted from image format headers (PBM/PGM/PPM).
 * Used to validate image data and calculate memory requirements.
 * Memory-optimized: uses fixed char array instead of std::string.
 */
struct ImageHeader {
    int width;             ///< Image width in pixels
    int height;            ///< Image height in pixels
    int max_value;         ///< Maximum pixel value (for PGM/PPM)
    ColorMode color_mode;  ///< Detected color mode
    char format[4];        ///< Format identifier ("P1", "P2", etc.) - fixed size to avoid allocation
    int header_bytes;      ///< Size of header in bytes
    int data_bytes;        ///< Size of pixel data in bytes
    bool valid;            ///< True if header was parsed successfully
    
    ImageHeader() : width(0), height(0), max_value(0), 
                   color_mode(ColorMode::MONO_BLACK_WHITE),
                   header_bytes(0), data_bytes(0), valid(false) {
        format[0] = '\0'; // Initialize as empty string
    }
    
    /// @brief Set format string (safe copy to fixed array)
    void set_format(const char* fmt) {
        if (fmt && strlen(fmt) < sizeof(format)) {
            strcpy(format, fmt);
        }
    }
};

/**
 * @struct PixelData
 * @brief Container for parsed pixel data with zero-copy design
 * 
 * Holds pixel data using direct pointers to avoid memory copying.
 * For memory-constrained devices, this avoids doubling memory usage.
 */
struct PixelData {
    const uint8_t* data;     ///< Pixel data pointer (not owned, points to original data)
    int width;               ///< Width in pixels
    int height;              ///< Height in pixels
    int bytes_per_pixel;     ///< Bytes per pixel (depends on color mode)
    int data_stride;         ///< Bytes per row in source data (for padding)
    int start_offset;        ///< Offset from data pointer to first pixel
    ColorMode mode;          ///< Color mode of pixel data
    bool owns_data;          ///< True if this PixelData owns the data pointer
    
    PixelData() : data(nullptr), width(0), height(0), bytes_per_pixel(0), 
                 data_stride(0), start_offset(0), mode(ColorMode::MONO_BLACK_WHITE),
                 owns_data(false) {}
    
    /// @brief Constructor for direct pointer reference (zero-copy)
    PixelData(const uint8_t* data_ptr, int w, int h, int bpp, int stride, 
              ColorMode color_mode, int offset = 0) :
        data(data_ptr), width(w), height(h), bytes_per_pixel(bpp),
        data_stride(stride), start_offset(offset), mode(color_mode), owns_data(false) {}
    
    /// @brief Destructor cleans up only if we own the data
    ~PixelData() {
        if (owns_data && data) {
            delete[] data;
        }
        data = nullptr;
    }
    
    /// @brief Move constructor for efficient transfer
    PixelData(PixelData&& other) noexcept : 
        data(other.data), width(other.width), height(other.height),
        bytes_per_pixel(other.bytes_per_pixel), data_stride(other.data_stride),
        start_offset(other.start_offset), mode(other.mode), owns_data(other.owns_data) {
        other.data = nullptr;
        other.owns_data = false;
    }
    
    /// @brief Move assignment operator
    PixelData& operator=(PixelData&& other) noexcept {
        if (this != &other) {
            if (owns_data && data) delete[] data;
            data = other.data;
            width = other.width;
            height = other.height;
            bytes_per_pixel = other.bytes_per_pixel;
            data_stride = other.data_stride;
            start_offset = other.start_offset;
            mode = other.mode;
            owns_data = other.owns_data;
            other.data = nullptr;
            other.owns_data = false;
        }
        return *this;
    }
    
    /// @brief Get pointer to pixel at specific row and column
    const uint8_t* get_pixel_ptr(int row, int col) const {
        if (!data) return nullptr;
        return data + start_offset + (row * data_stride) + (col * bytes_per_pixel);
    }
    
    /// @brief Get pointer to start of specific row
    const uint8_t* get_row_ptr(int row) const {
        if (!data) return nullptr;
        return data + start_offset + (row * data_stride);
    }
    
    // Prevent copying (pixel data should be moved, not copied)
    PixelData(const PixelData&) = delete;
    PixelData& operator=(const PixelData&) = delete;
};

/**
 * @struct NetworkResult
 * @brief Result of network operation (HTTP or socket)
 * 
 * Provides structured result information for both successful and failed
 * network operations, with detailed error reporting.
 */
struct NetworkResult {
    bool success;
    ErrorType error_type;
    std::string error_message;
    std::string content;  // HTTP response content
    std::string data;     // Alias for content (for compatibility)
    int status_code;
    int bytes_received;   // Number of bytes received
    
    NetworkResult() : success(false), error_type(ErrorType::SERVER_UNREACHABLE), status_code(0), bytes_received(0) {}
};

/**
 * @brief Convert UpdateState enum to human-readable string
 * @param state The state to convert
 * @return String representation of the state
 */
const char* update_state_to_string(UpdateState state);

/**
 * @brief Convert ColorMode enum to human-readable string  
 * @param mode The color mode to convert
 * @return String representation of the color mode
 */
const char* color_mode_to_string(ColorMode mode);

/**
 * @brief Convert ErrorType enum to human-readable string
 * @param error The error type to convert
 * @return String representation of the error type
 */
const char* error_type_to_string(ErrorType error);

} // namespace webink
} // namespace esphome
