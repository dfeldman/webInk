/**
 * @file webink_image.h
 * @brief Memory-efficient image format processor for WebInk component
 * 
 * The WebInkImageProcessor class handles parsing of PBM/PGM/PPM image formats
 * with memory-efficient streaming support. It's designed to work within the
 * severe memory constraints of ESP32 devices by processing images in small
 * row-based chunks rather than loading entire images into memory.
 * 
 * Key features:
 * - Row-based streaming for memory efficiency
 * - Support for PBM/PGM/PPM formats (P1-P6)
 * - Multiple color mode support (B&W, grayscale, 4-color, RGB)
 * - Automatic memory requirement calculation
 * - Format validation and error handling
 * - Progressive parsing with yield points
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

namespace esphome {
namespace webink {

/**
 * @class WebInkImageProcessor
 * @brief Memory-efficient image format parser with streaming support
 * 
 * This class provides comprehensive image format parsing for the WebInk component,
 * with a focus on memory efficiency and streaming support for large images.
 * It handles the complete PBM/PGM/PPM format family and automatically manages
 * memory allocation based on available resources.
 * 
 * The processor operates in two phases:
 * 1. Header parsing: Validates format and extracts metadata
 * 2. Data parsing: Streams pixel data in configurable row chunks
 * 
 * Features:
 * - Supports ASCII and binary variants of PBM/PGM/PPM
 * - Row-based streaming to minimize memory usage
 * - Automatic format detection and validation
 * - Color mode conversion and format adaptation
 * - Memory requirement prediction
 * - Progressive parsing with yield support
 * 
 * @example Basic Usage
 * @code
 * WebInkImageProcessor processor;
 * 
 * // Parse header first
 * ImageHeader header;
 * if (processor.parse_header(data, size, header)) {
 *     // Calculate optimal row batch size
 *     int max_rows = processor.calculate_max_rows_for_memory(800, 700);
 *     
 *     // Parse data in chunks
 *     for (int row = 0; row < header.height; row += max_rows) {
 *         PixelData pixels;
 *         if (processor.parse_rows(data, size, header, row, max_rows, pixels)) {
 *             // Process pixel data
 *         }
 *     }
 * }
 * @endcode
 * 
 * @example Memory-Aware Processing
 * @code
 * int available_memory = 700; // bytes available for pixel buffer
 * int max_rows = WebInkImageProcessor::calculate_max_rows_for_memory(
 *     800, ColorMode::MONO_BLACK_WHITE, available_memory);
 * 
 * ESP_LOGI("app", "Can process %d rows at once", max_rows);
 * @endcode
 */
class WebInkImageProcessor {
public:
    /**
     * @brief Constructor with optional logging callback
     * @param log_callback Function for logging messages (optional)
     */
    WebInkImageProcessor(std::function<void(const std::string&)> log_callback = nullptr);

    /**
     * @brief Default destructor
     */
    ~WebInkImageProcessor() = default;

    //=========================================================================
    // HEADER PARSING
    //=========================================================================

    /**
     * @brief Parse image header from data buffer
     * @param data Pointer to image data buffer
     * @param data_size Size of data buffer in bytes
     * @param[out] header Parsed header information
     * @return True if header was parsed successfully
     * 
     * Parses the header portion of PBM/PGM/PPM format images to extract
     * metadata including dimensions, color depth, and format type.
     * Handles both ASCII and binary format variants.
     */
    bool parse_header(const uint8_t* data, int data_size, ImageHeader& header);

    /**
     * @brief Validate image format without full parsing
     * @param data Pointer to image data buffer
     * @param data_size Size of data buffer in bytes
     * @return True if format appears valid
     * 
     * Performs lightweight validation to check if data contains
     * a valid image format header without full parsing.
     */
    bool validate_format(const uint8_t* data, int data_size);

    /**
     * @brief Extract format information from magic number
     * @param data Pointer to start of image data
     * @param data_size Size of available data
     * @param[out] format Format string (e.g., "P4", "P5")
     * @param[out] is_binary True if binary format, false if ASCII
     * @return True if magic number was recognized
     */
    bool extract_format_info(const uint8_t* data, int data_size,
                            std::string& format, bool& is_binary);

    //=========================================================================
    // DATA PARSING
    //=========================================================================

    /**
     * @brief Parse rows of pixel data from image
     * @param data Pointer to complete image data (including header)
     * @param data_size Size of complete data buffer
     * @param header Previously parsed header information
     * @param start_row Starting row number (0-based)
     * @param num_rows Number of rows to parse
     * @param[out] pixels Parsed pixel data (caller owns data pointer)
     * @return True if rows were parsed successfully
     * 
     * Parses a specified range of rows from the image data, creating
     * a pixel buffer containing the requested rows. The caller is
     * responsible for freeing the pixel data when done.
     */
    bool parse_rows(const uint8_t* data, int data_size, const ImageHeader& header,
                   int start_row, int num_rows, PixelData& pixels);

    /**
     * @brief Parse complete image into pixel buffer
     * @param data Pointer to complete image data
     * @param data_size Size of data buffer
     * @param[out] pixels Complete parsed pixel data
     * @return True if image was parsed successfully
     * 
     * Convenience method to parse an entire image at once.
     * Use with caution on memory-constrained devices.
     */
    bool parse_complete_image(const uint8_t* data, int data_size, PixelData& pixels);

    //=========================================================================
    // FORMAT-SPECIFIC PARSERS
    //=========================================================================

    /**
     * @brief Parse PBM format data (monochrome)
     * @param data Pixel data portion (after header)
     * @param header Image header information
     * @param start_row Starting row to parse
     * @param num_rows Number of rows to parse
     * @param[out] pixels Parsed pixel data
     * @return True if parsing succeeded
     */
    bool parse_pbm_data(const uint8_t* data, const ImageHeader& header,
                       int start_row, int num_rows, PixelData& pixels);

    /**
     * @brief Parse PGM format data (grayscale)
     * @param data Pixel data portion (after header)
     * @param header Image header information
     * @param start_row Starting row to parse
     * @param num_rows Number of rows to parse
     * @param[out] pixels Parsed pixel data
     * @return True if parsing succeeded
     */
    bool parse_pgm_data(const uint8_t* data, const ImageHeader& header,
                       int start_row, int num_rows, PixelData& pixels);

    /**
     * @brief Parse PPM format data (full color)
     * @param data Pixel data portion (after header)
     * @param header Image header information
     * @param start_row Starting row to parse
     * @param num_rows Number of rows to parse
     * @param[out] pixels Parsed pixel data
     * @return True if parsing succeeded
     */
    bool parse_ppm_data(const uint8_t* data, const ImageHeader& header,
                       int start_row, int num_rows, PixelData& pixels);

    //=========================================================================
    // FORMAT CONVERSION
    //=========================================================================

    /**
     * @brief Convert pixel data between color modes
     * @param input Source pixel data
     * @param target_mode Desired output color mode
     * @param[out] output Converted pixel data
     * @return True if conversion was successful
     * 
     * Converts pixel data from one color mode to another, handling
     * bit depth changes and color space conversions.
     */
    bool convert_color_mode(const PixelData& input, ColorMode target_mode, PixelData& output);

    /**
     * @brief Apply color palette conversion for 4-color RGBB mode
     * @param input Grayscale or RGB input data
     * @param[out] output 4-color RGBB output data
     * @return True if conversion succeeded
     */
    bool convert_to_rgbb_palette(const PixelData& input, PixelData& output);

    /**
     * @brief Dither grayscale data to monochrome
     * @param input Grayscale input data
     * @param[out] output Monochrome output data
     * @param dither_type Dithering algorithm to use
     * @return True if dithering succeeded
     */
    bool dither_to_monochrome(const PixelData& input, PixelData& output, int dither_type = 0);

    //=========================================================================
    // MEMORY MANAGEMENT UTILITIES
    //=========================================================================

    /**
     * @brief Calculate bytes needed for one row of pixels
     * @param width Image width in pixels
     * @param mode Color mode
     * @return Bytes required for one row
     */
    static int calculate_bytes_per_row(int width, ColorMode mode);

    /**
     * @brief Calculate maximum rows that fit in available memory
     * @param width Image width in pixels
     * @param mode Color mode
     * @param available_bytes Available memory in bytes
     * @return Maximum number of rows that fit in memory
     */
    static int calculate_max_rows_for_memory(int width, ColorMode mode, int available_bytes);

    /**
     * @brief Calculate total memory needed for complete image
     * @param width Image width in pixels
     * @param height Image height in pixels
     * @param mode Color mode
     * @return Total bytes needed for complete image
     */
    static int calculate_total_memory_needed(int width, int height, ColorMode mode);

    /**
     * @brief Get recommended memory allocation for given constraints
     * @param width Image width
     * @param height Image height
     * @param mode Color mode
     * @param max_available_bytes Maximum memory available
     * @param[out] recommended_rows Recommended rows per chunk
     * @param[out] total_chunks Number of chunks needed
     * @return True if allocation is feasible
     */
    static bool get_memory_allocation_recommendation(int width, int height, ColorMode mode,
                                                    int max_available_bytes,
                                                    int& recommended_rows, int& total_chunks);

    //=========================================================================
    // VALIDATION AND UTILITIES
    //=========================================================================

    /**
     * @brief Validate pixel coordinates and ranges
     * @param width Image width
     * @param height Image height
     * @param start_row Starting row
     * @param num_rows Number of rows
     * @return True if coordinates are valid
     */
    static bool validate_pixel_range(int width, int height, int start_row, int num_rows);

    /**
     * @brief Get format description string
     * @param header Image header
     * @return Human-readable format description
     */
    static std::string get_format_description(const ImageHeader& header);

private:
    std::function<void(const std::string&)> log_callback_;  ///< Logging callback
    static const char* TAG;                                 ///< Logging tag

    //=========================================================================
    // HEADER PARSING HELPERS
    //=========================================================================

    /**
     * @brief Parse PBM format header
     * @param[in,out] data Pointer to current position (updated after parsing)
     * @param end Pointer to end of data
     * @param[out] header Header structure to fill
     * @return True if header was parsed successfully
     */
    bool parse_pbm_header(const uint8_t*& data, const uint8_t* end, ImageHeader& header);

    /**
     * @brief Parse PGM format header
     * @param[in,out] data Pointer to current position (updated after parsing)
     * @param end Pointer to end of data
     * @param[out] header Header structure to fill
     * @return True if header was parsed successfully
     */
    bool parse_pgm_header(const uint8_t*& data, const uint8_t* end, ImageHeader& header);

    /**
     * @brief Parse PPM format header
     * @param[in,out] data Pointer to current position (updated after parsing)
     * @param end Pointer to end of data
     * @param[out] header Header structure to fill
     * @return True if header was parsed successfully
     */
    bool parse_ppm_header(const uint8_t*& data, const uint8_t* end, ImageHeader& header);

    //=========================================================================
    // PARSING UTILITIES
    //=========================================================================

    /**
     * @brief Skip whitespace and comments in image data
     * @param data Current position in data
     * @param end End of data buffer
     * @return Pointer to next non-whitespace, non-comment character
     */
    const uint8_t* skip_whitespace_and_comments(const uint8_t* data, const uint8_t* end);

    /**
     * @brief Parse integer value from ASCII data
     * @param[in,out] data Current position (updated after parsing)
     * @param end End of data buffer
     * @param[out] value Parsed integer value
     * @return True if integer was parsed successfully
     */
    bool parse_integer(const uint8_t*& data, const uint8_t* end, int& value);

    /**
     * @brief Detect color mode from format and max value
     * @param format Format string (e.g., "P4")
     * @param max_value Maximum pixel value (for PGM/PPM)
     * @return Detected color mode
     */
    ColorMode detect_color_mode(const std::string& format, int max_value);

    /**
     * @brief Check for UTF-8 BOM and skip if present
     * @param[in,out] data Current position (updated if BOM found)
     * @param end End of data buffer
     */
    void skip_utf8_bom(const uint8_t*& data, const uint8_t* end);

    //=========================================================================
    // DATA CONVERSION HELPERS
    //=========================================================================

    /**
     * @brief Convert single pixel between color modes
     * @param input_pixel Input pixel value
     * @param input_mode Input color mode
     * @param target_mode Target color mode
     * @param max_value Maximum value for input pixel
     * @return Converted pixel value
     */
    uint32_t convert_pixel_color_mode(uint32_t input_pixel, ColorMode input_mode,
                                     ColorMode target_mode, int max_value);

    /**
     * @brief Apply Floyd-Steinberg dithering
     * @param input Grayscale input data
     * @param[out] output Dithered monochrome output
     * @return True if dithering succeeded
     */
    bool apply_floyd_steinberg_dithering(const PixelData& input, PixelData& output);

    /**
     * @brief Log parsing progress
     * @param message Progress message
     */
    void log_message(const std::string& message);
};

} // namespace webink
} // namespace esphome
