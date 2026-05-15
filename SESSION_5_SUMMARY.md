# Session 5 Summary - Debugging Setup

**Date**: 2026-03-13
**Status**: ✅ **SUCCESS** - Complete debugging environment configured!

---

## 🎯 Accomplishments

### ✅ LLDB Debugging Environment
- **Created comprehensive debugging guide** (`docs/DEBUGGING.md`)
  - GDB vs LLDB command comparison
  - Complete workflow for attaching to PostgreSQL backends
  - Breakpoint management and code stepping
  - Variable inspection techniques
  - Troubleshooting common issues
  - macOS-specific considerations (SIP, code signing)

### ✅ Hands-On Debugging Practice
- Successfully attached LLDB to PostgreSQL backend process
- Set breakpoints in extension functions (`rollups_version`, `rollups_time_bucket`)
- Stepped through C++ code and inspected variables
- Learned PostgreSQL-specific debugging patterns
- Understood extension loading lifecycle

### ✅ PostgreSQL Type Formatters
- **Created Python LLDB formatters** (`scripts/lldb_postgres_formatters.py`)
  - `text*` types display as readable strings (not raw pointers!)
  - `Timestamp` displays as formatted dates: `2024-01-15 14:00:00.000000 UTC`
  - `Interval` displays as human-readable: `1 hour` or `30 days`
  - `Datum` shows both hex and decimal representations
- **Automatic loading** via `.lldbinit` configuration
- **Much easier debugging** - no more writing complex expressions to see strings!

### ✅ IDE Integration
- Set up VS Code debugging with LLDB extension
- Can now set breakpoints directly in IDE
- Navigate code visually while debugging
- Inspect variables in VS Code's debug panel

---

## 📊 Key Learnings

### PostgreSQL Extension Debugging Workflow

**The fundamental workflow**:
```
1. Start PostgreSQL
2. Connect with psql → creates backend process
3. Get backend PID: SELECT pg_backend_pid();
4. Attach LLDB: lldb -p <PID>
5. Set breakpoints in extension code
6. Run SQL commands in psql to trigger breakpoints
7. Step through code, inspect variables
```

**Critical insight**: Each psql connection is a separate backend process - you debug ONE connection at a time.

### Extension Loading Lifecycle

**Discovery**: Extension `.so` files are loaded on-demand, not at PostgreSQL startup!

- First call to an extension function loads the `.so` into the backend process
- Setting breakpoints before loading shows "pending" warnings (this is normal)
- Two approaches:
  1. Set "pending" breakpoints, run SQL, they activate when library loads
  2. Load extension first (call a function), then set breakpoints

### C++ Name Mangling and PostgreSQL

**Function names in compiled code**:
- C++ mangles names: `rollups_version` → `_Z15rollups_version...`
- `extern "C"` prevents mangling: `rollups_version` → `_rollups_version`
- macOS adds `_` prefix automatically: final name is `_rollups_version`
- Always use `nm` to check actual symbol names: `nm build/rollups.so`

### LLDB vs GDB on macOS

**Why LLDB, not GDB**:
- GDB doesn't work well on modern macOS (SIP, code signing issues)
- LLDB is Apple's native debugger, seamless integration
- Commands are similar: `b`, `n`, `s`, `c`, `p`, `bt` work in both
- Main differences in longer commands (see DEBUGGING.md for mapping)

---

## 📂 Files Created/Updated

### New Files
- `docs/DEBUGGING.md` - Comprehensive LLDB debugging guide (600+ lines)
- `scripts/lldb_postgres_formatters.py` - Python formatters for PostgreSQL types
- `.lldbinit` - Auto-load configuration for LLDB
- `SESSION_5_SUMMARY.md` - This file

### Updated Files
- `CLAUDE.md` - Added Session 5 entry (to be updated)
- `NEXT_SESSION.md` - Will update with hooks study plan

---

## 🔧 Debugging Tools Setup

### Command-Line LLDB

**Quick debug session**:
```bash
# Terminal 1: Get backend PID
psql rollups_test
SELECT pg_backend_pid();  # e.g., 12345

# Terminal 2: Attach and debug
cd ~/dev/cpp/rollups  # Important: loads .lldbinit
lldb -p 12345
(lldb) b rollups_time_bucket
(lldb) c

# Terminal 1: Trigger breakpoint
SELECT rollups.time_bucket('1 hour', now()::timestamp);

# Terminal 2: Debug
(lldb) n          # Step
(lldb) print ts   # Now shows formatted timestamp!
(lldb) print *bucket_width  # Shows "1 hour"
```

### VS Code Integration

**Setup**: Install "CodeLLDB" extension

**Usage**:
1. Get backend PID from psql
2. Press F5 in VS Code
3. Choose "Attach to PostgreSQL Backend"
4. Enter PID
5. Set breakpoints by clicking line numbers
6. Run SQL in psql
7. Debug visually in VS Code!

---

## 🎓 Debugging Patterns Learned

### Pattern 1: Print PostgreSQL Strings

**Before formatters** (clunky):
```lldb
(lldb) x/s &result->vl_dat
0x12345: "1.0.0-cpp~\x7f\x7f"
```

**After formatters** (easy!):
```lldb
(lldb) print result
(text *) "1.0.0-cpp"
```

### Pattern 2: Inspect Timestamps

**Without formatters**:
```lldb
(lldb) print ts
(Timestamp) 822841042000000  # What date is this??
```

**With formatters**:
```lldb
(lldb) print ts
(Timestamp) 2024-01-15 14:37:22.000000 UTC
```

### Pattern 3: Debugging Crashes

```lldb
# After crash, see where it happened
(lldb) bt              # Backtrace
(lldb) frame select 0  # Jump to crash frame
(lldb) fr v            # See all local variables
```

### Pattern 4: Conditional Breakpoints

```lldb
# Only break when bucket_width_us is 0 (likely a bug)
(lldb) b rollups.cpp:232
(lldb) breakpoint modify 1 --condition 'bucket_width_us == 0'
```

---

## 🚀 What This Enables

Now that we have a solid debugging environment, we can:

1. **Study PostgreSQL hooks** with confidence
   - Set breakpoints in planner_hook, executor_hook
   - Step through PostgreSQL's internal functions
   - Understand exactly when hooks are called

2. **Debug complex features** as we build them
   - CREATE CONTINUOUS AGGREGATE parsing
   - Query rewriting logic
   - Background worker implementation
   - Trigger-based incremental updates

3. **Investigate crashes and errors** efficiently
   - See exact crash location with backtrace
   - Inspect PostgreSQL internals (memory contexts, MVCC)
   - Understand error handling flow (ereport)

4. **Learn PostgreSQL internals** by observation
   - Set breakpoints in PostgreSQL core code
   - See how planner optimizes queries
   - Watch executor process tuples
   - Understand transaction visibility

---

## 🎯 Next Session (Session 6)

Now that debugging is set up, we can proceed with the **original Session 5 goals**:

### Primary Goals (Deferred from Session 5)

1. **Study PostgreSQL hooks**
   - Examine pg_stat_statements source code as example
   - Understand hook types (planner, executor, utility)
   - Learn safe hook installation/uninstallation patterns
   - Study shared memory usage

2. **Research TimescaleDB architecture**
   - Clone TimescaleDB source repository
   - Study continuous aggregates implementation
   - Understand catalog schema and metadata management
   - Learn query rewriting approach

3. **Design C++ helper classes**
   - Plan class hierarchy for rollup management
   - Design memory management strategy (palloc integration)
   - Plan PostgreSQL memory context integration
   - Consider STL container usage with PostgreSQL allocators

### Key Questions to Answer

- Which hooks do we need for CREATE CONTINUOUS AGGREGATE?
- How does pg_stat_statements use planner/executor hooks?
- How does TimescaleDB intercept and rewrite queries?
- What's the best way to store rollup metadata?
- How to handle shared memory for extension state?

---

## 💡 Key Insights

### 1. Debugging is Essential for Learning

Building extensions without debugging is like driving blind. We need to:
- See how PostgreSQL calls our functions
- Understand the call stack and execution flow
- Inspect internal data structures
- Learn by observation, not just documentation

### 2. Tooling Matters

Good tools make complex tasks manageable:
- LLDB formatters turn cryptic pointers into readable data
- IDE integration enables visual debugging
- Automation (`.lldbinit`) reduces repetitive typing
- Documentation (`DEBUGGING.md`) captures knowledge for future reference

### 3. PostgreSQL Extensions Are Dynamic

- Extensions load on-demand, not at startup
- Each backend process loads its own copy
- Debugging requires understanding process architecture
- Cannot debug "PostgreSQL" - must debug a specific backend

### 4. C++ Integration Works Well

- `extern "C"` successfully bridges C++ and PostgreSQL
- Name mangling is prevented correctly
- Debugging C++ extensions works just like C
- Ready to use C++ features for complex logic

---

## 🛠️ Quick Reference

### Attach LLDB and Debug

```bash
# Get PID
psql rollups_test -c "SELECT pg_backend_pid();"

# Attach (from project directory!)
cd ~/dev/cpp/rollups
lldb -p <PID>

# Set breakpoints
(lldb) b rollups_time_bucket
(lldb) c

# In psql: run SQL
# In LLDB: step and inspect
(lldb) n                    # Next line
(lldb) print variable       # Shows formatted output!
(lldb) bt                   # Backtrace
```

### Check Symbol Names

```bash
# List all symbols
nm build/rollups.so

# Search for specific function
nm build/rollups.so | grep time_bucket
```

### View PostgreSQL Logs

```bash
tail -f ~/pgdev/logfile
```

---

## 📈 Overall Project Status

**Phase**: Phase 2 - Basic Extension Structure
**Progress**: ~50% of Phase 2 complete (debugging added)
**Status**: ✅ Extension working + debugging environment ready
**Next Milestone**: Study hooks and design CREATE CONTINUOUS AGGREGATE

**Timeline**:
- ✅ Session 1: Project initialization
- ✅ Session 2: Environment setup planning
- ✅ Session 3: C++ extension scaffold with CMake
- ✅ Session 4: Build, install, and test
- ✅ **Session 5: Debugging setup** ← **YOU ARE HERE**
- 🔜 Session 6: Study PostgreSQL hooks (original Session 5 goals)
- 🔜 Session 7: TimescaleDB architecture research
- 🔜 Session 8: Design C++ classes for rollups

---

**Outcome**: Professional debugging environment configured! 🎉

**Ready for**: Advanced PostgreSQL internals study (hooks, shared memory, query rewriting)

**Key Achievement**: We can now debug both our extension AND PostgreSQL's core code with full visibility into internal data structures.

---

*See `docs/DEBUGGING.md` for complete debugging guide*
*See `CLAUDE.md` for complete project documentation*
