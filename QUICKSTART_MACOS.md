# Quick Start Guide - macOS

## 🚀 One-Time Setup (5-10 minutes)

```bash
# 1. Install prerequisites
xcode-select --install

# 2. Navigate to project
cd /Users/maxim.podkolzin/dev/cpp/rollups

# 3. Run automated setup
chmod +x scripts/setup_macos.sh
./scripts/setup_macos.sh

# 4. Reload your shell
source ~/.zshrc

# 5. Verify installation
pg_config --version
psql rollups_test -c "SELECT version();"
```

**Done!** PostgreSQL is now running with debug symbols.

---

## 📋 Daily Commands

```bash
# Start PostgreSQL
pg_ctl start

# Stop PostgreSQL
pg_ctl stop

# Check status
pg_ctl status

# Connect to test database
psql rollups_test

# View logs
tail -f ~/pgdev/logfile
```

---

## 🐛 Debugging with LLDB

```bash
# Get backend process ID
psql rollups_test -c "SELECT pg_backend_pid();"

# Attach LLDB
lldb -p <pid>

# Set breakpoint
(lldb) breakpoint set --name my_function
(lldb) continue

# Step through code
(lldb) step
(lldb) next
(lldb) print variable_name
```

---

## 🔨 Building Extensions

```bash
# Navigate to your extension source
cd ~/pgdev/postgresql-source/contrib/pg_stat_statements

# Build
make

# Install
make install

# Load in database
psql rollups_test -c "CREATE EXTENSION pg_stat_statements;"
```

---

## 📂 Important Paths

| What | Path |
|------|------|
| PostgreSQL Source | `~/pgdev/postgresql-source/` |
| PostgreSQL Install | `~/pgdev/postgres/` |
| Database Data | `~/pgdev/data/` |
| Logs | `~/pgdev/logfile` |
| Example Extensions | `~/pgdev/postgresql-source/contrib/` |
| Your Extension | `/Users/maxim.podkolzin/dev/cpp/rollups/` |

---

## 🆘 Quick Troubleshooting

### PostgreSQL won't start
```bash
tail -20 ~/pgdev/logfile
lsof -i :5432  # Check if port is in use
```

### Command not found
```bash
export PATH="$HOME/pgdev/postgres/bin:$PATH"
source ~/.zshrc
```

### Check your architecture
```bash
uname -m  # Your Mac
file ~/pgdev/postgres/bin/postgres  # PostgreSQL binary
# Should match: both arm64 OR both x86_64
```

---

## 📖 Documentation Quick Links

- **Next Session Steps**: `NEXT_SESSION.md`
- **Complete Setup Guide**: `DEVELOPMENT.md`
- **Project Plan**: `claude.md`
- **Scripts Help**: `scripts/README.md`

---

## 🎯 Quick Info

- **Platform**: macOS (Apple Silicon & Intel)
- **PostgreSQL Version**: 16.6
- **Debugger**: LLDB
- **Compiler**: Clang
- **Package Manager**: Homebrew
- **Default Shell**: zsh

---

**Need more details?** See `NEXT_SESSION.md` for step-by-step instructions.
