# WebInk Component Build System
# For Mac development and testing

CXX := clang++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
# Targets
TARGET_MAC := test_webink_mac
TARGET_TYPES := test_types_only
TARGET_INTEGRATION := test_integration
TARGET_SERVER := test_server_connection
TARGET_PROTOCOL := test_protocol

# Mac native test (mocks ESPHome dependencies)
$(TARGET_MAC): test_mac.cpp webink_types.cpp
	@echo "🔨 Building WebInk Mac test..."
	$(CXX) $(CXXFLAGS) -o $@ $^
	@echo "✅ Build complete: $@"

# Types-only test (minimal dependencies)
$(TARGET_TYPES): webink_types.cpp
	@echo "🔨 Building types test..."
	$(CXX) $(CXXFLAGS) -DTEST_TYPES_ONLY -o $@ $^
	@echo "✅ Build complete: $@"

# Integration test (connects to real server)
$(TARGET_INTEGRATION): test_integration.cpp webink_types.cpp webink_config.cpp webink_state.cpp webink_network.cpp webink_image.cpp webink_display_stub.cpp
	@echo "🔨 Building integration test..."
	$(CXX) $(CXXFLAGS) -I. -DWEBINK_MAC_INTEGRATION_TEST -o $@ $^
	@echo "✅ Build complete: $@"

# Server connection test (lightweight, uses curl)
$(TARGET_SERVER): test_server_connection.cpp
	@echo "🔨 Building server connection test..."
	$(CXX) $(CXXFLAGS) -o $@ $^
	@echo "✅ Build complete: $@"

# Protocol verification test (simplified, no config dependencies)
$(TARGET_PROTOCOL): test_protocol.cpp webink_types.cpp
	@echo "🔨 Building protocol test..."
	$(CXX) $(CXXFLAGS) -I. -o $@ $^
	@echo "✅ Build complete: $@"

# Run Mac test
test-mac: $(TARGET_MAC)
	@echo "🧪 Running WebInk Mac tests..."
	@echo "================================"
	./$(TARGET_MAC)

# Run quick types test
test-types: $(TARGET_TYPES)
	@echo "🧪 Running types test..."
	./$(TARGET_TYPES)

# Run integration test (connects to real server)
test-integration: $(TARGET_INTEGRATION)
	@echo "🌐 Running WebInk integration test..."
	@echo "===================================="
	./$(TARGET_INTEGRATION)

# Run server connection test (simple and reliable)
test-server: $(TARGET_SERVER)
	@echo "🌐 Running server connection test..."
	@echo "==================================="
	./$(TARGET_SERVER)

# Run server connection test with custom settings
test-server-custom: $(TARGET_SERVER)
	@echo "🌐 Running server connection test with custom settings..."
	@read -p "Enter server URL (default: http://192.168.68.69:8090): " server; \
	read -p "Enter device ID (default: mac-connection-test): " device; \
	read -p "Enter API key (default: myapikey): " apikey; \
	./$(TARGET_SERVER) \
		$${server:+--server "$$server"} \
		$${device:+--device "$$device"} \
		$${apikey:+--api-key "$$apikey"}

# Run protocol verification test
test-protocol: $(TARGET_PROTOCOL)
	@echo "🔍 Running protocol verification test..."
	@echo "======================================="
	./$(TARGET_PROTOCOL)

# Clean build artifacts
clean:
	@echo "🧹 Cleaning build artifacts..."
	rm -f $(TARGET_MAC) $(TARGET_TYPES) $(TARGET_INTEGRATION) $(TARGET_SERVER) $(TARGET_PROTOCOL) *.pgm *.dat
	@echo "✅ Clean complete"

# Show build info
info:
	@echo "WebInk Component Build System"
	@echo "=============================="
	@echo "Compiler: $(CXX)"
	@echo "Flags: $(CXXFLAGS)"
	@echo ""
	@echo "Available targets:"
	@echo "  make test-mac           - Build and run Mac native tests"
	@echo "  make test-types         - Build and run types-only tests"
	@echo "  make test-protocol      - Verify client protocol URLs (fast)"
	@echo "  make test-server        - Test WebInk server connection (recommended)"
	@echo "  make test-server-custom - Test with custom server settings"
	@echo "  make test-integration   - Full integration test (may need fixes)"
	@echo "  make clean              - Clean build artifacts"
	@echo "  make info               - Show this information"
	@echo ""
	@echo "Files:"
	@echo "  test_mac.cpp         - Mac native test with mocked dependencies"
	@echo "  test_integration.cpp - Integration test connecting to real server"
	@echo "  webink_types.cpp     - Core types and enums"

# Check if we can build (verify clang++ is available)
check:
	@echo "🔍 Checking build environment..."
	@which $(CXX) >/dev/null || (echo "❌ clang++ not found" && exit 1)
	@$(CXX) --version | head -1
	@echo "✅ Build environment OK"

# Quick memory test
test-memory: $(TARGET_MAC)
	@echo "🧠 Running memory optimization tests..."
	@echo "======================================"
	./$(TARGET_MAC) | grep -E "(Memory|bytes|rows)" || true

.PHONY: test-mac test-types clean info check test-memory

# Default target
.DEFAULT_GOAL := info
