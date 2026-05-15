# Development Guide (macOS)

## Phase 1: Setting Up PostgreSQL Development Environment on macOS

### Prerequisites

macOS comes with most tools, but you'll need:
- **Xcode Command Line Tools**: `xcode-select --install`
- **Homebrew**: Install from https://brew.sh if not already installed
- **Basic tools**: Git, Make, C compiler (clang) - included with Xcode CLI tools

### Recommended: Use Our Automated Setup Script

We've created an automated setup script that handles everything:

```bash
cd /Users/maxim.podkolzin/dev/cpp/rollups
chmod +x scripts/setup_macos.sh
./scripts/setup_macos.sh
```

This will:
1. Install dependencies via Homebrew (readline, zlib, openssl, icu4c)
2. Download PostgreSQL 16.6 source
3. Build with debug symbols optimized for learning
4. Install to `~/pgdev/postgres/`
5. Initialize database cluster
6. Create test database `rollups_test`

**Estimated time**: 5-10 minutes

---

## Manual Setup (Alternative)

If you prefer manual control or want to understand each step:

### Step 1: Install Build Dependencies

```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install PostgreSQL build dependencies
brew install readline zlib openssl@3 icu4c pkg-config
```

### Step 2: Download PostgreSQL Source

```bash
# Create development directory
mkdir -p ~/pgdev
cd ~/pgdev

# Download PostgreSQL 16.6 stable release
curl -O https://ftp.postgresql.org/pub/source/v16.6/postgresql-16.6.tar.gz
tar -xzf postgresql-16.6.tar.gz
mv postgresql-16.6 postgresql-source
cd postgresql-source
```

### Step 3: Configure with Debug Symbols

```bash
# Set up environment for Homebrew libraries (Apple Silicon)
export PKG_CONFIG_PATH="/opt/homebrew/opt/icu4c/lib/pkgconfig:$PKG_CONFIG_PATH"
export LDFLAGS="-L/opt/homebrew/opt/readline/lib -L/opt/homebrew/opt/openssl@3/lib"
export CPPFLAGS="-I/opt/homebrew/opt/readline/include -I/opt/homebrew/opt/openssl@3/include"

# For Intel Macs, use /usr/local instead of /opt/homebrew

# Configure PostgreSQL with optimal settings for learning
./configure \
  --prefix=$HOME/pgdev/postgres \
  --enable-debug \
  --enable-cassert \
  --enable-depend \
  --with-openssl \
  --with-readline \
  --with-icu \
  CFLAGS="-ggdb -O0 -g3 -fno-omit-frame-pointer"
```

**What these flags mean**:
- `--prefix`: Install to separate directory (won't conflict with system PostgreSQL)
- `--enable-debug`: Include debug symbols
- `--enable-cassert`: Enable assertions (catches bugs, helps understand invariants)
- `--enable-depend`: Automatic dependency tracking for rebuilds
- `-ggdb`: Debug info optimized for GDB/LLDB
- `-O0`: No optimization (code matches source exactly)
- `-g3`: Maximum debug info including macros
- `-fno-omit-frame-pointer`: Better stack traces

### Step 4: Build PostgreSQL

```bash
# Build using all CPU cores
make -j$(sysctl -n hw.ncpu)

# Install to ~/pgdev/postgres/
make install

# Build contrib modules (important learning examples)
cd contrib
make -j$(sysctl -n hw.ncpu)
make install
```

### Step 5: Add to PATH

```bash
# For zsh (default on modern macOS)
echo 'export PATH="$HOME/pgdev/postgres/bin:$PATH"' >> ~/.zshrc
echo 'export PGDATA="$HOME/pgdev/data"' >> ~/.zshrc
source ~/.zshrc

# For bash (older macOS)
# echo 'export PATH="$HOME/pgdev/postgres/bin:$PATH"' >> ~/.bashrc
# source ~/.bashrc
```

### Step 6: Initialize Database Cluster

```bash
# Initialize PostgreSQL data directory
initdb -D ~/pgdev/data

# Configure for development (optional but recommended)
cat >> ~/pgdev/data/postgresql.conf <<EOF

# Development settings
log_min_messages = DEBUG1
log_statement = 'all'
shared_preload_libraries = 'pg_stat_statements'
EOF
```

### Step 7: Start PostgreSQL and Create Test Database

```bash
# Start PostgreSQL
pg_ctl -D ~/pgdev/data -l ~/pgdev/logfile start

# Wait a moment for startup
sleep 2

# Create test database
createdb rollups_test

# Install example extension
psql rollups_test -c "CREATE EXTENSION pg_stat_statements;"

# Connect and verify
psql rollups_test
# You should see the PostgreSQL prompt
# Type \dx to see installed extensions
# Type \q to quit
```

---

## Verify Extension Building Works

Test that you can build extensions using PGXS:

```bash
# Navigate to example extension
cd ~/pgdev/postgresql-source/contrib/pg_stat_statements

# Build using PGXS
make

# Install (no sudo needed since we own ~/pgdev)
make install

# Test in database
psql rollups_test -c "CREATE EXTENSION IF NOT EXISTS pg_stat_statements;"
psql rollups_test -c "\dx"
```

You should see `pg_stat_statements` listed. This confirms your environment is ready for extension development!

---

## IDE Setup for macOS

### VS Code

1. Install C/C++ extension: `ms-vscode.cpptools`

2. Create `.vscode/c_cpp_properties.json`:

```json
{
  "configurations": [
    {
      "name": "Mac",
      "includePath": [
        "${workspaceFolder}/**",
        "${env:HOME}/pgdev/postgres/include/server",
        "${env:HOME}/pgdev/postgres/include",
        "/opt/homebrew/include"
      ],
      "defines": [],
      "macFrameworkPath": [
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks"
      ],
      "compilerPath": "/usr/bin/clang",
      "cStandard": "c17",
      "cppStandard": "c++17",
      "intelliSenseMode": "macos-clang-arm64"
    }
  ],
  "version": 4
}
```

**Note**: Change `arm64` to `x64` if you're on Intel Mac.

3. Create `.vscode/launch.json` for debugging:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Attach to Postgres Backend",
      "type": "lldb",
      "request": "attach",
      "program": "${env:HOME}/pgdev/postgres/bin/postgres",
      "pid": "${command:pickProcess}"
    }
  ]
}
```

### CLion

1. **File → Settings → Build, Execution, Deployment → Toolchains**
   - Set compiler to `/usr/bin/clang`

2. **File → Settings → Languages & Frameworks → C/C++**
   - Add include paths:
     - `~/pgdev/postgres/include/server`
     - `~/pgdev/postgres/include`

3. **Run → Edit Configurations → + → LLDB Attach**
   - Select process: postgres (use PID from `pg_backend_pid()`)

---

## Debugging on macOS

### Using LLDB (Native macOS Debugger)

```bash
# Find backend process ID
psql rollups_test -c "SELECT pg_backend_pid();"
# Example output: 12345

# Attach LLDB
lldb -p 12345

# Set breakpoints
(lldb) breakpoint set --name my_extension_function
(lldb) breakpoint set --file my_extension.c --line 42

# Continue execution
(lldb) continue

# When breakpoint hits
(lldb) print variable_name
(lldb) frame variable
(lldb) step
(lldb) next
(lldb) finish
```

### View Logs in Real-Time

```bash
# Follow PostgreSQL logs
tail -f ~/pgdev/logfile

# Or use Console.app to filter logs
```

---

## Useful Commands for macOS

### PostgreSQL Management

```bash
# Check if PostgreSQL is running
pg_ctl status

# Start PostgreSQL
pg_ctl start

# Stop PostgreSQL
pg_ctl stop

# Restart PostgreSQL
pg_ctl restart

# Reload configuration (no restart)
pg_ctl reload

# Get PostgreSQL configuration info
pg_config --version
pg_config --configure
pg_config --includedir-server
pg_config --libdir
pg_config --pkglibdir
```

### System Information

```bash
# Check CPU cores (for parallel builds)
sysctl -n hw.ncpu

# Check architecture (Apple Silicon vs Intel)
uname -m
# arm64 = Apple Silicon (M1/M2/M3)
# x86_64 = Intel

# Find Homebrew prefix
brew --prefix
# /opt/homebrew on Apple Silicon
# /usr/local on Intel
```

---

## Study Extension Examples

Once setup is complete, study these contrib modules in order:

1. **`pg_stat_statements`** - Hooks and shared memory
   ```bash
   cd ~/pgdev/postgresql-source/contrib/pg_stat_statements
   less pg_stat_statements.c
   ```

2. **`bloom`** - Custom index access method
   ```bash
   cd ~/pgdev/postgresql-source/contrib/bloom
   less bloom.c
   ```

3. **`postgres_fdw`** - Complex extension with planner hooks
   ```bash
   cd ~/pgdev/postgresql-source/contrib/postgres_fdw
   less postgres_fdw.c
   ```

---

## Troubleshooting macOS-Specific Issues

### "pg_config not found"
```bash
# Add to PATH
echo 'export PATH="$HOME/pgdev/postgres/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

### "library not found" when building
```bash
# Make sure Homebrew paths are in environment
export LDFLAGS="-L$(brew --prefix)/opt/readline/lib -L$(brew --prefix)/opt/openssl@3/lib"
export CPPFLAGS="-I$(brew --prefix)/opt/readline/include -I$(brew --prefix)/opt/openssl@3/include"
```

### "cannot connect to server"
```bash
# Check if PostgreSQL is running
pg_ctl status

# Check logs for errors
tail -20 ~/pgdev/logfile

# Make sure PGDATA is set
echo $PGDATA
# Should be: /Users/maxim.podkolzin/pgdev/data
```

### "initdb: command not found"
```bash
# Use full path before adding to PATH
~/pgdev/postgres/bin/initdb -D ~/pgdev/data
```

### Apple Silicon specific issues

If you're on Apple Silicon (M1/M2/M3) and see architecture mismatches:
```bash
# Check if Homebrew is installed for ARM
file $(which brew)
# Should contain "arm64"

# Rebuild PostgreSQL ensuring ARM architecture
ARCHFLAGS="-arch arm64" ./configure ...
```

---

## Next Steps

Once your environment is ready:

1. **Verify everything works**:
   ```bash
   pg_config --version
   psql rollups_test -c "SELECT version();"
   ```

2. **Study extension examples** in `~/pgdev/postgresql-source/contrib/`

3. **Begin Phase 2**: Create basic extension scaffolding (see `claude.md`)

4. **Research continuous aggregates**:
   - TimescaleDB architecture
   - ClickHouse materialized views

---

## Quick Reference Card

```bash
# Daily commands
pg_ctl start                          # Start PostgreSQL
psql rollups_test                     # Connect to test DB
tail -f ~/pgdev/logfile              # View logs

# Development
cd ~/pgdev/postgresql-source/contrib # Study examples
make && make install                 # Build and install extension

# Debugging
psql -c "SELECT pg_backend_pid();"   # Get backend PID
lldb -p <pid>                         # Attach debugger

# Configuration
pg_config --configure                # See build flags
cat ~/pgdev/data/postgresql.conf     # View settings
```

See `claude.md` for the complete implementation roadmap.
