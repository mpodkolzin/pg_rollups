# PostgreSQL Continuous Rollup Aggregations

## Project Vision

Build a PostgreSQL extension implementing continuous rollup aggregations similar to TimescaleDB and ClickHouse, with the primary goal of **learning PostgreSQL internals** through hands-on implementation.

### Learning Objectives
1. **PostgreSQL Internals**: Understand storage layer, MVCC, query execution, and planner
2. **Extension Development**: Master C API, hooks, and extension architecture
3. **Aggregation Algorithms**: Learn incremental computation, materialization strategies, and optimization

### Target Features
- Time-based bucketing (aggregate into hourly/daily/monthly buckets)
- Incremental updates (efficient updates without full recomputation)
- Materialization policies (real-time, scheduled, on-demand refresh)
- Multi-level rollups (hierarchical: minute→hour→day→month)

---

## Modes of Operation

This project operates in 3 distinct modes. **Always start in Design mode** unless I explicitly ask to implement something.

### 1. Design Mode (Default)
**When to use**: At the start of any new feature, phase, or significant change.

**What you do**:
- Discuss the problem and goals
- Present 2-3 viable approaches with tradeoffs
- Recommend one (don't hedge unless genuinely ambiguous)
- Ask clarifying questions (one at a time, not a list)
- Explore edge cases and constraints
- Reference what we've learned so far
- Document design decisions in this file
- **No code writing** until I say "let's implement" or switch to Code mode

**Example flow**:
```
User: "We need to handle incremental updates"
You: "Two main approaches: triggers or background workers. 
Triggers are simpler and transactional - good for learning MVCC 
and transaction handling. Background workers add complexity but 
needed later for scheduled policies anyway. I'd start with triggers 
- simpler foundation, we can layer background workers on top. 
Which direction feels right?"
[Discussion continues until design settles]
User: "Alright, let's go with triggers"
You: [Updates Design Decisions section, then waits]
User: "Now implement it" or "Switch to code mode"
You: [Switches to Code mode]
```

### 2. Code Mode
**When to use**: After design is settled, when I say "implement", "let's build it", "code mode", or explicitly request code.

**What you do**:
- Write the actual implementation
- Follow the agreed design (don't redesign while coding)
- Add inline comments for non-obvious decisions
- Explain C++ constructs as you use them (lambdas, move semantics, etc.)
- Explain PostgreSQL internals as you use them (memory contexts, hooks, etc.)
- Update progress tracking in this file
- **Stay focused on implementation**, don't switch to redesigning

**Pace & interaction (IMPORTANT - this is a learning project)**:
- **Pause after each file.** After creating or editing a file, stop and
  explain what you wrote in that file - the purpose of each part, why it's
  structured that way, and any C++/PostgreSQL concepts involved. Wait for me
  to acknowledge or ask questions before moving to the next file.
- **Never execute bash commands yourself.** Do not build, install, run psql,
  or run tests. Instead, print the exact command(s) for me to run and tell me
  what output to expect. I will run them and report back. I need to do these
  steps myself to learn them.
- This applies to build, install, test, git, and any other shell command.

**What you don't do**:
- Don't question the design mid-implementation (raise it, but keep coding)
- Don't refactor unrelated code
- Don't add features beyond the agreed scope
- Don't write code when design isn't clear (switch back to Design mode)
- Don't run shell commands - hand them to me instead (see Pace & interaction)
- Don't race ahead through multiple files without pausing (see Pace & interaction)

### 3. Explainer Mode (On-Demand)
**When to use**: When I ask "explain X", "how does X work", "I don't understand X", or request documentation.

**What you do**:
- Explain the specific concept in depth
- Use examples and diagrams where helpful
- Connect to what we've already learned
- Document in the appropriate docs/ file if it's reference material
- Assume I might have forgotten or never fully understood it

**Topics you might explain**:
- **PostgreSQL internals**: Memory contexts, MVCC, hooks, query pipeline, type system, WAL, buffer manager, storage layout
- **C++ concepts**: Modern features (auto, lambdas, move, templates), STL containers, RAII, const correctness, name mangling
- **Design decisions**: Why we chose approach X over Y, what tradeoffs we accepted, what we learned from implementation

**Example**:
```
User: "Explain how memory contexts work"
You: [Detailed explanation with examples]
You: "Should I add this to docs/MEMORY_CONTEXTS.md for reference?"
```

---

## Mode Switching Rules

- **Default mode**: Design
- **Switching to Code mode**: Requires explicit signal from me ("implement", "code mode", "let's build it")
- **Switching to Explainer mode**: I ask a question or request explanation ("explain X", "how does X work")
- **Switching back to Design mode**: When implementation is done, or when you hit a design question during coding

**Progress tracking**: Keep the Session Log in Progress Tracking section updated regardless of mode.

---

## Implementation Plan

### Phase 1: Foundation & Learning (CURRENT)
**Goal**: Set up development environment and understand the basics

#### Tasks
- [x] Set up PostgreSQL development environment (COMPLETED)
  - [x] Created environment check script
  - [x] Created automated setup script for macOS
  - [x] Documented setup process
  - [x] Run setup script and verify installation
  - [ ] Configure IDE for C development (optional - can do as needed)
- [x] Study extension basics (COMPLETED)
  - [x] Understand extension control files (.control)
  - [x] Learn SQL script structure (--1.0.sql, upgrade scripts)
  - [x] Created working example with detailed documentation
  - [ ] Study advanced examples (pg_stat_statements for hooks - next session)
- [ ] Research existing implementations (NEXT PRIORITY)
  - Analyze TimescaleDB continuous aggregates architecture
  - Study ClickHouse materialized views approach
  - Document key design patterns from both

#### Key Learning Resources
- PostgreSQL Extension Building: https://www.postgresql.org/docs/current/extend-extensions.html
- Backend Internals: https://www.postgresql.org/docs/current/internals.html
- TimescaleDB source: https://github.com/timescale/timescaledb
- ClickHouse materialized views: https://clickhouse.com/docs/en/guides/developer/cascading-materialized-views

---

### Phase 2: Basic Extension Structure (PARTIALLY COMPLETE)
**Goal**: Create minimal working extension that hooks into PostgreSQL

#### Tasks
- [x] Create extension scaffolding (COMPLETED in Session 3)
  - [x] Makefile using PGXS with helpful targets
  - [x] Control file with metadata
  - [x] Basic SQL functions (version, time_bucket)
  - [x] Build and test documentation
- [ ] Implement first hook integration (NEXT PRIORITY)
  - Study planner/executor hooks
  - Hook into query execution to observe behavior
  - Add logging/debugging infrastructure
- [x] Understand catalog tables (STARTED in Session 3)
  - [x] Design custom catalog tables for rollup metadata
  - [ ] Learn system catalog structure (pg_class, pg_attribute, etc.) - ongoing
  - [ ] Implement catalog management functions

#### Design Decisions to Document
- Which hooks to use (planner_hook, executor hooks, utility hooks)? → TBD next session
- How to store rollup configuration? → **Decided: Custom catalog table `rollups.continuous_aggregates`**
- Extension naming and versioning strategy → **Decided: `rollups` extension, schema-qualified objects in `rollups` schema**

---

### Phase 3: Core Rollup Engine
**Goal**: Implement time-based bucketing and basic aggregation

#### Tasks
- [x] Time bucket implementation (rollups.time_bucket, day-based granularities)
  - [x] Time bucketing algorithm (timestamp → bucket via integer division)
  - [ ] Full timezone handling (currently casts to ::timestamp)
  - [ ] Month/year granularities (variable-length, deferred)
- [x] Create rollup definition system
  - [x] Function-based API instead of CREATE CONTINUOUS AGGREGATE DDL
        (custom DDL keywords can't reach ProcessUtility_hook — see Session 10)
  - [x] Store metadata in catalog tables (CatalogManager)
- [x] Basic materialization (MaterializationEngine)
  - [x] Initial population of rollup tables (CREATE TABLE AS)
  - [x] Aggregation functions via user-supplied select_clause
  - [ ] Query rewriting to use materialized data (next: Phase 3 remainder)

#### PostgreSQL Internals to Master
- **Parser/Analyzer**: How to extend SQL grammar or use utility hooks
- **Storage Manager**: How to create and manage tables efficiently
- **Executor Nodes**: Understanding Agg nodes and custom scan providers
- **Query Rewriting**: Transforming queries to use materialized tables

---

### Phase 4: Incremental Updates
**Goal**: Efficiently update aggregates as new data arrives

#### Tasks
- [ ] Trigger-based approach (initial)
  - Create triggers on source tables
  - Implement delta computation
  - Apply deltas to rollup tables
- [ ] Study advanced approaches
  - Background worker processes
  - Logical decoding for change capture
  - Compare performance characteristics
- [ ] Handle UPDATE and DELETE
  - Design algorithms for non-additive operations
  - Handle edge cases (time bucket changes)

#### Key Challenges to Explore
- How to efficiently track what needs updating?
- How to handle high-frequency inserts without blocking?
- Transactional consistency between source and rollup tables

---

### Phase 5: Materialization Policies
**Goal**: Flexible refresh strategies

#### Tasks
- [ ] Policy definition system
  - Real-time (every transaction)
  - Scheduled (cron-like)
  - On-demand (manual refresh)
  - Threshold-based (after N rows or time period)
- [ ] Background worker integration
  - Use PostgreSQL bgworker framework
  - Implement job scheduling
  - Handle worker lifecycle and crashes
- [ ] Partial refresh optimization
  - Identify changed time buckets
  - Refresh only affected buckets
  - Handle cascading updates for multi-level rollups

---

### Phase 6: Multi-Level Rollups
**Goal**: Hierarchical aggregations for different granularities

#### Tasks
- [ ] Dependency tracking
  - Model parent-child relationships
  - Ensure correct refresh order
  - Detect and prevent cycles
- [ ] Cascading updates
  - Propagate changes from fine to coarse granularities
  - Optimize by aggregating from intermediate levels
- [ ] Query optimization
  - Automatically choose optimal granularity for queries
  - Cost-based selection of rollup level

---

### Phase 7: Advanced Features & Optimization
**Goal**: Production-ready features and performance tuning

#### Tasks
- [ ] Compression and retention
  - Compress old rollup data
  - Implement retention policies
  - Archive and drop old buckets
- [ ] Parallel processing
  - Parallelize initial materialization
  - Concurrent updates for different buckets
- [ ] Advanced aggregates
  - Approximate aggregates (HyperLogLog for COUNT DISTINCT)
  - User-defined aggregates
  - Window functions and percentiles
- [ ] Monitoring and observability
  - Expose metrics (refresh lag, size, query hits)
  - pg_stat_statements integration
  - Performance analysis tools

---

## Design Decisions & Rationale

### 1. Extension vs. Fork
**Decision**: Build as extension, not PostgreSQL fork
**Rationale**:
- Easier to develop, test, and deploy
- Portable across PostgreSQL versions (with care)
- Follows PostgreSQL community best practices
- Lower barrier for users to adopt

### 2. C++ Implementation
**Decision**: Use C++ (C++17) for extension implementation
**Rationale**:
- **Better abstractions**: Classes, namespaces for complex rollup algorithms
- **Type safety**: Compile-time checks, modern C++ features (auto, lambdas)
- **STL containers**: Vector, map, set for efficient data structures
- **Maintainability**: Better code organization for complex logic
- **PostgreSQL compatibility**: Use extern "C" for PostgreSQL-callable functions
- **Performance**: Comparable to C when compiled without exceptions/RTTI
**Trade-offs accepted**:
- Need extern "C" blocks for PostgreSQL functions
- Cannot use C++ exceptions (PostgreSQL uses setjmp/longjmp)
- Must use palloc/pfree instead of new/delete for PostgreSQL memory
- Slightly more complex build setup
**Decision date**: Session 3 (2026-02-11)

### 3. Storage Strategy
**Decision**: TBD - evaluate options
**Options to explore**:
- **Separate tables**: Simple, uses standard storage
- **Custom access method**: Full control, complex implementation
- **Columnar storage**: Better compression, requires custom AM
**Next Steps**: Research TimescaleDB's approach (they use hypertables + chunks)

### 4. Update Strategy
**Decision**: Start with triggers, evolve to background workers
**Rationale**:
- Triggers are simpler to implement and understand
- Good for learning transaction handling and MVCC
- Background workers needed for scheduled policies
- Logical replication might be overkill initially

### 5. Query Rewriting
**Decision**: TBD - hook vs. custom scan provider
**Options to explore**:
- **planner_hook**: Intercept and rewrite queries
- **Custom scan provider**: More structured, better for optimization
- **View system**: Leverage existing PostgreSQL views
**Next Steps**: Prototype both approaches and compare

### 6. Build System: CMake instead of PGXS
**Decision**: Use CMake as build system instead of PostgreSQL's PGXS
**Rationale**:
- **IDE integration**: Superior support in CLion, VS Code, etc.
- **Explicit control**: Understand what PGXS does automatically (learning goal)
- **Flexibility**: Easy to add external libraries and custom build steps
- **Modern tooling**: compile_commands.json for clangd/clang-tidy
- **Cross-platform**: Easier to extend to Linux/Windows later
- **Learning value**: Understand both approaches (PGXS and manual CMake)
**Implementation**:
- CMakeLists.txt: Main build configuration with PostgreSQL integration
- Makefile wrapper: Convenience layer for familiar workflow (make, make install, etc.)
- Build directory: All artifacts in build/ to keep source tree clean
**Trade-offs accepted**:
- More verbose than PGXS (but educational)
- Less "standard" for PostgreSQL community
- Need to manually manage pg_config queries
- More complex for simple extensions
**Why worth it for this project**:
- We'll add complex C++ logic that benefits from IDE support
- Learning to build extensions "from scratch" provides deeper understanding
- Better debugging experience with modern IDE integration
**Decision date**: Session 3 (2026-02-11)

### 7. Development Environment
**Decision**: Native installation with debug symbols, not containerized
**Rationale**:
- **Superior debugging**: Direct GDB/LLDB attachment to backend processes
- **IDE integration**: Native debugging tools work seamlessly (VS Code, CLion)
- **Source access**: Can step through PostgreSQL core code alongside extension
- **Learning focus**: Need to understand internals deeply, debugging is essential
- **Performance profiling**: Easier to use profilers (Instruments on macOS, perf on Linux)

**Build Configuration**:
```bash
./configure \
  --enable-debug \           # Full debug symbols
  --enable-cassert \         # Enable assertions for learning
  --enable-depend \          # Automatic dependency tracking
  CFLAGS="-ggdb -O0 -g3 -fno-omit-frame-pointer"  # Maximum debug info, no optimization
```

**Why these flags matter for learning**:
- `-ggdb`: Generates debug info optimized for GDB
- `-O0`: Disables optimization so code matches source exactly
- `-g3`: Maximum debug information (includes macro definitions)
- `-fno-omit-frame-pointer`: Better stack traces
- `--enable-cassert`: Assertions help catch bugs and understand invariants

**Trade-offs accepted**:
- Slower execution (irrelevant for learning)
- Larger binaries (disk space not a concern)
- Non-production configuration (this is intentional)

### 8. C++ Class Design: Hybrid Wrapper Approach
**Decision**: Use ServiceNow-style hybrid C++/C approach (Approach 1.5)
**Rationale**:
- **Data in C structs** (palloc'd, managed by PostgreSQL memory contexts)
- **Operations in C++ classes** (stack-allocated wrappers or stateless managers)
- **Proven in production** - ServiceNow's custom PostgreSQL (`/dev/snc/tembo/db`) uses this extensively
- **Safe for PostgreSQL** - Respects memory contexts, no exceptions, no new/delete
- **Better than pure C** - Namespaces, type safety, inline methods, cleaner organization
- **Learning value** - Understand PostgreSQL constraints while using modern C++

**Architecture**:
```cpp
// C struct for data (palloc'd)
typedef struct ContinuousAggregateData {
    Oid agg_id;
    NameData agg_name;
    // ... more fields
} ContinuousAggregateData;

// C++ wrapper class (stack-allocated)
class ContinuousAggregate {
    ContinuousAggregateData *data_;  // Points to palloc'd data
public:
    inline Oid get_id() const { return data_->agg_id; }
    void refresh();
};

// Stateless manager class (static methods only)
class CatalogManager {
public:
    static ContinuousAggregateData* load(const char *name);
    static void save(const ContinuousAggregateData *data);
private:
    CatalogManager() = delete;  // No instances
};
```

**Key classes**:
- `ContinuousAggregate` - Wrapper for aggregate data
- `CatalogManager` - CRUD operations on metadata catalog
- `MaterializationEngine` - Populate and refresh operations  
- `QueryParser` - Parse CREATE CONTINUOUS AGGREGATE syntax

**Constraints respected**:
- No RAII cleanup (destructors are no-ops)
- No C++ exceptions (use ereport)
- No new/delete (use palloc/pfree)
- No STL with default allocators (would need custom allocators)

**Reference**: See `docs/CLASS_DESIGN.md` for complete design documentation

**Decision date**: Session 7 (2026-05-15)  
**Validated by**: ServiceNow's production PostgreSQL code in `/dev/snc/tembo/db/`

### 9. Continuous Aggregate Creation API: SQL Functions, not Custom DDL
**Decision**: Manage aggregates with SQL functions, not a `CREATE CONTINUOUS AGGREGATE` statement
**Rationale**:
- **Custom DDL keywords cannot be intercepted.** `ProcessUtility_hook` runs
  *after* the parser. PostgreSQL's grammar is a fixed Bison grammar, so a
  brand-new keyword sequence (`CREATE CONTINUOUS AGGREGATE ...`) is a syntax
  error at raw-parse time and never reaches the hook. Real new keywords would
  require forking PostgreSQL's grammar — rejected by Design Decision #1.
- **Function API is the simplest correct option** — arguments arrive pre-split,
  so no DDL string parsing (and no QueryParser class) is needed.
**API**:
- `rollups.create_continuous_aggregate(name, source_table, time_column, bucket_width, select_clause)`
- `rollups.refresh_continuous_aggregate(name)`
- `rollups.drop_continuous_aggregate(name)`
**Future option (not chosen now)**: Piggyback on `CREATE MATERIALIZED VIEW ...
WITH (rollups.continuous, ...)` — valid grammar, so the hook *does* fire. This
is TimescaleDB's actual approach and could be layered on later.
**Trade-offs accepted**:
- Not "real" DDL; the existing `ProcessUtility_hook` is not used for creation
  (kept for observability and future matview-DDL interception).
**Decision date**: Session 10 (2026-05-20)

---

## Environment Setup Guide (macOS)

### Setup Scripts

#### 1. Environment Check (`scripts/check_environment.sh`)
**Purpose**: Diagnose current PostgreSQL installation on macOS and determine setup needs

**What it checks**:
- PostgreSQL binaries (psql, pg_config)
- Development headers (postgres.h in includedir-server)
- Build configuration (debug symbols, assertions)
- Development tools (clang, make, git - from Xcode Command Line Tools)
- Homebrew package manager
- Apple Silicon vs Intel architecture

**Usage**:
```bash
chmod +x scripts/check_environment.sh
./scripts/check_environment.sh
```

**Output**: Reports what's installed, what's missing, and whether existing installation is suitable for extension development

#### 2. Automated Setup (`scripts/setup_macos.sh`)
**Purpose**: Build PostgreSQL from source on macOS with optimal configuration for learning

**macOS Requirements**:
- Xcode Command Line Tools (`xcode-select --install`)
- Homebrew package manager
- Works on both Apple Silicon (M1/M2/M3) and Intel Macs

**What it does**:
1. Installs build dependencies via Homebrew (readline, zlib, openssl@3, icu4c, pkg-config)
2. Configures environment for Homebrew libraries (`/opt/homebrew` or `/usr/local`)
3. Downloads PostgreSQL 16.6 source
4. Configures with debug symbols and assertions optimized for LLDB (macOS debugger)
5. Builds PostgreSQL using all CPU cores (`sysctl -n hw.ncpu`, ~5-10 minutes)
6. Installs to `~/pgdev/postgres` (separate from any system PostgreSQL)
7. Builds contrib modules (important learning examples)
8. Adds to PATH in `.zshrc` (default shell on modern macOS)
9. Initializes database cluster in `~/pgdev/data`
10. Configures for development (verbose logging, pg_stat_statements)
11. Starts PostgreSQL and creates test database

**Installation locations**:
- **Source**: `~/pgdev/postgresql-source/` (for reading/debugging)
- **Install**: `~/pgdev/postgres/` (binaries, libraries)
- **Data**: `~/pgdev/data/` (database cluster)
- **Logs**: `~/pgdev/logfile`

**Key configuration**:
```
log_min_messages = DEBUG1        # Verbose logging
log_statement = 'all'            # Log all queries
shared_preload_libraries = 'pg_stat_statements'  # Example extension
```

**Usage**:
```bash
chmod +x scripts/setup_macos.sh
./scripts/setup_macos.sh
```

**Post-install commands**:
```bash
# Reload shell to get updated PATH
source ~/.zshrc   # or ~/.bashrc

# Verify installation
pg_config --version
pg_config --configure | grep debug

# Connect to test database
psql rollups_test

# View logs
tail -f ~/pgdev/logfile

# Stop/start PostgreSQL
pg_ctl stop
pg_ctl start
```

### Manual Setup Alternative

If you prefer manual control or need to customize:

1. **Download source**:
   ```bash
   curl -O https://ftp.postgresql.org/pub/source/v16.6/postgresql-16.6.tar.gz
   tar xzf postgresql-16.6.tar.gz
   cd postgresql-16.6
   ```

2. **Configure**:
   ```bash
   ./configure \
     --prefix=$HOME/pgdev/postgres \
     --enable-debug \
     --enable-cassert \
     --enable-depend \
     --with-openssl \
     --with-readline \
     CFLAGS="-ggdb -O0 -g3"
   ```

3. **Build and install**:
   ```bash
   make -j$(sysctl -n hw.ncpu)
   make install
   cd contrib
   make -j$(sysctl -n hw.ncpu)
   make install
   ```

4. **Initialize and start**:
   ```bash
   $HOME/pgdev/postgres/bin/initdb -D $HOME/pgdev/data
   $HOME/pgdev/postgres/bin/pg_ctl -D $HOME/pgdev/data -l logfile start
   ```

### Verifying Extension Development Setup

After installation, test that you can build extensions:

```bash
# Navigate to an example extension
cd ~/pgdev/postgresql-source/contrib/pg_stat_statements

# Build using PGXS
make

# Install
make install

# Test in database
psql rollups_test -c "CREATE EXTENSION pg_stat_statements;"
psql rollups_test -c "\dx"
```

Expected output should show pg_stat_statements installed successfully.

### IDE Configuration Examples (macOS)

#### VS Code (`.vscode/c_cpp_properties.json`)
```json
{
  "configurations": [{
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
  }]
}
```

**Note**: Change `arm64` to `x64` if you're on Intel Mac, and use `/usr/local` instead of `/opt/homebrew`.

#### VS Code Debug Configuration (`.vscode/launch.json`)
```json
{
  "version": "0.2.0",
  "configurations": [{
    "name": "Attach to Postgres Backend",
    "type": "lldb",
    "request": "attach",
    "program": "${env:HOME}/pgdev/postgres/bin/postgres",
    "pid": "${command:pickProcess}"
  }]
}
```

#### LLDB Configuration for Debugging (macOS Native Debugger)
```bash
# Find backend PID
psql rollups_test -c "SELECT pg_backend_pid();"

# Attach LLDB (not GDB on macOS)
lldb -p <pid>

# Set breakpoints
(lldb) breakpoint set --name my_extension_function
(lldb) breakpoint set --file my_extension.c --line 42
(lldb) continue

# Debug commands
(lldb) print variable_name
(lldb) frame variable
(lldb) step
(lldb) next
```

### Troubleshooting Common Issues (macOS)

**Issue**: `pg_config not found`
- **Solution**: Add to PATH: `echo 'export PATH="$HOME/pgdev/postgres/bin:$PATH"' >> ~/.zshrc && source ~/.zshrc`

**Issue**: `postgres.h not found` when building extension
- **Solution**: Check `pg_config --includedir-server` points to valid directory
- **macOS**: Ensure Xcode Command Line Tools are installed: `xcode-select --install`

**Issue**: Extension compiles but won't load
- **Solution**: Check PostgreSQL logs, ensure library installed to correct location
- **macOS**: Verify architecture matches: `file ~/pgdev/postgres/bin/postgres` and `file <your_extension>.so`

**Issue**: Can't connect to PostgreSQL
- **Solution**: Check if running with `pg_ctl status`, view logs in `~/pgdev/logfile`
- **macOS**: Check if port 5432 is available: `lsof -i :5432`

**Issue**: Homebrew library not found during build
- **Solution**: Set environment variables:
  ```bash
  export LDFLAGS="-L$(brew --prefix)/opt/readline/lib"
  export CPPFLAGS="-I$(brew --prefix)/opt/readline/include"
  ```

**Issue**: Apple Silicon vs Intel confusion
- **Solution**: Check your architecture with `uname -m`
  - `arm64` = Apple Silicon (use `/opt/homebrew`)
  - `x86_64` = Intel (use `/usr/local`)

---

## Progress Tracking

### Session Log

Detailed session notes live in `docs/sessions/`:

- [Session 1: 2026-02-09 - Project Initialization](docs/sessions/session-01.md)
- [Session 2: 2026-02-09 - Environment Setup Planning (macOS)](docs/sessions/session-02.md)
- [Session 3: 2026-02-11 - Extension Basics with C++ and CMake](docs/sessions/session-03.md)
- [Session 4: 2026-02-12 - Extension Build, Install, and Testing](docs/sessions/session-04.md)
- [Session 5: 2026-03-13 - Debugging Setup](docs/sessions/session-05.md)
- [Session 6: 2026-03-13 - PostgreSQL Hooks Study (In Progress)](docs/sessions/session-06.md)
- [Session 7: 2026-05-15 - C++ Class Design and Documentation](docs/sessions/session-07.md)
- [Session 8: 2026-05-15 - C++ Class Implementation (Phase 3 Begin)](docs/sessions/session-08.md)
- [Session 9: 2026-05-15 - CatalogManager Testing and Verification](docs/sessions/session-09.md)
- [Session 10: 2026-05-20 - Function API and MaterializationEngine (Phase 3)](docs/sessions/session-10.md)
- [Session 11: 2026-05-29 - Query Rewriting Stage 1 (planner_hook)](docs/sessions/session-11.md)

---

## Key Concepts to Master

### PostgreSQL Internals

#### Storage Layer
- **Heap Pages**: Physical storage format, TOAST
- **Buffer Manager**: Page caching, pinning, dirty pages
- **WAL (Write-Ahead Log)**: Crash recovery, replication
- **MVCC**: Transaction visibility, snapshot isolation, vacuum

#### Query Processing
- **Parser**: SQL text → parse tree
- **Analyzer/Rewriter**: Parse tree → query tree, view expansion
- **Planner/Optimizer**: Query tree → plan tree, cost estimation
- **Executor**: Plan tree → tuples, various node types

#### Extension Points
- **Hooks**: Function pointers for intercepting operations
- **Custom Scan Providers**: Alternative execution strategies
- **Background Workers**: Long-running background processes
- **Event Triggers**: Respond to DDL commands

### Aggregation Algorithms

#### Incremental Computation
- **Algebraic aggregates**: Can be computed from partial results (SUM, COUNT)
- **Holistic aggregates**: Require full dataset (MEDIAN, MODE)
- **Delta processing**: Computing (old_agg + new_data) vs. full recomputation

#### Materialized View Maintenance
- **Self-maintainable**: Can be updated from deltas alone
- **Non-self-maintainable**: May require querying base tables
- **Immediate vs. Deferred**: Update in same transaction vs. async

#### Time-Series Specific
- **Time bucketing**: Mapping timestamps to fixed intervals
- **Rollup hierarchies**: Aggregating aggregates (minute→hour→day)
- **Late-arriving data**: Handling out-of-order inserts

---

## Reference Implementations

### TimescaleDB Continuous Aggregates
- **Architecture**: Hypertables + materialization workers + refresh policies
- **Key Features**: Automatic query rewriting, real-time aggregation layer
- **Source**: `src/continuous_agg.c`, `tsl/src/continuous_aggs/`

### ClickHouse Materialized Views
- **Architecture**: Triggers on INSERT, separate storage engine for MV
- **Key Features**: Real-time updates, chain of MVs, target table flexibility
- **Docs**: https://clickhouse.com/docs/en/sql-reference/statements/create/view#materialized-view

### PostgreSQL Built-in Features
- **Materialized Views**: `CREATE MATERIALIZED VIEW`, manual refresh
- **Incremental View Maintenance**: Proposed feature (not yet in core)
- **pg_stat_statements**: Good example of extension hooks and shared memory

---

## Development Guidelines

### Code Organization
```
rollups/
├── src/               # C++ source files (*.cpp)
├── sql/               # SQL script files (installation, upgrades)
├── test/              # Regression tests
├── docs/              # Documentation
├── CMakeLists.txt     # CMake build configuration
├── Makefile           # Convenience wrapper around CMake
├── rollups.control    # Extension metadata
└── build/             # CMake build directory (git-ignored)
    ├── rollups.so     # Compiled extension
    └── compile_commands.json  # For IDE tooling
```

### Testing Strategy
- Unit tests for individual functions
- Regression tests using pg_regress
- Performance benchmarks for comparison
- Fuzzing for edge cases (time zones, large datasets)

### Documentation As We Go

**When to Create Design Documents** (`docs/*.md`):
- **Before implementing** any new major feature or component
- When making significant architectural decisions
- When designing class hierarchies or module interfaces
- Examples: `docs/CLASS_DESIGN.md`, `docs/HOOKS_GUIDE.md`, `docs/DEBUGGING.md`

**What to Include in Design Docs**:
- Overview and purpose
- Design alternatives considered and why chosen approach was selected
- Class/component diagrams
- API specifications with examples
- Memory management strategy
- Error handling approach
- Testing strategy
- Future enhancements section

**Code Documentation**:
- Comment all hook implementations with "why", not just "what"
- Explain non-obvious C++ idioms inline
- Note PostgreSQL constraints (no exceptions, use palloc, extern "C")
- Document "why" for architectural decisions
- Include learning notes at end of significant code sections

**Project Documentation** (this file):
- Document design tradeoffs in "Design Decisions & Rationale" section
- Keep Session Log up to date with each session's work
- Update progress tracking as you complete tasks
- Write examples for each feature as implemented

### Communication & Explanation Guidelines

**This is a LEARNING project** - always prioritize explanation over implementation speed.

#### Explain Before Coding
- **Concept first, code second**: Discuss what we're about to do and why before writing code
- **Alternatives and trade-offs**: Present different approaches and discuss pros/cons
- **Connect to fundamentals**: Relate new concepts to things we've already learned

#### What to Explain

**PostgreSQL-Specific:**
- Extension mechanisms (hooks, background workers, custom scan nodes)
- Memory management (palloc/pfree, memory contexts, MVCC)
- Type system (Datum, pass-by-value vs. pass-by-reference)
- Build system specifics (MODULE vs SHARED, macOS bundles, symbol resolution)
- Header organization and include dependencies

**C++ Constructs (user has some experience but it's been a while):**
- **Modern C++17 features**: auto, lambdas, structured bindings, std::optional, std::variant
- **STL containers**: vector, map, set, unordered_map - when to use which
- **Smart pointers**: unique_ptr, shared_ptr, weak_ptr (if we use them)
- **Move semantics**: std::move, rvalue references (&&)
- **Templates**: Function templates, class templates, template specialization
- **RAII patterns**: Resource acquisition is initialization
- **Iterators and algorithms**: std::find_if, std::transform, range-based for
- **Type traits**: std::is_same, enable_if, SFINAE
- **Namespace usage**: When and why to use namespaces
- **Const correctness**: const references, const methods, constexpr

**Examples of Good Explanations:**
```cpp
// DON'T just write:
auto result = std::find_if(vec.begin(), vec.end(), [](const auto& x) { return x > 5; });

// DO explain:
// We're using std::find_if with a lambda to search the vector
// - auto: Compiler deduces the type (std::vector<int>::iterator here)
// - Lambda [](const auto& x) { ... }: Anonymous function, captures nothing ([])
// - const auto& x: Generic parameter, works with any container element type
// - Returns iterator to first element > 5, or vec.end() if not found
auto result = std::find_if(vec.begin(), vec.end(),
    [](const auto& x) { return x > 5; });
```

#### Interactive Learning
- Ask user to run commands and observe output
- Request feedback on what they see
- Check understanding before moving forward
- Encourage questions about anything unclear

#### Code Comments
- Explain non-obvious C++ idioms inline
- Note PostgreSQL constraints (no exceptions, use palloc, extern "C")
- Document "why" for architectural decisions
- Include learning notes at end of significant code sections

---

## Open Questions

1. **Concurrency**: How to handle concurrent inserts during refresh?
2. **Consistency**: Strong vs. eventual consistency for rollups?
3. **Schema changes**: How to handle ALTER TABLE on source tables?
4. **Performance**: What's acceptable refresh lag for different use cases?
5. **Compatibility**: How to maintain compatibility across PostgreSQL versions?

---

## Resources & Reading List

### Essential Reading
- [ ] PostgreSQL Server Programming, Chapter on C Extensions
- [ ] PostgreSQL Documentation: Part VII (Internals)
- [ ] TimescaleDB Blog: "Continuous Aggregates" architecture posts
- [ ] "Maintaining Views Incrementally" (Gupta et al., 1993)
- [ ] PostgreSQL source: `src/backend/executor/nodeAgg.c`

### Code to Study
- [ ] contrib/pg_stat_statements (hooks, shared memory)
- [ ] contrib/btree_gist (index AM basics)
- [ ] TimescaleDB continuous_aggs module
- [ ] PostgreSQL matview.c (materialized view implementation)

---

## Notes & Insights

### Why Learn by Building This?

**TimescaleDB approach**: Excellent case study because it:
- Uses many PostgreSQL extension mechanisms (hooks, bgworkers, custom scan)
- Solves real production problems (high-frequency time-series data)
- Demonstrates advanced concepts (query rewriting, distributed execution)

**ClickHouse approach**: Different philosophy to compare:
- Column-oriented storage vs. row-oriented
- Real-time triggers vs. background workers
- Simpler model but less flexibility

**By building this, we'll master**:
- Extension architecture and lifecycle
- Query processing pipeline (parser → executor)
- Storage and transaction systems
- Performance optimization techniques
- Production-ready C programming in PostgreSQL context

---

## Future Enhancements (Nice to Have)

- Distributed rollups across multiple nodes
- Integration with existing time-series extensions (pg_timeseries)
- Support for window functions in rollups
- Automatic rollup recommendation based on query patterns
- Web UI for management and monitoring
- Compatibility layer for TimescaleDB syntax

---

**Last Updated**: 2026-05-29 (Session 11 - Paused)
**Current Phase**: Phase 3 - Core Rollup Engine (query rewriting Stage 1: planner_hook)
**Status**: 🚧 Header defined, implementation file next - see docs/sessions/session-11.md for checklist
**Platform**: macOS (Apple Silicon & Intel)
**Language**: C++17 with extern "C" for PostgreSQL interface
**Build System**: CMake 3.20+ with Makefile wrapper
**Debugger**: LLDB (macOS native) with PostgreSQL type formatters + VS Code integration
**Key Files**: rollups.control, sql/rollups--1.0.sql, src/rollups.cpp, src/catalog_manager.cpp, src/continuous_aggregate.cpp, src/materialization_engine.cpp, include/rollups/*.hpp (including query_rewriter.hpp NEW), CMakeLists.txt, Makefile, docs/BUILDING.md, docs/DEBUGGING.md, docs/HOOKS_GUIDE.md, docs/CLASS_DESIGN.md, docs/QUERY_REWRITING.md (NEW), scripts/lldb_postgres_formatters.py, .lldbinit
**Next Session**: Implement src/query_rewriter.cpp (pattern matching, query tree surgery, hook registration) - start in Code Mode
