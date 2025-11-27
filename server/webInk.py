#!/usr/bin/env python3
"""
webInk - Web Page Snapshot Server for eInk Devices

A server that captures web pages using Playwright and serves them as tiled images
to low-memory eInk devices. Supports multiple resolutions and color modes.

Usage: uv run webInk.py
"""

import asyncio
import hashlib
import io
import json
import logging
import os
import sys
import threading
import time
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Any

import yaml
from PIL import Image, ImageOps
import numpy as np
from fastapi import FastAPI, HTTPException, Query, Request
from fastapi.responses import HTMLResponse, Response, FileResponse
from fastapi.staticfiles import StaticFiles
import uvicorn
from playwright.async_api import async_playwright

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Constants
CONFIG_FILE = "config.yaml"
DATA_DIR = Path("data")
CLIENT_DATA_FILE = DATA_DIR / "clients.json"
DEFAULT_REFRESH_INTERVAL = 600  # seconds
SNAPSHOT_LEAD_TIME = 5  # seconds before refresh to take snapshot
DEFAULT_SLEEP_CHECK_INTERVAL = 30  # seconds for no-sleep mode

app = FastAPI(title="webInk Server")


class Config:
    """Configuration manager for webInk"""
    
    def __init__(self, config_path: str = CONFIG_FILE):
        self.config_path = config_path
        self.pages = {}
        self.devices = {}
        self.supported_modes = []
        self.api_key = ""
        self.socket_port = 8091
        self.load_config()
        
    def load_config(self):
        """Load configuration from YAML file"""
        try:
            with open(self.config_path, 'r') as f:
                data = yaml.safe_load(f)
                
            # Parse pages
            for page_config in data.get('pages', []):
                for page_id, page_data in page_config.items():
                    if isinstance(page_data, list):
                        page_info = {}
                        for item in page_data:
                            if isinstance(item, dict):
                                page_info.update(item)
                        
                        self.pages[page_id] = {
                            'url': page_info.get('url', ''),
                            'refresh_interval': page_info.get('refresh_interval', DEFAULT_REFRESH_INTERVAL),
                            'suppress_refresh': page_info.get('suppress_refresh', {}),
                            'mandatory_refresh': page_info.get('mandatory_refresh', []),
                            'zoom_level': page_info.get('zoom_level', 1.0),
                            'rotation': page_info.get('rotation', 0),
                            'scroll_to_element': page_info.get('scroll_to_element', None)
                        }
            
            # Parse devices
            for device_config in data.get('devices', []):
                for device_name, device_data in device_config.items():
                    if isinstance(device_data, list):
                        for item in device_data:
                            if isinstance(item, dict) and 'page' in item:
                                self.devices[device_name] = {'page': item['page']}
            
            # Parse supported modes
            self.supported_modes = data.get('supported_modes', [])
            
            # Parse API key
            self.api_key = data.get('api_key', 'myapikey')
            
            # Parse socket port
            self.socket_port = data.get('socket_port', 8091)
            
            logger.info(f"Loaded config: {len(self.pages)} pages, {len(self.devices)} devices, {len(self.supported_modes)} modes, socket_port={self.socket_port}")
            
        except Exception as e:
            logger.error(f"Failed to load config: {e}")
            sys.exit(1)


class ClientManager:
    """Manages client device data and persistence"""
    
    def __init__(self, data_file: Path = CLIENT_DATA_FILE):
        self.data_file = data_file
        self.clients = {}
        self.load_clients()
        
    def load_clients(self):
        """Load client data from JSON file"""
        if self.data_file.exists():
            try:
                with open(self.data_file, 'r') as f:
                    self.clients = json.load(f)
                logger.info(f"Loaded {len(self.clients)} clients from {self.data_file}")
            except Exception as e:
                logger.error(f"Failed to load client data: {e}")
                self.clients = {}
        else:
            self.clients = {}
    
    def save_clients(self):
        """Save client data to JSON file"""
        try:
            with open(self.data_file, 'w') as f:
                json.dump(self.clients, f, indent=2, default=str)
        except Exception as e:
            logger.error(f"Failed to save client data: {e}")
    
    def update_client(self, device_name: str, data: Dict):
        """Update client information"""
        if device_name not in self.clients:
            self.clients[device_name] = {
                'first_seen': datetime.now().isoformat(),
                'api_calls': 0,
                'sleep_disabled': False
            }
        
        self.clients[device_name].update(data)
        self.clients[device_name]['last_seen'] = datetime.now().isoformat()
        self.clients[device_name]['api_calls'] = self.clients[device_name].get('api_calls', 0) + 1
        self.save_clients()
        
    def get_client_info(self, device_name: str) -> Dict:
        """Get client information"""
        return self.clients.get(device_name, {})


class SocketServer:
    """TCP/IP socket server for low-power embedded devices
    
    Protocol: webInkV1
    Format: webInkV1 <api_key> <device> <mode> <x> <y> <w> <h> <format>\n
    Response: Raw pixel data (PBM/PGM/PPM format without header)
    
    This allows embedded devices to:
    - Use a fixed-size buffer (request N pixels, get N pixels)
    - Avoid HTTP overhead and parsing
    - Avoid resizable buffers
    """
    
    def __init__(self, config: Config, client_manager, snapshot_manager):
        self.config = config
        self.client_manager = client_manager
        self.snapshot_manager = snapshot_manager
        self.server = None
        
    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        """Handle a single client connection"""
        addr = writer.get_extra_info('peername')
        logger.info(f"[SOCKET] New connection from {addr}")
        
        try:
            # Read request line (max 512 bytes)
            request_line = await asyncio.wait_for(reader.readline(), timeout=5.0)
            request = request_line.decode('utf-8').strip()
            
            logger.info(f"[SOCKET] Request from {addr}: {request}")
            
            # Parse request: webInkV1 <api_key> <device> <mode> <x> <y> <w> <h> <format>
            parts = request.split()
            
            if len(parts) != 9:
                error_msg = f"ERROR: Invalid request format. Expected 9 parts, got {len(parts)}\n"
                writer.write(error_msg.encode('utf-8'))
                await writer.drain()
                logger.warning(f"[SOCKET] Invalid request from {addr}: wrong number of parameters")
                return
            
            protocol, api_key, device, mode, x_str, y_str, w_str, h_str, format_str = parts
            
            # Validate protocol version
            if protocol != "webInkV1":
                error_msg = f"ERROR: Unsupported protocol '{protocol}'. Expected 'webInkV1'\n"
                writer.write(error_msg.encode('utf-8'))
                await writer.drain()
                logger.warning(f"[SOCKET] Invalid protocol from {addr}: {protocol}")
                return
            
            # Validate API key
            if api_key != self.config.api_key:
                error_msg = "ERROR: Invalid API key\n"
                writer.write(error_msg.encode('utf-8'))
                await writer.drain()
                logger.warning(f"[SOCKET] Invalid API key from {addr}")
                return
            
            # Parse coordinates
            try:
                x = int(x_str)
                y = int(y_str)
                w = int(w_str)
                h = int(h_str)
            except ValueError as e:
                error_msg = f"ERROR: Invalid coordinates: {e}\n"
                writer.write(error_msg.encode('utf-8'))
                await writer.drain()
                logger.warning(f"[SOCKET] Invalid coordinates from {addr}: {e}")
                return
            
            # Validate format
            if format_str not in ['pbm', 'pgm', 'ppm']:
                error_msg = f"ERROR: Invalid format '{format_str}'. Expected pbm, pgm, or ppm\n"
                writer.write(error_msg.encode('utf-8'))
                await writer.drain()
                logger.warning(f"[SOCKET] Invalid format from {addr}: {format_str}")
                return
            
            # Update client info
            self.client_manager.update_client(device, {'mode': mode, 'connection_type': 'socket'})
            
            # Get device page
            device_info = self.config.devices.get(device, self.config.devices.get('default', {}))
            page_id = device_info.get('page')
            
            if not page_id:
                error_msg = "ERROR: No page configured for device\n"
                writer.write(error_msg.encode('utf-8'))
                await writer.drain()
                logger.warning(f"[SOCKET] No page configured for device {device}")
                return
            
            # Check if mode is supported
            if mode not in self.config.supported_modes:
                error_msg = f"ERROR: Unsupported mode: {mode}\n"
                writer.write(error_msg.encode('utf-8'))
                await writer.drain()
                logger.warning(f"[SOCKET] Unsupported mode from {addr}: {mode}")
                return
            
            # Load image
            filename = DATA_DIR / f"{page_id}_{mode}.png"
            
            if not filename.exists():
                error_msg = f"ERROR: Image not available for {page_id} in mode {mode}\n"
                writer.write(error_msg.encode('utf-8'))
                await writer.drain()
                logger.warning(f"[SOCKET] Image not available: {filename}")
                return
            
            # Open and crop image
            img = Image.open(filename)
            
            # Validate crop parameters
            if x < 0 or y < 0 or x + w > img.width or y + h > img.height:
                error_msg = f"ERROR: Invalid crop parameters (image is {img.width}x{img.height})\n"
                writer.write(error_msg.encode('utf-8'))
                await writer.drain()
                logger.warning(f"[SOCKET] Invalid crop from {addr}: x={x} y={y} w={w} h={h}")
                return
            
            # Crop the image
            cropped = img.crop((x, y, x + w, y + h))
            
            # Convert to requested format and extract raw pixel data
            if format_str == 'pbm':
                # PBM format for 1-bit images
                if cropped.mode != '1':
                    cropped = cropped.convert('1')
                
                # Generate PBM data
                output = io.BytesIO()
                cropped.save(output, format='PPM')
                full_data = output.getvalue()
                
                # Extract raw pixel data (skip header)
                raw_data = self._extract_raw_pixels(full_data, w, h, 1)
                
            elif format_str == 'pgm':
                # PGM format for grayscale
                if cropped.mode != 'L':
                    cropped = cropped.convert('L')
                
                # Generate PGM data
                output = io.BytesIO()
                cropped.save(output, format='PPM')
                full_data = output.getvalue()
                
                # Extract raw pixel data (skip header)
                raw_data = self._extract_raw_pixels(full_data, w, h, 8)
                
            elif format_str == 'ppm':
                # PPM format for color
                if cropped.mode != 'RGB':
                    cropped = cropped.convert('RGB')
                
                # Generate PPM data
                output = io.BytesIO()
                cropped.save(output, format='PPM')
                full_data = output.getvalue()
                
                # Extract raw pixel data (skip header)
                raw_data = self._extract_raw_pixels(full_data, w, h, 24)
            
            # Send raw pixel data
            writer.write(raw_data)
            await writer.drain()
            
            logger.info(f"[SOCKET] Sent {len(raw_data)} bytes to {addr} (device={device}, {w}x{h}, format={format_str})")
            
        except asyncio.TimeoutError:
            logger.warning(f"[SOCKET] Timeout reading from {addr}")
        except Exception as e:
            logger.error(f"[SOCKET] Error handling client {addr}: {e}")
            try:
                error_msg = f"ERROR: {str(e)}\n"
                writer.write(error_msg.encode('utf-8'))
                await writer.drain()
            except:
                pass
        finally:
            try:
                writer.close()
                await writer.wait_closed()
            except:
                pass
    
    def _extract_raw_pixels(self, pnm_data: bytes, width: int, height: int, bits_per_pixel: int) -> bytes:
        """Extract raw pixel data from PNM format (skip header)
        
        Args:
            pnm_data: Full PNM file data (with header)
            width: Image width
            height: Image height
            bits_per_pixel: 1 for PBM, 8 for PGM, 24 for PPM
            
        Returns:
            Raw pixel data without header
        """
        # Find the end of the header (after width, height, and optional maxval)
        # PBM: P4 width height [binary data]
        # PGM: P5 width height maxval [binary data]
        # PPM: P6 width height maxval [binary data]
        
        header_end = 0
        lines_found = 0
        i = 0
        
        # Skip magic number (P4, P5, or P6)
        while i < len(pnm_data) and pnm_data[i:i+1] != b'\n':
            i += 1
        i += 1  # Skip newline
        
        # Skip comments and find width/height
        while i < len(pnm_data) and lines_found < 2:
            # Skip comments
            if pnm_data[i:i+1] == b'#':
                while i < len(pnm_data) and pnm_data[i:i+1] != b'\n':
                    i += 1
                i += 1
                continue
            
            # Skip whitespace
            if pnm_data[i:i+1] in (b' ', b'\t', b'\r', b'\n'):
                i += 1
                continue
            
            # Found a token (width, height, or maxval)
            token_start = i
            while i < len(pnm_data) and pnm_data[i:i+1] not in (b' ', b'\t', b'\r', b'\n'):
                i += 1
            
            lines_found += 1
            
            # For PGM/PPM, we need to skip maxval too
            if bits_per_pixel > 1 and lines_found == 2:
                # Skip whitespace before maxval
                while i < len(pnm_data) and pnm_data[i:i+1] in (b' ', b'\t', b'\r', b'\n'):
                    i += 1
                # Skip maxval
                while i < len(pnm_data) and pnm_data[i:i+1] not in (b' ', b'\t', b'\r', b'\n'):
                    i += 1
                lines_found += 1
        
        # Skip final whitespace before binary data
        while i < len(pnm_data) and pnm_data[i:i+1] in (b' ', b'\t', b'\r', b'\n'):
            i += 1
        
        header_end = i
        
        # Return raw pixel data
        return pnm_data[header_end:]
    
    async def start(self):
        """Start the socket server"""
        self.server = await asyncio.start_server(
            self.handle_client,
            '0.0.0.0',
            self.config.socket_port
        )
        
        addr = self.server.sockets[0].getsockname()
        logger.info(f"[SOCKET] TCP/IP socket server started on {addr[0]}:{addr[1]}")
        logger.info(f"[SOCKET] Protocol: webInkV1")
        
    async def stop(self):
        """Stop the socket server"""
        if self.server:
            self.server.close()
            await self.server.wait_closed()
            logger.info("[SOCKET] Socket server stopped")


class ImageProcessor:
    """Handles image processing and dithering for different modes"""
    
    @staticmethod
    def parse_mode(mode: str) -> Tuple[int, int, int, str]:
        """Parse mode string like '800x480x1xB' into components"""
        parts = mode.split('x')
        if len(parts) != 4:
            raise ValueError(f"Invalid mode format: {mode}")
        
        width = int(parts[0])
        height = int(parts[1])
        bits = int(parts[2])
        color_mode = parts[3]
        
        return width, height, bits, color_mode
    
    @staticmethod
    def downscale_image(img: Image.Image, target_width: int, target_height: int) -> Image.Image:
        """Downscale image to target resolution using high-quality Lanczos resampling"""
        if img.width == target_width and img.height == target_height:
            return img
        
        logger.info(f"Downscaling image from {img.width}x{img.height} to {target_width}x{target_height}")
        return img.resize((target_width, target_height), Image.LANCZOS)
    
    @staticmethod
    def dither_image(img: Image.Image, bits: int, color_mode: str) -> Image.Image:
        """Dither image to specified bit depth and color mode"""
        if color_mode == 'B':  # Black and white
            if bits == 1:
                # Convert to 1-bit black and white with dithering
                img = img.convert('L')  # Convert to grayscale first
                img = img.convert('1', dither=Image.FLOYDSTEINBERG)
            else:
                raise ValueError(f"Unsupported bit depth {bits} for B&W mode")
                
        elif color_mode == 'G':  # Grayscale
            img = img.convert('L')
            if bits == 2:
                # 4-level grayscale with proper mapping to full range
                # Convert to numpy for easier manipulation
                img_array = np.array(img)
                # Quantize to 4 levels (0, 1, 2, 3) then map to full range (0, 85, 170, 255)
                quantized = (img_array // 64).astype(np.uint8)  # 0-3
                # Map to full 8-bit range: 0->0, 1->85, 2->170, 3->255
                img_array = quantized * 85
                img = Image.fromarray(img_array, mode='L')
            elif bits == 8:
                pass  # Already 8-bit grayscale
            else:
                raise ValueError(f"Unsupported bit depth {bits} for grayscale")
                
        elif color_mode == 'RGB':  # Color
            if bits == 2:
                # 4 colors: black, red, green, blue
                # Convert to limited palette
                palette = [
                    0, 0, 0,      # Black
                    255, 0, 0,    # Red
                    0, 255, 0,    # Green
                    0, 0, 255,    # Blue
                ]
                # Extend palette to 256 colors (required by PIL)
                palette.extend([0, 0, 0] * 252)
                
                # Create palette image
                palette_img = Image.new('P', (1, 1))
                palette_img.putpalette(palette)
                
                # Quantize to 4 colors
                img = img.convert('RGB').quantize(palette=palette_img, dither=Image.FLOYDSTEINBERG)
                img = img.convert('RGB')
                
            elif bits == 8:
                img = img.convert('RGB')
            else:
                raise ValueError(f"Unsupported bit depth {bits} for RGB")
        
        return img
    
    @staticmethod
    def image_to_pbm(img: Image.Image) -> bytes:
        """Convert image to PBM format (1-bit)"""
        if img.mode != '1':
            img = img.convert('1')
        
        output = io.BytesIO()
        img.save(output, format='PPM')
        return output.getvalue()
    
    @staticmethod
    def image_to_ppm(img: Image.Image) -> bytes:
        """Convert image to PPM format (multi-bit)"""
        output = io.BytesIO()
        img.save(output, format='PPM')
        return output.getvalue()


class SnapshotManager:
    """Manages web page snapshots using Playwright"""
    
    def __init__(self, config: Config):
        self.config = config
        self.next_refresh_times = {}
        self.last_render_duration = {}  # Track render duration in seconds
        self.initialize_refresh_times()
        
    def initialize_refresh_times(self):
        """Initialize next refresh times for all pages"""
        now = datetime.now()
        # Use estimated total render time for initial scheduling
        # Assume 30s per page if we don't have actual data yet
        estimated_total_render = len(self.config.pages) * 30.0
        lead_time = estimated_total_render + SNAPSHOT_LEAD_TIME
        
        for page_id, page_info in self.config.pages.items():
            refresh_interval = page_info['refresh_interval']
            # Set initial refresh time with estimated lead time
            self.next_refresh_times[page_id] = now + timedelta(seconds=refresh_interval - lead_time)
    
    def should_snapshot(self, page_id: str) -> bool:
        """Check if a page should be snapshotted now"""
        if page_id not in self.next_refresh_times:
            return False
        
        now = datetime.now()
        page_info = self.config.pages[page_id]
        
        # Check suppress_refresh periods
        suppress = page_info.get('suppress_refresh', {})
        if suppress:
            current_time = now.time()
            if 'start' in suppress and 'end' in suppress:
                start_time = datetime.strptime(suppress['start'], '%H:%M').time()
                end_time = datetime.strptime(suppress['end'], '%H:%M').time()
                
                if start_time <= current_time <= end_time:
                    # In suppression period, skip
                    return False
        
        return now >= self.next_refresh_times[page_id]
    
    def calculate_total_render_time(self) -> float:
        """Calculate total estimated render time for all pages"""
        total = 0.0
        for page_id in self.config.pages:
            # Use last render duration if available, otherwise estimate
            duration = self.last_render_duration.get(page_id, 30.0)  # Default 30s if unknown
            total += duration
        return total
    
    def update_next_refresh(self, page_id: str):
        """Update next refresh time for a page based on total render time"""
        page_info = self.config.pages[page_id]
        refresh_interval = page_info['refresh_interval']
        
        # Calculate total time needed to render all pages
        total_render_time = self.calculate_total_render_time()
        
        # Add slack time for safety
        lead_time = total_render_time + SNAPSHOT_LEAD_TIME
        
        # Schedule next refresh
        self.next_refresh_times[page_id] = datetime.now() + timedelta(seconds=refresh_interval - lead_time)
        
        logger.info(f"Next snapshot for '{page_id}' scheduled at {self.next_refresh_times[page_id]} "
                   f"(lead time: {lead_time:.1f}s = {total_render_time:.1f}s render + {SNAPSHOT_LEAD_TIME}s slack)")
    
    async def capture_page(self, page_id: str):
        """Capture screenshots of a page in all supported modes"""
        if page_id not in self.config.pages:
            logger.error(f"Unknown page: {page_id}")
            return
        
        page_info = self.config.pages[page_id]
        url = page_info['url']
        zoom_level = page_info.get('zoom_level', 1.0)
        scroll_to_element = page_info.get('scroll_to_element', None)
        rotation = page_info.get('rotation', 0)
        
        if rotation not in (0, 90, -90, 180):
            logger.warning(f"Invalid rotation {rotation} for page '{page_id}'. Expected one of -90, 90, 180, or 0. Using 0.")
            rotation = 0
        
        logger.info(f"Starting capture for page '{page_id}' ({url}) with zoom_level={zoom_level}, rotation={rotation}, scroll_to_element={scroll_to_element}")
        
        # Track total render time
        start_time = time.time()
        
        # Capture each mode separately to avoid browser issues
        for mode in self.config.supported_modes:
            try:
                width, height, bits, color_mode = ImageProcessor.parse_mode(mode)
                
                # Calculate capture resolution based on zoom level
                capture_width = int(width * zoom_level)
                capture_height = int(height * zoom_level)
                if abs(rotation) == 90:
                    capture_width, capture_height = capture_height, capture_width
                
                async with async_playwright() as p:
                    browser = await p.chromium.launch(headless=True)
                    
                    try:
                        # Create browser context with zoomed viewport size
                        context = await browser.new_context(
                            viewport={'width': capture_width, 'height': capture_height},
                            device_scale_factor=1.0
                        )
                        page = await context.new_page()
                        
                        # Navigate to URL with shorter timeout and less strict wait
                        await page.goto(url, wait_until='domcontentloaded', timeout=30000)
                        
                        # Wait a bit for any dynamic content
                        await asyncio.sleep(2)
                        
                        # Scroll to element if specified
                        if scroll_to_element:
                            try:
                                # Try to find the element and scroll it into view
                                element = await page.query_selector(scroll_to_element)
                                if element:
                                    await element.scroll_into_view_if_needed()
                                    logger.info(f"Scrolled to element: {scroll_to_element}")
                                    # Wait a moment after scrolling for any lazy-loaded content
                                    await asyncio.sleep(1)
                                else:
                                    logger.warning(f"Element not found: {scroll_to_element}")
                            except Exception as e:
                                logger.error(f"Failed to scroll to element '{scroll_to_element}': {e}")
                        
                        # Take screenshot
                        screenshot_bytes = await page.screenshot(full_page=False)
                        
                        # Process image
                        img = Image.open(io.BytesIO(screenshot_bytes))
                        
                        if rotation in (90, -90, 180):
                            img = img.rotate(-rotation, expand=True)
                        
                        # Downscale if zoom level was used
                        if zoom_level > 1.0 or abs(rotation) == 90:
                            img = ImageProcessor.downscale_image(img, width, height)
                        
                        img = ImageProcessor.dither_image(img, bits, color_mode)
                        
                        # Save to file
                        filename = DATA_DIR / f"{page_id}_{mode}.png"
                        img.save(filename, 'PNG')
                        logger.info(f"Saved snapshot: {filename}")
                        
                        await context.close()
                        
                    finally:
                        await browser.close()
                        
            except Exception as e:
                error_msg = str(e)
                if "Executable doesn't exist" in error_msg or "browser is not installed" in error_msg.lower():
                    logger.error(f"Playwright browser not installed. Please run: uv run playwright install chromium")
                    return  # Don't try other modes if browser isn't installed
                else:
                    logger.error(f"Failed to capture {page_id} in mode {mode}: {e}")
        
        # Record render duration
        duration = time.time() - start_time
        self.last_render_duration[page_id] = duration
        logger.info(f"Total render time for '{page_id}': {duration:.2f}s")
        
        self.update_next_refresh(page_id)
    
    def get_image_hash(self, page_id: str, mode: str) -> Optional[str]:
        """Get hash of an image file"""
        filename = DATA_DIR / f"{page_id}_{mode}.png"
        
        if not filename.exists():
            return None
        
        try:
            with open(filename, 'rb') as f:
                content = f.read()
                hash_obj = hashlib.sha1(content)
                return hash_obj.hexdigest()[:8]
        except Exception as e:
            logger.error(f"Failed to get hash for {filename}: {e}")
            return None
    
    async def capture_missing_snapshots(self):
        """Capture snapshots for all pages that don't have any images yet"""
        pages_to_capture = []
        
        for page_id in self.config.pages:
            # Check if at least one snapshot exists for this page
            has_snapshot = False
            for mode in self.config.supported_modes:
                filename = DATA_DIR / f"{page_id}_{mode}.png"
                if filename.exists():
                    has_snapshot = True
                    break
            
            if not has_snapshot:
                pages_to_capture.append(page_id)
        
        if pages_to_capture:
            logger.info(f"Capturing initial snapshots for {len(pages_to_capture)} pages: {', '.join(pages_to_capture)}")
            for page_id in pages_to_capture:
                try:
                    await self.capture_page(page_id)
                except Exception as e:
                    logger.error(f"Failed to capture initial snapshot for {page_id}: {e}")
        else:
            logger.info("All pages have existing snapshots")
    
    def get_sleep_seconds(self, device_name: str) -> int:
        """Calculate sleep time for a device"""
        # Check if sleep is disabled for this device
        client_info = client_manager.get_client_info(device_name)
        if client_info.get('sleep_disabled', False):
            return 0
        
        # Get the page for this device
        device_info = self.config.devices.get(device_name, self.config.devices.get('default', {}))
        page_id = device_info.get('page')
        
        if not page_id or page_id not in self.config.pages:
            return DEFAULT_REFRESH_INTERVAL
        
        page_info = self.config.pages[page_id]
        refresh_interval = page_info['refresh_interval']
        
        now = datetime.now()
        
        # Check for mandatory refresh times
        mandatory_times = page_info.get('mandatory_refresh', [])
        if mandatory_times:
            # Find next mandatory refresh
            for time_str in mandatory_times:
                try:
                    refresh_time = datetime.strptime(time_str, '%H:%M').time()
                    next_refresh = datetime.combine(now.date(), refresh_time)
                    
                    if next_refresh <= now:
                        # Already passed today, schedule for tomorrow
                        next_refresh += timedelta(days=1)
                    
                    seconds_until = (next_refresh - now).total_seconds()
                    
                    # If this is sooner than the regular interval, use it
                    if seconds_until < refresh_interval:
                        return int(seconds_until)
                        
                except Exception as e:
                    logger.error(f"Failed to parse mandatory refresh time '{time_str}': {e}")
        
        # Check suppress periods
        suppress = page_info.get('suppress_refresh', {})
        if suppress and 'start' in suppress and 'end' in suppress:
            try:
                start_time = datetime.strptime(suppress['start'], '%H:%M').time()
                end_time = datetime.strptime(suppress['end'], '%H:%M').time()
                
                current_time = now.time()
                
                # If we're in suppression period, sleep until it ends
                if start_time <= current_time <= end_time:
                    end_datetime = datetime.combine(now.date(), end_time)
                    seconds_until_end = (end_datetime - now).total_seconds()
                    return int(seconds_until_end)
                    
                # If suppression period starts within the refresh interval
                suppress_start = datetime.combine(now.date(), start_time)
                if suppress_start <= now:
                    suppress_start += timedelta(days=1)
                    
                seconds_until_suppress = (suppress_start - now).total_seconds()
                if seconds_until_suppress < refresh_interval:
                    # Sleep until suppression starts
                    return int(seconds_until_suppress)
                    
            except Exception as e:
                logger.error(f"Failed to parse suppress times: {e}")
        
        return refresh_interval


# Global instances
config = Config()
client_manager = ClientManager()
snapshot_manager = SnapshotManager(config)
socket_server = SocketServer(config, client_manager, snapshot_manager)


def snapshot_worker():
    """Background worker thread for taking snapshots"""
    logger.info("Snapshot worker started")
    
    while True:
        try:
            for page_id in config.pages:
                if snapshot_manager.should_snapshot(page_id):
                    logger.info(f"Triggering snapshot for page '{page_id}'")
                    # Run async capture in sync context
                    asyncio.run(snapshot_manager.capture_page(page_id))
            
            time.sleep(1)  # Check every second
            
        except Exception as e:
            logger.error(f"Error in snapshot worker: {e}")
            time.sleep(5)  # Wait a bit on error


# API Endpoints

@app.on_event("startup")
async def startup_event():
    """Initialize the application"""
    # Ensure data directory exists
    DATA_DIR.mkdir(exist_ok=True)
    
    # Capture initial snapshots for any pages without images
    await snapshot_manager.capture_missing_snapshots()
    
    # Start snapshot worker thread
    worker_thread = threading.Thread(target=snapshot_worker, daemon=True)
    worker_thread.start()
    
    # Start TCP socket server
    await socket_server.start()
    
    logger.info("webInk server started")


@app.get("/get_hash")
async def get_hash(
    api_key: str = Query(...),
    device: str = Query(...),
    mode: str = Query(...)
):
    """Get the hash of the current image for a device"""
    # Verify API key
    if api_key != config.api_key:
        raise HTTPException(status_code=401, detail="Invalid API key")
    
    # Update client info
    client_manager.update_client(device, {'mode': mode})
    
    # Get device page
    device_info = config.devices.get(device, config.devices.get('default', {}))
    page_id = device_info.get('page')
    
    if not page_id:
        raise HTTPException(status_code=404, detail="No page configured for device")
    
    # Check if mode is supported
    if mode not in config.supported_modes:
        raise HTTPException(status_code=404, detail=f"Unsupported mode: {mode}")
    
    # Get image hash
    image_hash = snapshot_manager.get_image_hash(page_id, mode)
    
    if not image_hash:
        raise HTTPException(status_code=404, detail="Image not available yet")
    
    return {"hash": image_hash}


@app.post("/post_log")
async def post_log(
    request: Request,
    api_key: str = Query(...),
    device: str = Query(...)
):
    """Receive log message from device"""
    # Verify API key
    if api_key != config.api_key:
        raise HTTPException(status_code=401, detail="Invalid API key")
    
    # Get log message from body
    body = await request.body()
    log_message = body.decode('utf-8')
    
    # Log it
    logger.info(f"Device log [{device}]: {log_message}")
    
    # Update client info
    client_manager.update_client(device, {'last_log': log_message})
    
    return {"status": "ok"}


@app.post("/post_metrics")
async def post_metrics(
    request: Request,
    api_key: str = Query(...),
    device: str = Query(...)
):
    """Receive metrics from device"""
    # Verify API key
    if api_key != config.api_key:
        raise HTTPException(status_code=401, detail="Invalid API key")
    
    # Get metrics from body
    try:
        metrics = await request.json()
        logger.info(f"Device metrics [{device}]: {metrics}")
        
        # Update client info
        client_manager.update_client(device, {'metrics': metrics})
        
        return {"status": "ok"}
    except Exception as e:
        logger.error(f"Failed to parse metrics from {device}: {e}")
        raise HTTPException(status_code=400, detail="Invalid metrics format")


@app.get("/get_sleep")
async def get_sleep(
    api_key: str = Query(...),
    device: str = Query(...)
):
    """Get sleep duration for device"""
    # Verify API key
    if api_key != config.api_key:
        raise HTTPException(status_code=401, detail="Invalid API key")
    
    # Update client info
    client_manager.update_client(device, {})
    
    # Calculate sleep time
    sleep_seconds = snapshot_manager.get_sleep_seconds(device)
    
    # Update expected next refresh
    if sleep_seconds > 0:
        next_refresh = datetime.now() + timedelta(seconds=sleep_seconds)
        client_manager.update_client(device, {'next_refresh': next_refresh.isoformat()})
    
    return {"sleep_seconds": sleep_seconds}


@app.get("/get_image")
async def get_image(
    api_key: str = Query(...),
    device: str = Query(...),
    mode: str = Query(...),
    x: int = Query(...),
    y: int = Query(...),
    w: int = Query(...),
    h: int = Query(...),
    format: str = Query(default="png")
):
    """Get a cropped chunk of the image"""
    # Verify API key
    if api_key != config.api_key:
        raise HTTPException(status_code=401, detail="Invalid API key")
    
    # Update client info
    client_manager.update_client(device, {})
    
    # Get device page
    device_info = config.devices.get(device, config.devices.get('default', {}))
    page_id = device_info.get('page')
    
    if not page_id:
        # Use default if device unknown
        device_info = config.devices.get('default', {})
        page_id = device_info.get('page')
        
        if not page_id:
            raise HTTPException(status_code=500, detail="No page configured for device")
    
    # Load image
    filename = DATA_DIR / f"{page_id}_{mode}.png"
    
    if not filename.exists():
        raise HTTPException(status_code=500, detail=f"Image not available for {page_id} in mode {mode}")
    
    try:
        # Open and crop image
        img = Image.open(filename)
        
        # Validate crop parameters
        if x < 0 or y < 0 or x + w > img.width or y + h > img.height:
            raise HTTPException(status_code=500, detail="Invalid crop parameters")
        
        # Crop the image
        cropped = img.crop((x, y, x + w, y + h))
        
        # Convert to requested format
        output = io.BytesIO()
        
        if format == 'pbm':
            # PBM format for 1-bit images
            if cropped.mode != '1':
                cropped = cropped.convert('1')
            cropped.save(output, format='PPM')
            media_type = "image/x-portable-bitmap"
            
        elif format == 'ppm':
            # PPM format for multi-bit images
            cropped.save(output, format='PPM')
            media_type = "image/x-portable-pixmap"
            
        elif format == 'png':
            # PNG format
            cropped.save(output, format='PNG')
            media_type = "image/png"
            
        else:
            raise HTTPException(status_code=500, detail=f"Unsupported format: {format}")
        
        output.seek(0)
        return Response(content=output.read(), media_type=media_type)
        
    except Exception as e:
        logger.error(f"Failed to process image: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/", response_class=HTMLResponse)
async def dashboard():
    """Web dashboard"""
    html_content = """
<!DOCTYPE html>
<html>
<head>
    <title>webInk Dashboard</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 0;
            padding: 20px;
            background: #f5f5f5;
        }
        h1 {
            color: #333;
        }
        .tabs {
            display: flex;
            border-bottom: 2px solid #ddd;
            margin-bottom: 20px;
        }
        .tab {
            padding: 10px 20px;
            cursor: pointer;
            background: #fff;
            border: 1px solid #ddd;
            border-bottom: none;
            margin-right: 5px;
        }
        .tab.active {
            background: #007bff;
            color: white;
        }
        .tab-content {
            display: none;
            background: white;
            padding: 20px;
            border-radius: 5px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        .tab-content.active {
            display: block;
        }
        .mode-selector {
            margin-bottom: 20px;
        }
        .page-preview {
            margin-bottom: 30px;
            padding: 15px;
            background: #f9f9f9;
            border-radius: 5px;
        }
        .page-preview h3 {
            margin-top: 0;
            color: #555;
        }
        .page-preview img {
            max-width: 100%;
            border: 1px solid #ddd;
            display: block;
        }
        .error-image {
            padding: 50px;
            background: #fee;
            color: #c00;
            text-align: center;
            border: 1px solid #fcc;
        }
        .device-table {
            width: 100%;
            border-collapse: collapse;
        }
        .device-table th,
        .device-table td {
            padding: 10px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }
        .device-table th {
            background: #f0f0f0;
            font-weight: bold;
        }
        .status-overdue {
            color: red;
            font-weight: bold;
        }
        .metrics {
            font-size: 0.9em;
            color: #666;
        }
        .sleep-toggle {
            cursor: pointer;
            padding: 5px 10px;
            background: #28a745;
            color: white;
            border: none;
            border-radius: 3px;
        }
        .sleep-toggle.disabled {
            background: #dc3545;
        }
    </style>
</head>
<body>
    <h1>webInk Dashboard</h1>
    
    <div class="tabs">
        <div class="tab active" onclick="showTab('pages')">Pages</div>
        <div class="tab" onclick="showTab('devices')">Devices</div>
    </div>
    
    <div id="pages" class="tab-content active">
        <div class="mode-selector">
            <label for="mode-select">Display Mode:</label>
            <select id="mode-select" onchange="updatePagePreviews()">
                <!-- Will be populated by JavaScript -->
            </select>
        </div>
        <div id="page-previews">
            <!-- Will be populated by JavaScript -->
        </div>
    </div>
    
    <div id="devices" class="tab-content">
        <table class="device-table">
            <thead>
                <tr>
                    <th>Device Name</th>
                    <th>Last Seen</th>
                    <th>Next Refresh</th>
                    <th>API Calls</th>
                    <th>Metrics</th>
                    <th>Sleep Mode</th>
                </tr>
            </thead>
            <tbody id="device-list">
                <!-- Will be populated by JavaScript -->
            </tbody>
        </table>
    </div>
    
    <script>
        const SNAPSHOT_LEAD_TIME = 5;  // Must match Python constant
        let config = null;
        let clients = null;
        
        async function loadData() {
            // Load configuration
            const configResponse = await fetch('/api/config');
            config = await configResponse.json();
            
            // Load client data
            const clientsResponse = await fetch('/api/clients');
            clients = await clientsResponse.json();
            
            // Populate mode selector
            const modeSelect = document.getElementById('mode-select');
            modeSelect.innerHTML = '';
            config.supported_modes.forEach(mode => {
                const option = document.createElement('option');
                option.value = mode;
                option.text = mode;
                modeSelect.appendChild(option);
            });
            
            updatePagePreviews();
            updateDeviceList();
        }
        
        function showTab(tabName) {
            // Update tab buttons
            document.querySelectorAll('.tab').forEach(tab => {
                tab.classList.remove('active');
            });
            event.target.classList.add('active');
            
            // Update tab content
            document.querySelectorAll('.tab-content').forEach(content => {
                content.classList.remove('active');
            });
            document.getElementById(tabName).classList.add('active');
            
            // Refresh data if showing devices
            if (tabName === 'devices') {
                updateDeviceList();
            }
        }
        
        async function updatePagePreviews() {
            const mode = document.getElementById('mode-select').value;
            const container = document.getElementById('page-previews');
            container.innerHTML = '';
            
            // Fetch page status
            const statusResponse = await fetch('/api/page_status');
            const statusData = await statusResponse.json();
            const pageStatus = statusData.pages;
            
            // Add summary info at the top
            const summary = document.createElement('div');
            summary.style.cssText = 'background: #f0f0f0; padding: 15px; border-radius: 5px; margin-bottom: 20px;';
            summary.innerHTML = `
                <strong>Render Summary:</strong><br>
                Total render time: ${statusData.total_render_time.toFixed(1)}s<br>
                Lead time (with ${SNAPSHOT_LEAD_TIME}s slack): ${statusData.lead_time.toFixed(1)}s
            `;
            container.appendChild(summary);
            
            Object.entries(config.pages).forEach(([pageId, pageInfo]) => {
                const div = document.createElement('div');
                div.className = 'page-preview';
                
                const header = document.createElement('div');
                header.style.cssText = 'display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;';
                
                const titleDiv = document.createElement('div');
                const title = document.createElement('h3');
                title.textContent = `${pageId} - ${pageInfo.url}`;
                title.style.margin = '0';
                titleDiv.appendChild(title);
                
                // Add next update time and render duration
                const status = pageStatus[pageId];
                if (status && status.seconds_until !== null) {
                    const timeInfo = document.createElement('div');
                    timeInfo.style.cssText = 'font-size: 0.9em; color: #666; margin-top: 5px;';
                    const minutes = Math.floor(status.seconds_until / 60);
                    const seconds = Math.floor(status.seconds_until % 60);
                    timeInfo.textContent = `Next update in: ${minutes}m ${seconds}s`;
                    timeInfo.id = `time-${pageId}`;
                    titleDiv.appendChild(timeInfo);
                }
                
                // Add last render duration
                if (status && status.last_render_duration !== null && status.last_render_duration !== undefined) {
                    const renderInfo = document.createElement('div');
                    renderInfo.style.cssText = 'font-size: 0.9em; color: #666; margin-top: 3px;';
                    renderInfo.textContent = `Last render: ${status.last_render_duration.toFixed(2)}s`;
                    titleDiv.appendChild(renderInfo);
                }
                
                header.appendChild(titleDiv);
                
                // Add update button
                const updateBtn = document.createElement('button');
                updateBtn.textContent = 'Update Now';
                updateBtn.style.cssText = 'padding: 8px 16px; cursor: pointer; background: #007bff; color: white; border: none; border-radius: 4px;';
                updateBtn.onclick = () => updatePageNow(pageId);
                header.appendChild(updateBtn);
                
                div.appendChild(header);
                
                const img = document.createElement('img');
                img.src = `/api/preview/${pageId}?mode=${mode}&t=${Date.now()}`;
                img.onerror = function() {
                    this.style.display = 'none';
                    const error = document.createElement('div');
                    error.className = 'error-image';
                    error.textContent = `Image not available for ${pageId} in mode ${mode}`;
                    this.parentElement.appendChild(error);
                };
                div.appendChild(img);
                
                container.appendChild(div);
            });
        }
        
        async function updatePageNow(pageId) {
            try {
                const response = await fetch('/api/update_page', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ page_id: pageId })
                });
                const result = await response.json();
                alert(result.message || 'Update triggered');
                
                // Refresh preview after a delay
                setTimeout(() => updatePagePreviews(), 3000);
            } catch (error) {
                alert('Failed to trigger update: ' + error);
            }
        }
        
        // Update countdown timers every second
        setInterval(async () => {
            if (document.getElementById('pages').classList.contains('active')) {
                const statusResponse = await fetch('/api/page_status');
                const statusData = await statusResponse.json();
                const pageStatus = statusData.pages;
                
                Object.entries(pageStatus).forEach(([pageId, status]) => {
                    const timeElement = document.getElementById(`time-${pageId}`);
                    if (timeElement && status.seconds_until !== null) {
                        const minutes = Math.floor(status.seconds_until / 60);
                        const seconds = Math.floor(status.seconds_until % 60);
                        timeElement.textContent = `Next update in: ${minutes}m ${seconds}s`;
                    }
                });
            }
        }, 1000);
        
        async function updateDeviceList() {
            // Reload client data
            const clientsResponse = await fetch('/api/clients');
            clients = await clientsResponse.json();
            
            const tbody = document.getElementById('device-list');
            tbody.innerHTML = '';
            
            Object.entries(clients).forEach(([deviceName, deviceInfo]) => {
                const row = document.createElement('tr');
                
                // Device name
                const nameCell = document.createElement('td');
                nameCell.textContent = deviceName;
                row.appendChild(nameCell);
                
                // Last seen
                const lastSeenCell = document.createElement('td');
                const lastSeen = new Date(deviceInfo.last_seen);
                lastSeenCell.textContent = lastSeen.toLocaleString();
                row.appendChild(lastSeenCell);
                
                // Next refresh
                const nextRefreshCell = document.createElement('td');
                if (deviceInfo.next_refresh) {
                    const nextRefresh = new Date(deviceInfo.next_refresh);
                    const now = new Date();
                    
                    if (nextRefresh < now.getTime() - 60000) {  // Overdue by 1 minute
                        nextRefreshCell.className = 'status-overdue';
                        nextRefreshCell.textContent = nextRefresh.toLocaleString() + ' (OVERDUE)';
                    } else {
                        nextRefreshCell.textContent = nextRefresh.toLocaleString();
                    }
                } else {
                    nextRefreshCell.textContent = 'N/A';
                }
                row.appendChild(nextRefreshCell);
                
                // API calls
                const apiCallsCell = document.createElement('td');
                apiCallsCell.textContent = deviceInfo.api_calls || 0;
                row.appendChild(apiCallsCell);
                
                // Metrics
                const metricsCell = document.createElement('td');
                if (deviceInfo.metrics) {
                    const metrics = deviceInfo.metrics;
                    metricsCell.className = 'metrics';
                    metricsCell.innerHTML = `
                        SSID: ${metrics.ssid || 'N/A'}<br>
                        Signal: ${metrics.dbm || 'N/A'} dBm<br>
                        Battery: ${metrics.battery || 'N/A'}%
                    `;
                } else {
                    metricsCell.textContent = 'No metrics';
                }
                row.appendChild(metricsCell);
                
                // Sleep toggle
                const sleepCell = document.createElement('td');
                const sleepButton = document.createElement('button');
                sleepButton.className = 'sleep-toggle';
                
                if (deviceInfo.sleep_disabled) {
                    sleepButton.textContent = 'Sleep Disabled';
                    sleepButton.classList.add('disabled');
                } else {
                    sleepButton.textContent = 'Sleep Enabled';
                }
                
                sleepButton.onclick = () => toggleSleep(deviceName, !deviceInfo.sleep_disabled);
                sleepCell.appendChild(sleepButton);
                row.appendChild(sleepCell);
                
                tbody.appendChild(row);
            });
        }
        
        async function toggleSleep(deviceName, disable) {
            await fetch('/api/toggle_sleep', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({device: deviceName, disable: disable})
            });
            updateDeviceList();
        }
        
        // Load data on page load
        loadData();
        
        // Refresh device list every 10 seconds
        setInterval(() => {
            if (document.getElementById('devices').classList.contains('active')) {
                updateDeviceList();
            }
        }, 10000);
    </script>
</body>
</html>
    """
    return HTMLResponse(content=html_content)


@app.get("/api/config")
async def get_config():
    """API endpoint to get configuration"""
    return {
        "pages": config.pages,
        "devices": config.devices,
        "supported_modes": config.supported_modes
    }


@app.get("/api/clients")
async def get_clients():
    """API endpoint to get client data"""
    return client_manager.clients


@app.get("/api/preview/{page_id}")
async def get_preview(page_id: str, mode: str = Query(...)):
    """Get preview image for dashboard"""
    filename = DATA_DIR / f"{page_id}_{mode}.png"
    
    if filename.exists():
        return FileResponse(filename, media_type="image/png")
    else:
        raise HTTPException(status_code=404, detail="Image not found")


@app.get("/api/page_status")
async def get_page_status():
    """Get status of all pages including next refresh times"""
    total_render_time = snapshot_manager.calculate_total_render_time()
    
    status = {
        'pages': {},
        'total_render_time': total_render_time,
        'lead_time': total_render_time + SNAPSHOT_LEAD_TIME
    }
    
    for page_id in config.pages:
        next_refresh = snapshot_manager.next_refresh_times.get(page_id)
        status['pages'][page_id] = {
            'next_refresh': next_refresh.isoformat() if next_refresh else None,
            'seconds_until': (next_refresh - datetime.now()).total_seconds() if next_refresh else None,
            'last_render_duration': snapshot_manager.last_render_duration.get(page_id)
        }
    return status


@app.post("/api/update_page")
async def update_page_now(request: Request):
    """Trigger immediate update of a page"""
    data = await request.json()
    page_id = data.get('page_id')
    
    if page_id not in config.pages:
        raise HTTPException(status_code=404, detail="Page not found")
    
    # Trigger capture in background
    asyncio.create_task(snapshot_manager.capture_page(page_id))
    
    return {"status": "ok", "message": f"Update triggered for {page_id}"}


@app.post("/api/toggle_sleep")
async def toggle_sleep(request: Request):
    """Toggle sleep mode for a device"""
    data = await request.json()
    device_name = data.get('device')
    disable = data.get('disable', False)
    
    if device_name in client_manager.clients:
        client_manager.clients[device_name]['sleep_disabled'] = disable
        client_manager.save_clients()
        
    return {"status": "ok"}


if __name__ == "__main__":
    # Check for required dependencies
    try:
        import playwright
    except ImportError:
        logger.error("Playwright not installed. Please run: pip install playwright && playwright install chromium")
        sys.exit(1)
    
    # Create default config if it doesn't exist
    if not Path(CONFIG_FILE).exists():
        default_config = {
            'pages': [
                {'nytimes': [
                    {'url': 'https://nytimes.com'},
                    {'refresh_interval': 300}
                ]},
                {'google': [
                    {'url': 'https://google.com'},
                    {'suppress_refresh': {'start': '01:00', 'end': '08:00'}},
                    {'mandatory_refresh': ['08:00']}
                ]}
            ],
            'devices': [
                {'default': [{'page': 'nytimes'}]}
            ],
            'supported_modes': [
                '800x480x1xB',
                '800x480x2xG',
                '800x480x2xRGB',
                '800x480x8xRGB',
                '1600x1200x1xB'
            ],
            'api_key': 'myapikey'
        }
        
        with open(CONFIG_FILE, 'w') as f:
            yaml.dump(default_config, f, default_flow_style=False)
        logger.info(f"Created default config file: {CONFIG_FILE}")
    
    # Run the server
    uvicorn.run(app, host="0.0.0.0", port=8000, log_level="info")


    