"""Calendar Panel for dashboard."""
import aiohttp
import asyncio
from datetime import datetime, timedelta, time
from typing import Dict, Any, List
from icalendar import Calendar
try:
    from .base_panel import BasePanel
except ImportError:
    from base_panel import BasePanel


class CalendarPanel(BasePanel):
    """Panel that displays calendar events from ICS feed(s)."""
    
    async def _fetch_single_calendar(self, url: str, session: aiohttp.ClientSession) -> List[Dict[str, Any]]:
        """Fetch and parse a single ICS calendar."""
        events = []
        try:
            async with session.get(url, timeout=aiohttp.ClientTimeout(total=10)) as response:
                content = await response.read()
            
            # Parse calendar
            cal = Calendar.from_ical(content)
            
            # Extract events
            now = datetime.now()
            days_ahead = self.config.get('days_ahead', 14)  # Increased default
            end_date = now + timedelta(days=days_ahead)
            
            for component in cal.walk():
                if component.name == "VEVENT":
                    dtstart = component.get('dtstart')
                    if dtstart:
                        event_date = dtstart.dt
                        # Handle both date and datetime objects
                        if isinstance(event_date, datetime):
                            event_datetime = event_date
                            is_all_day = False
                        else:
                            event_datetime = datetime.combine(event_date, datetime.min.time())
                            is_all_day = True
                        
                        # Only include upcoming events
                        if now <= event_datetime <= end_date:
                            # Get end time if available
                            dtend = component.get('dtend')
                            end_time = None
                            if dtend and not is_all_day:
                                if isinstance(dtend.dt, datetime):
                                    end_time = dtend.dt
                            
                            # Get calendar color/category if available
                            color = str(component.get('color', ''))
                            categories = component.get('categories', [])
                            if categories:
                                categories = [str(cat) for cat in categories]
                            
                            events.append({
                                'summary': str(component.get('summary', 'No title')),
                                'start': event_datetime,
                                'end': end_time,
                                'location': str(component.get('location', '')),
                                'description': str(component.get('description', '')),
                                'is_all_day': is_all_day,
                                'color': color,
                                'categories': categories,
                            })
        except Exception as e:
            # Log but don't fail the entire panel for one calendar
            print(f"Warning: Failed to fetch calendar from {url}: {e}")
        
        return events
    
    async def fetch_data(self) -> None:
        """Fetch calendar data from ICS URL(s)."""
        try:
            # Support both single URL and list of URLs
            urls = self.config.get('urls') or self.config.get('url')
            if not urls:
                raise ValueError("Calendar panel requires 'url' or 'urls' in configuration")
            
            # Normalize to list
            if isinstance(urls, str):
                urls = [urls]
            
            # Fetch all calendars in parallel
            all_events = []
            async with aiohttp.ClientSession() as session:
                tasks = [self._fetch_single_calendar(url, session) for url in urls]
                results = await asyncio.gather(*tasks, return_exceptions=True)
                
                for result in results:
                    if isinstance(result, list):
                        all_events.extend(result)
            
            # Sort by start time
            all_events.sort(key=lambda x: x['start'])
            
            # Smart filtering: if no events today, start from tomorrow
            now = datetime.now()
            today_start = datetime.combine(now.date(), time.min)
            today_end = datetime.combine(now.date(), time.max)
            
            # Check if there are any events today
            events_today = [e for e in all_events if today_start <= e['start'] <= today_end]
            
            if not events_today and all_events:
                # No events today, filter to show from tomorrow onwards
                tomorrow_start = today_start + timedelta(days=1)
                all_events = [e for e in all_events if e['start'] >= tomorrow_start]
            
            self.data = {
                'events': all_events,
                'showing_from_tomorrow': not events_today and len(all_events) > 0
            }
            self.error = None
            
        except Exception as e:
            self.error = f"Failed to fetch calendar: {str(e)}"
            self.data = None
    
    def get_html(self, size: str) -> str:
        """Generate HTML for calendar panel."""
        if self.error:
            return self.get_error_html(size)
        
        if not self.data:
            return "<div class='calendar-loading'>Loading calendar...</div>"
        
        # Determine how many events to show based on size
        event_limit = {
            '2x2': 12,
            '1x2': 10,
            '2x1': 6,
            '1x1': 4
        }.get(size, 8)
        
        events_html = ""
        if not self.data['events']:
            events_html = "<div class='calendar-empty'>No upcoming events</div>"
        else:
            now = datetime.now()
            today = now.date()
            tomorrow = today + timedelta(days=1)
            
            for event in self.data['events'][:event_limit]:
                event_date = event['start'].date()
                
                # Determine date display
                if event_date == today:
                    date_badge = "<span class='date-badge today'>Today</span>"
                    date_str = event['start'].strftime('%b %d')
                elif event_date == tomorrow:
                    date_badge = "<span class='date-badge tomorrow'>Tomorrow</span>"
                    date_str = event['start'].strftime('%b %d')
                else:
                    date_badge = ""
                    date_str = event['start'].strftime('%a, %b %d')
                
                # Format time based on whether it's all-day or has end time
                if event.get('is_all_day'):
                    time_str = "<span class='time-badge all-day'>All Day</span>"
                elif event.get('end'):
                    start_time = event['start'].strftime('%-I:%M %p')
                    end_time = event['end'].strftime('%-I:%M %p')
                    time_str = f"<span class='time-badge'>{start_time} - {end_time}</span>"
                else:
                    time_str = f"<span class='time-badge'>{event['start'].strftime('%-I:%M %p')}</span>"
                
                location = f"<div class='event-location'>📍 {event['location']}</div>" if event['location'] else ""
                
                # Determine event styling based on timing
                event_class = "calendar-event"
                if event_date == today:
                    event_class += " event-today"
                elif event_date == tomorrow:
                    event_class += " event-tomorrow"
                
                events_html += f"""
                <div class="{event_class}">
                    <div class="event-header">
                        <div class="event-date-line">
                            {date_badge}
                            <span class="event-date">{date_str}</span>
                        </div>
                        {time_str}
                    </div>
                    <div class="event-title">{event['summary']}</div>
                    {location}
                </div>
                """
        
        # Header message
        header_text = "📅 Upcoming Events"
        if self.data.get('showing_from_tomorrow'):
            header_text = "📅 Upcoming Events (from tomorrow)"
        
        return f"""
        <div class="calendar-panel">
            <div class="calendar-header">{header_text}</div>
            <div class="calendar-events">
                {events_html}
            </div>
        </div>
        """
    
    def get_css(self, size: str) -> str:
        """Generate CSS for calendar panel."""
        return """
        .calendar-panel {
            height: 100%;
            display: flex;
            flex-direction: column;
            padding: 15px;
            background: white;
            overflow: hidden;
        }
        .calendar-header {
            font-size: 18px;
            font-weight: bold;
            margin-bottom: 15px;
            padding-bottom: 10px;
            border-bottom: 2px solid #333;
        }
        .calendar-events {
            flex: 1;
            overflow: hidden;
        }
        .calendar-event {
            margin-bottom: 10px;
            padding: 12px;
            background: #fafafa;
            border-left: 4px solid #666;
            border-radius: 4px;
            transition: all 0.2s;
        }
        .calendar-event.event-today {
            background: #f0f0f0;
            border-left-color: #000;
            border-left-width: 5px;
        }
        .calendar-event.event-tomorrow {
            background: #f5f5f5;
            border-left-color: #333;
        }
        .event-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 6px;
            flex-wrap: wrap;
            gap: 6px;
        }
        .event-date-line {
            display: flex;
            align-items: center;
            gap: 6px;
        }
        .date-badge {
            display: inline-block;
            padding: 2px 8px;
            border-radius: 3px;
            font-size: 10px;
            font-weight: bold;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        .date-badge.today {
            background: #000;
            color: white;
        }
        .date-badge.tomorrow {
            background: #333;
            color: white;
        }
        .event-date {
            font-size: 11px;
            color: #666;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.3px;
        }
        .time-badge {
            display: inline-block;
            padding: 2px 8px;
            background: white;
            border: 1px solid #ddd;
            border-radius: 3px;
            font-size: 10px;
            font-weight: 600;
            color: #333;
            white-space: nowrap;
        }
        .time-badge.all-day {
            background: #f0f0f0;
            border-color: #999;
        }
        .event-title {
            font-size: 14px;
            font-weight: bold;
            line-height: 1.3;
            color: #000;
        }
        .event-location {
            font-size: 11px;
            color: #666;
            margin-top: 4px;
            font-style: italic;
        }
        .calendar-empty, .calendar-loading {
            display: flex;
            align-items: center;
            justify-content: center;
            height: 100%;
            font-size: 16px;
            color: #999;
        }
        """
