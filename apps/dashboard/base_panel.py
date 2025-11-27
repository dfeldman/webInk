"""Base Panel class for dashboard panels."""
from abc import ABC, abstractmethod
from typing import Dict, Any
import asyncio


class BasePanel(ABC):
    """Base class for all dashboard panels."""
    
    def __init__(self, config: Dict[str, Any], panel_id: str):
        """Initialize panel with configuration.
        
        Args:
            config: Panel configuration dictionary
            panel_id: Unique identifier for this panel
        """
        self.config = config
        self.panel_id = panel_id
        self.data = None
        self.error = None
        self.refresh_interval = config.get('refresh_interval', 300)  # Default 5 minutes
        
    @abstractmethod
    async def fetch_data(self) -> None:
        """Fetch data for this panel. Should update self.data or self.error."""
        pass
    
    @abstractmethod
    def get_html(self, size: str) -> str:
        """Generate HTML content for this panel.
        
        Args:
            size: Panel size - one of: '2x2', '1x2', '2x1', '1x1'
            
        Returns:
            HTML string for the panel content
        """
        pass
    
    def get_css(self, size: str) -> str:
        """Generate CSS for this panel. Override if custom CSS needed.
        
        Args:
            size: Panel size - one of: '2x2', '1x2', '2x1', '1x1'
            
        Returns:
            CSS string for the panel
        """
        return ""
    
    def get_error_html(self, size: str) -> str:
        """Generate error display HTML.
        
        Args:
            size: Panel size
            
        Returns:
            HTML string showing the error
        """
        return f"""
        <div class="panel-error">
            <div class="error-icon">⚠️</div>
            <div class="error-title">Panel Error</div>
            <div class="error-message">{self.error}</div>
        </div>
        """
    
    def get_error_css(self) -> str:
        """CSS for error display."""
        return """
        .panel-error {
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            height: 100%;
            padding: 20px;
            text-align: center;
            background: #f5f5f5;
            border: 2px dashed #999;
        }
        .error-icon {
            font-size: 48px;
            margin-bottom: 10px;
        }
        .error-title {
            font-size: 20px;
            font-weight: bold;
            margin-bottom: 10px;
            color: #333;
        }
        .error-message {
            font-size: 14px;
            color: #666;
            word-wrap: break-word;
            max-width: 90%;
        }
        """
