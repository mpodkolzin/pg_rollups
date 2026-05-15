#!/bin/bash
# Rollups extension development environment setup for macOS
#
# Prerequisites: PostgreSQL must already be installed (run setup_macos.sh first)
# This script sets up the rollups extension build environment

set -e  # Exit on error

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Rollups Extension Setup ===${NC}"
echo "This script will:"
echo "  1. Verify PostgreSQL development environment"
echo "  2. Install CMake (build system)"
echo "  3. Build and install rollups extension"
echo "  4. Set up LLDB formatters for debugging"
echo "  5. Create VS Code configuration (optional)"
echo "  6. Test extension loading"
echo ""

# Configuration
INSTALL_DIR="$HOME/pgdev/postgres"
ROLLUPS_DIR="$(cd "$(dirname "$0")/.." && pwd)"  # Parent of scripts/ directory

echo -e "${BLUE}Rollups source directory: ${ROLLUPS_DIR}${NC}"
echo ""

# Check prerequisites
echo -e "${BLUE}[1/7] Checking prerequisites...${NC}"

# Check if PostgreSQL is installed
if [ ! -f "$INSTALL_DIR/bin/pg_config" ]; then
    echo -e "${RED}Error: PostgreSQL not found at $INSTALL_DIR${NC}"
    echo "Please run setup_macos.sh first to install PostgreSQL"
    exit 1
fi

PG_VERSION=$("$INSTALL_DIR/bin/pg_config" --version)
echo -e "${GREEN}✓ PostgreSQL found: $PG_VERSION${NC}"

# Check if PostgreSQL headers exist
PG_INCLUDEDIR_SERVER=$("$INSTALL_DIR/bin/pg_config" --includedir-server)
if [ ! -f "$PG_INCLUDEDIR_SERVER/postgres.h" ]; then
    echo -e "${RED}Error: PostgreSQL headers not found at $PG_INCLUDEDIR_SERVER${NC}"
    exit 1
fi
echo -e "${GREEN}✓ PostgreSQL headers found${NC}"

# Check if Homebrew is available
if ! command -v brew &> /dev/null; then
    echo -e "${RED}Error: Homebrew not found. Install from https://brew.sh${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Homebrew found${NC}"

# Check if extension source files exist
if [ ! -f "$ROLLUPS_DIR/CMakeLists.txt" ]; then
    echo -e "${RED}Error: CMakeLists.txt not found in $ROLLUPS_DIR${NC}"
    echo "Make sure you're in the rollups extension directory"
    exit 1
fi
echo -e "${GREEN}✓ Extension source files found${NC}"

# Install CMake
echo ""
echo -e "${BLUE}[2/7] Installing CMake...${NC}"
if command -v cmake &> /dev/null; then
    CMAKE_VERSION=$(cmake --version | head -n1)
    echo -e "${GREEN}✓ CMake already installed: $CMAKE_VERSION${NC}"
else
    echo "Installing CMake via Homebrew..."
    brew install cmake
    echo -e "${GREEN}✓ CMake installed${NC}"
fi

# Verify CMake version
CMAKE_VERSION=$(cmake --version | head -n1 | grep -oE '[0-9]+\.[0-9]+' | head -1)
CMAKE_MAJOR=$(echo $CMAKE_VERSION | cut -d. -f1)
CMAKE_MINOR=$(echo $CMAKE_VERSION | cut -d. -f2)
if [ "$CMAKE_MAJOR" -lt 3 ] || ([ "$CMAKE_MAJOR" -eq 3 ] && [ "$CMAKE_MINOR" -lt 20 ]); then
    echo -e "${RED}Error: CMake 3.20+ required, found $CMAKE_VERSION${NC}"
    exit 1
fi

# Build extension
echo ""
echo -e "${BLUE}[3/7] Building rollups extension...${NC}"
cd "$ROLLUPS_DIR"

# Clean build directory if it exists
if [ -d "build" ]; then
    echo "Cleaning existing build directory..."
    rm -rf build
fi

mkdir build
cd build

echo "Configuring with CMake..."
cmake ..

echo "Building extension..."
make

echo -e "${GREEN}✓ Extension built successfully${NC}"

# Install extension
echo ""
echo -e "${BLUE}[4/7] Installing extension...${NC}"
make install
echo -e "${GREEN}✓ Extension installed to $INSTALL_DIR/lib/rollups.so${NC}"

# Set up LLDB formatters
echo ""
echo -e "${BLUE}[5/7] Setting up LLDB formatters...${NC}"
mkdir -p "$HOME/.lldb"

if [ -f "$ROLLUPS_DIR/scripts/lldb_postgres_formatters.py" ]; then
    cp "$ROLLUPS_DIR/scripts/lldb_postgres_formatters.py" "$HOME/.lldb/"

    # Add to .lldbinit if not already present
    if [ ! -f "$HOME/.lldbinit" ] || ! grep -q "lldb_postgres_formatters.py" "$HOME/.lldbinit"; then
        echo "command script import ~/.lldb/lldb_postgres_formatters.py" >> "$HOME/.lldbinit"
        echo -e "${GREEN}✓ LLDB formatters installed${NC}"
    else
        echo -e "${GREEN}✓ LLDB formatters already configured${NC}"
    fi
else
    echo -e "${YELLOW}⚠ LLDB formatters script not found (optional)${NC}"
fi

# Create VS Code configuration
echo ""
echo -e "${BLUE}[6/7] Setting up VS Code configuration...${NC}"
read -p "Create VS Code configuration? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    mkdir -p "$ROLLUPS_DIR/.vscode"

    # Create c_cpp_properties.json
    if [ ! -f "$ROLLUPS_DIR/.vscode/c_cpp_properties.json" ]; then
        cat > "$ROLLUPS_DIR/.vscode/c_cpp_properties.json" <<EOF
{
    "configurations": [
        {
            "name": "Mac",
            "includePath": [
                "\${workspaceFolder}/**",
                "$PG_INCLUDEDIR_SERVER",
                "$("$INSTALL_DIR/bin/pg_config" --includedir)",
                "/opt/homebrew/include"
            ],
            "defines": [],
            "macFrameworkPath": [
                "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks"
            ],
            "compilerPath": "/usr/bin/clang",
            "cStandard": "c17",
            "cppStandard": "c++17",
            "intelliSenseMode": "macos-clang-arm64",
            "compileCommands": "\${workspaceFolder}/build/compile_commands.json"
        }
    ],
    "version": 4
}
EOF
        echo -e "${GREEN}✓ Created .vscode/c_cpp_properties.json${NC}"
    else
        echo -e "${GREEN}✓ c_cpp_properties.json already exists${NC}"
    fi

    # Create launch.json
    if [ ! -f "$ROLLUPS_DIR/.vscode/launch.json" ]; then
        cat > "$ROLLUPS_DIR/.vscode/launch.json" <<EOF
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Attach to Postgres Backend",
            "type": "lldb",
            "request": "attach",
            "program": "$INSTALL_DIR/bin/postgres",
            "pid": "\${command:pickProcess}",
            "MIMode": "lldb"
        }
    ]
}
EOF
        echo -e "${GREEN}✓ Created .vscode/launch.json${NC}"
    else
        echo -e "${GREEN}✓ launch.json already exists${NC}"
    fi

    # Create settings.json with helpful defaults
    if [ ! -f "$ROLLUPS_DIR/.vscode/settings.json" ]; then
        cat > "$ROLLUPS_DIR/.vscode/settings.json" <<EOF
{
    "C_Cpp.default.compileCommands": "\${workspaceFolder}/build/compile_commands.json",
    "files.associations": {
        "*.h": "c",
        "*.c": "c",
        "*.hpp": "cpp",
        "*.cpp": "cpp"
    }
}
EOF
        echo -e "${GREEN}✓ Created .vscode/settings.json${NC}"
    else
        echo -e "${GREEN}✓ settings.json already exists${NC}"
    fi
else
    echo -e "${YELLOW}⚠ Skipped VS Code configuration${NC}"
fi

# Test extension loading
echo ""
echo -e "${BLUE}[7/7] Testing extension...${NC}"

# Check if PostgreSQL is running
if ! "$INSTALL_DIR/bin/pg_ctl" status -D "$HOME/pgdev/data" &> /dev/null; then
    echo -e "${YELLOW}PostgreSQL is not running. Starting it...${NC}"
    "$INSTALL_DIR/bin/pg_ctl" start -D "$HOME/pgdev/data" -l "$HOME/pgdev/logfile"
    sleep 2
fi

# Create test database if it doesn't exist
"$INSTALL_DIR/bin/createdb" rollups_test 2>/dev/null || true

# Try to create extension
echo "Attempting to load extension..."
if "$INSTALL_DIR/bin/psql" rollups_test -c "DROP EXTENSION IF EXISTS rollups CASCADE;" &> /dev/null && \
   "$INSTALL_DIR/bin/psql" rollups_test -c "CREATE EXTENSION rollups;" &> /dev/null; then

    # Test a function
    VERSION=$("$INSTALL_DIR/bin/psql" rollups_test -t -A -c "SELECT rollups.version();")
    echo -e "${GREEN}✓ Extension loaded successfully!${NC}"
    echo -e "${GREEN}  Version: $VERSION${NC}"

    # Show available functions
    echo ""
    echo "Available functions:"
    "$INSTALL_DIR/bin/psql" rollups_test -c "\\df rollups.*"
else
    echo -e "${RED}✗ Extension failed to load${NC}"
    echo "Check PostgreSQL logs: tail -f ~/pgdev/logfile"
    exit 1
fi

# Print summary
echo ""
echo -e "${GREEN}=== Setup Complete! ===${NC}"
echo ""
echo "Rollups extension installed and tested successfully!"
echo ""
echo -e "${BLUE}Extension files:${NC}"
echo "  Library: $INSTALL_DIR/lib/rollups.so"
echo "  Control: $INSTALL_DIR/share/extension/rollups.control"
echo "  SQL: $INSTALL_DIR/share/extension/rollups--1.0.sql"
echo ""
echo -e "${BLUE}Development workflow:${NC}"
echo "  cd $ROLLUPS_DIR"
echo "  make                    # Build"
echo "  make install            # Install"
echo "  make test               # Run tests (if available)"
echo ""
echo -e "${BLUE}Quick commands:${NC}"
echo "  psql rollups_test -c 'SELECT rollups.version();'   # Test extension"
echo "  psql rollups_test -c '\\df rollups.*'               # List functions"
echo "  psql rollups_test -c 'SELECT * FROM rollups.rollup_info;'  # View aggregates"
echo ""
echo -e "${BLUE}Debugging:${NC}"
echo "  1. Find backend PID: psql rollups_test -c 'SELECT pg_backend_pid();'"
echo "  2. Attach LLDB: lldb -p <pid>"
echo "  3. Or use VS Code: Run > Start Debugging > Attach to Postgres Backend"
echo ""
echo -e "${BLUE}Next steps:${NC}"
echo "  - Read docs/BUILDING.md for build details"
echo "  - Read docs/DEBUGGING.md for debugging guide"
echo "  - Run scripts/test_catalog.sh to test catalog operations"
echo ""
