# Next Session (Session 6 Continued / Session 7)

**Current Status**: Session 6 partially complete - hook infrastructure implemented, needs testing + TimescaleDB study

---

## Where We Left Off

### ✅ Completed in Session 6 So Far

1. **Studied PostgreSQL hooks deeply**
   - Analyzed pg_stat_statements source code
   - Created comprehensive `docs/HOOKS_GUIDE.md`
   - Understood hook pattern: save previous → install → call chain

2. **Debugged hooks with VS Code**
   - Set breakpoints in pg_stat_statements hooks
   - Witnessed query execution pipeline:
     - `CREATE TABLE` → `ProcessUtility_hook`
     - `SELECT` → `planner_hook` → `ExecutorStart_hook`

3. **Implemented ProcessUtility_hook in our extension**
   - Added `_PG_init()` function
   - Created `rollups_ProcessUtility()` hook
   - Implemented hook chaining
   - Added DEBUG logging

### 🔄 In Progress - Resume Here!

**Modified code needs testing**:
- `src/rollups.cpp` - Added ProcessUtility_hook implementation
- Hook is ready but not yet tested

---

## Immediate Resume Tasks

### 1. Test the Hook Implementation (15-20 minutes)

**Rebuild and install**:
```bash
cd ~/dev/cpp/rollups
make clean
make
make install

# Restart PostgreSQL to load new version
pg_ctl restart -D ~/pgdev/data -l ~/pgdev/logfile
```

**Test in psql**:
```bash
psql rollups_test
```

```sql
-- Reload extension
DROP EXTENSION IF EXISTS rollups CASCADE;
CREATE EXTENSION rollups;

-- Enable debug logging
SET client_min_messages = DEBUG1;

-- Test DDL commands - should see our hook fire
CREATE TABLE hook_test1 (id int, name text);
-- Expected output:
-- DEBUG:  rollups: ProcessUtility hook fired
-- DETAIL:  Query: CREATE TABLE hook_test1 (id int, name text);

ALTER TABLE hook_test1 ADD COLUMN email text;
-- Should see hook fire again

DROP TABLE hook_test1;
-- Should see hook fire again
```

**Test hook chaining with pg_stat_statements**:
```sql
-- Load pg_stat_statements if not already loaded
CREATE EXTENSION IF NOT EXISTS pg_stat_statements;

-- Both hooks should fire
CREATE TABLE hook_test2 (id int);

-- Verify pg_stat_statements recorded it
SELECT query FROM pg_stat_statements
WHERE query LIKE '%hook_test2%';
```

### 2. Debug in VS Code (10-15 minutes)

**Set up debugging session**:
1. Get backend PID: `SELECT pg_backend_pid();` in psql
2. In VS Code: Start debugging, attach to PID
3. Set breakpoint in `rollups_ProcessUtility` function
4. In psql: `CREATE TABLE debug_test (id int);`
5. Should hit breakpoint!

**Inspect variables**:
- `queryString` - Should show the DDL command text
- `prev_ProcessUtility_hook` - Should show previous hook or NULL
- `pstmt->utilityStmt` - The parsed statement node

**Step through**:
- Step through the function
- Watch it call `prev_ProcessUtility_hook` or `standard_ProcessUtility`
- Verify hook chaining works correctly

---

## Remaining Session 6/7 Goals

### 3. Study TimescaleDB Continuous Aggregates (1-2 hours)

**Clone TimescaleDB**:
```bash
cd ~/dev
git clone https://github.com/timescale/timescaledb.git
cd timescaledb

# Explore continuous aggregates
cd tsl/src/continuous_aggs/
ls -la
```

**Key files to read**:
```bash
# SQL API definition
less sql/timescaledb--*.sql  # Search for "continuous aggregate"

# Core implementation files
less tsl/src/continuous_aggs/create.c    # CREATE logic
less tsl/src/continuous_aggs/refresh.c   # Refresh logic
less tsl/src/continuous_aggs/common.c    # Common utilities
```

**Questions to answer**:
1. **How do they intercept CREATE CONTINUOUS AGGREGATE?**
   - Search for `ProcessUtility` in their code
   - Find where they parse the syntax
   - How do they detect their custom DDL?

2. **What catalog schema do they use?**
   - Look for CREATE TABLE statements for metadata
   - What columns do they store?
   - How do they link rollups to source tables?

3. **How do they handle query rewriting?**
   - Search for `planner_hook`
   - How do they detect queries that can use rollups?
   - How do they rewrite the plan?

4. **What's their refresh strategy?**
   - How do they track what needs updating?
   - Triggers vs background workers?
   - Incremental update algorithm?

**Document findings**:
Create `docs/TIMESCALEDB_NOTES.md` with key insights.

### 4. Design C++ Class Hierarchy (1 hour)

Based on what we learned from hooks and TimescaleDB, design our classes:

**Create `docs/CPP_DESIGN.md`**:

```cpp
// Proposed class hierarchy (to refine based on TimescaleDB study)

namespace rollups {

// Time bucket utilities
class TimeBucket {
public:
    static Timestamp bucket(Interval interval, Timestamp ts);
    static TimeRange get_bucket_range(Interval interval, Timestamp ts);

private:
    // Use PostgreSQL memory contexts (palloc, not new)
};

// Represents a continuous aggregate definition
class RollupDefinition {
public:
    int64_t id;
    std::string name;
    std::string source_table;
    std::string time_column;
    Interval bucket_interval;
    std::string aggregate_sql;

    // Load from catalog
    static RollupDefinition* load_from_catalog(const char* name);

    // Save to catalog
    void save_to_catalog();
};

// Manages rollup metadata and operations
class RollupManager {
public:
    // DDL operations
    void create_continuous_aggregate(const char* ddl_sql);
    void drop_continuous_aggregate(const char* name);

    // Query operations
    void refresh(const char* name, TimeRange range);
    bool can_use_rollup(const Query* query);

private:
    // Catalog table interactions
    void insert_catalog_entry(RollupDefinition* def);
    void delete_catalog_entry(const char* name);
};

// Parser for CREATE CONTINUOUS AGGREGATE syntax
class DDLParser {
public:
    static RollupDefinition* parse_create_continuous_aggregate(
        const char* sql
    );

private:
    // Manual parsing helpers
    static const char* skip_whitespace(const char* str);
    static bool starts_with(const char* str, const char* prefix);
};

} // namespace rollups
```

**Key design decisions to document**:
1. **Memory management**: Use palloc in constructors, or placement new?
2. **Error handling**: Return NULL or use ereport(ERROR)?
3. **STL usage**: Custom allocator for std::string, std::vector?
4. **RAII limitations**: Can't rely on destructors (ereport longjmp issue)

### 5. Plan Implementation Roadmap (30 minutes)

Update `CLAUDE.md` with Phase 3 plan:

**Phase 3: Core Rollup Engine** (upcoming sessions)

Session 8-9: CREATE CONTINUOUS AGGREGATE parsing
- Implement DDLParser class
- Parse syntax: `CREATE CONTINUOUS AGGREGATE name WITH (bucket_interval = '1 hour') AS SELECT ...`
- Extract: name, bucket_interval, source table, aggregate SQL
- Validate parameters

Session 10: Catalog schema and metadata
- Design catalog tables (refine existing schema)
- Implement RollupDefinition class
- CRUD operations for rollup definitions
- Test metadata persistence

Session 11: Materialization table creation
- Generate CREATE TABLE SQL for materialization
- Include time bucket column + aggregate columns
- Create indexes for efficient queries
- Test table creation

Session 12: Query rewriting (planner_hook)
- Implement planner_hook
- Detect queries that match rollup definition
- Rewrite query plan to use materialization table
- Cost estimation for rollup vs base table

Sessions 13+: Incremental updates
- Design refresh mechanism
- Implement trigger-based updates (simple approach first)
- Handle INSERT/UPDATE/DELETE on source tables
- Test incremental refresh

---

## Quick Reference

### Files to Review
- `SESSION_6_SUMMARY.md` - What we accomplished so far
- `docs/HOOKS_GUIDE.md` - Complete hooks reference
- `docs/DEBUGGING.md` - Debugging with LLDB/VS Code
- `src/rollups.cpp` - Our hook implementation (needs testing)

### Commands to Remember

**Rebuild and test**:
```bash
cd ~/dev/cpp/rollups
make clean && make && make install
pg_ctl restart -D ~/pgdev/data -l ~/pgdev/logfile
psql rollups_test
```

**Debug in VS Code**:
```sql
SELECT pg_backend_pid();  -- Get PID
```
Then attach debugger in VS Code to that PID.

**Check PostgreSQL logs**:
```bash
tail -f ~/pgdev/logfile
```

---

## Session 6/7 Checklist

- [x] Study pg_stat_statements source code
- [x] Create HOOKS_GUIDE.md documentation
- [x] Debug pg_stat_statements hooks with VS Code
- [x] Implement ProcessUtility_hook in rollups extension
- [ ] **Test ProcessUtility_hook** ← **START HERE**
- [ ] Debug hook in VS Code
- [ ] Clone TimescaleDB source
- [ ] Study TimescaleDB continuous aggregates
- [ ] Understand their ProcessUtility_hook usage
- [ ] Document their catalog schema
- [ ] Design C++ class hierarchy
- [ ] Plan CREATE CONTINUOUS AGGREGATE parsing
- [ ] Create Phase 3 implementation roadmap

---

## Estimated Time to Complete Session 6/7

- Testing hook: 15-20 min
- Debugging in VS Code: 10-15 min
- TimescaleDB study: 1-2 hours
- C++ design: 1 hour
- Planning roadmap: 30 min

**Total**: ~3-4 hours

Could be split into:
- **Short session**: Test hook (30 min)
- **Long session**: TimescaleDB study + C++ design (2-3 hours)

---

**Current Status**: Hook infrastructure implemented, ready to test
**Next Action**: Rebuild, test, and debug the ProcessUtility_hook
**Goal**: Complete hooks study, then move to implementation planning

---

*Resume with testing the hook implementation, then continue to TimescaleDB study*
