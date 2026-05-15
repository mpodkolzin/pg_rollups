# Session 6 Summary - PostgreSQL Hooks Study (In Progress)

**Date**: 2026-03-13
**Status**: 🔄 **IN PROGRESS** - Hook fundamentals learned, basic implementation added

---

## 🎯 Accomplishments So Far

### ✅ Studied PostgreSQL Hooks via pg_stat_statements

**Deep dive into hook architecture**:
- Analyzed pg_stat_statements source code (`~/pgdev/postgresql-source/contrib/pg_stat_statements/`)
- Identified 8 different hooks it uses:
  - `shmem_request_hook`, `shmem_startup_hook` - Shared memory management
  - `post_parse_analyze_hook` - After query parsing
  - `planner_hook` - Query planning interception
  - `ExecutorStart_hook`, `ExecutorRun_hook`, `ExecutorFinish_hook`, `ExecutorEnd_hook` - Execution pipeline
  - `ProcessUtility_hook` - DDL command interception (**critical for our use case**)

**Understood the hook pattern**:
```c
// 1. Save previous hook (for chaining)
static ProcessUtility_hook_type prev_hook = NULL;

// 2. Install in _PG_init()
prev_hook = ProcessUtility_hook;
ProcessUtility_hook = my_hook;

// 3. In hook function, call previous or standard
if (prev_hook)
    prev_hook(...);
else
    standard_ProcessUtility(...);
```

### ✅ Created Comprehensive Documentation

**Created `docs/HOOKS_GUIDE.md`** (extensive reference document):
- What hooks are and how they work
- Complete hook pattern with examples from pg_stat_statements
- Available PostgreSQL hooks (query processing, DDL, shared memory, etc.)
- Query execution pipeline diagram showing when hooks fire
- Hook chaining mechanism explained
- ProcessUtility_hook details (critical for CREATE CONTINUOUS AGGREGATE)
- Shared memory usage patterns
- Hook safety considerations (error handling, memory contexts, recursion)
- What we need for rollups extension
- Testing hooks with LLDB/VS Code

**Key insights documented**:
- Hooks are function pointers that form a chain (middleware pattern)
- ProcessUtility_hook intercepts ALL DDL commands
- planner_hook is for query rewriting
- Always save and call previous hook (or standard function)
- Use palloc, never malloc
- Watch for recursion in hooks

### ✅ Hands-On Debugging with VS Code

**Debugged pg_stat_statements hooks**:
- Set breakpoints in `pgss_ProcessUtility`, `pgss_planner`, `pgss_ExecutorStart`
- Ran DDL command (`CREATE TABLE`) → hit `pgss_ProcessUtility`
- Ran SELECT query → hit `pgss_planner` then `pgss_ExecutorStart`
- **Witnessed the query execution pipeline in action!**

**Observed**:
- Different SQL commands fire different hooks
- DDL (CREATE TABLE) → ProcessUtility_hook
- DML (SELECT) → planner_hook → ExecutorStart_hook
- Hook chaining works as documented

### ✅ Implemented ProcessUtility_hook in Rollups Extension

**Added hook infrastructure to `src/rollups.cpp`**:
- Added necessary includes: `tcop/utility.h`, `commands/explain.h`
- Created `_PG_init()` function for extension initialization
- Implemented `rollups_ProcessUtility()` hook function
- Added hook chaining support (saves and calls `prev_ProcessUtility_hook`)
- Added DEBUG1 logging to trace DDL commands

**What the hook does (v1.0)**:
- Logs all DDL commands for learning purposes
- Passes through to next hook or standard_ProcessUtility
- **Future**: Will detect and parse `CREATE CONTINUOUS AGGREGATE`

**Code structure**:
```cpp
// Hook installation in _PG_init()
prev_ProcessUtility_hook = ProcessUtility_hook;
ProcessUtility_hook = rollups_ProcessUtility;

// Hook implementation
static void rollups_ProcessUtility(...)
{
    // Log DDL command
    ereport(DEBUG1, (errmsg("rollups: ProcessUtility hook fired"),
                     errdetail("Query: %s", queryString)));

    // TODO: Detect CREATE CONTINUOUS AGGREGATE

    // Pass through to next hook or standard
    if (prev_ProcessUtility_hook)
        prev_ProcessUtility_hook(...);
    else
        standard_ProcessUtility(...);
}
```

---

## 📊 Key Learnings

### Hook Chaining Mechanism

**How multiple extensions share hooks**:
1. Extension A loads → installs hook_A
2. Extension B loads → saves hook_A, installs hook_B
3. Extension C loads → saves hook_B, installs hook_C

**When query runs**:
```
PostgreSQL → hook_C → hook_B → hook_A → standard_function
```

Last installed hook is first called (stack/middleware pattern).

### Query Execution Pipeline

**What we observed in debugger**:
```
CREATE TABLE test (id int);
  ↓
[ProcessUtility_hook] → DDL commands go here

SELECT * FROM test;
  ↓
[planner_hook] → Query planning
  ↓
[ExecutorStart_hook] → Execution begins
```

### ProcessUtility_hook is Key for Us

**Why we need it**:
- Intercepts ALL DDL commands
- We'll use it to detect `CREATE CONTINUOUS AGGREGATE`
- Can parse `queryString` to identify our custom syntax
- Can create rollup metadata and materialization tables

**Future implementation**:
```cpp
if (strncmp(queryString, "CREATE CONTINUOUS AGGREGATE", 27) == 0)
{
    ParseCreateContinuousAggregate(queryString);
    CreateRollupMetadata(...);
    CreateMaterializationTable(...);
    return;  // Don't call standard_ProcessUtility
}
```

### Hook Safety

**Critical points**:
- Always use `palloc`, never `malloc` (memory context cleanup)
- Use `PG_TRY/PG_CATCH` for cleanup on ereport(ERROR)
- Watch for recursion (hooks can call themselves)
- Always call previous hook or standard function (don't break the chain!)

---

## 📂 Files Created/Modified

### New Files
- `docs/HOOKS_GUIDE.md` - Comprehensive PostgreSQL hooks reference (~800 lines)

### Modified Files
- `src/rollups.cpp` - Added ProcessUtility_hook implementation
  - Added includes for hook headers
  - Implemented `_PG_init()` function
  - Implemented `rollups_ProcessUtility()` hook
  - Added hook chaining support
  - Added DEBUG logging

---

## 🚧 Next Steps (When Resuming)

### Immediate Tasks
1. **Test the hook** (where we paused):
   ```bash
   make clean && make && make install
   pg_ctl restart -D ~/pgdev/data -l ~/pgdev/logfile
   psql rollups_test
   ```

   ```sql
   DROP EXTENSION IF EXISTS rollups CASCADE;
   CREATE EXTENSION rollups;
   SET client_min_messages = DEBUG1;
   CREATE TABLE hook_test (id int);
   -- Should see: DEBUG: rollups: ProcessUtility hook fired
   ```

2. **Debug the hook in VS Code**:
   - Set breakpoint in `rollups_ProcessUtility`
   - Run DDL commands
   - Inspect `queryString` variable
   - Verify hook chaining with pg_stat_statements

### Remaining Session 6 Goals

**Not yet started**:
- [ ] Clone TimescaleDB source repository
- [ ] Study TimescaleDB continuous aggregates implementation
- [ ] Understand their ProcessUtility_hook usage
- [ ] Learn their catalog schema design
- [ ] Design C++ class hierarchy for rollups
- [ ] Plan memory management strategy with PostgreSQL contexts
- [ ] Create implementation roadmap for Phase 3

**These can be done in Session 7 or later sessions.**

---

## 💡 Key Insights

### 1. Hooks Are Middleware

The hook pattern is essentially middleware:
- Each hook wraps the next
- Can observe, modify, or replace behavior
- Must maintain the chain for correctness

### 2. Visual Debugging is Powerful

**Before (Session 5)**: Command-line LLDB with formatters
**Now (Session 6)**: VS Code integration with breakpoints

**Impact**: Can step through PostgreSQL core code and see exactly when/how hooks fire.

### 3. Extension Initialization Pattern

```cpp
void _PG_init(void)
{
    // 1. Check if we should initialize
    // 2. Set up custom GUC parameters
    // 3. Install hooks (save previous, set new)
    // 4. Request shared memory (if needed)
    // 5. Register background workers (if needed)
}
```

This is the standard pattern for all PostgreSQL extensions.

### 4. ProcessUtility_hook is More Flexible Than Expected

**We can**:
- Detect custom syntax by parsing `queryString`
- Handle completely custom DDL commands
- Bypass standard_ProcessUtility for our commands
- Pass through everything else unchanged

**This means**: We can implement `CREATE CONTINUOUS AGGREGATE` without modifying PostgreSQL's grammar!

---

## 🔧 Testing Instructions (For Next Session)

### Test Hook Installation

```bash
# Rebuild
cd ~/dev/cpp/rollups
make clean && make && make install

# Restart PostgreSQL
pg_ctl restart -D ~/pgdev/data -l ~/pgdev/logfile

# Test
psql rollups_test
```

```sql
DROP EXTENSION IF EXISTS rollups CASCADE;
CREATE EXTENSION rollups;

-- Enable debug logging
SET client_min_messages = DEBUG1;

-- Test DDL commands
CREATE TABLE test1 (id int);
-- Should see: DEBUG: rollups: ProcessUtility hook fired

ALTER TABLE test1 ADD COLUMN name text;
-- Should see hook fire again

DROP TABLE test1;
-- Should see hook fire again
```

### Test Hook Chaining

```sql
-- Load pg_stat_statements too
CREATE EXTENSION IF NOT EXISTS pg_stat_statements;

-- Both hooks should fire
CREATE TABLE test2 (id int);

-- Check pg_stat_statements recorded it
SELECT query FROM pg_stat_statements
WHERE query LIKE '%test2%';
```

### Debug in VS Code

1. Get backend PID: `SELECT pg_backend_pid();`
2. Attach debugger in VS Code (use launch config)
3. Set breakpoint in `rollups_ProcessUtility`
4. Run DDL: `CREATE TABLE debug_test (id int);`
5. Step through and inspect `queryString`

---

## 📈 Overall Project Status

**Phase**: Phase 2 - Basic Extension Structure (Hooks added!)
**Progress**: ~60% of Phase 2 complete
**Status**: 🔄 Extension with basic hook infrastructure (testing in progress)
**Next Milestone**: Parse CREATE CONTINUOUS AGGREGATE syntax

**Timeline**:
- ✅ Session 1: Project initialization
- ✅ Session 2: Environment setup
- ✅ Session 3: C++ extension scaffold
- ✅ Session 4: Build, install, and test
- ✅ Session 5: Debugging setup (LLDB + formatters)
- 🔄 **Session 6: PostgreSQL hooks study** ← **YOU ARE HERE** (partially complete)
- 🔜 Session 7: TimescaleDB architecture + C++ design
- 🔜 Session 8: Implement CREATE CONTINUOUS AGGREGATE parsing

---

## 🎓 What We Can Do Now

**Understanding**:
- ✅ How PostgreSQL hooks work
- ✅ Hook chaining and multiple extensions
- ✅ When different hooks fire in query pipeline
- ✅ How to safely install and implement hooks
- ✅ ProcessUtility_hook for DDL interception

**Implementation**:
- ✅ Basic ProcessUtility_hook in our extension
- ✅ Hook chaining support
- ✅ Debug logging for learning
- ✅ Foundation for CREATE CONTINUOUS AGGREGATE

**Ready for**:
- Parsing CREATE CONTINUOUS AGGREGATE syntax
- Creating rollup metadata
- Query rewriting with planner_hook
- Background workers (future)

---

**Session Status**: Paused after implementing basic hook infrastructure
**Resume Point**: Test the hook, then study TimescaleDB architecture
**Estimated Completion**: Session 6 is ~50% done, need 1-2 more sessions for full hooks study

---

*See `docs/HOOKS_GUIDE.md` for complete hooks reference*
*See `SESSION_5_SUMMARY.md` for debugging environment setup*
*See `CLAUDE.md` for complete project documentation*
