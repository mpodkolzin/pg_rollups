# Makefile - CMake wrapper for rollups extension
#
# This Makefile is a convenience wrapper around CMake for developers
# familiar with the traditional PGXS workflow (make, make install, etc.)
#
# WHY A MAKEFILE WRAPPER?
# - Familiar interface for PostgreSQL extension developers
# - Shorter commands: "make" instead of "cd build && cmake .. && make"
# - Hides CMake build directory complexity
# - Provides helpful targets similar to PGXS
#
# The actual build logic is in CMakeLists.txt

# Build directory
BUILD_DIR := build

# Default target: configure and build
.PHONY: all
all: configure
	@cmake --build $(BUILD_DIR) --parallel

# Configure CMake (creates build directory and runs cmake)
.PHONY: configure
configure: $(BUILD_DIR)/Makefile

$(BUILD_DIR)/Makefile: CMakeLists.txt
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake ..

# Install extension to PostgreSQL
.PHONY: install
install: all
	@cmake --build $(BUILD_DIR) --target install

# Clean build artifacts
.PHONY: clean
clean:
	@if [ -d $(BUILD_DIR) ]; then cmake --build $(BUILD_DIR) --target clean; fi

# Complete clean (remove build directory)
.PHONY: distclean
distclean:
	@rm -rf $(BUILD_DIR)
	@rm -f compile_commands.json
	@echo "Build directory removed"

# Rebuild from scratch
.PHONY: rebuild
rebuild: distclean all install
	@echo "Extension rebuilt successfully"

# Show PostgreSQL configuration
.PHONY: show-pg-config
show-pg-config: configure
	@cmake --build $(BUILD_DIR) --target show-pg-config

# Create extension in database (drops and recreates)
.PHONY: create-extension
create-extension: install
	@cmake --build $(BUILD_DIR) --target create-extension

# Run basic tests
.PHONY: test
test: create-extension
	@cmake --build $(BUILD_DIR) --target test-extension

# Generate compile_commands.json for IDE tooling (clangd, etc.)
.PHONY: compile-commands
compile-commands: configure
	@ln -sf $(BUILD_DIR)/compile_commands.json .
	@echo "compile_commands.json linked (for clangd/IDE support)"

# Help target
.PHONY: help
help:
	@echo "PostgreSQL Rollups Extension (CMake build)"
	@echo ""
	@echo "Available targets:"
	@echo "  make              - Build the extension (configure + compile)"
	@echo "  make install      - Install to PostgreSQL"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make distclean    - Remove entire build directory"
	@echo "  make rebuild      - Clean + build + install"
	@echo ""
	@echo "Development targets:"
	@echo "  make test                - Build, install, and run basic tests"
	@echo "  make create-extension    - Drop and recreate extension in database"
	@echo "  make show-pg-config      - Show PostgreSQL configuration"
	@echo "  make compile-commands    - Generate compile_commands.json for IDE"
	@echo ""
	@echo "Typical workflow:"
	@echo "  1. make                   # Build"
	@echo "  2. make install           # Install to PostgreSQL"
	@echo "  3. make create-extension  # Load in database"
	@echo ""
	@echo "Or simply:"
	@echo "  make test                 # Does all of the above + runs tests"
	@echo ""
	@echo "Build directory: $(BUILD_DIR)/"
	@echo "Build system: CMake (see CMakeLists.txt)"

# Default target when just running 'make' with no arguments
.DEFAULT_GOAL := all
