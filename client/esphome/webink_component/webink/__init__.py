"""
WebInk ESPHome Component

This module provides ESPHome integration for the WebInk e-ink display component.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, font, deep_sleep, binary_sensor
from esphome.const import CONF_ID

# Define the WebInk namespace and component class
webink_ns = cg.esphome_ns.namespace("webink")
WebInkESPHomeComponent = webink_ns.class_("WebInkESPHomeComponent", cg.Component)

# Configuration schema
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WebInkESPHomeComponent),
        cv.Required("server_url"): cv.string,
        cv.Required("device_id"): cv.string, 
        cv.Required("api_key"): cv.string,
        cv.Optional("display_mode", default="800x480x1xB"): cv.string,
        cv.Optional("socket_port", default=8091): cv.int_,
        cv.Optional("rows_per_slice", default=8): cv.int_range(min=1, max=64),
        cv.Required("display_id"): cv.use_id(display.Display),
        cv.Optional("normal_font"): cv.use_id(font.Font),
        cv.Optional("large_font"): cv.use_id(font.Font),
        cv.Optional("deep_sleep_id"): cv.use_id(deep_sleep.DeepSleepComponent),
        cv.Optional("boot_button_id"): cv.use_id(binary_sensor.BinarySensor),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    """Generate the C++ code for the WebInk ESPHome component."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set configuration values
    cg.add(var.set_server_url(config["server_url"]))
    cg.add(var.set_device_id(config["device_id"]))
    cg.add(var.set_api_key(config["api_key"]))
    cg.add(var.set_display_mode(config["display_mode"]))
    cg.add(var.set_socket_port(config["socket_port"]))
    cg.add(var.set_rows_per_slice(config["rows_per_slice"]))

    # Link to required components
    display_component = await cg.get_variable(config["display_id"])
    cg.add(var.set_display_component(display_component))

    # Optional components
    if "normal_font" in config:
        font_component = await cg.get_variable(config["normal_font"])
        cg.add(var.set_normal_font(font_component))
    
    if "large_font" in config:
        font_component = await cg.get_variable(config["large_font"])
        cg.add(var.set_large_font(font_component))
        
    if "deep_sleep_id" in config:
        sleep_component = await cg.get_variable(config["deep_sleep_id"])
        cg.add(var.set_deep_sleep_component(sleep_component))
        
    if "boot_button_id" in config:
        button_component = await cg.get_variable(config["boot_button_id"])
        cg.add(var.set_boot_button(button_component))

    # Include the main WebInk component header
    cg.add_global(cg.RawStatement('#include "webink_esphome.h"'))
