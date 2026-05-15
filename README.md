# PostgreSQL Continuous Rollup Aggregations

A PostgreSQL extension for continuous aggregation and rollups, built for learning PostgreSQL internals.

**Platform**: macOS (Apple Silicon & Intel)

## Project Goal

Learn PostgreSQL internals by implementing continuous rollup aggregations similar to TimescaleDB and ClickHouse, including:
- Time-based bucketing
- Incremental updates
- Materialization policies
- Multi-level hierarchical rollups

## Status

🚧 **Phase 1: Foundation & Learning** - Environment setup ready

See `claude.md` for detailed implementation plan, design decisions, and progress tracking.

## Quick Start (macOS)

### Prerequisites
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install Homebrew (if not already installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### Automated Setup
```bash
# Check your current environment
./scripts/check_environment.sh

# Run automated setup (builds PostgreSQL from source with debug symbols)
./scripts/setup_macos.sh

# Reload shell
source ~/.zshrc

# Verify installation
pg_config --version
psql rollups_test
```

See `DEVELOPMENT.md` for detailed setup instructions and manual installation steps.

## Documentation

- `claude.md` - Complete implementation plan, design decisions, session notes, and macOS setup guide
- `DEVELOPMENT.md` - Detailed macOS development environment setup
- `NEXT_SESSION.md` - Quick start guide for next session
- `scripts/README.md` - Setup scripts documentation
- `docs/` - Additional documentation (coming soon)

## Platform Requirements

- macOS 12.0 (Monterey) or later
- Apple Silicon (M1/M2/M3/M4) or Intel Mac
- Xcode Command Line Tools
- Homebrew package manager

## License

TBD
# pg_rollups
