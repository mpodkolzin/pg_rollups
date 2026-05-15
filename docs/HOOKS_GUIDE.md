# PostgreSQL Hooks Guide

**Learning from pg_stat_statements** - A comprehensive guide to PostgreSQL extension hooks

---

## What Are Hooks?

PostgreSQL **hooks** are function pointers that allow extensions to intercept and modify PostgreSQL's internal behavior without modifying core code.

**Think of hooks as "event listeners"** - PostgreSQL calls your function at specific points in query processing, allowing you to:
- Observe what's happening (logging, statistics)
- Modify behavior (query rewriting, custom planning)
- Add functionality (custom DDL commands, access control)

---

## The Hook Pattern (from pg_stat_statements)

### 1. Global Hook Variables

PostgreSQL core defines hook variables as function pointers:

```c
// In PostgreSQL source (src/include/commands/explain.h, executor/executor.h, etc.)
extern PGDLLIMPORT planner_hook_type planner_hook;
extern PGDLLIMPORT ExecutorStart_hook_type ExecutorStart_hook;
extern PGDLLIMPORT ExecutorRun_hook_type ExecutorRun_hook;
extern PGDLLIMPORT ProcessUtility_hook_type ProcessUtility_hook;
// ... many more
```

These start as `NULL` - no hook installed.

### 2. Save Previous Hook Value

**Critical for hook chaining!** Multiple extensions can use the same hook.

```c
// From pg_stat_statements.c:255-258
static planner_hook_type prev_planner_hook = NULL;
static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorRun_hook_type prev_ExecutorRun = NULL;
static ProcessUtility_hook_type prev_ProcessUtility = NULL;
```

**Why save it?**
- Another extension might have already installed a hook
- You must call the previous hook (or the default function) to maintain correct behavior
- This creates a "chain" of hooks

### 3. Install Hook in _PG_init()

```c
// From pg_stat_statements.c:458-475 (_PG_init function)
void
_PG_init(void)
{
    // ... other initialization ...

    // Save previous hook (might be NULL or another extension's hook)
    prev_planner_hook = planner_hook;

    // Install our hook
    planner_hook = pgss_planner;

    // Same pattern for all hooks
    prev_ExecutorStart = ExecutorStart_hook;
    ExecutorStart_hook = pgss_ExecutorStart;

    prev_ExecutorRun = ExecutorRun_hook;
    ExecutorRun_hook = pgss_ExecutorRun;

    prev_ProcessUtility = ProcessUtility_hook;
    ProcessUtility_hook = pgss_ProcessUtility;
}
```

**Key points**:
- `_PG_init()` is called when the extension is loaded
- For shared_preload_libraries, this happens at PostgreSQL startup
- For other extensions, when first used in a session

### 4. Implement Hook Function

**The critical pattern**: Always call the previous hook or the standard function!

```c
// From pg_stat_statements.c:871-958 (simplified)
static PlannedStmt *
pgss_planner(Query *parse,
             const char *query_string,
             int cursorOptions,
             ParamListInfo boundParams)
{
    PlannedStmt *result;

    // Our custom logic BEFORE standard planning
    if (should_track_this_query)
    {
        start_timer();

        // Call the next hook in the chain (or standard_planner if none)
        if (prev_planner_hook)
            result = prev_planner_hook(parse, query_string,
                                      cursorOptions, boundParams);
        else
            result = standard_planner(parse, query_string,
                                     cursorOptions, boundParams);

        // Our custom logic AFTER standard planning
        stop_timer();
        record_statistics();
    }
    else
    {
        // Not tracking - just pass through
        if (prev_planner_hook)
            result = prev_planner_hook(parse, query_string,
                                      cursorOptions, boundParams);
        else
            result = standard_planner(parse, query_string,
                                     cursorOptions, boundParams);
    }

    return result;
}
```

**The pattern**:
1. Do your "before" work
2. Call `prev_hook` (if exists) OR `standard_function`
3. Do your "after" work
4. Return the result

**Why this matters**:
- If you don't call the previous hook, you break other extensions
- If you don't call the standard function, PostgreSQL doesn't work!
- This is like middleware in web frameworks

---

## Available PostgreSQL Hooks

Based on pg_stat_statements and PostgreSQL source code:

### Query Processing Hooks

| Hook | When It's Called | Use Cases |
|------|------------------|-----------|
| `post_parse_analyze_hook` | After parsing SQL, before planning | Query normalization, logging raw queries |
| `planner_hook` | During query planning | Custom optimization, query rewriting, cost estimation |
| `ExecutorStart_hook` | Before query execution starts | Setup per-query state, modify plan |
| `ExecutorRun_hook` | During query execution | Collect runtime statistics |
| `ExecutorFinish_hook` | After execution, before cleanup | Final statistics collection |
| `ExecutorEnd_hook` | During executor cleanup | Resource cleanup, final accounting |

### DDL/Utility Hooks

| Hook | When It's Called | Use Cases |
|------|------------------|-----------|
| `ProcessUtility_hook` | For DDL commands (CREATE, ALTER, DROP, etc.) | **This is what we need for CREATE CONTINUOUS AGGREGATE!** |

### Shared Memory Hooks

| Hook | When It's Called | Use Cases |
|------|------------------|-----------|
| `shmem_request_hook` | At startup, to request shared memory | Declare how much shared memory you need |
| `shmem_startup_hook` | After shared memory allocated | Initialize shared memory structures |

### Other Hooks

| Hook | When It's Called | Use Cases |
|------|------------------|-----------|
| `object_access_hook` | When objects are accessed/modified | Security, auditing, custom permissions |
| `emit_log_hook` | When PostgreSQL logs a message | Custom logging, error tracking |

**Many more exist** - search PostgreSQL source for `_hook_type`:

```bash
cd ~/pgdev/postgresql-source
grep -r "_hook_type" src/include/
```

---

## Query Execution Pipeline (When Hooks Fire)

Understanding when hooks are called:

```
1. SQL String arrives
   ↓
2. PARSER: SQL → Parse Tree
   ↓
3. ANALYZER: Parse Tree → Query Tree
   ↓
   [post_parse_analyze_hook] ← AFTER parsing/analysis
   ↓
4. REWRITER: Apply rules (views, etc.)
   ↓
5. PLANNER: Query Tree → Plan Tree
   ↓
   [planner_hook] ← DURING planning
   ↓
6. EXECUTOR: Execute Plan Tree
   ↓
   [ExecutorStart_hook] ← BEFORE execution starts
   ↓
   [ExecutorRun_hook] ← DURING execution
   ↓
   [ExecutorFinish_hook] ← AFTER execution, before cleanup
   ↓
   [ExecutorEnd_hook] ← DURING cleanup
   ↓
7. Return results to client
```

**DDL Commands** (CREATE, DROP, etc.) skip most of this and go through:
```
1. PARSER
   ↓
2. [ProcessUtility_hook] ← DDL COMMANDS GO HERE
   ↓
3. Execute utility command
```

---

## Hook Chaining Example

What happens when multiple extensions install the same hook?

**Scenario**: Three extensions install `planner_hook`:
1. Extension A (pg_stat_statements) - tracks queries
2. Extension B (pg_hint_plan) - modifies planning
3. Extension C (our rollups extension) - rewrites queries

**Loading order** (in `shared_preload_libraries`):
```
shared_preload_libraries = 'pg_stat_statements,pg_hint_plan,rollups'
```

**What happens**:

```c
// Initially:
planner_hook = NULL

// Extension A loads (_PG_init):
prev_hook_A = planner_hook;  // NULL
planner_hook = hook_A;

// Extension B loads:
prev_hook_B = planner_hook;  // hook_A
planner_hook = hook_B;

// Extension C loads:
prev_hook_C = planner_hook;  // hook_B
planner_hook = hook_C;
```

**When a query runs**:
```
PostgreSQL calls: hook_C
  ↓
  hook_C does its work, then calls prev_hook_C (hook_B)
    ↓
    hook_B does its work, then calls prev_hook_B (hook_A)
      ↓
      hook_A does its work, then calls prev_hook_A (NULL)
        ↓
        hook_A calls standard_planner (the original PostgreSQL function)
          ↓
          Returns to hook_A
        ↓
      Returns to hook_B
    ↓
  Returns to hook_C
  ↓
Returns to PostgreSQL
```

**Key insight**: Hooks form a **stack** (last installed is first called, like middleware).

---

## ProcessUtility Hook (For CREATE CONTINUOUS AGGREGATE)

This is the hook we need for custom DDL commands!

### What Is It?

`ProcessUtility_hook` intercepts **all DDL commands**:
- CREATE TABLE, DROP TABLE, ALTER TABLE
- CREATE INDEX, DROP INDEX
- CREATE EXTENSION, DROP EXTENSION
- **Custom commands we can add!**

### How It Works

```c
// Hook signature (simplified from src/include/tcop/utility.h)
typedef void (*ProcessUtility_hook_type) (
    PlannedStmt *pstmt,        // The parsed statement
    const char *queryString,   // Original SQL text
    bool readOnlyTree,         // Whether parse tree can be modified
    ProcessUtilityContext context,  // Where it's being called from
    ParamListInfo params,      // Parameters (if any)
    QueryEnvironment *queryEnv, // Query environment
    DestReceiver *dest,        // Where to send results
    QueryCompletion *qc        // Completion tag
);
```

### Example Usage (What We'll Implement)

```c
static ProcessUtility_hook_type prev_ProcessUtility_hook = NULL;

void _PG_init(void)
{
    prev_ProcessUtility_hook = ProcessUtility_hook;
    ProcessUtility_hook = rollups_ProcessUtility;
}

static void
rollups_ProcessUtility(PlannedStmt *pstmt,
                       const char *queryString,
                       bool readOnlyTree,
                       ProcessUtilityContext context,
                       ParamListInfo params,
                       QueryEnvironment *queryEnv,
                       DestReceiver *dest,
                       QueryCompletion *qc)
{
    Node *parsetree = pstmt->utilityStmt;

    // Check if this is our custom DDL command
    if (IsCreateContinuousAggregateStmt(queryString))
    {
        // Parse and execute CREATE CONTINUOUS AGGREGATE
        ParseCreateContinuousAggregate(queryString);
        CreateRollupDefinition(...);
        CreateMaterializationTable(...);
        // ... our custom logic ...

        // Don't call standard ProcessUtility for our custom command!
        return;
    }

    // Not our command - pass through to next hook or standard
    if (prev_ProcessUtility_hook)
        prev_ProcessUtility_hook(pstmt, queryString, readOnlyTree,
                                context, params, queryEnv, dest, qc);
    else
        standard_ProcessUtility(pstmt, queryString, readOnlyTree,
                               context, params, queryEnv, dest, qc);
}
```

**Key points**:
- We inspect `queryString` to detect our custom syntax
- We handle our commands completely
- We pass through all other commands

### How TimescaleDB Does It

TimescaleDB uses ProcessUtility_hook to intercept:
- `CREATE TABLE` - convert to hypertable
- `CREATE INDEX` - create distributed indexes
- Custom commands like `CREATE CONTINUOUS AGGREGATE`

They parse the SQL string manually or use PostgreSQL's parser with custom extensions.

---

## Shared Memory (For Extension State)

Extensions that need global state across all backends use **shared memory**.

### Why Shared Memory?

- Each PostgreSQL backend is a separate process
- Regular variables are per-process (not shared!)
- Shared memory allows all backends to access common data

**pg_stat_statements** uses shared memory to store query statistics visible to all connections.

### The Pattern

```c
// 1. Declare how much shared memory you need
static void
rollups_shmem_request(void)
{
    if (prev_shmem_request_hook)
        prev_shmem_request_hook();

    // Request shared memory
    RequestAddinShmemSpace(1024 * 1024);  // 1MB example
    RequestNamedLWLockTranche("rollups", 1);  // 1 lock
}

// 2. Initialize shared memory structures
static void
rollups_shmem_startup(void)
{
    bool found;

    if (prev_shmem_startup_hook)
        prev_shmem_startup_hook();

    // Get or create shared memory segment
    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

    rollups_shared_state = ShmemInitStruct("rollups_state",
                                           sizeof(RollupsSharedState),
                                           &found);

    if (!found)
    {
        // First time - initialize the structure
        memset(rollups_shared_state, 0, sizeof(RollupsSharedState));
        rollups_shared_state->lock = &(GetNamedLWLockTranche("rollups")->lock);
    }

    LWLockRelease(AddinShmemInitLock);
}

// 3. Access shared memory with locks
void some_function(void)
{
    // Acquire lock before accessing shared data
    LWLockAcquire(rollups_shared_state->lock, LW_SHARED);

    // Read shared data...
    int value = rollups_shared_state->some_value;

    LWLockRelease(rollups_shared_state->lock);

    // Modify shared data - need exclusive lock
    LWLockAcquire(rollups_shared_state->lock, LW_EXCLUSIVE);

    rollups_shared_state->some_value++;

    LWLockRelease(rollups_shared_state->lock);
}
```

**LWLock** (Lightweight Lock):
- `LW_SHARED` - Multiple readers allowed
- `LW_EXCLUSIVE` - Only one writer, no readers

**Critical**: Always release locks! Deadlocks will hang PostgreSQL.

---

## Hook Safety Considerations

### 1. Error Handling

**PostgreSQL uses `ereport(ERROR, ...)` which does `longjmp`!**

```c
void my_hook(...)
{
    // Allocate resource
    void *memory = palloc(1024);

    // If error happens here, we longjmp out!
    // C++ destructors won't run
    // Regular cleanup won't happen

    // PostgreSQL's solution: PG_TRY/PG_CATCH
    PG_TRY();
    {
        // Code that might ereport(ERROR)
        do_something_risky();
    }
    PG_CATCH();
    {
        // Cleanup code
        pfree(memory);
        PG_RE_THROW();  // Re-throw the error
    }
    PG_END_TRY();

    pfree(memory);
}
```

**For C++**: This is why we can't use C++ exceptions or RAII reliably with PostgreSQL!

### 2. Memory Contexts

**Always use PostgreSQL's memory management in hooks!**

```c
void my_hook(...)
{
    // BAD: Don't use malloc
    char *bad = malloc(100);  // Won't be freed on ereport(ERROR)

    // GOOD: Use palloc
    char *good = palloc(100);  // Automatically freed when memory context is reset
}
```

### 3. Recursion

Hooks can be called recursively!

```c
// From pg_stat_statements:
static int plan_nested_level = 0;
static int exec_nested_level = 0;

void pgss_planner(...)
{
    plan_nested_level++;

    // ... do work ...

    // This might call the planner again! (for subqueries, CTEs, etc.)
    result = standard_planner(...);

    plan_nested_level--;
}
```

**Solution**: Track nesting level to avoid infinite loops or double-counting.

---

## What We Need for Rollups Extension

Based on this analysis, here's what we need:

### Hooks to Install

1. **`ProcessUtility_hook`** (CRITICAL)
   - Intercept `CREATE CONTINUOUS AGGREGATE`
   - Parse custom DDL syntax
   - Create rollup metadata
   - Create materialization table

2. **`planner_hook`** (IMPORTANT)
   - Detect queries that can use rollups
   - Rewrite query plans to use materialization tables
   - Cost estimation for rollup vs. base table

3. **`ExecutorEnd_hook`** (OPTIONAL - for initial version)
   - Track modifications to source tables
   - Record which rollups need refreshing
   - Alternative: use triggers instead

### Shared Memory Needs

**Do we need shared memory?**
- For v1.0: **Probably not!**
- Rollup metadata is in catalog tables (persistent, not in-memory)
- Refresh tracking can use catalog tables too
- Shared memory is for high-frequency, cross-backend state

**Maybe later** for:
- Caching rollup metadata (faster than catalog lookups)
- Coordinating background workers
- Tracking refresh statistics

### Implementation Strategy

**Phase 1** (Next steps):
1. Implement simple `ProcessUtility_hook` that just logs DDL commands
2. Test hook chaining with pg_stat_statements loaded
3. Debug with LLDB to see when it's called

**Phase 2**:
1. Parse `CREATE CONTINUOUS AGGREGATE` from query string
2. Create catalog entries
3. Create materialization tables

**Phase 3**:
1. Implement `planner_hook` for query rewriting
2. Test with real queries

---

## Testing Hooks with LLDB

Now that we understand hooks, we can debug them!

### Example Session

```bash
# Terminal 1: Load pg_stat_statements
psql rollups_test
CREATE EXTENSION pg_stat_statements;
SELECT pg_backend_pid();  -- Note the PID
```

```bash
# Terminal 2: Attach LLDB
cd ~/dev/cpp/rollups
lldb -p <PID>

# Set breakpoints in hooks
(lldb) b pgss_ProcessUtility
(lldb) b pgss_planner
(lldb) b pgss_ExecutorStart
(lldb) c
```

```sql
-- Terminal 1: Run various commands
CREATE TABLE test (id int);  -- Triggers ProcessUtility hook
SELECT * FROM test;          -- Triggers planner and executor hooks
```

```lldb
# Terminal 2: See hooks fire!
# Step through with 'n', inspect variables with 'print'
(lldb) n
(lldb) print queryString
(lldb) print pstmt
(lldb) bt  -- See the call stack
```

---

## Key Takeaways

1. **Hooks are function pointers** that let extensions intercept PostgreSQL behavior
2. **Always save and call the previous hook** (or standard function)
3. **Hooks form a chain** - last loaded is first called
4. **ProcessUtility_hook is for DDL** - we need this for CREATE CONTINUOUS AGGREGATE
5. **planner_hook is for query rewriting** - we need this to use rollups
6. **Shared memory is for cross-backend state** - we probably don't need it initially
7. **Always use palloc, never malloc** - memory context cleanup is automatic
8. **Use PG_TRY/PG_CATCH for cleanup** - ereport() does longjmp
9. **Watch for recursion** - hooks can call themselves

---

## Next Steps

1. ✅ Understand hook pattern (THIS DOCUMENT)
2. 🔄 Debug pg_stat_statements hooks with LLDB (NEXT)
3. ⏭️ Study TimescaleDB ProcessUtility_hook usage
4. ⏭️ Implement simple ProcessUtility_hook in our extension
5. ⏭️ Parse CREATE CONTINUOUS AGGREGATE syntax
6. ⏭️ Implement planner_hook for query rewriting

---

**See**:
- `DEBUGGING.md` - How to debug with LLDB
- PostgreSQL source: `contrib/pg_stat_statements/pg_stat_statements.c`
- PostgreSQL docs: https://www.postgresql.org/docs/current/hooks.html

---

*Created during Session 6 - PostgreSQL Hooks Study*
