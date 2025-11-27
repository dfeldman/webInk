"""E-Ink Dashboard Server - Simple, YAML-configured dashboard for e-ink displays."""
import asyncio
import yaml
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Any, Optional
from aiohttp import web
import logging

try:
    from .base_panel import BasePanel
    from .rss_panel import RssPanel
    from .calendar_panel import CalendarPanel
    from .weather_panel import WeatherPanel
    from .webpage_panel import WebpagePanel
    from .homeassistant_panel import HomeAssistantPanel
except ImportError:
    # Handle running as script
    from base_panel import BasePanel
    from rss_panel import RssPanel
    from calendar_panel import CalendarPanel
    from weather_panel import WeatherPanel
    from webpage_panel import WebpagePanel
    from homeassistant_panel import HomeAssistantPanel

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# Panel type registry
PANEL_TYPES = {
    'rss': RssPanel,
    'calendar': CalendarPanel,
    'weather': WeatherPanel,
    'webpage': WebpagePanel,
    'homeassistant_entities': HomeAssistantPanel,
}


class DashboardLayout:
    """Manages dashboard layout and panel positioning."""
    
    LAYOUTS = {
        '2x2': {
            'grid': '2x2',
            'panels': [
                {'id': 0, 'row': 0, 'col': 0, 'size': '1x1'},
                {'id': 1, 'row': 0, 'col': 1, 'size': '1x1'},
                {'id': 2, 'row': 1, 'col': 0, 'size': '1x1'},
                {'id': 3, 'row': 1, 'col': 1, 'size': '1x1'},
            ]
        },
        '1x2': {
            'grid': '1x2',
            'panels': [
                {'id': 0, 'row': 0, 'col': 0, 'size': '1x2'},
                {'id': 1, 'row': 1, 'col': 0, 'size': '1x2'},
            ]
        },
        '2x1': {
            'grid': '2x1',
            'panels': [
                {'id': 0, 'row': 0, 'col': 0, 'size': '2x1'},
                {'id': 1, 'row': 0, 'col': 1, 'size': '2x1'},
            ]
        },
        '1x1': {
            'grid': '1x1',
            'panels': [
                {'id': 0, 'row': 0, 'col': 0, 'size': '2x2'},
            ]
        },
        'left_split': {
            'grid': 'left_split',
            'panels': [
                {'id': 0, 'row': 0, 'col': 0, 'size': '1x1'},
                {'id': 1, 'row': 1, 'col': 0, 'size': '1x1'},
                {'id': 2, 'row': 0, 'col': 1, 'rowspan': 2, 'size': '1x2'},
            ]
        },
        'right_split': {
            'grid': 'right_split',
            'panels': [
                {'id': 0, 'row': 0, 'col': 0, 'rowspan': 2, 'size': '1x2'},
                {'id': 1, 'row': 0, 'col': 1, 'size': '1x1'},
                {'id': 2, 'row': 1, 'col': 1, 'size': '1x1'},
            ]
        },
        'top_split': {
            'grid': 'top_split',
            'panels': [
                {'id': 0, 'row': 0, 'col': 0, 'size': '1x1'},
                {'id': 1, 'row': 0, 'col': 1, 'size': '1x1'},
                {'id': 2, 'row': 1, 'col': 0, 'colspan': 2, 'size': '2x1'},
            ]
        },
        'bottom_split': {
            'grid': 'bottom_split',
            'panels': [
                {'id': 0, 'row': 0, 'col': 0, 'colspan': 2, 'size': '2x1'},
                {'id': 1, 'row': 1, 'col': 0, 'size': '1x1'},
                {'id': 2, 'row': 1, 'col': 1, 'size': '1x1'},
            ]
        },
    }
    
    @classmethod
    def get_layout(cls, layout_name: str) -> Dict[str, Any]:
        """Get layout configuration by name."""
        if layout_name not in cls.LAYOUTS:
            raise ValueError(f"Unknown layout: {layout_name}. Available: {list(cls.LAYOUTS.keys())}")
        return cls.LAYOUTS[layout_name]
    
    @classmethod
    def get_grid_css(cls, layout_name: str) -> str:
        """Generate CSS grid layout for the specified layout."""
        layout = cls.get_layout(layout_name)
        
        if layout_name == '2x2':
            return "grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr;"
        elif layout_name == '1x2':
            return "grid-template-columns: 1fr; grid-template-rows: 1fr 1fr;"
        elif layout_name == '2x1':
            return "grid-template-columns: 1fr 1fr; grid-template-rows: 1fr;"
        elif layout_name == '1x1':
            return "grid-template-columns: 1fr; grid-template-rows: 1fr;"
        elif layout_name == 'left_split':
            return "grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr;"
        elif layout_name == 'right_split':
            return "grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr;"
        elif layout_name == 'top_split':
            return "grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr;"
        elif layout_name == 'bottom_split':
            return "grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr;"
        
        return "grid-template-columns: 1fr; grid-template-rows: 1fr;"


class Dashboard:
    """Main dashboard controller."""
    
    def __init__(self, config_path: str):
        """Initialize dashboard with configuration file.
        
        Args:
            config_path: Path to YAML configuration file
        """
        self.config_path = Path(config_path)
        self.config = None
        self.panels: List[BasePanel] = []
        self.layout = None
        self.refresh_tasks = []
        
    def load_config(self) -> None:
        """Load configuration from YAML file."""
        logger.info(f"Loading configuration from {self.config_path}")
        
        with open(self.config_path, 'r') as f:
            self.config = yaml.safe_load(f)
        
        # Validate configuration
        if 'layout' not in self.config:
            raise ValueError("Configuration must specify 'layout'")
        if 'panels' not in self.config:
            raise ValueError("Configuration must specify 'panels'")
        
        self.layout = self.config['layout']
        layout_info = DashboardLayout.get_layout(self.layout)
        
        # Validate panel count
        expected_panels = len(layout_info['panels'])
        actual_panels = len(self.config['panels'])
        if actual_panels != expected_panels:
            logger.warning(f"Layout '{self.layout}' expects {expected_panels} panels, but {actual_panels} configured")
        
        logger.info(f"Loaded configuration: layout={self.layout}, panels={actual_panels}")
    
    def create_panels(self) -> None:
        """Create panel instances from configuration."""
        self.panels = []
        
        for idx, panel_config in enumerate(self.config['panels']):
            panel_name = list(panel_config.keys())[0]
            panel_data = panel_config[panel_name]
            
            # Convert list format to dict
            if isinstance(panel_data, list):
                panel_dict = {}
                for item in panel_data:
                    if isinstance(item, dict):
                        panel_dict.update(item)
                panel_data = panel_dict
            
            panel_type = panel_data.get('type')
            if not panel_type:
                logger.error(f"Panel '{panel_name}' missing 'type' field")
                continue
            
            if panel_type not in PANEL_TYPES:
                logger.error(f"Unknown panel type: {panel_type}")
                continue
            
            # Create panel instance
            panel_class = PANEL_TYPES[panel_type]
            panel = panel_class(panel_data, panel_name)
            self.panels.append(panel)
            
            logger.info(f"Created panel {idx}: {panel_name} (type={panel_type})")
    
    async def start_refresh_tasks(self) -> None:
        """Start background tasks to refresh panel data."""
        for panel in self.panels:
            task = asyncio.create_task(self._refresh_panel_loop(panel))
            self.refresh_tasks.append(task)
    
    async def _refresh_panel_loop(self, panel: BasePanel) -> None:
        """Background task to periodically refresh a panel.
        
        Args:
            panel: Panel to refresh
        """
        while True:
            try:
                logger.info(f"Fetching data for panel: {panel.panel_id}")
                await panel.fetch_data()
                if panel.error:
                    logger.error(f"Panel {panel.panel_id} error: {panel.error}")
            except Exception as e:
                logger.error(f"Error refreshing panel {panel.panel_id}: {e}")
                panel.error = str(e)
            
            await asyncio.sleep(panel.refresh_interval)
    
    async def initial_fetch(self) -> None:
        """Fetch data for all panels initially."""
        tasks = [panel.fetch_data() for panel in self.panels]
        await asyncio.gather(*tasks, return_exceptions=True)
    
    def generate_html(self) -> str:
        """Generate complete HTML page for dashboard."""
        layout_info = DashboardLayout.get_layout(self.layout)
        grid_css = DashboardLayout.get_grid_css(self.layout)
        
        # Generate dateline if configured
        dateline_html = ""
        if self.config.get('dateline', False):
            now = datetime.now()
            date_str = now.strftime('%A, %B %d, %Y')
            dateline_html = f'<div class="dateline">{date_str}</div>'
        
        # Generate panels HTML
        panels_html = ""
        all_css = ""
        
        for idx, panel_info in enumerate(layout_info['panels']):
            if idx >= len(self.panels):
                # Not enough panels configured
                panels_html += f'<div class="panel panel-{idx}"><div class="panel-empty">Panel {idx + 1}</div></div>'
                continue
            
            panel = self.panels[idx]
            panel_size = panel_info['size']
            
            # Generate panel HTML and CSS
            try:
                if panel.error:
                    html = panel.get_error_html(panel_size)
                    css = panel.get_error_css()
                else:
                    html = panel.get_html(panel_size)
                    css = panel.get_css(panel_size)
                
                # Wrap in panel container
                rowspan = panel_info.get('rowspan', 1)
                colspan = panel_info.get('colspan', 1)
                style = f"grid-row: span {rowspan}; grid-column: span {colspan};"
                
                panels_html += f'<div class="panel panel-{idx}" style="{style}">{html}</div>'
                all_css += css
                
            except Exception as e:
                logger.error(f"Error generating HTML for panel {idx}: {e}")
                panels_html += f'<div class="panel panel-{idx}"><div class="panel-error">Error: {e}</div></div>'
        
        # Complete HTML document
        html = f"""
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>E-Ink Dashboard</title>
            <style>
                * {{
                    margin: 0;
                    padding: 0;
                    box-sizing: border-box;
                }}
                
                body {{
                    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
                    background: #f0f0f0;
                    width: 100vw;
                    height: 100vh;
                    overflow: hidden;
                }}
                
                .dateline {{
                    background: #333;
                    color: white;
                    padding: 15px 20px;
                    font-size: 18px;
                    font-weight: bold;
                    text-align: center;
                }}
                
                .dashboard {{
                    display: grid;
                    {grid_css}
                    gap: 2px;
                    background: #ccc;
                    height: {'calc(100vh - 50px)' if self.config.get('dateline') else '100vh'};
                    width: 100vw;
                }}
                
                .panel {{
                    background: white;
                    overflow: hidden;
                    position: relative;
                }}
                
                .panel-empty {{
                    display: flex;
                    align-items: center;
                    justify-content: center;
                    height: 100%;
                    color: #999;
                    font-size: 18px;
                }}
                
                {all_css}
            </style>
            <script>
                // Auto-refresh page every 60 seconds to get updated data
                setTimeout(function() {{
                    location.reload();
                }}, 60000);
            </script>
        </head>
        <body>
            {dateline_html}
            <div class="dashboard">
                {panels_html}
            </div>
        </body>
        </html>
        """
        
        return html


class DashboardServer:
    """Web server for dashboard."""
    
    def __init__(self, dashboard: Dashboard, host: str = '0.0.0.0', port: int = 8080):
        """Initialize server.
        
        Args:
            dashboard: Dashboard instance
            host: Host to bind to
            port: Port to bind to
        """
        self.dashboard = dashboard
        self.host = host
        self.port = port
        self.app = web.Application()
        self.setup_routes()
    
    def setup_routes(self) -> None:
        """Setup web routes."""
        self.app.router.add_get('/', self.handle_index)
        self.app.router.add_get('/health', self.handle_health)
    
    async def handle_index(self, request: web.Request) -> web.Response:
        """Handle main dashboard page."""
        html = self.dashboard.generate_html()
        return web.Response(text=html, content_type='text/html')
    
    async def handle_health(self, request: web.Request) -> web.Response:
        """Handle health check."""
        return web.json_response({'status': 'ok', 'panels': len(self.dashboard.panels)})
    
    async def start(self) -> None:
        """Start the server."""
        # Load configuration and create panels
        self.dashboard.load_config()
        self.dashboard.create_panels()
        
        # Initial data fetch
        logger.info("Fetching initial panel data...")
        await self.dashboard.initial_fetch()
        
        # Start refresh tasks
        logger.info("Starting refresh tasks...")
        await self.dashboard.start_refresh_tasks()
        
        # Start web server
        runner = web.AppRunner(self.app)
        await runner.setup()
        site = web.TCPSite(runner, self.host, self.port)
        await site.start()
        
        logger.info(f"Dashboard server running at http://{self.host}:{self.port}")
        logger.info("Press Ctrl+C to stop")
        
        # Keep running
        try:
            await asyncio.Event().wait()
        except KeyboardInterrupt:
            logger.info("Shutting down...")


async def main(config_path: str, host: str = '0.0.0.0', port: int = 8080):
    """Main entry point for dashboard server.
    
    Args:
        config_path: Path to YAML configuration file
        host: Host to bind to (default: 0.0.0.0)
        port: Port to bind to (default: 8080)
    """
    dashboard = Dashboard(config_path)
    server = DashboardServer(dashboard, host, port)
    await server.start()


if __name__ == '__main__':
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: python dashboard.py <config.yaml> [host] [port]")
        sys.exit(1)
    
    config_path = sys.argv[1]
    host = sys.argv[2] if len(sys.argv) > 2 else '0.0.0.0'
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 8080
    
    asyncio.run(main(config_path, host, port))
