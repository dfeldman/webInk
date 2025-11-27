# E-Ink Dashboard

A simple, YAML-configured dashboard system designed for e-ink displays. Shows multiple panels with different content types in a clean, static layout optimized for e-ink screens.

## Features

- **YAML Configuration**: Complete dashboard configuration through simple YAML files
- **Modular Panels**: Extensible panel system with built-in support for:
  - RSS feeds
  - Calendar (ICS format)
  - Weather (using Open-Meteo API)
  - Web pages (iframe)
  - Home Assistant entities
- **Flexible Layouts**: Multiple layout options (2x2, 1x2, 2x1, 1x1, left_split, right_split)
- **Auto-refresh**: Panels automatically refresh at configurable intervals
- **Error Handling**: Graceful error display when panels fail to load
- **E-Ink Optimized**: Clean, static design perfect for e-ink displays

## Installation

Install dependencies:

```bash
pip install aiohttp pyyaml feedparser icalendar
```

Or if using the webInk project:

```bash
cd webInk/server
pip install -e .
```

## Quick Start

1. Create a configuration file (see `dashboard_config.yaml` for example):

```yaml
layout: 2x2
dateline: true

panels:
  - panel1:
      - type: rss
      - url: https://rss.nytimes.com/services/xml/rss/nyt/HomePage.xml
      - max_items: 5
      
  - panel2:
      - type: weather
      - location: "40.7128,-74.0060"  # NYC
      
  - panel3:
      - type: calendar
      - url: https://calendar.google.com/calendar/ical/YOUR_ID/public/basic.ics
      
  - panel4:
      - type: webpage
      - url: https://www.google.com
```

2. Run the dashboard:

```bash
python -m dashboard.dashboard dashboard_config.yaml
```

Or specify host and port:

```bash
python -m dashboard.dashboard dashboard_config.yaml 0.0.0.0 8080
```

3. Open your browser to `http://localhost:8080`

## Configuration

### Layout Options

- **2x2**: Four equal panels in a 2x2 grid
- **1x2**: Two panels stacked vertically
- **2x1**: Two panels side by side
- **1x1**: Single full-screen panel
- **left_split**: Left side split into two panels, right side single panel
- **right_split**: Right side split into two panels, left side single panel

### Panel Types

#### RSS Panel
```yaml
- panel_name:
    - type: rss
    - url: https://example.com/feed.xml
    - max_items: 5  # Optional, default 5
    - refresh_interval: 300  # Optional, seconds
```

#### Calendar Panel
```yaml
- panel_name:
    - type: calendar
    # Single calendar
    - url: https://calendar.google.com/calendar/ical/YOUR_ID/public/basic.ics
    # OR multiple calendars (combined)
    - urls:
        - https://calendar.google.com/calendar/ical/personal/public/basic.ics
        - https://calendar.google.com/calendar/ical/work/public/basic.ics
    - days_ahead: 14  # Optional, default 14
    - refresh_interval: 600  # Optional, seconds
```

**Enhanced Features:**
- Supports multiple ICS URLs (events are merged and sorted)
- Smart date filtering: shows tomorrow's events if nothing today
- Modern visual design with Today/Tomorrow badges
- Improved typography and spacing for e-ink displays

#### Weather Panel
```yaml
- panel_name:
    - type: weather
    - location: "40.7128,-74.0060"  # lat,lon
    - refresh_interval: 1800  # Optional, seconds
```

Uses the free Open-Meteo API (no API key required).

#### Webpage Panel
```yaml
- panel_name:
    - type: webpage
    - url: https://www.example.com
```

Displays a webpage in an iframe.

#### Home Assistant Entities Panel
```yaml
- panel_name:
    - type: homeassistant_entities
    - url: "http://homeassistant.local:8123"
    - api_key: "your_long_lived_access_token"
    - entities:
        - sensor.living_room_temperature.state
        - sensor.living_room_humidity.state
        - sensor.outdoor_temperature.state
    - refresh_interval: 60  # Optional, seconds
```

Displays entity states from Home Assistant. Supports:
- Automatic layout switching (big boxes for ≤4 entities, table for >4)
- Nested attribute access using dot notation
- Graceful handling of unavailable entities
- Auto-formatting of values and units

See [HOMEASSISTANT_PANEL.md](HOMEASSISTANT_PANEL.md) for detailed documentation.

### Dateline

Set `dateline: true` to show the current date at the top of the dashboard.

## Architecture

### Core Components

- **`dashboard.py`**: Main server and layout engine
- **`base_panel.py`**: Abstract base class for all panels
- **Panel implementations**: Each panel type in its own file
  - `rss_panel.py`
  - `calendar_panel.py`
  - `weather_panel.py`
  - `webpage_panel.py`
  - `homeassistant_panel.py`

### Panel Lifecycle

1. **Configuration**: Panels are configured via YAML
2. **Initialization**: Panel instances created with config
3. **Data Fetching**: `fetch_data()` called periodically
4. **Rendering**: `get_html(size)` and `get_css(size)` generate content
5. **Error Handling**: Errors displayed gracefully in panel space

### Adding New Panel Types

1. Create a new file in the `dashboard` directory
2. Inherit from `BasePanel`
3. Implement `fetch_data()` and `get_html(size)`
4. Optionally override `get_css(size)`
5. Register in `dashboard.py` `PANEL_TYPES` dict

Example:

```python
from .base_panel import BasePanel

class MyPanel(BasePanel):
    async def fetch_data(self):
        # Fetch your data
        self.data = {"key": "value"}
        self.error = None
    
    def get_html(self, size: str) -> str:
        if self.error:
            return self.get_error_html(size)
        return f"<div>My content: {self.data['key']}</div>"
    
    def get_css(self, size: str) -> str:
        return "/* Your CSS */"
```

## API Endpoints

- **`/`**: Main dashboard page
- **`/health`**: Health check endpoint (returns JSON with status and panel count)

## Notes

- The dashboard auto-refreshes every 60 seconds to update content
- Each panel refreshes its data independently based on `refresh_interval`
- All panels are sized to fit exactly within their allocated space
- Content that exceeds panel size is clipped (overflow: hidden)
- Designed for modern Chrome browsers (required for e-ink devices)

## Future Enhancements

- Home Assistant graphs and history panels
- More layout options
- Panel-specific refresh controls
- Custom CSS themes
- Panel templates
