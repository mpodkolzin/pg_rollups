#!/bin/bash
# PostgreSQL development environment setup for macOS
# This script builds PostgreSQL from source with debug symbols

set -e  # Exit on error

# Configuration
PG_VERSION="16.6"  # Stable release
PG_MAJOR="16"
INSTALL_DIR="$HOME/pgdev/postgres"
DATA_DIR="$HOME/pgdev/data"
SOURCE_DIR="$HOME/pgdev/postgresql-source"

echo "=== PostgreSQL Development Environment Setup ==="
echo "This will:"
echo "  - Download PostgreSQL $PG_VERSION source"
echo "  - Build with debug symbols and assertions (for learning)"
echo "  - Install to $INSTALL_DIR"
echo "  - Create development database cluster in $DATA_DIR"
echo ""
read -p "Continue? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    exit 0
fi

# Check prerequisites
echo ""
echo "1. Checking prerequisites..."

if ! command -v brew &> /dev/null; then
    echo "Error: Homebrew not found. Install from https://brew.sh"
    exit 1
fi

# Install build dependencies
echo "   Installing build dependencies via Homebrew..."
brew install readline zlib openssl@3 icu4c pkg-config

# Set up build environment for Homebrew libraries
export PKG_CONFIG_PATH="/opt/homebrew/opt/icu4c/lib/pkgconfig:$PKG_CONFIG_PATH"
export LDFLAGS="-L/opt/homebrew/opt/readline/lib -L/opt/homebrew/opt/zlib/lib -L/opt/homebrew/opt/openssl@3/lib"
export CPPFLAGS="-I/opt/homebrew/opt/readline/include -I/opt/homebrew/opt/zlib/include -I/opt/homebrew/opt/openssl@3/include"

# Download PostgreSQL source
echo ""
echo "2. Downloading PostgreSQL $PG_VERSION source..."
mkdir -p "$HOME/pgdev"
cd "$HOME/pgdev"

if [ ! -d "postgresql-source" ]; then
    curl -O "https://ftp.postgresql.org/pub/source/v${PG_VERSION}/postgresql-${PG_VERSION}.tar.gz"
    tar -xzf "postgresql-${PG_VERSION}.tar.gz"
    mv "postgresql-${PG_VERSION}" postgresql-source
    rm "postgresql-${PG_VERSION}.tar.gz"
    echo "   ✓ Source downloaded and extracted"
else
    echo "   ✓ Source directory already exists"
fi

# Configure and build
echo ""
echo "3. Configuring PostgreSQL with debug symbols..."
cd "$SOURCE_DIR"

./configure \
    --prefix="$INSTALL_DIR" \
    --enable-debug \
    --enable-cassert \
    --enable-depend \
    --with-openssl \
    --with-readline \
    --with-icu \
    CFLAGS="-ggdb -O0 -g3 -fno-omit-frame-pointer -Wno-unguarded-availability-new" \
    LDFLAGS="$LDFLAGS" \
    CPPFLAGS="$CPPFLAGS"

echo ""
echo "4. Building PostgreSQL (this may take 5-10 minutes)..."
make -j$(sysctl -n hw.ncpu)

echo ""
echo "5. Installing PostgreSQL..."
make install

# Build contrib modules (important examples for learning)
echo ""
echo "6. Building contrib modules..."
cd contrib
make -j$(sysctl -n hw.ncpu)
make install

# Add to PATH
echo ""
echo "7. Updating PATH..."
SHELL_RC=""
if [ -f "$HOME/.zshrc" ]; then
    SHELL_RC="$HOME/.zshrc"
elif [ -f "$HOME/.bashrc" ]; then
    SHELL_RC="$HOME/.bashrc"
fi

if [ -n "$SHELL_RC" ]; then
    if ! grep -q "$INSTALL_DIR/bin" "$SHELL_RC"; then
        echo "" >> "$SHELL_RC"
        echo "# PostgreSQL development environment" >> "$SHELL_RC"
        echo "export PATH=\"$INSTALL_DIR/bin:\$PATH\"" >> "$SHELL_RC"
        echo "export PGDATA=\"$DATA_DIR\"" >> "$SHELL_RC"
        echo "   ✓ Added to $SHELL_RC"
    else
        echo "   ✓ Already in PATH"
    fi
fi

# Initialize database cluster
echo ""
echo "8. Initializing database cluster..."
export PATH="$INSTALL_DIR/bin:$PATH"

if [ ! -d "$DATA_DIR" ]; then
    "$INSTALL_DIR/bin/initdb" -D "$DATA_DIR"
    echo "   ✓ Database cluster initialized"
else
    echo "   ✓ Database cluster already exists"
fi

# Configure PostgreSQL for development
echo ""
echo "9. Configuring PostgreSQL for development..."
cat >> "$DATA_DIR/postgresql.conf" <<EOF

# Development settings added by setup script
log_min_messages = DEBUG1
log_statement = 'all'
shared_preload_libraries = 'pg_stat_statements'
EOF

echo "   ✓ Development settings added"

# Start PostgreSQL
echo ""
echo "10. Starting PostgreSQL..."
"$INSTALL_DIR/bin/pg_ctl" -D "$DATA_DIR" -l "$HOME/pgdev/logfile" start
sleep 2

# Create test database
echo ""
echo "11. Creating test database..."
"$INSTALL_DIR/bin/createdb" rollups_test || echo "   (Database may already exist)"

# Install pg_stat_statements
echo ""
echo "12. Setting up pg_stat_statements extension..."
"$INSTALL_DIR/bin/psql" rollups_test -c "CREATE EXTENSION IF NOT EXISTS pg_stat_statements;" || true

# Print summary
echo ""
echo "=== Setup Complete! ==="
echo ""
echo "PostgreSQL $PG_VERSION installed with DEBUG symbols"
echo "   Install directory: $INSTALL_DIR"
echo "   Data directory: $DATA_DIR"
echo "   Log file: $HOME/pgdev/logfile"
echo ""
echo "Important: Reload your shell to update PATH"
echo "   source $SHELL_RC"
echo ""
echo "Quick commands:"
echo "   pg_ctl status                    # Check if running"
echo "   psql rollups_test                # Connect to test database"
echo "   pg_ctl restart                   # Restart PostgreSQL"
echo "   tail -f ~/pgdev/logfile          # View logs"
echo ""
echo "Next step: Study extension examples in:"
echo "   $SOURCE_DIR/contrib/"
echo ""
echo "Documentation updated in claude.md"
