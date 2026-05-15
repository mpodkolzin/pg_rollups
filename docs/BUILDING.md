# Building and Testing the Rollups Extension (C++ with CMake)

This guide walks through building, installing, and testing the rollups extension on macOS using C++ and CMake.

## Prerequisites

- PostgreSQL 16.6 installed with development headers (completed in Session 2)
- `pg_config` in your PATH
- Xcode Command Line Tools (provides clang++, make, cmake)
- CMake 3.20 or later

Verify your setup:
```bash
pg_config --version
pg_config --configure | grep debug   # Should show --enable-debug
clang++ --version                    # Should show Apple clang
cmake --version                      # Should show 3.20+
```

## Why C++ and CMake?

### C++ Advantages
- **Better abstractions**: Classes, namespaces, RAII for complex rollup logic
- **Type safety**: Compile-time checking and modern C++ features
- **STL containers**: Vector, map, set for efficient data structures
- **Easier complex logic**: Future rollup algorithms will benefit from C++ features

### CMake Advantages
- **IDE integration**: Better support in CLion, VS Code, etc.
- **Cross-platform**: Easier to extend to Linux/Windows later
- **Explicit control**: Understand exactly what PGXS does automatically
- **Flexible**: Easy to add external libraries and custom build steps
- **compile_commands.json**: Automatic generation for clangd/IDE tooling

## Project Structure

```
rollups/
├── CMakeLists.txt           # CMake build configuration
├── Makefile                 # Convenience wrapper around CMake
├── rollups.control          # Extension metadata
├── sql/
│   └── rollups--1.0.sql    # SQL installation script
└── src/
    └── rollups.cpp         # C++ implementation (with extern "C")
```

## Building the Extension

### Quick Start (using Makefile wrapper)

The simplest way to build:

```bash
make            # Configure CMake and build
make install    # Install to PostgreSQL
make test       # Build, install, and run tests
```

### Detailed Build Steps

#### Step 1: Configure CMake

```bash
# Create build directory and run CMake
mkdir build
cd build
cmake ..
```

Or use the Makefile wrapper:
```bash
make configure
```

**What CMake does:**
1. Finds `pg_config` in your PATH
2. Extracts PostgreSQL configuration (include paths, lib paths)
3. Verifies PostgreSQL headers exist
4. Configures compiler flags for debug build
5. Generates build files in `build/` directory
6. Creates `compile_commands.json` for IDE support

**Expected output:**
```
-- Found pg_config: /Users/you/pgdev/postgres/bin/pg_config
-- PostgreSQL version: PostgreSQL 16.6
-- PostgreSQL include (server): /Users/you/pgdev/postgres/include/server
-- Build Configuration:
  Build type: Debug
  C++ standard: C++17
```

#### Step 2: Compile

```bash
# From project root
make

# Or from build directory
cd build
make
```

This compiles `src/rollups.cpp` → `rollups.so`

**Expected output:**
```
[ 50%] Building CXX object CMakeFiles/rollups.dir/src/rollups.cpp.o
[100%] Linking CXX shared module rollups.so
```

**Troubleshooting:**
- **Error: `postgres.h not found`**: Run `pg_config --includedir-server` and verify directory exists
- **Error: `pg_config not found`**: Add to PATH: `export PATH="$HOME/pgdev/postgres/bin:$PATH"`
- **CMake version too old**: Install newer cmake: `brew install cmake`

#### Step 3: Install

```bash
make install
```

This installs files to your PostgreSQL installation:
- `rollups.so` → `$(pg_config --pkglibdir)/`
- `rollups.control` → `$(pg_config --sharedir)/extension/`
- `rollups--1.0.sql` → `$(pg_config --sharedir)/extension/`

Verify installation:
```bash
make show-pg-config
ls $(pg_config --pkglibdir)/rollups.so
ls $(pg_config --sharedir)/extension/rollups*
```

**Note**: If PostgreSQL is installed to a system location, you may need `sudo make install`.

#### Step 4: Create Extension in Database

```bash
# Using Makefile target (recommended)
make create-extension

# Or manually
psql rollups_test -c "DROP EXTENSION IF EXISTS rollups CASCADE; CREATE EXTENSION rollups;"
```

This executes `sql/rollups--1.0.sql`, which:
1. Creates the `rollups` schema
2. Defines C++ functions (`rollups.version()`, `rollups.time_bucket()`)
3. Creates metadata tables (`rollups.continuous_aggregates`)
4. Creates helper views (`rollups.rollup_info`)

## Testing the Extension

### Automated Tests

```bash
make test
```

This runs:
1. `make install` - Ensures extension is installed
2. `make create-extension` - Drops and recreates extension
3. `make test-extension` - Runs basic functionality tests

**Expected output:**
```
 version
-----------
 1.0.0-cpp

   time_bucket
------------------
 2024-02-11 10:00:00
```

### Manual Testing

#### 1. Test version function
```sql
psql rollups_test
SELECT rollups.version();
```
**Expected:** `1.0.0-cpp`

#### 2. Test time bucketing
```sql
-- Bucket a timestamp to 1-hour intervals
SELECT rollups.time_bucket('1 hour', '2024-01-15 14:32:17'::timestamp);
```
**Expected:** `2024-01-15 14:00:00`

#### 3. More time bucket examples
```sql
-- 5-minute buckets
SELECT rollups.time_bucket('5 minutes', '2024-01-15 14:32:17'::timestamp);
-- Expected: 2024-01-15 14:30:00

-- Daily buckets
SELECT rollups.time_bucket('1 day', '2024-01-15 14:32:17'::timestamp);
-- Expected: 2024-01-15 00:00:00

-- Weekly buckets (7 days)
SELECT rollups.time_bucket('7 days', '2024-01-15 14:32:17'::timestamp);
-- Expected: 2024-01-14 00:00:00
```

#### 4. Test with real data
```sql
-- Create a test table with time-series data
CREATE TABLE events (
    id SERIAL PRIMARY KEY,
    event_type TEXT,
    value NUMERIC,
    created_at TIMESTAMP
);

-- Insert sample data (last 24 hours)
INSERT INTO events (event_type, value, created_at)
SELECT
    CASE (random() * 3)::int
        WHEN 0 THEN 'click'
        WHEN 1 THEN 'view'
        ELSE 'purchase'
    END,
    random() * 100,
    now() - (random() * interval '24 hours')
FROM generate_series(1, 1000);

-- Aggregate by hour using time_bucket
SELECT
    rollups.time_bucket('1 hour', created_at) AS hour,
    event_type,
    count(*) AS event_count,
    avg(value)::numeric(10,2) AS avg_value
FROM events
GROUP BY 1, 2
ORDER BY 1 DESC, 2
LIMIT 10;
```

#### 5. View metadata tables
```sql
-- Check if metadata tables exist
\dt rollups.*

-- View rollup info (empty for now - we haven't created any rollups yet)
SELECT * FROM rollups.rollup_info;
```

### Error Handling Tests

```sql
-- Test negative bucket width (should error)
SELECT rollups.time_bucket('-1 hour', now());

-- Test month-based buckets (not yet supported, should error)
SELECT rollups.time_bucket('1 month', now());

-- Test NULL handling (STRICT function - should return NULL)
SELECT rollups.time_bucket(NULL, now());
SELECT rollups.time_bucket('1 hour', NULL);
```

## Development Workflow

### Quick Rebuild Cycle

When making changes to C++ code:

```bash
# 1. Edit src/rollups.cpp
vim src/rollups.cpp

# 2. Rebuild and reinstall
make rebuild

# 3. Reload extension in psql
psql rollups_test -c "DROP EXTENSION rollups CASCADE; CREATE EXTENSION rollups;"

# 4. Test your changes
psql rollups_test -c "SELECT rollups.version();"
```

**Or use the one-command approach:**
```bash
make test   # Builds, installs, and reloads automatically
```

**Important**: You must `DROP EXTENSION` to reload the shared library. Just reconnecting to psql is not enough.

### CMake Build Options

```bash
# Debug build (default)
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Release build (optimized)
cmake -DCMAKE_BUILD_TYPE=Release ..

# Specify PostgreSQL location
cmake -DPG_CONFIG=/path/to/pg_config ..

# Verbose build
cmake --build . --verbose
```

### IDE Support

#### Generate compile_commands.json

For clangd/clang-tidy support:
```bash
make compile-commands
```

This creates a symlink to `build/compile_commands.json` in the project root.

#### VS Code CMake Integration

Install "CMake Tools" extension, then:
1. Open Command Palette (Cmd+Shift+P)
2. "CMake: Configure"
3. "CMake: Build"

#### CLion

CLion automatically detects `CMakeLists.txt`:
1. Open project folder
2. CLion will configure CMake automatically
3. Use built-in build/run configurations

### Viewing PostgreSQL Logs

If something goes wrong, check the PostgreSQL logs:
```bash
tail -f ~/pgdev/logfile
```

Look for:
- `ERROR` messages from your extension
- `WARNING` messages about compilation issues
- `DEBUG` messages (if you add ereport(DEBUG1, ...) to your code)

### Debugging with LLDB

To debug C++ code:

1. Find the backend process ID:
```sql
SELECT pg_backend_pid();
```

2. Attach LLDB (in another terminal):
```bash
lldb -p <pid>
```

3. Set breakpoints:
```
(lldb) breakpoint set --name rollups_time_bucket
(lldb) continue
```

4. Run your query in psql:
```sql
SELECT rollups.time_bucket('1 hour', now());
```

5. LLDB will break at the function. Debug commands:
```
(lldb) frame variable          # Show local variables
(lldb) print bucket_width_us   # Print a specific variable
(lldb) step                    # Step into
(lldb) next                    # Step over
(lldb) continue                # Continue execution
```

**Note**: C++ code with optimizations can be hard to debug. Use Debug build (`-O0`).

## What We've Learned

### Extension Structure (C++ with CMake)
- **CMakeLists.txt**: Build configuration and PostgreSQL integration
- **Makefile wrapper**: Convenience layer for familiar workflow
- **.control file**: Extension metadata (unchanged from C)
- **SQL scripts**: Define SQL-visible objects (unchanged from C)
- **C++ implementation**: Core functionality with `extern "C"` for PostgreSQL

### C++ Integration with PostgreSQL
- **extern "C" blocks**: Required for PostgreSQL-callable functions
- **No C++ exceptions**: Must use `ereport(ERROR, ...)` instead
- **Memory management**: Use `palloc`/`pfree`, not `new`/`delete`
- **Function signatures**: Must be C-compatible (Datum, PG_FUNCTION_ARGS)
- **Internal logic**: Can use C++ features (classes, STL, auto, etc.)

### CMake for PostgreSQL Extensions
- **MODULE library**: Use `add_library(... MODULE)`, not SHARED
- **No lib prefix**: Set `PREFIX ""` for `rollups.so`, not `librollups.so`
- **Bundle on macOS**: Use `-bundle` flag, not `-dynamiclib`
- **PostgreSQL paths**: Extract from `pg_config` at configure time
- **IDE support**: `compile_commands.json` for clangd/tooling

## Next Steps

Now that we have a working C++ extension with CMake, we can:

1. **Add C++ helper classes** (Session 4+)
   - Create `TimeBucket` class for encapsulation
   - Use STL containers for rollup metadata
   - Leverage C++17 features (optional, auto, etc.)

2. **Study existing extensions** (Session 3 continuation)
   - Examine `pg_stat_statements` for hooks and shared memory
   - Look at how TimescaleDB implements continuous aggregates

3. **Add real rollup functionality** (Phase 3)
   - Implement `CREATE CONTINUOUS AGGREGATE` syntax
   - Hook into INSERT/UPDATE/DELETE for automatic updates
   - Create materialization logic

4. **Add tests** (Phase 3)
   - Write pg_regress tests
   - Test edge cases (timezones, DST, leap seconds)
   - Performance benchmarks

5. **Learn hooks** (Phase 4)
   - Hook into planner to rewrite queries
   - Hook into utility commands to intercept DDL
   - Background worker for scheduled refreshes

## Troubleshooting Common Issues

### CMake Configuration Issues

**CMake not found:**
```bash
brew install cmake
```

**pg_config not found:**
```bash
export PATH="$HOME/pgdev/postgres/bin:$PATH"
echo 'export PATH="$HOME/pgdev/postgres/bin:$PATH"' >> ~/.zshrc
```

**PostgreSQL headers not found:**
```bash
# Verify headers exist
ls $(pg_config --includedir-server)/postgres.h

# If missing, rebuild PostgreSQL from source (Session 2)
```

### Build Issues

**Extension won't load:**
```
ERROR:  could not load library "rollups.so"
```
**Solution**: Run `make install` and check that `$(pg_config --pkglibdir)/rollups.so` exists.

**Function not found:**
```
ERROR:  function rollups.version() does not exist
```
**Solution**: Extension not created. Run `make create-extension`.

**Changes to C++ code not taking effect:**
**Solution**: Must reload extension: `DROP EXTENSION rollups CASCADE; CREATE EXTENSION rollups;`

### C++ Specific Issues

**Undefined symbols:**
```
Undefined symbols: "typeinfo for ..."
```
**Solution**: Make sure functions are in `extern "C"` blocks.

**Name mangling issues:**
```
ERROR:  could not find function "rollups_version" in file
```
**Solution**: Verify `extern "C"` around PG_FUNCTION_INFO_V1 declarations.

## Useful Commands

```bash
# Build commands
make                    # Configure and build
make install            # Install to PostgreSQL
make test               # Build, install, reload, test
make rebuild            # Clean rebuild
make clean              # Remove build artifacts
make distclean          # Remove build directory

# Development commands
make show-pg-config     # Show PostgreSQL configuration
make create-extension   # Reload extension in database
make compile-commands   # Generate IDE tooling support

# CMake directly (from build/)
cd build
cmake ..                # Configure
cmake --build .         # Build
cmake --build . --target install   # Install

# Check installation
ls $(pg_config --pkglibdir)/rollups.so
ls $(pg_config --sharedir)/extension/rollups*

# View extension in database
psql rollups_test -c "\dx+ rollups"
psql rollups_test -c "\df rollups.*"
psql rollups_test -c "\dt rollups.*"
```

## References

- [CMake Documentation](https://cmake.org/documentation/)
- [PostgreSQL Extension Documentation](https://www.postgresql.org/docs/current/extend-extensions.html)
- [C Language Functions](https://www.postgresql.org/docs/current/xfunc-c.html) (still relevant for C++)
- [Writing PostgreSQL Extensions in C++](https://www.postgresql.org/message-id/flat/20130517131414.GB26987%40dh.xeodev.com)
- [Source Code: backend/utils/fmgr](https://github.com/postgres/postgres/tree/master/src/backend/utils/fmgr)

---

**Session 3 Update: C++ with CMake** - You now have a modern C++ PostgreSQL extension with CMake build system! 🎉
