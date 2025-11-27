"""Webpage Panel for dashboard."""
from typing import Dict, Any
try:
    from .base_panel import BasePanel
except ImportError:
    from base_panel import BasePanel


class WebpagePanel(BasePanel):
    """Panel that displays a webpage in an iframe."""
    
    async def fetch_data(self) -> None:
        """No data fetching needed for iframe - just validate config."""
        try:
            url = self.config.get('url')
            if not url:
                raise ValueError("Webpage panel requires 'url' in configuration")
            
            self.data = {'url': url}
            self.error = None
            
        except Exception as e:
            self.error = f"Invalid webpage configuration: {str(e)}"
            self.data = None
    
    def get_html(self, size: str) -> str:
        """Generate HTML for webpage panel."""
        if self.error:
            return self.get_error_html(size)
        
        if not self.data:
            return "<div class='webpage-loading'>Loading webpage...</div>"
        
        url = self.data['url']
        
        return f"""
        <div class="webpage-panel">
            <iframe src="{url}" class="webpage-iframe"></iframe>
        </div>
        """
    
    def get_css(self, size: str) -> str:
        """Generate CSS for webpage panel."""
        return """
        .webpage-panel {
            height: 100%;
            width: 100%;
            overflow: hidden;
        }
        .webpage-iframe {
            width: 100%;
            height: 100%;
            border: none;
        }
        .webpage-loading {
            display: flex;
            align-items: center;
            justify-content: center;
            height: 100%;
            font-size: 16px;
            color: #999;
        }
        """
