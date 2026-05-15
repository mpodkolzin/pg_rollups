# Setting Up Development Environment on New Machine

Complete guide for rebuilding the rollups extension dev environment from scratch on macOS.

## Two-Step Setup Process

### Step 1: PostgreSQL Development Environment (`setup_macos.sh`)

Installs PostgreSQL from source with debug symbols for extension development.

**What it does:**
- Downloads PostgreSQL 16.6 source
- Configures with debug symbols (`--enable-debug`, `--enable-cassert`)
- Builds optimized for LLDB debugging (`-ggdb -O0 -g3`)
- Installs to `~/pgdev/postgres`
- Creates database cluster in `~/pgdev/data`
- Installs contrib modules (pg_stat_statements, etc.)
- Sets up development logging (DEBUG1, log_statement='all')
- Creates `rollups_test` database
- Adds `pg_config` to PATH

**Prerequisites:**
- macOS (Apple Silicon or Intel)
- Xcode Command Line Tools: `xcode-select --install`
- Homebrew: https://brew.sh

**Run:**
```bash
cd ~/dev/cpp/rollups
chmod +x scripts/setup_macos.sh
./scripts/setup_macos.sh
source ~/.zshrc  # Reload PATH
```

**Time:** ~10-15 minutes (includes compilation)

**Verify:**
```bash
pg_config --version        # Should show PostgreSQL 16.6
pg_config --configure      # Should show --enable-debug
psql rollups_test          # Should connect
```

---

### Step 2: Rollups Extension Environment (`setup_rollups_extension.sh`)

Builds and installs the rollups extension on top of existing PostgreSQL.

**What it does:**
- Verifies PostgreSQL is installed
- Installs CMake 3.20+ (build system)
- Builds rollups extension with CMake
- Installs `rollups.so` to PostgreSQL
- Sets up LLDB formatters for debugging PostgreSQL types
- Creates VS Code configuration (optional)
- Tests extension loads correctly

**Prerequisites:**
- PostgreSQL installed via `setup_macos.sh`
- Rollups source code present in current directory

**Run:**
```bash
cd ~/dev/cpp/rollups
chmod +x scripts/setup_rollups_extension.sh
./scripts/setup_rollups_extension.sh
```

**Time:** ~2-3 minutes

**Verify:**
```bash
psql rollups_test -c "SELECT rollups.version();"
# Should return: Rollups Extension 1.0-cpp

psql rollups_test -c "\\df rollups.*"
# Should list: version(), time_bucket(), test_create_aggregate(), etc.
```

---

## Complete Fresh Install (Both Steps)

```bash
# Assumes: macOS, Xcode CLI tools, Homebrew, rollups source in ~/dev/cpp/rollups

cd ~/dev/cpp/rollups

# Step 1: PostgreSQL (10-15 min)
./scripts/setup_macos.sh
source ~/.zshrc

# Step 2: Extension (2-3 min)
./scripts/setup_rollups_extension.sh

# Test
psql rollups_test -c "SELECT rollups.version();"
```

---

## What Gets Installed Where

### PostgreSQL (Step 1)
```
~/pgdev/
├── postgres/              # Installation directory
│   ├── bin/              # pg_config, psql, postgres, etc.
│   ├── lib/              # PostgreSQL libraries
│   ├── include/          # PostgreSQL headers
│   └── share/extension/  # Extension files (.control, .sql)
├── data/                 # Database cluster
├── logfile               # PostgreSQL logs
└── postgresql-source/    # Source code (for debugging)
```

### Rollups Extension (Step 2)
```
~/dev/cpp/rollups/
├── build/                # CMake build artifacts
│   ├── rollups.so       # Compiled extension
│   └── compile_commands.json  # For IDEs
├── include/rollups/      # C++ headers
├── src/                  # C++ source files
├── sql/                  # SQL installation scripts
├── scripts/              # Helper scripts
└── .vscode/              # VS Code config (if created)

~/.lldb/                  # LLDB formatters
└── lldb_postgres_formatters.py

~/.lldbinit               # Auto-loads formatters

~/pgdev/postgres/
├── lib/rollups.so       # Installed extension (copied from build/)
└── share/extension/
    ├── rollups.control
    └── rollups--1.0.sql
```

---

## Troubleshooting

### PostgreSQL won't start
```bash
# Check logs
tail -f ~/pgdev/logfile

# Common: port 5432 in use
lsof -i :5432
# Kill the process or change port in postgresql.conf
```

### Extension won't compile
```bash
# Verify pg_config is in PATH
which pg_config
# Should show: ~/pgdev/postgres/bin/pg_config

# Verify PostgreSQL headers
pg_config --includedir-server
ls $(pg_config --includedir-server)/postgres.h
# Should exist

# Clean rebuild
cd ~/dev/cpp/rollups
rm -rf build
./scripts/setup_rollups_extension.sh
```

### Extension won't load
```bash
# Check if library exists
ls ~/pgdev/postgres/lib/rollups.so

# Check PostgreSQL logs
tail -f ~/pgdev/logfile

# Try manual load
psql rollups_test
\dx rollups  -- List extension
DROP EXTENSION IF EXISTS rollups CASCADE;
CREATE EXTENSION rollups;
```

### CMake version too old
```bash
# Check version
cmake --version

# Must be 3.20+, upgrade via Homebrew
brew upgrade cmake
```

---

## Minimal Setup (Without Scripts)

If you can't use the scripts, here's the manual equivalent:

### PostgreSQL (Manual)
```bash
# Install dependencies
brew install readline zlib openssl@3 icu4c pkg-config

# Download and build
cd ~/pgdev
curl -O https://ftp.postgresql.org/pub/source/v16.6/postgresql-16.6.tar.gz
tar xzf postgresql-16.6.tar.gz
cd postgresql-16.6

./configure --prefix=$HOME/pgdev/postgres --enable-debug --enable-cassert \
  CFLAGS="-ggdb -O0 -g3" --with-openssl --with-readline --with-icu

make -j$(sysctl -n hw.ncpu)
make install
cd contrib && make -j$(sysctl -n hw.ncpu) && make install

# Initialize and start
export PATH="$HOME/pgdev/postgres/bin:$PATH"
initdb -D ~/pgdev/data
pg_ctl -D ~/pgdev/data -l ~/pgdev/logfile start
createdb rollups_test
```

### Rollups Extension (Manual)
```bash
brew install cmake

cd ~/dev/cpp/rollups
mkdir build && cd build
cmake ..
make
make install

# Test
psql rollups_test -c "CREATE EXTENSION rollups;"
```

---

## Architecture Differences: Apple Silicon vs Intel

The scripts handle both automatically, but if configuring manually:

**Apple Silicon (M1/M2/M3):**
- Homebrew prefix: `/opt/homebrew`
- Architecture: `arm64`
- IntelliSense mode: `macos-clang-arm64`

**Intel:**
- Homebrew prefix: `/usr/local`
- Architecture: `x86_64`
- IntelliSense mode: `macos-clang-x64`

Check yours: `uname -m`

---

## VS Code Extensions (Optional)

After running `setup_rollups_extension.sh`:

1. **C/C++ Extension Pack** (Microsoft)
   - IntelliSense, debugging, code navigation

2. **CodeLLDB** (Vadim Chugunov)
   - LLDB debugging integration

Install:
```
code --install-extension ms-vscode.cpptools-extension-pack
code --install-extension vadimcn.vscode-lldb
```

---

## Testing the Setup

After both scripts complete:

```bash
# Run automated tests
cd ~/dev/cpp/rollups
./scripts/test_catalog.sh

# Should show:
# ✓ All tests completed!
# 8/9 tests passed
```

---

## Next Steps After Setup

1. **Study the code:**
   ```bash
   cd ~/dev/cpp/rollups
   code .  # Open in VS Code
   ```

2. **Read documentation:**
   - `docs/BUILDING.md` - Build system details
   - `docs/DEBUGGING.md` - Debugging with LLDB
   - `docs/CLASS_DESIGN.md` - Architecture overview
   - `CLAUDE.md` - Project status and session history

3. **Try debugging:**
   ```bash
   # Find backend PID
   psql rollups_test -c "SELECT pg_backend_pid();"
   
   # Attach LLDB
   lldb -p <pid>
   (lldb) breakpoint set --name rollups_version
   
   # In psql, trigger breakpoint
   SELECT rollups.version();
   ```

4. **Make changes:**
   ```bash
   # Edit code
   vim src/catalog_manager.cpp
   
   # Rebuild and test
   make && make install
   psql rollups_test -c "DROP EXTENSION IF EXISTS rollups CASCADE; CREATE EXTENSION rollups;"
   ```

---

## Keeping in Sync Across Machines

**What to version control (git):**
- All source code (`src/`, `include/`, `sql/`)
- Build configuration (`CMakeLists.txt`, `Makefile`, `rollups.control`)
- Documentation (`docs/`, `CLAUDE.md`)
- Scripts (`scripts/*.sh`, `scripts/*.py`)

**What NOT to commit:**
- `build/` directory (CMake artifacts)
- `.vscode/` if machine-specific paths
- `~/.lldbinit` (personal config)

**Syncing workflow:**
```bash
# Machine A (after making changes)
git add -A
git commit -m "Implement MaterializationEngine"
git push

# Machine B (to get changes)
git pull
make && make install
./scripts/test_catalog.sh
```

---

## Uninstalling

**Remove PostgreSQL:**
```bash
pg_ctl stop -D ~/pgdev/data
rm -rf ~/pgdev
# Remove from .zshrc: PATH and PGDATA exports
```

**Remove Extension Only:**
```bash
cd ~/dev/cpp/rollups
rm -rf build
rm ~/pgdev/postgres/lib/rollups.so
rm ~/pgdev/postgres/share/extension/rollups*
rm -rf .vscode  # if you want
```

**Remove LLDB formatters:**
```bash
rm ~/.lldb/lldb_postgres_formatters.py
# Edit ~/.lldbinit and remove the import line
```

---

**Last Updated:** 2026-05-15
**Tested On:** macOS 14+ (Sonoma), Apple Silicon & Intel
