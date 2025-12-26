/**
 * @file webink_types.cpp
 * @brief Implementation of utility functions for WebInk types
 * 
 * @author WebInk Component Authors
 * @version 1.0.0
 */

#include "webink_types.h"

#ifdef TEST_TYPES_ONLY
#include <iostream>
#include <cstring>
#endif

namespace esphome {
namespace webink {

//=============================================================================
// ENUM TO STRING CONVERSIONS
//=============================================================================

const char* update_state_to_string(UpdateState state) {
    switch (state) {
        case UpdateState::IDLE:           return "IDLE";
        case UpdateState::WIFI_WAIT:      return "WIFI_WAIT";
        case UpdateState::HASH_CHECK:     return "HASH_CHECK";
        case UpdateState::HASH_REQUEST:   return "HASH_REQUEST";
        case UpdateState::HASH_PARSE:     return "HASH_PARSE";
        case UpdateState::IMAGE_REQUEST:  return "IMAGE_REQUEST";
        case UpdateState::IMAGE_DOWNLOAD: return "IMAGE_DOWNLOAD";
        case UpdateState::IMAGE_PARSE:    return "IMAGE_PARSE";
        case UpdateState::IMAGE_DISPLAY:  return "IMAGE_DISPLAY";
        case UpdateState::DISPLAY_UPDATE: return "DISPLAY_UPDATE";
        case UpdateState::ERROR_DISPLAY:  return "ERROR_DISPLAY";
        case UpdateState::SLEEP_PREPARE:  return "SLEEP_PREPARE";
        case UpdateState::COMPLETE:       return "COMPLETE";
        default:                          return "UNKNOWN";
    }
}

const char* color_mode_to_string(ColorMode mode) {
    switch (mode) {
        case ColorMode::MONO_BLACK_WHITE: return "MONO_BLACK_WHITE";
        case ColorMode::GRAYSCALE_8BIT:   return "GRAYSCALE_8BIT";
        case ColorMode::RGBB_4COLOR:      return "RGBB_4COLOR";
        case ColorMode::RGB_FULL_COLOR:   return "RGB_FULL_COLOR";
        default:                          return "UNKNOWN";
    }
}

const char* error_type_to_string(ErrorType error) {
    switch (error) {
        case ErrorType::NONE:                return "NONE";
        case ErrorType::WIFI_TIMEOUT:       return "WIFI_TIMEOUT";
        case ErrorType::SERVER_UNREACHABLE: return "SERVER_UNREACHABLE";
        case ErrorType::INVALID_RESPONSE:   return "INVALID_RESPONSE";
        case ErrorType::PARSE_ERROR:        return "PARSE_ERROR";
        case ErrorType::MEMORY_ERROR:       return "MEMORY_ERROR";
        case ErrorType::SOCKET_ERROR:       return "SOCKET_ERROR";
        case ErrorType::DISPLAY_ERROR:      return "DISPLAY_ERROR";
        default:                             return "UNKNOWN";
    }
}

#ifdef TEST_TYPES_ONLY
//=============================================================================
// STANDALONE TEST (for types-only testing)
//=============================================================================

int main() {
    std::cout << "WebInk Types Test" << std::endl;
    std::cout << "=================" << std::endl;
    
    // Test enums
    std::cout << "Color modes:" << std::endl;
    std::cout << "  MONO_BLACK_WHITE: " << color_mode_to_string(ColorMode::MONO_BLACK_WHITE) << std::endl;
    std::cout << "  GRAYSCALE_8BIT: " << color_mode_to_string(ColorMode::GRAYSCALE_8BIT) << std::endl;
    std::cout << "  RGBB_4COLOR: " << color_mode_to_string(ColorMode::RGBB_4COLOR) << std::endl;
    std::cout << "  RGB_FULL_COLOR: " << color_mode_to_string(ColorMode::RGB_FULL_COLOR) << std::endl;
    
    std::cout << "\nUpdate states:" << std::endl;
    std::cout << "  IDLE: " << update_state_to_string(UpdateState::IDLE) << std::endl;
    std::cout << "  IMAGE_DOWNLOAD: " << update_state_to_string(UpdateState::IMAGE_DOWNLOAD) << std::endl;
    std::cout << "  COMPLETE: " << update_state_to_string(UpdateState::COMPLETE) << std::endl;
    
    std::cout << "\nError types:" << std::endl;
    std::cout << "  NONE: " << error_type_to_string(ErrorType::NONE) << std::endl;
    std::cout << "  WIFI_TIMEOUT: " << error_type_to_string(ErrorType::WIFI_TIMEOUT) << std::endl;
    std::cout << "  MEMORY_ERROR: " << error_type_to_string(ErrorType::MEMORY_ERROR) << std::endl;
    
    // Test structures
    std::cout << "\nTesting structures:" << std::endl;
    
    DisplayRect rect(10, 20, 800, 480);
    std::cout << "DisplayRect: " << rect.x << "," << rect.y << " " << rect.width << "x" << rect.height << std::endl;
    
    ImageHeader header;
    header.width = 640;
    header.height = 480;
    header.set_format("P4");
    std::cout << "ImageHeader: " << header.format << " " << header.width << "x" << header.height << std::endl;
    
    // Test PixelData zero-copy
    uint8_t test_data[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    PixelData pixels(test_data, 4, 4, 1, 4, ColorMode::MONO_BLACK_WHITE, 0);
    std::cout << "PixelData: " << pixels.width << "x" << pixels.height << ", owns_data=" << pixels.owns_data << std::endl;
    
    const uint8_t* row1 = pixels.get_row_ptr(1);
    std::cout << "Row 1 data: " << (int)row1[0] << "," << (int)row1[1] << "," << (int)row1[2] << "," << (int)row1[3] << std::endl;
    
    std::cout << "\n✅ All types tests passed!" << std::endl;
    return 0;
}
#endif

} // namespace webink
} // namespace esphome
