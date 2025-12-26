/**
 * @file webink_image.cpp
 * @brief Implementation of WebInkImageProcessor class for memory-efficient image parsing
 * 
 * @author WebInk Component Authors
 * @version 1.0.0
 */

#include "webink_image.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace esphome {
namespace webink {

const char* WebInkImageProcessor::TAG = "webink.image";

//=============================================================================
// CONSTRUCTOR
//=============================================================================

WebInkImageProcessor::WebInkImageProcessor(std::function<void(const std::string&)> log_callback)
    : log_callback_(log_callback) {
    ESP_LOGD(TAG, "WebInkImageProcessor initialized");
}

//=============================================================================
// HEADER PARSING
//=============================================================================

bool WebInkImageProcessor::parse_header(const uint8_t* data, int data_size, ImageHeader& header) {
    if (!data || data_size < 10) {
        log_message("Invalid input data for header parsing");
        return false;
    }

    const uint8_t* cur = data;
    const uint8_t* end = data + data_size;

    // Skip UTF-8 BOM if present
    skip_utf8_bom(cur, end);

    // Skip initial whitespace and comments
    cur = skip_whitespace_and_comments(cur, end);

    // Parse magic number (P1, P2, P3, P4, P5, P6)
    if (cur + 2 > end || cur[0] != 'P') {
        log_message("Invalid magic number - not a PBM/PGM/PPM file");
        return false;
    }

    char format_char = cur[1];
    header.format[0] = 'P';
    header.format[1] = format_char;
    header.format[2] = '\0';
    cur += 2;

    ESP_LOGD(TAG, "Parsing format %s", header.format);

    // Parse format-specific headers
    bool success = false;
    switch (format_char) {
        case '1': // ASCII PBM
        case '4': // Binary PBM
            success = parse_pbm_header(cur, end, header);
            break;
        case '2': // ASCII PGM  
        case '5': // Binary PGM
            success = parse_pgm_header(cur, end, header);
            break;
        case '3': // ASCII PPM
        case '6': // Binary PPM
            success = parse_ppm_header(cur, end, header);
            break;
        default:
            log_message("Unsupported format: P" + std::string(1, format_char));
            return false;
    }

    if (success) {
        header.header_bytes = cur - data;
        header.valid = true;
        
        ESP_LOGI(TAG, "Parsed header: %dx%d %s, %d header bytes, %d data bytes",
                 header.width, header.height, header.format,
                 header.header_bytes, header.data_bytes);
    }

    return success;
}

bool WebInkImageProcessor::validate_format(const uint8_t* data, int data_size) {
    ImageHeader header;
    return parse_header(data, data_size, header);
}

bool WebInkImageProcessor::extract_format_info(const uint8_t* data, int data_size,
                                               std::string& format, bool& is_binary) {
    if (!data || data_size < 2) {
        return false;
    }

    const uint8_t* cur = data;
    const uint8_t* end = data + data_size;
    skip_utf8_bom(cur, end);

    if (cur + 2 > end || cur[0] != 'P') {
        return false;
    }

    char format_char = cur[1];
    // Avoid std::string allocation - directly build the format string
    char temp_format[4];
    temp_format[0] = 'P';
    temp_format[1] = format_char;
    temp_format[2] = '\0';
    format = temp_format;

    // Binary formats are P4, P5, P6
    is_binary = (format_char == '4' || format_char == '5' || format_char == '6');

    return true;
}

//=============================================================================
// DATA PARSING
//=============================================================================

bool WebInkImageProcessor::parse_rows(const uint8_t* data, int data_size, const ImageHeader& header,
                                     int start_row, int num_rows, PixelData& pixels) {
    if (!header.valid || !data) {
        log_message("Invalid header or data for row parsing");
        return false;
    }

    if (!validate_pixel_range(header.width, header.height, start_row, num_rows)) {
        log_message("Invalid pixel range for parsing");
        return false;
    }

    // Adjust num_rows if it extends beyond image
    int actual_rows = std::min(num_rows, header.height - start_row);
    
    ESP_LOGD(TAG, "Parsing %d rows starting at row %d (format: %s)",
             actual_rows, start_row, header.format);

    // Skip to pixel data (after header)
    const uint8_t* pixel_data = data + header.header_bytes;
    int pixel_data_size = data_size - header.header_bytes;

    if (pixel_data_size < header.data_bytes) {
        log_message("Insufficient pixel data in buffer");
        return false;
    }

    // Call format-specific parser
    bool success = false;
    if (strcmp(header.format, "P1") == 0 || strcmp(header.format, "P4") == 0) {
        success = parse_pbm_data(pixel_data, header, start_row, actual_rows, pixels);
    } else if (strcmp(header.format, "P2") == 0 || strcmp(header.format, "P5") == 0) {
        success = parse_pgm_data(pixel_data, header, start_row, actual_rows, pixels);
    } else if (strcmp(header.format, "P3") == 0 || strcmp(header.format, "P6") == 0) {
        success = parse_ppm_data(pixel_data, header, start_row, actual_rows, pixels);
    }

    if (success) {
        ESP_LOGD(TAG, "Successfully parsed %d rows (%d bytes)",
                 actual_rows, actual_rows * calculate_bytes_per_row(header.width, header.color_mode));
    }

    return success;
}

bool WebInkImageProcessor::parse_complete_image(const uint8_t* data, int data_size, PixelData& pixels) {
    ImageHeader header;
    if (!parse_header(data, data_size, header)) {
        return false;
    }

    return parse_rows(data, data_size, header, 0, header.height, pixels);
}

//=============================================================================
// FORMAT-SPECIFIC PARSERS
//=============================================================================

bool WebInkImageProcessor::parse_pbm_data(const uint8_t* data, const ImageHeader& header,
                                          int start_row, int num_rows, PixelData& pixels) {
    int bytes_per_row = (header.width + 7) / 8;  // 1-bit packed
    
    // Zero-copy: Point directly to the source data
    const uint8_t* row_start = data + (start_row * bytes_per_row);
    
    // Set up PixelData to point directly to source (no allocation, no copying)
    pixels = PixelData(
        data,                           // Original data pointer
        header.width,                   // Width in pixels
        num_rows,                       // Height in pixels
        1,                             // Bytes per pixel (for bit-packed, this is approximate)
        bytes_per_row,                 // Data stride (bytes per row)
        ColorMode::MONO_BLACK_WHITE,   // Color mode
        start_row * bytes_per_row      // Offset to start of our rows
    );

    ESP_LOGD(TAG, "Parsed PBM data (zero-copy): %d rows, %d bytes/row, offset=%d",
             num_rows, bytes_per_row, start_row * bytes_per_row);

    return true;
}

bool WebInkImageProcessor::parse_pgm_data(const uint8_t* data, const ImageHeader& header,
                                          int start_row, int num_rows, PixelData& pixels) {
    bool is_binary = (strcmp(header.format, "P5") == 0);
    int bytes_per_pixel = (header.max_value > 255) ? 2 : 1;
    int bytes_per_row = header.width * bytes_per_pixel;

    if (is_binary) {
        // Binary PGM - zero-copy: point directly to source data
        pixels = PixelData(
            data,                           // Original data pointer
            header.width,                   // Width in pixels
            num_rows,                       // Height in pixels
            bytes_per_pixel,               // Bytes per pixel
            bytes_per_row,                 // Data stride (bytes per row)
            ColorMode::GRAYSCALE_8BIT,     // Color mode
            start_row * bytes_per_row      // Offset to start of our rows
        );

        ESP_LOGD(TAG, "Parsed PGM data (zero-copy): %d rows, %d bytes/row, offset=%d",
                 num_rows, bytes_per_row, start_row * bytes_per_row);
    } else {
        // ASCII PGM - unfortunately must allocate and parse text values
        // This is unavoidable since ASCII format requires text-to-binary conversion
        int total_bytes = bytes_per_row * num_rows;
        uint8_t* allocated_data = new(std::nothrow) uint8_t[total_bytes];
        if (!allocated_data) {
            log_message("Failed to allocate " + std::to_string(total_bytes) + " bytes for ASCII PGM data");
            return false;
        }

        const uint8_t* cur = data;
        const uint8_t* end = data + header.data_bytes;
        
        // Skip to start_row
        for (int skip_row = 0; skip_row < start_row; skip_row++) {
            for (int x = 0; x < header.width; x++) {
                int value;
                if (!parse_integer(cur, end, value)) {
                    delete[] allocated_data;
                    return false;
                }
            }
        }
        
        // Parse requested rows
        for (int y = 0; y < num_rows && y + start_row < header.height; y++) {
            for (int x = 0; x < header.width; x++) {
                int value;
                if (!parse_integer(cur, end, value)) {
                    delete[] allocated_data;
                    return false;
                }
                
                // Store pixel value (scale to 8-bit if needed)
                if (bytes_per_pixel == 1) {
                    allocated_data[y * header.width + x] = 
                        (value * 255) / header.max_value;
                } else {
                    // 16-bit storage
                    uint16_t* pixel_16 = reinterpret_cast<uint16_t*>(allocated_data);
                    pixel_16[y * header.width + x] = value;
                }
            }
        }

        // Set up PixelData with owned allocated data
        pixels = PixelData(allocated_data, header.width, num_rows, bytes_per_pixel, 
                          bytes_per_row, ColorMode::GRAYSCALE_8BIT, 0);
        pixels.owns_data = true; // We own this data and must clean it up

        ESP_LOGD(TAG, "Parsed PGM data (ASCII, allocated): %d rows, %d bytes", num_rows, total_bytes);
    }

    return true;
}

bool WebInkImageProcessor::parse_ppm_data(const uint8_t* data, const ImageHeader& header,
                                          int start_row, int num_rows, PixelData& pixels) {
    bool is_binary = (strcmp(header.format, "P6") == 0);
    int bytes_per_pixel = 3; // RGB
    int bytes_per_row = header.width * bytes_per_pixel;

    if (is_binary) {
        // Binary PPM - zero-copy: point directly to source data
        pixels = PixelData(
            data,                           // Original data pointer
            header.width,                   // Width in pixels
            num_rows,                       // Height in pixels
            bytes_per_pixel,               // Bytes per pixel
            bytes_per_row,                 // Data stride (bytes per row)
            ColorMode::RGB_FULL_COLOR,     // Color mode
            start_row * bytes_per_row      // Offset to start of our rows
        );

        ESP_LOGD(TAG, "Parsed PPM data (zero-copy): %d rows, %d bytes/row, offset=%d",
                 num_rows, bytes_per_row, start_row * bytes_per_row);
    } else {
        // ASCII PPM - must allocate and parse text RGB triplets
        int total_bytes = bytes_per_row * num_rows;
        uint8_t* allocated_data = new(std::nothrow) uint8_t[total_bytes];
        if (!allocated_data) {
            log_message("Failed to allocate " + std::to_string(total_bytes) + " bytes for ASCII PPM data");
            return false;
        }

        const uint8_t* cur = data;
        const uint8_t* end = data + header.data_bytes;
        
        // Skip to start_row
        for (int skip_row = 0; skip_row < start_row; skip_row++) {
            for (int x = 0; x < header.width; x++) {
                int r, g, b;
                if (!parse_integer(cur, end, r) ||
                    !parse_integer(cur, end, g) ||
                    !parse_integer(cur, end, b)) {
                    delete[] allocated_data;
                    return false;
                }
            }
        }
        
        // Parse requested rows
        for (int y = 0; y < num_rows && y + start_row < header.height; y++) {
            for (int x = 0; x < header.width; x++) {
                int r, g, b;
                if (!parse_integer(cur, end, r) ||
                    !parse_integer(cur, end, g) ||
                    !parse_integer(cur, end, b)) {
                    delete[] allocated_data;
                    return false;
                }
                
                int pixel_idx = (y * header.width + x) * 3;
                allocated_data[pixel_idx + 0] = (r * 255) / header.max_value;
                allocated_data[pixel_idx + 1] = (g * 255) / header.max_value;
                allocated_data[pixel_idx + 2] = (b * 255) / header.max_value;
            }
        }

        // Set up PixelData with owned allocated data
        pixels = PixelData(allocated_data, header.width, num_rows, bytes_per_pixel,
                          bytes_per_row, ColorMode::RGB_FULL_COLOR, 0);
        pixels.owns_data = true; // We own this data and must clean it up

        ESP_LOGD(TAG, "Parsed PPM data (ASCII, allocated): %d rows, %d bytes", num_rows, total_bytes);
    }

    return true;
}

//=============================================================================
// MEMORY MANAGEMENT UTILITIES
//=============================================================================

int WebInkImageProcessor::calculate_bytes_per_row(int width, ColorMode mode) {
    switch (mode) {
        case ColorMode::MONO_BLACK_WHITE:
            return (width + 7) / 8;  // 1-bit packed
        case ColorMode::GRAYSCALE_8BIT:
            return width;            // 1 byte per pixel
        case ColorMode::RGBB_4COLOR:
            return (width + 3) / 4;  // 2-bits per pixel
        case ColorMode::RGB_FULL_COLOR:
            return width * 3;        // 3 bytes per pixel (RGB)
        default:
            return width;            // Conservative fallback
    }
}

int WebInkImageProcessor::calculate_max_rows_for_memory(int width, ColorMode mode, int available_bytes) {
    int bytes_per_row = calculate_bytes_per_row(width, mode);
    if (bytes_per_row == 0) {
        return 1;
    }
    
    int max_rows = available_bytes / bytes_per_row;
    max_rows = std::max(1, max_rows);      // Always allow at least 1 row
    max_rows = std::min(max_rows, 128);    // Cap at reasonable maximum
    
    ESP_LOGD(TAG, "Memory calc: %d bytes available, %d bytes/row -> %d rows max",
             available_bytes, bytes_per_row, max_rows);
    
    return max_rows;
}

int WebInkImageProcessor::calculate_total_memory_needed(int width, int height, ColorMode mode) {
    return calculate_bytes_per_row(width, mode) * height;
}

bool WebInkImageProcessor::get_memory_allocation_recommendation(int width, int height, ColorMode mode,
                                                               int max_available_bytes,
                                                               int& recommended_rows, int& total_chunks) {
    int bytes_per_row = calculate_bytes_per_row(width, mode);
    int total_memory_needed = bytes_per_row * height;
    
    if (total_memory_needed <= max_available_bytes) {
        // Can fit entire image
        recommended_rows = height;
        total_chunks = 1;
        return true;
    }
    
    // Need to chunk the image
    recommended_rows = calculate_max_rows_for_memory(width, mode, max_available_bytes);
    if (recommended_rows == 0) {
        return false; // Not feasible
    }
    
    total_chunks = (height + recommended_rows - 1) / recommended_rows; // Ceiling division
    
    ESP_LOGD(TAG, "Memory recommendation: %d rows/chunk, %d chunks for %dx%d image",
             recommended_rows, total_chunks, width, height);
    
    return true;
}

//=============================================================================
// VALIDATION AND UTILITIES
//=============================================================================

bool WebInkImageProcessor::validate_pixel_range(int width, int height, int start_row, int num_rows) {
    if (width <= 0 || height <= 0 || start_row < 0 || num_rows <= 0) {
        return false;
    }
    
    if (start_row >= height) {
        return false;
    }
    
    // Allow num_rows to extend beyond image (will be clipped)
    return true;
}

std::string WebInkImageProcessor::get_format_description(const ImageHeader& header) {
    // Use static buffer to avoid multiple string allocations
    static char desc[128];
    
    // Build description efficiently without multiple string concatenations
    if (strcmp(header.format, "P1") == 0 || strcmp(header.format, "P4") == 0) {
        snprintf(desc, sizeof(desc), "%s (PBM monochrome) %dx%d", 
                 header.format, header.width, header.height);
    } else if (strcmp(header.format, "P2") == 0 || strcmp(header.format, "P5") == 0) {
        snprintf(desc, sizeof(desc), "%s (PGM grayscale, max=%d) %dx%d",
                 header.format, header.max_value, header.width, header.height);
    } else if (strcmp(header.format, "P3") == 0 || strcmp(header.format, "P6") == 0) {
        snprintf(desc, sizeof(desc), "%s (PPM color, max=%d) %dx%d",
                 header.format, header.max_value, header.width, header.height);
    } else {
        snprintf(desc, sizeof(desc), "%s %dx%d", 
                 header.format, header.width, header.height);
    }
    
    return std::string(desc);
}

//=============================================================================
// HEADER PARSING HELPERS
//=============================================================================

bool WebInkImageProcessor::parse_pbm_header(const uint8_t*& data, const uint8_t* end, ImageHeader& header) {
    // Parse width
    data = skip_whitespace_and_comments(data, end);
    if (!parse_integer(data, end, header.width)) {
        return false;
    }

    // Parse height
    data = skip_whitespace_and_comments(data, end);
    if (!parse_integer(data, end, header.height)) {
        return false;
    }

    // Skip final whitespace before pixel data
    data = skip_whitespace_and_comments(data, end);

    header.max_value = 1;
    header.color_mode = ColorMode::MONO_BLACK_WHITE;
    
    // Calculate data bytes
    if (strcmp(header.format, "P4") == 0) {
        // Binary PBM
        header.data_bytes = ((header.width + 7) / 8) * header.height;
    } else {
        // ASCII PBM (P1) - harder to calculate exactly, estimate
        header.data_bytes = header.width * header.height * 2; // Conservative estimate
    }

    ESP_LOGD(TAG, "PBM header: %dx%d, data_bytes=%d", 
             header.width, header.height, header.data_bytes);

    return header.width > 0 && header.height > 0;
}

bool WebInkImageProcessor::parse_pgm_header(const uint8_t*& data, const uint8_t* end, ImageHeader& header) {
    // Parse width
    data = skip_whitespace_and_comments(data, end);
    if (!parse_integer(data, end, header.width)) {
        return false;
    }

    // Parse height
    data = skip_whitespace_and_comments(data, end);
    if (!parse_integer(data, end, header.height)) {
        return false;
    }

    // Parse max value
    data = skip_whitespace_and_comments(data, end);
    if (!parse_integer(data, end, header.max_value)) {
        return false;
    }

    // Skip final whitespace before pixel data
    data = skip_whitespace_and_comments(data, end);

    header.color_mode = ColorMode::GRAYSCALE_8BIT;
    
    // Calculate data bytes
    int bytes_per_pixel = (header.max_value > 255) ? 2 : 1;
    if (strcmp(header.format, "P5") == 0) {
        // Binary PGM
        header.data_bytes = header.width * header.height * bytes_per_pixel;
    } else {
        // ASCII PGM (P2) - estimate
        header.data_bytes = header.width * header.height * 4; // Conservative estimate
    }

    ESP_LOGD(TAG, "PGM header: %dx%d, max=%d, data_bytes=%d", 
             header.width, header.height, header.max_value, header.data_bytes);

    return header.width > 0 && header.height > 0 && header.max_value > 0;
}

bool WebInkImageProcessor::parse_ppm_header(const uint8_t*& data, const uint8_t* end, ImageHeader& header) {
    // Parse width
    data = skip_whitespace_and_comments(data, end);
    if (!parse_integer(data, end, header.width)) {
        return false;
    }

    // Parse height
    data = skip_whitespace_and_comments(data, end);
    if (!parse_integer(data, end, header.height)) {
        return false;
    }

    // Parse max value
    data = skip_whitespace_and_comments(data, end);
    if (!parse_integer(data, end, header.max_value)) {
        return false;
    }

    // Skip final whitespace before pixel data
    data = skip_whitespace_and_comments(data, end);

    header.color_mode = ColorMode::RGB_FULL_COLOR;
    
    // Calculate data bytes
    int bytes_per_pixel = 3; // RGB
    if (strcmp(header.format, "P6") == 0) {
        // Binary PPM
        header.data_bytes = header.width * header.height * bytes_per_pixel;
    } else {
        // ASCII PPM (P3) - estimate
        header.data_bytes = header.width * header.height * bytes_per_pixel * 4; // Conservative
    }

    ESP_LOGD(TAG, "PPM header: %dx%d, max=%d, data_bytes=%d", 
             header.width, header.height, header.max_value, header.data_bytes);

    return header.width > 0 && header.height > 0 && header.max_value > 0;
}

//=============================================================================
// PARSING UTILITIES
//=============================================================================

const uint8_t* WebInkImageProcessor::skip_whitespace_and_comments(const uint8_t* data, const uint8_t* end) {
    while (data < end) {
        if (*data == '#') {
            // Skip comment line
            while (data < end && *data != '\n') {
                data++;
            }
        } else if (*data == ' ' || *data == '\t' || *data == '\r' || *data == '\n') {
            // Skip whitespace
            data++;
        } else {
            break;
        }
    }
    return data;
}

bool WebInkImageProcessor::parse_integer(const uint8_t*& data, const uint8_t* end, int& value) {
    // Skip leading whitespace
    data = skip_whitespace_and_comments(data, end);
    
    const uint8_t* start = data;
    value = 0;
    
    while (data < end && *data >= '0' && *data <= '9') {
        value = value * 10 + (*data - '0');
        data++;
    }
    
    return data > start; // Must have parsed at least one digit
}

void WebInkImageProcessor::skip_utf8_bom(const uint8_t*& data, const uint8_t* end) {
    if ((end - data) >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        data += 3;
        ESP_LOGD(TAG, "Skipped UTF-8 BOM");
    }
}

void WebInkImageProcessor::log_message(const std::string& message) {
    if (log_callback_) {
        log_callback_(message);
    }
    ESP_LOGD(TAG, "%s", message.c_str());
}

} // namespace webink
} // namespace esphome
