#!/usr/bin/env python3
"""
Test Client for webInk Server

A command-line test client that exercises all API endpoints without displaying images.
Tests the complete workflow: get hash, post log, post metrics, get image by tiling, and sleep.
"""

import argparse
import hashlib
import io
import sys
import time
from typing import Optional, Tuple

import requests
from PIL import Image


class Colors:
    """ANSI color codes for terminal output"""
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    RESET = '\033[0m'
    BOLD = '\033[1m'


class WebInkTestClient:
    """Test client for webInk server API"""
    
    def __init__(self, server_url: str, api_key: str, device_name: str, mode: str, tile_size: int = 200):
        self.server_url = server_url.rstrip('/')
        self.api_key = api_key
        self.device_name = device_name
        self.mode = mode
        self.tile_size = tile_size
        self.session = requests.Session()
        
        # Parse mode to get dimensions
        self.width, self.height, self.bits, self.color_mode = self._parse_mode(mode)
        
    def _parse_mode(self, mode: str) -> Tuple[int, int, int, str]:
        """Parse mode string like '800x480x1xB' into components"""
        parts = mode.split('x')
        if len(parts) != 4:
            raise ValueError(f"Invalid mode format: {mode}")
        
        width = int(parts[0])
        height = int(parts[1])
        bits = int(parts[2])
        color_mode = parts[3]
        
        return width, height, bits, color_mode
    
    def _print_status(self, message: str, status: str = "INFO"):
        """Print formatted status message"""
        if status == "OK":
            color = Colors.GREEN
            symbol = "✓"
        elif status == "ERROR":
            color = Colors.RED
            symbol = "✗"
        elif status == "WARN":
            color = Colors.YELLOW
            symbol = "⚠"
        else:
            color = Colors.BLUE
            symbol = "ℹ"
        
        print(f"{color}{Colors.BOLD}[{symbol} {status}]{Colors.RESET} {message}")
    
    def _print_section(self, title: str):
        """Print section header"""
        print(f"\n{Colors.CYAN}{Colors.BOLD}{'='*60}{Colors.RESET}")
        print(f"{Colors.CYAN}{Colors.BOLD}{title}{Colors.RESET}")
        print(f"{Colors.CYAN}{Colors.BOLD}{'='*60}{Colors.RESET}\n")
    
    def test_get_hash(self) -> Optional[str]:
        """Test GET /get_hash endpoint"""
        self._print_section("TEST: Get Image Hash")
        
        try:
            url = f"{self.server_url}/get_hash"
            params = {
                'api_key': self.api_key,
                'device': self.device_name,
                'mode': self.mode
            }
            
            self._print_status(f"Requesting hash from {url}", "INFO")
            self._print_status(f"Device: {self.device_name}, Mode: {self.mode}", "INFO")
            
            response = self.session.get(url, params=params, timeout=10)
            
            if response.status_code == 200:
                data = response.json()
                image_hash = data.get('hash')
                self._print_status(f"Image hash: {image_hash}", "OK")
                return image_hash
            else:
                self._print_status(f"Failed with status {response.status_code}: {response.text}", "ERROR")
                return None
                
        except Exception as e:
            self._print_status(f"Exception: {e}", "ERROR")
            return None
    
    def test_post_log(self, message: str) -> bool:
        """Test POST /post_log endpoint"""
        self._print_section("TEST: Post Log Message")
        
        try:
            url = f"{self.server_url}/post_log"
            params = {
                'api_key': self.api_key,
                'device': self.device_name
            }
            
            self._print_status(f"Posting log to {url}", "INFO")
            self._print_status(f"Message: {message}", "INFO")
            
            response = self.session.post(url, params=params, data=message.encode('utf-8'), timeout=10)
            
            if response.status_code == 200:
                self._print_status("Log posted successfully", "OK")
                return True
            else:
                self._print_status(f"Failed with status {response.status_code}: {response.text}", "ERROR")
                return False
                
        except Exception as e:
            self._print_status(f"Exception: {e}", "ERROR")
            return False
    
    def test_post_metrics(self) -> bool:
        """Test POST /post_metrics endpoint"""
        self._print_section("TEST: Post Device Metrics")
        
        try:
            url = f"{self.server_url}/post_metrics"
            params = {
                'api_key': self.api_key,
                'device': self.device_name
            }
            
            # Sample metrics
            metrics = {
                'ssid': 'TestNetwork',
                'dbm': -65,
                'battery': 87,
                'uptime': 12345,
                'free_memory': 45000
            }
            
            self._print_status(f"Posting metrics to {url}", "INFO")
            self._print_status(f"Metrics: {metrics}", "INFO")
            
            response = self.session.post(url, params=params, json=metrics, timeout=10)
            
            if response.status_code == 200:
                self._print_status("Metrics posted successfully", "OK")
                return True
            else:
                self._print_status(f"Failed with status {response.status_code}: {response.text}", "ERROR")
                return False
                
        except Exception as e:
            self._print_status(f"Exception: {e}", "ERROR")
            return False
    
    def test_get_sleep(self) -> Optional[int]:
        """Test GET /get_sleep endpoint"""
        self._print_section("TEST: Get Sleep Duration")
        
        try:
            url = f"{self.server_url}/get_sleep"
            params = {
                'api_key': self.api_key,
                'device': self.device_name
            }
            
            self._print_status(f"Requesting sleep duration from {url}", "INFO")
            
            response = self.session.get(url, params=params, timeout=10)
            
            if response.status_code == 200:
                data = response.json()
                sleep_seconds = data.get('sleep_seconds')
                self._print_status(f"Sleep duration: {sleep_seconds} seconds ({sleep_seconds/60:.1f} minutes)", "OK")
                return sleep_seconds
            else:
                self._print_status(f"Failed with status {response.status_code}: {response.text}", "ERROR")
                return None
                
        except Exception as e:
            self._print_status(f"Exception: {e}", "ERROR")
            return None
    
    def test_get_image_tiled(self, verify_hash: Optional[str] = None) -> bool:
        """Test GET /get_image endpoint by downloading the image in tiles"""
        self._print_section("TEST: Get Image (Tiled)")
        
        try:
            self._print_status(f"Image dimensions: {self.width}x{self.height}", "INFO")
            self._print_status(f"Tile size: {self.tile_size}x{self.tile_size}", "INFO")
            
            # Calculate number of tiles
            tiles_x = (self.width + self.tile_size - 1) // self.tile_size
            tiles_y = (self.height + self.tile_size - 1) // self.tile_size
            total_tiles = tiles_x * tiles_y
            
            self._print_status(f"Total tiles: {tiles_x} x {tiles_y} = {total_tiles}", "INFO")
            
            # Create a blank image to assemble tiles into
            if self.bits == 1 and self.color_mode == 'B':
                full_image = Image.new('1', (self.width, self.height), 1)
            elif self.color_mode == 'G':
                full_image = Image.new('L', (self.width, self.height), 255)
            else:
                full_image = Image.new('RGB', (self.width, self.height), (255, 255, 255))
            
            # Download each tile
            tile_count = 0
            failed_tiles = 0
            
            for ty in range(tiles_y):
                for tx in range(tiles_x):
                    tile_count += 1
                    
                    # Calculate tile coordinates
                    x = tx * self.tile_size
                    y = ty * self.tile_size
                    w = min(self.tile_size, self.width - x)
                    h = min(self.tile_size, self.height - y)
                    
                    # Request tile
                    url = f"{self.server_url}/get_image"
                    params = {
                        'api_key': self.api_key,
                        'device': self.device_name,
                        'mode': self.mode,
                        'x': x,
                        'y': y,
                        'w': w,
                        'h': h,
                        'format': 'png'
                    }
                    
                    try:
                        response = self.session.get(url, params=params, timeout=10)
                        
                        if response.status_code == 200:
                            # Load tile image
                            tile_img = Image.open(io.BytesIO(response.content))
                            
                            # Paste into full image
                            full_image.paste(tile_img, (x, y))
                            
                            # Progress indicator
                            if tile_count % 5 == 0 or tile_count == total_tiles:
                                progress = (tile_count / total_tiles) * 100
                                print(f"  Progress: {tile_count}/{total_tiles} tiles ({progress:.1f}%)", end='\r')
                        else:
                            failed_tiles += 1
                            self._print_status(f"Tile ({tx},{ty}) failed: {response.status_code}", "WARN")
                            
                    except Exception as e:
                        failed_tiles += 1
                        self._print_status(f"Tile ({tx},{ty}) exception: {e}", "WARN")
            
            print()  # New line after progress
            
            if failed_tiles > 0:
                self._print_status(f"Downloaded {tile_count - failed_tiles}/{total_tiles} tiles ({failed_tiles} failed)", "WARN")
            else:
                self._print_status(f"Downloaded all {total_tiles} tiles successfully", "OK")
            
            # Verify hash if provided
            if verify_hash:
                self._print_status("Verifying image hash...", "INFO")
                
                # Save to bytes and compute hash
                img_bytes = io.BytesIO()
                full_image.save(img_bytes, format='PNG')
                img_bytes.seek(0)
                
                computed_hash = hashlib.sha1(img_bytes.read()).hexdigest()[:8]
                
                if computed_hash == verify_hash:
                    self._print_status(f"Hash verification PASSED: {computed_hash}", "OK")
                else:
                    self._print_status(f"Hash verification FAILED: expected {verify_hash}, got {computed_hash}", "ERROR")
                    return False
            
            # Report image stats
            self._print_status(f"Final image size: {full_image.size}, mode: {full_image.mode}", "INFO")
            
            return failed_tiles == 0
            
        except Exception as e:
            self._print_status(f"Exception: {e}", "ERROR")
            return False
    
    def run_full_test(self, sleep_after: bool = True):
        """Run complete test workflow"""
        print(f"\n{Colors.BOLD}{'='*60}")
        print(f"webInk Test Client")
        print(f"{'='*60}{Colors.RESET}")
        print(f"Server: {self.server_url}")
        print(f"Device: {self.device_name}")
        print(f"Mode: {self.mode}")
        print(f"{'='*60}\n")
        
        results = {
            'get_hash': False,
            'post_log': False,
            'post_metrics': False,
            'get_image': False,
            'get_sleep': False
        }
        
        # Test 1: Get hash
        image_hash = self.test_get_hash()
        results['get_hash'] = image_hash is not None
        
        # Test 2: Post log
        results['post_log'] = self.test_post_log(f"Test client started - device: {self.device_name}")
        
        # Test 3: Post metrics
        results['post_metrics'] = self.test_post_metrics()
        
        # Test 4: Get image (tiled)
        results['get_image'] = self.test_get_image_tiled(verify_hash=image_hash)
        
        # Test 5: Get sleep duration
        sleep_seconds = self.test_get_sleep()
        results['get_sleep'] = sleep_seconds is not None
        
        # Summary
        self._print_section("TEST SUMMARY")
        
        passed = sum(1 for v in results.values() if v)
        total = len(results)
        
        for test_name, passed_test in results.items():
            status = "OK" if passed_test else "ERROR"
            self._print_status(f"{test_name}: {'PASSED' if passed_test else 'FAILED'}", status)
        
        print()
        if passed == total:
            self._print_status(f"All {total} tests PASSED", "OK")
        else:
            self._print_status(f"{passed}/{total} tests passed, {total - passed} failed", "ERROR")
        
        # Sleep if requested and we got a sleep duration
        if sleep_after and sleep_seconds is not None and sleep_seconds > 0:
            self._print_section("SLEEP")
            self._print_status(f"Sleeping for {sleep_seconds} seconds ({sleep_seconds/60:.1f} minutes)...", "INFO")
            
            try:
                # Show countdown
                for remaining in range(sleep_seconds, 0, -1):
                    mins = remaining // 60
                    secs = remaining % 60
                    print(f"  Time remaining: {mins:02d}:{secs:02d}", end='\r')
                    time.sleep(1)
                print()
                self._print_status("Sleep completed", "OK")
            except KeyboardInterrupt:
                print()
                self._print_status("Sleep interrupted by user", "WARN")
        
        return passed == total

    def run_basic_test(self) -> bool:
        """Run a basic subset of tests suitable for CI.
        
        This avoids image fetching and hash verification so it can run
        in constrained environments (e.g. CI) while still exercising
        key API endpoints.
        """
        self._print_section("BASIC TEST WORKFLOW")

        results = {
            'post_log': False,
            'post_metrics': False,
            'get_sleep': False,
        }

        # Basic log and metrics flow
        results['post_log'] = self.test_post_log(f"CI basic test - device: {self.device_name}")
        results['post_metrics'] = self.test_post_metrics()

        # Sleep endpoint should return a valid duration
        sleep_seconds = self.test_get_sleep()
        results['get_sleep'] = sleep_seconds is not None

        self._print_section("BASIC TEST SUMMARY")

        passed = sum(1 for v in results.values() if v)
        total = len(results)

        for test_name, passed_test in results.items():
            status = "OK" if passed_test else "ERROR"
            self._print_status(f"{test_name}: {'PASSED' if passed_test else 'FAILED'}", status)

        print()
        if passed == total:
            self._print_status(f"All {total} basic tests PASSED", "OK")
        else:
            self._print_status(f"{passed}/{total} basic tests passed, {total - passed} failed", "ERROR")

        return passed == total


def main():
    parser = argparse.ArgumentParser(
        description='Test client for webInk server API',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic test with default settings
  python test_client.py
  
  # Test specific device and mode
  python test_client.py --device kitchen --mode 800x480x2xG
  
  # Test without sleeping afterward
  python test_client.py --no-sleep
  
  # Test with custom server URL
  python test_client.py --server http://192.168.1.100:8000
  
  # Test with smaller tile size
  python test_client.py --tile-size 100
        """
    )
    
    parser.add_argument(
        '--server',
        default='http://localhost:8000',
        help='Server URL (default: http://localhost:8000)'
    )
    
    parser.add_argument(
        '--api-key',
        default='myapikey',
        help='API key (default: myapikey)'
    )
    
    parser.add_argument(
        '--device',
        default='test_device',
        help='Device name (default: test_device)'
    )
    
    parser.add_argument(
        '--mode',
        default='800x480x1xB',
        help='Display mode (default: 800x480x1xB)'
    )
    
    parser.add_argument(
        '--tile-size',
        type=int,
        default=200,
        help='Tile size for image download (default: 200)'
    )
    
    parser.add_argument(
        '--no-sleep',
        action='store_true',
        help='Do not sleep after tests'
    )

    parser.add_argument(
        '--basic',
        action='store_true',
        help='Run a basic subset of tests (no image/hash) suitable for CI'
    )
    
    args = parser.parse_args()
    
    try:
        client = WebInkTestClient(
            server_url=args.server,
            api_key=args.api_key,
            device_name=args.device,
            mode=args.mode,
            tile_size=args.tile_size
        )
        
        if args.basic:
            success = client.run_basic_test()
        else:
            success = client.run_full_test(sleep_after=not args.no_sleep)
        
        sys.exit(0 if success else 1)
        
    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}Test interrupted by user{Colors.RESET}")
        sys.exit(130)
    except Exception as e:
        print(f"\n{Colors.RED}{Colors.BOLD}Fatal error: {e}{Colors.RESET}")
        sys.exit(1)


if __name__ == '__main__':
    main()
