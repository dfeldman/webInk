"""RSS Feed Panel for dashboard."""
import aiohttp
import feedparser
import re
from datetime import datetime
from typing import Dict, Any, List
try:
    from .base_panel import BasePanel
except ImportError:
    from base_panel import BasePanel


class RssPanel(BasePanel):
    """Panel that displays RSS feed items from one or more feeds."""
    
    async def fetch_data(self) -> None:
        """Fetch RSS feed data from one or multiple feeds."""
        try:
            # Support both single URL and multiple URLs
            urls = self.config.get('urls') or [self.config.get('url')]
            if not urls or not any(urls):
                raise ValueError("RSS panel requires 'url' or 'urls' in configuration")
            
            # Ensure urls is a list
            if isinstance(urls, str):
                urls = [urls]
            
            all_items = []
            feed_titles = []
            
            # Fetch all feeds
            async with aiohttp.ClientSession() as session:
                for url in urls:
                    if not url:
                        continue
                    try:
                        async with session.get(url, timeout=aiohttp.ClientTimeout(total=10)) as response:
                            content = await response.text()
                        
                        # Parse feed
                        feed = feedparser.parse(content)
                        feed_title = feed.feed.get('title', 'RSS Feed')
                        feed_titles.append(feed_title)
                        
                        # Extract items with source
                        for entry in feed.entries:
                            # Parse published date
                            published_date = None
                            if entry.get('published_parsed'):
                                try:
                                    published_date = datetime(*entry.published_parsed[:6])
                                except:
                                    pass
                            elif entry.get('updated_parsed'):
                                try:
                                    published_date = datetime(*entry.updated_parsed[:6])
                                except:
                                    pass
                            
                            # Clean up summary - strip HTML tags
                            summary = entry.get('summary', entry.get('description', ''))
                            summary = re.sub(r'<[^>]+>', '', summary)  # Remove HTML tags
                            summary = re.sub(r'\s+', ' ', summary).strip()  # Normalize whitespace
                            
                            all_items.append({
                                'title': entry.get('title', 'No title'),
                                'summary': summary,
                                'link': entry.get('link', ''),
                                'published': published_date,
                                'source': feed_title if len(urls) > 1 else None
                            })
                    except Exception as e:
                        # Log error but continue with other feeds
                        print(f"Error fetching feed {url}: {e}")
                        continue
            
            # Sort by date (newest first), then by title
            all_items.sort(key=lambda x: (x['published'] or datetime.min, x['title']), reverse=True)
            
            # Determine title
            if len(feed_titles) == 1:
                panel_title = feed_titles[0]
            elif len(feed_titles) > 1:
                panel_title = "News Headlines"
            else:
                panel_title = "RSS Feed"
            
            self.data = {
                'feed_title': panel_title,
                'items': all_items,
                'multi_feed': len(urls) > 1
            }
            self.error = None
            
        except Exception as e:
            self.error = f"Failed to fetch RSS feed: {str(e)}"
            self.data = None
    
    def get_html(self, size: str) -> str:
        """Generate HTML for RSS panel."""
        if self.error:
            return self.get_error_html(size)
        
        if not self.data:
            return "<div class='rss-loading'>Loading RSS feed...</div>"
        
        # Compact mode for 1x1 - headlines only, no summaries
        if size == '1x1':
            items_html = ""
            for item in self.data['items'][:20]:  # Show up to 20 headlines
                source_tag = f"<span class='rss-source'>[{item['source']}]</span> " if item.get('source') else ""
                items_html += f'<div class="rss-headline">{source_tag}{item["title"]}</div>'
            
            return f"""
            <div class="rss-panel compact">
                <div class="rss-header-compact">{self.data['feed_title']}</div>
                <div class="rss-headlines">
                    {items_html}
                </div>
            </div>
            """
        
        # Standard mode - show as many as fit with summaries
        # Estimate: ~60px per item with summary, ~40px per item without
        show_summaries = size in ['2x2', '1x2']
        
        items_html = ""
        for item in self.data['items'][:30]:  # Process up to 30 items
            source_tag = f"<span class='rss-source'>[{item['source']}]</span> " if item.get('source') else ""
            
            if show_summaries and item['summary']:
                # Truncate summary based on size
                max_summary = 120 if size == '2x2' else 80
                summary = item['summary'][:max_summary] + "..." if len(item['summary']) > max_summary else item['summary']
                
                items_html += f"""
                <div class="rss-item">
                    <div class="rss-item-title">{source_tag}{item['title']}</div>
                    <div class="rss-item-summary">{summary}</div>
                </div>
                """
            else:
                # Title only
                items_html += f"""
                <div class="rss-item-compact">
                    <div class="rss-item-title-compact">{source_tag}{item['title']}</div>
                </div>
                """
        
        return f"""
        <div class="rss-panel">
            <div class="rss-header">{self.data['feed_title']}</div>
            <div class="rss-items">
                {items_html}
            </div>
        </div>
        """
    
    def get_css(self, size: str) -> str:
        """Generate CSS for RSS panel."""
        return """
        .rss-panel {
            height: 100%;
            display: flex;
            flex-direction: column;
            padding: 15px;
            background: white;
            overflow: hidden;
        }
        
        /* Compact mode for 1x1 - headlines only */
        .rss-panel.compact {
            padding: 8px;
        }
        .rss-header-compact {
            font-size: 14px;
            font-weight: bold;
            margin-bottom: 6px;
            padding-bottom: 4px;
            border-bottom: 2px solid #333;
        }
        .rss-headlines {
            flex: 1;
            overflow: hidden;
        }
        .rss-headline {
            font-size: 10px;
            line-height: 1.3;
            margin-bottom: 4px;
            padding-bottom: 4px;
            border-bottom: 1px solid #eee;
        }
        .rss-headline:last-child {
            border-bottom: none;
        }
        
        /* Standard mode */
        .rss-header {
            font-size: 16px;
            font-weight: bold;
            margin-bottom: 12px;
            padding-bottom: 8px;
            border-bottom: 2px solid #333;
        }
        .rss-items {
            flex: 1;
            overflow: hidden;
        }
        
        /* Items with summaries */
        .rss-item {
            margin-bottom: 12px;
            padding-bottom: 8px;
            border-bottom: 1px solid #ddd;
        }
        .rss-item:last-child {
            border-bottom: none;
        }
        .rss-item-title {
            font-weight: bold;
            font-size: 13px;
            margin-bottom: 4px;
            line-height: 1.3;
        }
        .rss-item-summary {
            font-size: 11px;
            color: #666;
            line-height: 1.3;
        }
        
        /* Compact items (title only) */
        .rss-item-compact {
            margin-bottom: 6px;
            padding-bottom: 6px;
            border-bottom: 1px solid #eee;
        }
        .rss-item-compact:last-child {
            border-bottom: none;
        }
        .rss-item-title-compact {
            font-size: 12px;
            line-height: 1.3;
            font-weight: 500;
        }
        
        /* Source tags for multi-feed */
        .rss-source {
            font-size: 9px;
            color: #999;
            font-weight: normal;
            text-transform: uppercase;
        }
        
        .rss-loading {
            display: flex;
            align-items: center;
            justify-content: center;
            height: 100%;
            font-size: 16px;
            color: #999;
        }
        """
