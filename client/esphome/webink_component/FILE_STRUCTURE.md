# WebInk Component File Structure

## ⚠️ IMPORTANT: Which Files to Edit

### For ESP32/ESPHome Production Code
**Edit files in `webink/` subdirectory:**
- `webink/webink_controller.cpp` ✅ (ESP32 production)
- `webink/webink_esphome.cpp` ✅ (ESP32 production)
- `webink/webink_network.cpp` ✅ (ESP32 production)
- `webink/webink_config.cpp` ✅ (ESP32 production)
- etc.

### For Mac Testing Only
**Root directory files are for Mac development/testing:**
- `webink_controller.cpp` (Mac testing copy)
- `webink_esphome.cpp` (Mac testing copy)
- `webink_network.cpp` (Mac testing copy)
- etc.

These are **separate copies** used by the Makefile for local Mac builds.

## Build Systems

### ESP32 (ESPHome)
- Uses files from `webink/` subdirectory
- Configured in `webink/__init__.py`
- Build with: `esphome compile webInk.yaml`

### Mac Testing (Makefile)
- Uses files from root directory
- Configured in `Makefile`
- Build with: `make test-types`, `make test-protocol`, etc.

## Rule of Thumb
**When fixing bugs or adding features for the ESP32 device, always edit files in `webink/` subdirectory!**
