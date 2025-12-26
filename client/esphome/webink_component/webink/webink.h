/**
 * @file webink.h
 * @brief Main header for WebInk ESPHome component
 * 
 * This is the primary include file for the WebInk component.
 * Include this file to get access to all WebInk functionality.
 * 
 * @author WebInk Component Authors
 * @version 1.0.0
 */

#pragma once

// Core WebInk types and enums
#include "webink_types.h"

// Configuration management
#include "webink_config.h"

// State management
#include "webink_state.h"

// Network communication
#include "webink_network.h"

// Image processing
#include "webink_image.h"

// Display management
#include "webink_display.h"

// Main controller
#include "webink_controller.h"

// ESPHome integration (only include if ESPHome headers are available)
#ifdef ESPHOME_CORE_COMPONENT_H
#include "webink_esphome.h"
#endif

/**
 * @namespace esphome::webink
 * @brief WebInk component namespace
 * 
 * All WebInk component classes and functions are contained within this namespace.
 * This prevents naming conflicts with other ESPHome components and libraries.
 */
namespace esphome {
namespace webink {

/**
 * @brief Factory function to create a WebInk controller
 * @return Shared pointer to a WebInk controller instance
 * 
 * This is the main entry point for creating a WebInk controller.
 * The controller manages the entire update process and coordinates
 * between all other WebInk components.
 * 
 * @code
 * auto controller = esphome::webink::create_webink_controller();
 * controller->set_config(config);
 * controller->set_display(display);
 * controller->setup();
 * @endcode
 */
std::shared_ptr<WebInkController> create_webink_controller();

/**
 * @brief Factory function to create a WebInk configuration
 * @return Shared pointer to a WebInk configuration instance
 * 
 * Creates a configuration object with default settings.
 * You can modify the configuration after creation.
 * 
 * @code
 * auto config = esphome::webink::create_webink_config();
 * config->set_server_url("http://192.168.1.100:8090");
 * config->set_device_id("my-display");
 * @endcode
 */
std::shared_ptr<WebInkConfig> create_webink_config();

} // namespace webink
} // namespace esphome
