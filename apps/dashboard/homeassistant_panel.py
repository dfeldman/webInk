"""Home Assistant Entities Panel for dashboard."""
import aiohttp
from typing import Dict, Any, List
try:
    from .base_panel import BasePanel
except ImportError:
    from base_panel import BasePanel


class HomeAssistantPanel(BasePanel):
    """Panel that displays Home Assistant entity states."""
    
    async def fetch_data(self) -> None:
        """Fetch entity states from Home Assistant API."""
        try:
            url = self.config.get('url')
            api_key = self.config.get('api_key')
            entities = self.config.get('entities', [])
            
            if not url:
                raise ValueError("HomeAssistant panel requires 'url' in configuration")
            if not api_key:
                raise ValueError("HomeAssistant panel requires 'api_key' in configuration")
            if not entities:
                raise ValueError("HomeAssistant panel requires 'entities' list in configuration")
            
            # Parse entities in entity.attribute format
            entity_data = []
            
            async with aiohttp.ClientSession() as session:
                headers = {
                    'Authorization': f'Bearer {api_key}',
                    'Content-Type': 'application/json',
                }
                
                for entity_spec in entities:
                    # Parse entity.attribute format
                    if '.' not in entity_spec:
                        continue
                    
                    parts = entity_spec.split('.')
                    if len(parts) < 2:
                        continue
                    
                    # First two parts are entity_id (domain.entity_name)
                    entity_id = f"{parts[0]}.{parts[1]}"
                    # Remaining parts are attribute path (if any)
                    attribute_path = parts[2:] if len(parts) > 2 else None
                    
                    try:
                        # Fetch entity state
                        entity_url = f"{url.rstrip('/')}/api/states/{entity_id}"
                        async with session.get(entity_url, headers=headers, timeout=aiohttp.ClientTimeout(total=10)) as response:
                            if response.status == 200:
                                state_data = await response.json()
                                
                                # Extract value based on attribute path
                                if attribute_path:
                                    # Navigate through attributes
                                    value = state_data.get('attributes', {})
                                    for attr in attribute_path:
                                        if isinstance(value, dict):
                                            value = value.get(attr)
                                        else:
                                            value = None
                                            break
                                else:
                                    # Use state value
                                    value = state_data.get('state')
                                
                                # Get friendly name and unit
                                friendly_name = state_data.get('attributes', {}).get('friendly_name', entity_id)
                                unit = state_data.get('attributes', {}).get('unit_of_measurement', '')
                                
                                # Handle unavailable state
                                if value == 'unavailable' or value is None:
                                    entity_data.append({
                                        'label': friendly_name,
                                        'value': 'unavailable',
                                        'unit': '',
                                        'available': False,
                                        'entity_id': entity_id,
                                    })
                                else:
                                    # Format value
                                    formatted_value = self._format_value(value)
                                    
                                    entity_data.append({
                                        'label': friendly_name,
                                        'value': formatted_value,
                                        'unit': unit,
                                        'available': True,
                                        'entity_id': entity_id,
                                    })
                            else:
                                # Entity not found or error
                                entity_data.append({
                                    'label': entity_id,
                                    'value': 'error',
                                    'unit': '',
                                    'available': False,
                                    'entity_id': entity_id,
                                })
                    except Exception as e:
                        # Handle individual entity fetch errors gracefully
                        entity_data.append({
                            'label': entity_id,
                            'value': 'error',
                            'unit': '',
                            'available': False,
                            'entity_id': entity_id,
                        })
            
            self.data = {'entities': entity_data}
            self.error = None
            
        except Exception as e:
            self.error = f"Failed to fetch Home Assistant data: {str(e)}"
            self.data = None
    
    def _format_value(self, value: Any) -> str:
        """Format a value for display."""
        if isinstance(value, (int, float)):
            # Round floats to 1 decimal place
            if isinstance(value, float):
                return f"{value:.1f}"
            return str(value)
        elif isinstance(value, bool):
            return "on" if value else "off"
        else:
            return str(value)
    
    def get_html(self, size: str) -> str:
        """Generate HTML for Home Assistant panel."""
        if self.error:
            return self.get_error_html(size)
        
        if not self.data:
            return "<div class='ha-loading'>Loading Home Assistant data...</div>"
        
        entities = self.data['entities']
        
        if not entities:
            return "<div class='ha-empty'>No entities configured</div>"
        
        # Determine layout based on number of entities
        # Few entities (<=4): big boxes
        # Many entities (>4): table
        if len(entities) <= 4:
            return self._get_boxes_html(entities, size)
        else:
            return self._get_table_html(entities, size)
    
    def _get_boxes_html(self, entities: List[Dict], size: str) -> str:
        """Generate big box layout for few entities."""
        boxes_html = ""
        
        for entity in entities:
            # Style based on availability
            box_class = "ha-box" if entity['available'] else "ha-box unavailable"
            
            # Format value with unit
            value_text = entity['value']
            if entity['available'] and entity['unit']:
                value_text = f"{value_text} {entity['unit']}"
            
            boxes_html += f"""
            <div class="{box_class}">
                <div class="ha-box-label">{entity['label']}</div>
                <div class="ha-box-value">{value_text}</div>
            </div>
            """
        
        return f"""
        <div class="ha-panel">
            <div class="ha-header">🏠 Home Assistant</div>
            <div class="ha-boxes">
                {boxes_html}
            </div>
        </div>
        """
    
    def _get_table_html(self, entities: List[Dict], size: str) -> str:
        """Generate table layout for many entities."""
        rows_html = ""
        
        for entity in entities:
            # Style based on availability
            row_class = "ha-row" if entity['available'] else "ha-row unavailable"
            
            # Format value with unit
            value_text = entity['value']
            if entity['available'] and entity['unit']:
                value_text = f"{value_text} {entity['unit']}"
            
            rows_html += f"""
            <tr class="{row_class}">
                <td class="ha-table-label">{entity['label']}</td>
                <td class="ha-table-value">{value_text}</td>
            </tr>
            """
        
        return f"""
        <div class="ha-panel">
            <div class="ha-header">🏠 Home Assistant</div>
            <div class="ha-table-container">
                <table class="ha-table">
                    <tbody>
                        {rows_html}
                    </tbody>
                </table>
            </div>
        </div>
        """
    
    def get_css(self, size: str) -> str:
        """Generate CSS for Home Assistant panel."""
        return """
        .ha-panel {
            height: 100%;
            display: flex;
            flex-direction: column;
            padding: 15px;
            background: white;
            overflow: hidden;
        }
        .ha-header {
            font-size: 18px;
            font-weight: bold;
            margin-bottom: 15px;
            padding-bottom: 10px;
            border-bottom: 2px solid #333;
        }
        
        /* Big boxes layout */
        .ha-boxes {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 10px;
            flex: 1;
            align-content: start;
        }
        .ha-box {
            background: #f9f9f9;
            border: 2px solid #333;
            border-radius: 8px;
            padding: 20px;
            text-align: center;
            display: flex;
            flex-direction: column;
            justify-content: center;
            min-height: 100px;
        }
        .ha-box.unavailable {
            background: #f5f5f5;
            border-color: #999;
            opacity: 0.6;
        }
        .ha-box-label {
            font-size: 12px;
            color: #666;
            margin-bottom: 8px;
            font-weight: 500;
        }
        .ha-box-value {
            font-size: 32px;
            font-weight: bold;
            color: #333;
            line-height: 1.2;
        }
        .ha-box.unavailable .ha-box-value {
            font-size: 16px;
            color: #999;
        }
        
        /* Table layout */
        .ha-table-container {
            flex: 1;
            overflow-y: auto;
        }
        .ha-table {
            width: 100%;
            border-collapse: collapse;
        }
        .ha-table tbody tr {
            border-bottom: 1px solid #e0e0e0;
        }
        .ha-table tbody tr:last-child {
            border-bottom: none;
        }
        .ha-row {
            transition: background-color 0.2s;
        }
        .ha-row.unavailable {
            opacity: 0.5;
        }
        .ha-table-label {
            padding: 10px 8px;
            font-size: 13px;
            color: #666;
            font-weight: 500;
        }
        .ha-table-value {
            padding: 10px 8px;
            font-size: 16px;
            font-weight: bold;
            text-align: right;
            color: #333;
        }
        .ha-row.unavailable .ha-table-value {
            font-size: 13px;
            font-weight: normal;
            color: #999;
        }
        
        /* Loading and empty states */
        .ha-loading, .ha-empty {
            display: flex;
            align-items: center;
            justify-content: center;
            height: 100%;
            font-size: 16px;
            color: #999;
        }
        """
