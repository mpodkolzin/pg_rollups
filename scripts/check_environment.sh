#!/bin/bash
# Environment check script for PostgreSQL extension development

echo "=== PostgreSQL Development Environment Check ==="
echo ""

# Check for PostgreSQL binaries
echo "1. Checking for PostgreSQL installation..."
if command -v psql &> /dev/null; then
    echo "   ✓ psql found: $(which psql)"
    psql --version
else
    echo "   ✗ psql not found"
fi

if command -v pg_config &> /dev/null; then
    echo "   ✓ pg_config found: $(which pg_config)"
    pg_config --version
else
    echo "   ✗ pg_config not found (needed for extension development)"
fi
echo ""

# Check pg_config details if available
if command -v pg_config &> /dev/null; then
    echo "2. PostgreSQL Configuration:"
    echo "   Include directory: $(pg_config --includedir-server)"
    echo "   Library directory: $(pg_config --libdir)"
    echo "   Extension directory: $(pg_config --sharedir)/extension"
    echo "   PGXS Makefile: $(pg_config --pgxs)"
    echo ""

    echo "3. Build Configuration:"
    pg_config --configure | tr ' ' '\n' | grep -E "(debug|assert|optimization)" || echo "   (No debug flags detected - not ideal for learning)"
    echo ""
fi

# Check for development tools
echo "4. Development Tools:"
if command -v gcc &> /dev/null; then
    echo "   ✓ gcc found: $(gcc --version | head -n1)"
else
    echo "   ✗ gcc not found"
fi

if command -v clang &> /dev/null; then
    echo "   ✓ clang found: $(clang --version | head -n1)"
else
    echo "   ✗ clang not found"
fi

if command -v make &> /dev/null; then
    echo "   ✓ make found: $(make --version | head -n1)"
else
    echo "   ✗ make not found"
fi

if command -v git &> /dev/null; then
    echo "   ✓ git found: $(git --version)"
else
    echo "   ✗ git not found"
fi
echo ""

# Check for Homebrew on macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "5. Package Manager (macOS):"
    if command -v brew &> /dev/null; then
        echo "   ✓ Homebrew found: $(brew --version | head -n1)"
        echo ""
        echo "   Checking for PostgreSQL in Homebrew:"
        brew list | grep postgresql || echo "   (No PostgreSQL formula installed)"
    else
        echo "   ✗ Homebrew not found"
        echo "   Install from: https://brew.sh"
    fi
    echo ""
fi

# Summary
echo "=== Summary ==="
if command -v pg_config &> /dev/null; then
    echo "PostgreSQL is installed. Checking if suitable for development..."

    # Check if server headers are available
    INCLUDE_DIR=$(pg_config --includedir-server)
    if [ -d "$INCLUDE_DIR" ] && [ -f "$INCLUDE_DIR/postgres.h" ]; then
        echo "✓ Server development headers found"
    else
        echo "✗ Server development headers NOT found"
        echo "  Install postgresql-server-dev package or build from source"
    fi

    # Check for debug build
    if pg_config --configure | grep -q "enable-debug"; then
        echo "✓ Built with debug symbols (ideal for learning!)"
    else
        echo "⚠ NOT built with debug symbols"
        echo "  Consider building from source with --enable-debug for better learning experience"
    fi
else
    echo "PostgreSQL not installed or not in PATH"
    echo "Recommended: Build from source with debug symbols"
fi

echo ""
echo "Next steps: See DEVELOPMENT.md for installation instructions"
