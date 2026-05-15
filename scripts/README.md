# Setup Scripts for macOS

This directory contains automated scripts for setting up PostgreSQL development environment on macOS.

## Prerequisites

Before running these scripts, ensure you have:

1. **Xcode Command Line Tools**:
   ```bash
   xcode-select --install
   ```

2. **Homebrew** (if not installed):
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

## Scripts

### 1. `check_environment.sh`

Diagnoses your current PostgreSQL setup and checks prerequisites.

**Usage**:
```bash
chmod +x check_environment.sh
./check_environment.sh
```

**What it checks**:
- PostgreSQL binaries (psql, pg_config)
- Development headers availability
- Build configuration (debug symbols)
- Development tools (clang, make, git)
- Homebrew installation
- Mac architecture (Apple Silicon vs Intel)

**When to use**: Run this first to see if you need to install PostgreSQL or if your existing installation is suitable for development.

---

### 2. `setup_macos.sh`

Automated setup that builds PostgreSQL from source with debug symbols.

**Usage**:
```bash
chmod +x setup_macos.sh
./setup_macos.sh
```

**What it does**:
1. Installs build dependencies via Homebrew
2. Detects your Mac architecture (Apple Silicon/Intel)
3. Downloads PostgreSQL 16.6 source
4. Configures with debug symbols and assertions
5. Builds PostgreSQL (uses all CPU cores)
6. Installs to `~/pgdev/postgres/`
7. Adds to PATH in your `.zshrc`
8. Initializes database cluster
9. Configures for development
10. Starts PostgreSQL
11. Creates test database `rollups_test`

**Duration**: 5-10 minutes depending on your Mac

**Installation locations**:
- **Source code**: `~/pgdev/postgresql-source/`
- **Installation**: `~/pgdev/postgres/`
- **Data**: `~/pgdev/data/`
- **Logs**: `~/pgdev/logfile`

---

## After Setup

Once setup completes, reload your shell:

```bash
source ~/.zshrc
```

Verify installation:

```bash
# Check version
pg_config --version

# Check debug build
pg_config --configure | grep debug

# Check architecture matches your Mac
uname -m                          # Your Mac architecture
file ~/pgdev/postgres/bin/postgres  # PostgreSQL binary architecture
# These should match (both arm64 or both x86_64)

# Connect to test database
psql rollups_test
```

---

## Troubleshooting

### Homebrew Issues

**If Homebrew is not found**:
```bash
# On Apple Silicon, add Homebrew to PATH
echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zshrc
source ~/.zshrc

# On Intel Mac
echo 'eval "$(/usr/local/bin/brew shellenv)"' >> ~/.zshrc
source ~/.zshrc
```

**Check Homebrew installation**:
```bash
brew --version
brew --prefix
# Apple Silicon: /opt/homebrew
# Intel: /usr/local
```

### Architecture Issues

**Check your Mac architecture**:
```bash
uname -m
# arm64 = Apple Silicon (M1/M2/M3)
# x86_64 = Intel Mac
```

**Verify PostgreSQL was built for correct architecture**:
```bash
file ~/pgdev/postgres/bin/postgres
```

### Build Failures

**If configure fails with missing libraries**:
```bash
# Reinstall dependencies
brew reinstall readline zlib openssl@3 icu4c pkg-config
```

**If build fails**:
```bash
# Clean and rebuild
cd ~/pgdev/postgresql-source
make clean
./configure [options from script]
make -j$(sysctl -n hw.ncpu)
```

### PostgreSQL Won't Start

**Check logs**:
```bash
tail -20 ~/pgdev/logfile
```

**Check if port is in use**:
```bash
lsof -i :5432
# If another PostgreSQL is running, stop it or change port
```

---

## Manual Setup

If you prefer manual control, see `../DEVELOPMENT.md` for step-by-step instructions.

---

## Next Steps

After successful setup:

1. Study extension examples in `~/pgdev/postgresql-source/contrib/`
2. See `../NEXT_SESSION.md` for what to do next
3. See `../claude.md` for the complete project roadmap

---

## Platform Support

✅ **Apple Silicon** (M1, M2, M3, M4) - Fully supported
✅ **Intel Mac** - Fully supported
✅ **macOS Monterey (12.0)** and later - Tested
⚠️ **Older macOS** - May work but not tested

---

## Getting Help

- Review `../DEVELOPMENT.md` for detailed setup guide
- Check `../claude.md` for project documentation
- See troubleshooting section above
- Check PostgreSQL logs: `tail -f ~/pgdev/logfile`
