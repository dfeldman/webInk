# webInk - display web pages on cheap, low-power eInk displays

## WARNING
This isn't ready for real users yet!!!

## What is it? 
There are now many cheap, low-power eInk displays that you can mount anywhere and go for months between charges. Wouldn't it be great if you could use them to show useful content like your calendar, news, weather, and home dashboard? 

webInk is a Python web server that captures and renders any web page in a tiled image format that's perfect for these low-memory displays. It also includes a client that runs on many of these devices that fetches the images and displays them with automatic refreshes. Finally, the app folder provides a few sample applications, including a smart home dashboard, room reservation system, and news aggregator. 

## Quick Start

1) Start the server on a Raspberry Pi or laptop: docker run --rm -p 8000:8000 -p 8080:8080 -p 8091:8091 --name webink webink:1.0.0 

2) Use ESPHome Web to flash the pre-built firmware image for your device, or build a custome firmware image using the ESPHome IDE.

3) Use the captive Wi-Fi portal (SSID: webInk-01) to configure your device with your Wi-Fi credentials.

4) The device will display its own IP address on-screen. Navigate to the web configuration interface on that IP address, and configure it to point at the webInk server.
 
(By default, the server address is homeassistant.local, so it will automatically connect if you happen to be running the webInk server on the same box as Home Assistant and mDNS is working. It's fine if you use a separate server, though - they are not integrated in any way.)

## Components
### Python Server
The Python server handles web page capture, rendering, and image tiling for the eInk displays. It uses Playwright to render the web pages using Chrome. It can then serve the images either over HTTP, or using a custom socket-based protocol which may be more efficient for large displays. 

### Client Firmware
The devices webInk is designed for are ESP32-based eInk displays. The ESP32 is a low-cost system-on-a-chip that has built-in Wi-Fi, but it has so little memory that it cannot render very much on the screen. These devices are now available for as little as $40 for a plug-in model, to $100 for a large-screen battery-powered model.

The webInk client is written as an ESPHome YAML file. Because it uses ESPHome, it has captive Wi-Fi configuration, OTA updates, a web interface, and a connection to the ESPHome dashboard. It's also easy to customize with additional ESPHome components.

A pre-built image that should work for most ESP32-C3 based devices is in client/build/webInk-esp32-c3.bin. You can flash it in your web browser with ESPHome Web, or on the command line with esp32.py.

If you need a nonstandard configuration, you'll have to do your own build. The easiest way is to set up ESPHome and then upload the file client/webInk.yaml into ESPHome, then use its web interface to build and flash the device. 

## Demo Apps
A few demo apps are included to help get started quickly with your eInk display. You'll almost certainly need to customize them for your specific use case. 

### Home Dashboard
Home Dashboard is a customizable personal dashboard. It can show weather, calendar, Home Assistant sensors, RSS feeds, and other data sources. It can show up to four of these at a time. 

## Alternatives
- sibbl/hass-lovelace-screensaver exports any Home Assistant dashboard as a static image
- TRMNL controls the same type of eInk displays, but without using web tech
- OpenEPaperLink for small price-tag-like displays, which require additional work beyond just supporting Wi-Fi
- SenseHMI SeedStudio's attempt at a dashboard builder
