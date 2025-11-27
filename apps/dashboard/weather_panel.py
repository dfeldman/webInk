"""Enhanced Weather Panel for dashboard with additional metrics."""
import aiohttp
from datetime import datetime
from typing import Dict, Any
try:
    from .base_panel import BasePanel
except ImportError:
    from base_panel import BasePanel


class WeatherPanel(BasePanel):
    """Panel that displays comprehensive weather information."""
    
    async def fetch_data(self) -> None:
        """Fetch weather data with extended metrics from multiple APIs."""
        try:
            location = self.config.get('location')
            if not location:
                raise ValueError("Weather panel requires 'location' (lat,lon) in configuration")
            
            # Parse location
            if isinstance(location, str):
                lat, lon = map(float, location.split(','))
            else:
                lat, lon = location
            
            async with aiohttp.ClientSession() as session:
                # Fetch weather data from Open-Meteo
                weather_url = (
                    f"https://api.open-meteo.com/v1/forecast?"
                    f"latitude={lat}&longitude={lon}"
                    f"&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,"
                    f"weather_code,wind_speed_10m,surface_pressure,dew_point_2m"
                    f"&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,sunrise,sunset"
                    f"&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch&timezone=auto"
                )
                
                async with session.get(weather_url, timeout=aiohttp.ClientTimeout(total=10)) as response:
                    weather_data = await response.json()
                
                # Fetch AQI data from Open-Meteo Air Quality API
                aqi = None
                try:
                    aqi_url = (
                        f"https://air-quality-api.open-meteo.com/v1/air-quality?"
                        f"latitude={lat}&longitude={lon}&hourly=us_aqi&timezone=auto"
                    )
                    async with session.get(aqi_url, timeout=aiohttp.ClientTimeout(total=5)) as aqi_resp:
                        if aqi_resp.status == 200:
                            aqi_data = await aqi_resp.json()
                            hourly = aqi_data.get('hourly', {})
                            values = hourly.get('us_aqi', [])
                            if values and values[0] is not None:
                                aqi = int(values[0])
                except Exception:
                    pass
                
                # Fetch sunrise/sunset and solar data
                sunrise = None
                sunset = None
                day_length = None
                solar_noon = None
                try:
                    sun_url = f"https://api.sunrise-sunset.org/json?lat={lat}&lng={lon}&formatted=0"
                    async with session.get(sun_url, timeout=aiohttp.ClientTimeout(total=5)) as sun_resp:
                        if sun_resp.status == 200:
                            sun_data = await sun_resp.json()
                            results = sun_data.get('results', {})
                            if sun_data.get('status') == 'OK' and results:
                                if results.get('sunrise'):
                                    sunrise_dt = datetime.fromisoformat(results['sunrise'].replace('Z', '+00:00'))
                                    sunrise = sunrise_dt.strftime('%-I:%M %p')
                                
                                if results.get('sunset'):
                                    sunset_dt = datetime.fromisoformat(results['sunset'].replace('Z', '+00:00'))
                                    sunset = sunset_dt.strftime('%-I:%M %p')
                                
                                if results.get('solar_noon'):
                                    solar_noon_dt = datetime.fromisoformat(results['solar_noon'].replace('Z', '+00:00'))
                                    solar_noon = solar_noon_dt.strftime('%-I:%M %p')
                                
                                if results.get('day_length') is not None:
                                    seconds = int(results['day_length'])
                                    hours = seconds // 3600
                                    minutes = (seconds % 3600) // 60
                                    day_length = f"{hours}h {minutes}m"
                except Exception:
                    pass
            
            # Weather code mapping (WMO codes)
            weather_descriptions = {
                0: ("Clear sky", "☀️"),
                1: ("Mainly clear", "🌤️"),
                2: ("Partly cloudy", "⛅"),
                3: ("Overcast", "☁️"),
                45: ("Foggy", "🌫️"),
                48: ("Foggy", "🌫️"),
                51: ("Light drizzle", "🌦️"),
                53: ("Drizzle", "🌦️"),
                55: ("Heavy drizzle", "🌧️"),
                61: ("Light rain", "🌧️"),
                63: ("Rain", "🌧️"),
                65: ("Heavy rain", "⛈️"),
                71: ("Light snow", "🌨️"),
                73: ("Snow", "❄️"),
                75: ("Heavy snow", "❄️"),
                77: ("Snow grains", "❄️"),
                80: ("Light showers", "🌦️"),
                81: ("Showers", "🌧️"),
                82: ("Heavy showers", "⛈️"),
                85: ("Light snow showers", "🌨️"),
                86: ("Snow showers", "❄️"),
                95: ("Thunderstorm", "⛈️"),
                96: ("Thunderstorm with hail", "⛈️"),
                99: ("Thunderstorm with hail", "⛈️"),
            }
            
            current = weather_data['current']
            daily = weather_data['daily']
            
            weather_code = current.get('weather_code', 0)
            weather_desc, weather_icon = weather_descriptions.get(weather_code, ("Unknown", "❓"))
            
            # Extract dew point and pressure
            dew_point = None
            if current.get('dew_point_2m') is not None:
                dew_point = round(current['dew_point_2m'])
            
            pressure_mb = None
            if current.get('surface_pressure') is not None:
                pressure_mb = round(current['surface_pressure'])
            
            self.data = {
                'current': {
                    'temp': round(current['temperature_2m']),
                    'feels_like': round(current['apparent_temperature']),
                    'humidity': round(current['relative_humidity_2m']),
                    'wind_speed': round(current['wind_speed_10m']),
                    'description': weather_desc,
                    'icon': weather_icon,
                    'dew_point': dew_point,
                    'pressure_mb': pressure_mb,
                    'aqi': aqi,
                    'sunrise': sunrise,
                    'sunset': sunset,
                    'day_length': day_length,
                    'solar_noon': solar_noon,
                },
                'forecast': []
            }
            
            # Add 5-day forecast
            for i in range(min(5, len(daily['time']))):
                date = datetime.fromisoformat(daily['time'][i])
                code = daily['weather_code'][i]
                desc, icon = weather_descriptions.get(code, ("Unknown", "❓"))
                
                self.data['forecast'].append({
                    'date': date.strftime('%a'),
                    'high': round(daily['temperature_2m_max'][i]),
                    'low': round(daily['temperature_2m_min'][i]),
                    'icon': icon
                })
            
            self.error = None
            
        except Exception as e:
            self.error = f"Failed to fetch weather: {str(e)}"
            self.data = None
    
    def get_html(self, size: str) -> str:
        """Generate HTML for weather panel."""
        if self.error:
            return self.get_error_html(size)
        
        if not self.data:
            return "<div class='weather-loading'>Loading weather...</div>"
        
        current = self.data['current']
        
        # Compact layout for 1x1 - squeeze everything in
        if size == '1x1':
            # Build compact details with all available data
            details_html = f"""
            <div class="weather-detail"><span>Feels:</span><span>{current['feels_like']}°</span></div>
            <div class="weather-detail"><span>Humidity:</span><span>{current['humidity']}%</span></div>
            <div class="weather-detail"><span>Wind:</span><span>{current['wind_speed']} mph</span></div>
            """
            
            if current.get('aqi') is not None:
                details_html += f'<div class="weather-detail"><span>AQI:</span><span>{current["aqi"]}</span></div>'
            
            if current.get('sunrise'):
                details_html += f'<div class="weather-detail"><span>Sunrise:</span><span>{current["sunrise"]}</span></div>'
            
            if current.get('sunset'):
                details_html += f'<div class="weather-detail"><span>Sunset:</span><span>{current["sunset"]}</span></div>'
            
            if current.get('dew_point') is not None:
                details_html += f'<div class="weather-detail"><span>Dew pt:</span><span>{current["dew_point"]}°</span></div>'
            
            if current.get('pressure_mb') is not None:
                details_html += f'<div class="weather-detail"><span>Press:</span><span>{current["pressure_mb"]}</span></div>'
            
            # Mini forecast for 1x1
            forecast_html = ""
            if self.data['forecast']:
                for day in self.data['forecast'][:3]:
                    forecast_html += f'<div class="forecast-mini"><div>{day["date"]}</div><div>{day["icon"]}</div><div>{day["high"]}°</div></div>'
            
            return f"""
            <div class="weather-panel compact">
                <div class="weather-main-compact">
                    <div class="weather-icon-compact">{current['icon']}</div>
                    <div class="weather-info-compact">
                        <div class="weather-temp-compact">{current['temp']}°F</div>
                        <div class="weather-desc-compact">{current['description']}</div>
                    </div>
                </div>
                <div class="weather-details-compact">
                    {details_html}
                </div>
                <div class="forecast-compact">
                    {forecast_html}
                </div>
            </div>
            """
        
        # Standard layout for other sizes
        details_html = f"""
        <div class="weather-detail">
            <span>Feels like:</span>
            <span>{current['feels_like']}°F</span>
        </div>
        <div class="weather-detail">
            <span>Humidity:</span>
            <span>{current['humidity']}%</span>
        </div>
        <div class="weather-detail">
            <span>Wind:</span>
            <span>{current['wind_speed']} mph</span>
        </div>
        """
        
        # Add extended metrics for all non-1x1 panels
        if current.get('aqi') is not None:
            details_html += f"""
            <div class="weather-detail">
                <span>AQI (US):</span>
                <span>{current['aqi']}</span>
            </div>
            """
        
        if current.get('sunrise'):
            details_html += f"""
            <div class="weather-detail">
                <span>Sunrise:</span>
                <span>{current['sunrise']}</span>
            </div>
            """
        
        if current.get('sunset'):
            details_html += f"""
            <div class="weather-detail">
                <span>Sunset:</span>
                <span>{current['sunset']}</span>
            </div>
            """
        
        if current.get('dew_point') is not None:
            details_html += f"""
            <div class="weather-detail">
                <span>Dew point:</span>
                <span>{current['dew_point']}°F</span>
            </div>
            """
        
        if current.get('pressure_mb') is not None:
            details_html += f"""
            <div class="weather-detail">
                <span>Pressure:</span>
                <span>{current['pressure_mb']} hPa</span>
            </div>
            """
        
        if current.get('day_length'):
            details_html += f"""
            <div class="weather-detail">
                <span>Day length:</span>
                <span>{current['day_length']}</span>
            </div>
            """
        
        if current.get('solar_noon'):
            details_html += f"""
            <div class="weather-detail">
                <span>Solar noon:</span>
                <span>{current['solar_noon']}</span>
            </div>
            """
        
        # Build forecast HTML
        forecast_html = ""
        forecast_limit = {
            '2x2': 5,
            '1x2': 5,
            '2x1': 4,
        }.get(size, 3)
        
        if self.data['forecast']:
            for day in self.data['forecast'][:forecast_limit]:
                forecast_html += f"""
                <div class="forecast-day">
                    <div class="forecast-date">{day['date']}</div>
                    <div class="forecast-icon">{day['icon']}</div>
                    <div class="forecast-temps">
                        <span class="temp-high">{day['high']}°</span>
                        <span class="temp-low">{day['low']}°</span>
                    </div>
                </div>
                """
        
        forecast_section = f"""
        <div class="weather-forecast">
            <div class="forecast-title">Forecast</div>
            <div class="forecast-days">
                {forecast_html}
            </div>
        </div>
        """ if forecast_html else ""
        
        return f"""
        <div class="weather-panel">
            <div class="weather-current">
                <div class="weather-icon-large">{current['icon']}</div>
                <div class="weather-temp-large">{current['temp']}°F</div>
                <div class="weather-description">{current['description']}</div>
            </div>
            <div class="weather-details">
                {details_html}
            </div>
            {forecast_section}
        </div>
        """
    
    def get_css(self, size: str) -> str:
        """Generate CSS for weather panel."""
        return """
        .weather-panel {
            height: 100%;
            display: flex;
            flex-direction: column;
            padding: 15px;
            background: white;
            overflow: hidden;
        }
        
        /* Compact layout for 1x1 */
        .weather-panel.compact {
            padding: 8px;
        }
        .weather-main-compact {
            display: flex;
            align-items: center;
            gap: 8px;
            padding-bottom: 6px;
            border-bottom: 2px solid #333;
            margin-bottom: 6px;
        }
        .weather-icon-compact {
            font-size: 42px;
            line-height: 1;
        }
        .weather-info-compact {
            flex: 1;
        }
        .weather-temp-compact {
            font-size: 28px;
            font-weight: bold;
            line-height: 1;
        }
        .weather-desc-compact {
            font-size: 11px;
            color: #666;
            margin-top: 2px;
        }
        .weather-details-compact {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 3px 6px;
            font-size: 10px;
            margin-bottom: 6px;
        }
        .weather-details-compact .weather-detail {
            display: flex;
            justify-content: space-between;
            padding: 1px 0;
        }
        .weather-details-compact .weather-detail span:first-child {
            color: #666;
        }
        .weather-details-compact .weather-detail span:last-child {
            font-weight: bold;
        }
        .forecast-compact {
            display: flex;
            justify-content: space-around;
            gap: 4px;
            border-top: 1px solid #ddd;
            padding-top: 6px;
            margin-top: auto;
        }
        .forecast-mini {
            text-align: center;
            font-size: 10px;
            flex: 1;
        }
        .forecast-mini div:first-child {
            font-weight: bold;
            margin-bottom: 2px;
        }
        .forecast-mini div:nth-child(2) {
            font-size: 20px;
            margin-bottom: 2px;
        }
        .forecast-mini div:last-child {
            font-weight: bold;
        }
        
        /* Standard layout */
        .weather-current {
            text-align: center;
            padding: 10px 0;
            border-bottom: 2px solid #ddd;
            margin-bottom: 10px;
        }
        .weather-icon-large {
            font-size: 56px;
            margin-bottom: 5px;
        }
        .weather-temp-large {
            font-size: 36px;
            font-weight: bold;
            margin-bottom: 3px;
        }
        .weather-description {
            font-size: 16px;
            color: #666;
            margin-bottom: 10px;
        }
        .weather-details {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 6px 10px;
            font-size: 12px;
            margin-bottom: 10px;
        }
        .weather-detail {
            display: flex;
            justify-content: space-between;
            padding: 2px 0;
        }
        .weather-detail span:first-child {
            color: #666;
        }
        .weather-detail span:last-child {
            font-weight: bold;
        }
        .weather-forecast {
            margin-top: auto;
            border-top: 1px solid #ddd;
            padding-top: 10px;
        }
        .forecast-title {
            font-size: 14px;
            font-weight: bold;
            margin-bottom: 8px;
        }
        .forecast-days {
            display: flex;
            justify-content: space-around;
            gap: 5px;
        }
        .forecast-day {
            text-align: center;
            flex: 1;
        }
        .forecast-date {
            font-size: 11px;
            font-weight: bold;
            margin-bottom: 3px;
        }
        .forecast-icon {
            font-size: 28px;
            margin-bottom: 3px;
        }
        .forecast-temps {
            font-size: 12px;
        }
        .temp-high {
            font-weight: bold;
            margin-right: 3px;
        }
        .temp-low {
            color: #666;
        }
        .weather-loading {
            display: flex;
            align-items: center;
            justify-content: center;
            height: 100%;
            font-size: 16px;
            color: #999;
        }
        """
