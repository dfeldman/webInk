FROM python:3.11-slim

ARG APP_VERSION=1.0.0

LABEL org.opencontainers.image.title="webInk"
LABEL org.opencontainers.image.description="webInk snapshot server and e-ink dashboard"
LABEL org.opencontainers.image.version="${APP_VERSION}"

ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PATH="/root/.local/bin:${PATH}"

# System dependencies and runtime libraries (Debian/glibc) for Python and Playwright
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        bash \
        curl \
        ca-certificates \
        build-essential \
        libffi-dev \
        libssl-dev \
        zlib1g-dev \
        libjpeg-dev \
        fontconfig \
        # Common Playwright / Chromium runtime deps
        libnss3 \
        libatk1.0-0 \
        libatk-bridge2.0-0 \
        libcups2 \
        libdrm2 \
        libxkbcommon0 \
        libxcomposite1 \
        libxdamage1 \
        libxfixes3 \
        libxrandr2 \
        libgbm1 \
        libpango-1.0-0 \
        libasound2 \
        fonts-liberation \
        libxshmfence1 \
        libx11-xcb1 \
        libx11-6 \
        libxext6 \
        libxss1 \
        libglib2.0-0 \
        libcairo2 \
        libxrender1 \
    && rm -rf /var/lib/apt/lists/*

# Install uv (Python packaging and runtime manager)
RUN curl -LsSf https://astral.sh/uv/install.sh | sh

WORKDIR /app

# Copy project metadata first to leverage Docker layer caching
COPY server/pyproject.toml server/uv.lock ./server/

WORKDIR /app/server

# Install Python dependencies into a local virtual environment
RUN uv sync --frozen --no-dev

WORKDIR /app

# Copy application source code
COPY server ./server
COPY apps ./apps
COPY client ./client

WORKDIR /app/server

# Install Playwright browser binaries (Chromium) for webInk
RUN uv run playwright install chromium

# Default configuration files are baked into the image
#   - webInk config:      /app/server/config.yaml (can be overridden via volume)
#   - dashboard config:   /app/apps/dashboard/dashboard_config.yaml (can be overridden via volume)
ENV WEBINK_CONFIG_PATH=/app/server/config.yaml \
    DASHBOARD_CONFIG_PATH=/app/apps/dashboard/dashboard_config.yaml

# Expose webInk (8000), dashboard (8080) and TCP socket (8091) ports
EXPOSE 8000 8080 8091

# Start both services under uv
# - webInk server (uses CONFIG_FILE = "config.yaml" in /app/server)
# - dashboard server (expects a YAML config path argument)
CMD ["sh", "-c", "uv run webInk.py & uv run ../apps/dashboard/dashboard.py ${DASHBOARD_CONFIG_PATH} & wait"]

