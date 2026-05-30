# Query Rewriting Design

## Overview

Automatically rewrite queries that match rollup definitions to use materialized rollup tables instead of scanning the source table.

**Goal**: Make rollups transparent - users query the source table, PostgreSQL automatically uses the faster rollup.

---

## Two-Stage Implementation

### Stage 1: planner_hook (Quick Win - Current)
**Approach**: Intercept queries in `planner_hook`, pattern match, rewrite query tree
**Timeline**: 1-2 sessions
**Learning Focus**: Query tree structures, tree surgery, hook mechanics

### Stage 2: Custom Scan Provider (Proper Architecture - Future)
**Approach**: Register custom scan provider, cost-based selection by planner
**Timeline**: 2-3 sessions (after Stage 1 complete)
**Learning Focus**: Scan providers, cost estimation, executor nodes, proper PostgreSQL patterns

**Why staged?**
- Stage 1 proves concept quickly, validates pattern matching
- Stage 2 teaches "the right way" and provides cleaner architecture
- Natural refactoring exercise common in real projects

---

## Stage 1: planner_hook Design

### Query Lifecycle & Hook Placement

```
User SQL: SELECT time_bucket('1 day', ts), SUM(val) FROM events GROUP BY 1
    ↓
Parser (raw SQL → parse tree)
    ↓
Analyzer (parse tree → Query tree, semantic analysis)
    ↓
planner_hook ← **WE INTERCEPT HERE**
    ├─ Pattern match: eligible for rollup rewrite?
    ├─ YES → Rewrite Query tree (swap events → _rollup_1_data)
    └─ NO  → Pass through unchanged
    ↓
Planner (Query tree → Plan tree, optimization)
    ↓
Executor (Plan tree → tuples)
```

**Why planner_hook?**
- Fires after parse/analyze (query tree is fully formed)
- Before optimization (can still modify structure)
- Simple to hook (single function pointer)
- Can call `standard_planner()` to continue normal flow

---

### Pattern Matching Rules (Stage 1 Scope)

**We WILL rewrite queries that:**
1. Are `SELECT` with `GROUP BY`
2. Have aggregate functions in target list
3. Group by `time_bucket(interval, column)` (exactly matching rollup interval)
4. Query exactly one table (the rollup's source table)
5. Aggregates exactly match what's materialized

**We will NOT rewrite (silently fall through):**
- Queries with `JOIN`s
- Queries with `WHERE` clauses (Stage 1 - defer filtering to Stage 2)
- Queries with subqueries
- Different time bucket interval
- Different aggregate functions
- `HAVING`, `WINDOW`, `DISTINCT`, etc.

**Why these limitations?**
- Keep Stage 1 simple and correct
- Avoid subtle bugs from complex rewrites
- Stage 2 (custom scan provider) will handle these properly

---

### Query Tree Structures to Master

#### Query Struct
```c
typedef struct Query {
    NodeTag     type;
    CmdType     commandType;    // CMD_SELECT, CMD_INSERT, etc.
    List       *rtable;          // Range table (FROM clause tables)
    List       *targetList;      // SELECT list (TargetEntry nodes)
    Node       *jointree;        // FROM/WHERE clause structure
    List       *groupClause;     // GROUP BY expressions
    Node       *havingQual;      // HAVING clause
    // ... many more fields
} Query;
```

#### RangeTblEntry (RTE)
Represents a table in the `FROM` clause:
```c
typedef struct RangeTblEntry {
    RTEKind     rtekind;        // RTE_RELATION, RTE_SUBQUERY, etc.
    Oid         relid;          // Table OID (for RTE_RELATION)
    Alias      *eref;           // Alias name
    // ... more fields
} RangeTblEntry;
```

**Our rewrite**: Change `relid` from `source_table` → `_rollup_N_data`

#### TargetEntry (SELECT list item)
```c
typedef struct TargetEntry {
    Expr       *expr;           // Expression (Var, Aggref, FuncExpr, etc.)
    AttrNumber  resno;          // Result column number
    char       *resname;        // Column alias
    bool        resjunk;        // Junk column (not in output)
} TargetEntry;
```

**Our rewrite**: Map aggregates to rollup columns
- `time_bucket(...)` → `Var` referencing `bucket` column
- `SUM(value)` → `Var` referencing `sum_value` column

#### Aggref (Aggregate function call)
```c
typedef struct Aggref {
    Oid         aggfnoid;       // Function OID (e.g., SUM, COUNT)
    List       *args;           // Argument expressions
    // ... more fields
} Aggref;
```

**Pattern matching**: Identify which aggregates are present, match to rollup schema

#### FuncExpr (Function call)
```c
typedef struct FuncExpr {
    Oid         funcid;         // Function OID
    List       *args;           // Argument list
    // ... more fields
} FuncExpr;
```

**Pattern matching**: Detect `time_bucket(interval, column)` calls

#### Var (Column reference)
```c
typedef struct Var {
    Index       varno;          // Range table index
    AttrNumber  varattno;       // Column attribute number
    Oid         vartype;        // Data type OID
    // ... more fields
} Var;
```

**Our rewrite**: Change column references to match rollup table schema

---

### Implementation Components

#### 1. Hook Registration (`src/rollups.cpp`)
```cpp
static planner_hook_type prev_planner_hook = NULL;

void _PG_init(void) {
    // ... existing code
    prev_planner_hook = planner_hook;
    planner_hook = rollups_planner_hook;
}

void _PG_fini(void) {
    planner_hook = prev_planner_hook;
    // ... existing code
}
```

**Hook chaining**: Save previous hook, call it in our hook

#### 2. Planner Hook (`src/query_rewriter.cpp`)
```cpp
extern "C" PlannedStmt *rollups_planner_hook(
    Query *parse,
    const char *query_string,
    int cursorOptions,
    ParamListInfo boundParams
) {
    // 1. Check if rewritable
    if (is_rollup_eligible_query(parse)) {
        // 2. Find matching rollup
        ContinuousAggregateData *agg = find_matching_rollup(parse);
        if (agg) {
            // 3. Rewrite query tree in-place
            rewrite_query_for_rollup(parse, agg);
            elog(DEBUG1, "Rewrote query to use rollup: %s", 
                 NameStr(agg->agg_name));
        }
    }
    
    // 4. Chain to standard planner
    if (prev_planner_hook)
        return prev_planner_hook(parse, query_string, cursorOptions, boundParams);
    else
        return standard_planner(parse, query_string, cursorOptions, boundParams);
}
```

**Flow**: Detect → Find rollup → Rewrite → Continue planning

#### 3. Eligibility Check
```cpp
static bool is_rollup_eligible_query(Query *parse) {
    // CMD_SELECT with GROUP BY
    if (parse->commandType != CMD_SELECT || !parse->groupClause)
        return false;
    
    // Exactly one range table entry (no JOINs)
    if (list_length(parse->rtable) != 1)
        return false;
    
    // No subqueries, CTEs, etc.
    if (parse->hasSubLinks || parse->cteList)
        return false;
    
    // Has aggregates in target list
    if (!parse->hasAggs)
        return false;
    
    return true;
}
```

**Conservative**: Better to skip than to rewrite incorrectly

#### 4. Rollup Matching
```cpp
static ContinuousAggregateData *find_matching_rollup(Query *parse) {
    // 1. Extract source table OID from rtable
    RangeTblEntry *rte = (RangeTblEntry *)linitial(parse->rtable);
    Oid source_table_oid = rte->relid;
    
    // 2. Find time_bucket() call in GROUP BY
    char *bucket_width = extract_bucket_width_from_group_clause(parse);
    if (!bucket_width)
        return NULL;
    
    // 3. Query catalog for matching rollup
    // WHERE source_table = source_table_oid 
    //   AND bucket_width = bucket_width
    ContinuousAggregateData *agg = CatalogManager::find_by_source_and_bucket(
        source_table_oid, bucket_width
    );
    
    return agg;
}
```

**Matching criteria**: Source table + bucket width (Stage 1 - simple match)

#### 5. Query Rewriting
```cpp
static void rewrite_query_for_rollup(Query *parse, ContinuousAggregateData *agg) {
    // 1. Swap source table for rollup table in rtable
    RangeTblEntry *rte = (RangeTblEntry *)linitial(parse->rtable);
    rte->relid = agg->rollup_table_oid;
    // Update relation name in alias
    rte->eref->aliasname = get_rel_name(agg->rollup_table_oid);
    
    // 2. Rewrite target list (SELECT items)
    ListCell *lc;
    foreach(lc, parse->targetList) {
        TargetEntry *tle = (TargetEntry *)lfirst(lc);
        rewrite_target_entry(tle, agg);
    }
    
    // 3. Rewrite GROUP BY clause
    foreach(lc, parse->groupClause) {
        SortGroupClause *sgc = (SortGroupClause *)lfirst(lc);
        // Update to reference rollup table columns
        rewrite_group_clause(sgc, agg);
    }
    
    // Note: WHERE clause rewriting deferred to Stage 2
}
```

**Target entry rewriting**:
- `time_bucket('1 hour', ts)` → `Var(varattno=1)` (bucket column)
- `SUM(value)` → `Var(varattno=2)` (sum_value column)
- `COUNT(*)` → `Var(varattno=3)` (count_value column)

---

### Column Name Mapping Strategy

**Problem**: How do we know `SUM(value)` maps to `sum_value` column in rollup table?

**Stage 1 Solution: Naming Convention**
- Store `select_clause` in catalog (already done)
- Parse it to extract column aliases:
  - `SUM(value) AS sum_value` → aggregate `SUM(value)` maps to column `sum_value`
  - `COUNT(*) AS count_value` → aggregate `COUNT(*)` maps to column `count_value`
- During rewrite, match aggregate expression to select_clause, lookup alias

**Implementation**:
```cpp
// Parse select_clause once when finding rollup, cache mapping
struct AggregateMapping {
    char *aggregate_expr;   // "SUM(value)", "COUNT(*)"
    char *rollup_column;    // "sum_value", "count_value"
    AttrNumber attnum;      // Column number in rollup table
};

std::vector<AggregateMapping> parse_aggregate_mappings(
    ContinuousAggregateData *agg
);
```

**Alternative (Stage 2)**: Store structured metadata instead of raw SELECT clause

---

### Testing Strategy

#### Test 1: Exact Match (Should Rewrite)
```sql
-- Setup
SELECT rollups.create_continuous_aggregate(
    'hourly_stats',
    'events',
    'timestamp',
    '1 hour',
    'time_bucket(''1 hour''::interval, timestamp) AS bucket, 
     SUM(value) AS sum_value, 
     COUNT(*) AS count_value'
);
SELECT rollups.refresh_continuous_aggregate('hourly_stats');

-- Enable debug logging
SET client_min_messages = DEBUG1;

-- This should get rewritten
SELECT time_bucket('1 hour'::interval, timestamp) AS bucket,
       SUM(value),
       COUNT(*)
FROM events
GROUP BY bucket;

-- Expected output:
-- DEBUG: Rewrote query to use rollup: hourly_stats
-- (results from rollup table)
```

#### Test 2: Different Interval (Should NOT Rewrite)
```sql
-- Same rollup as above (1 hour), but query asks for 1 day
SELECT time_bucket('1 day'::interval, timestamp),
       SUM(value)
FROM events
GROUP BY 1;

-- Expected: No rewrite, scans events table directly
```

#### Test 3: JOIN Query (Should NOT Rewrite)
```sql
SELECT time_bucket('1 hour'::interval, e.timestamp),
       SUM(e.value)
FROM events e
JOIN other_table o ON e.id = o.event_id
GROUP BY 1;

-- Expected: No rewrite (has JOIN)
```

#### Test 4: Verify EXPLAIN Output
```sql
EXPLAIN SELECT time_bucket('1 hour'::interval, timestamp),
               SUM(value)
        FROM events
        GROUP BY 1;

-- Before rewrite: Seq Scan on events
-- After rewrite:  Seq Scan on _rollup_1_data (or Index Scan if we have one)
```

---

### Files to Create/Modify

**New Files**:
- `src/query_rewriter.cpp` - Hook implementation and rewriting logic
- `include/rollups/query_rewriter.hpp` - Function declarations

**Modified Files**:
- `src/rollups.cpp` - Hook registration in `_PG_init()/_PG_fini()`
- `CMakeLists.txt` - Add `src/query_rewriter.cpp` to sources
- `src/catalog_manager.cpp` - Add `find_by_source_and_bucket()` helper

---

### PostgreSQL APIs We'll Use

**Query Tree Navigation**:
- `linitial(list)` - Get first element of List
- `lnext(list, cell)` - Get next element
- `foreach(cell, list)` - Iterate over List
- `list_length(list)` - Number of elements

**Node Type Checking**:
- `IsA(node, NodeTag)` - Check node type (e.g., `IsA(expr, Aggref)`)
- `nodeTag(node)` - Get NodeTag

**Catalog Lookups**:
- `get_rel_name(Oid)` - Table name from OID
- `get_attname(Oid, AttrNumber)` - Column name from table OID + attr number
- `get_relname_relid(name, namespace)` - OID from table name

**Memory Management**:
- All query tree modifications happen in `CurrentMemoryContext` (query context)
- No need to pfree - cleaned up after query completes
- Use `copyObject()` if creating new nodes (not just modifying existing)

---

### Debugging Tips

**Enable verbose logging**:
```sql
SET client_min_messages = DEBUG1;
SET log_min_messages = DEBUG1;
```

**Add strategic elog() calls**:
```cpp
elog(DEBUG1, "Query has %d range table entries", list_length(parse->rtable));
elog(DEBUG1, "Found time_bucket with width: %s", bucket_width);
elog(DEBUG1, "Matched rollup: %s", NameStr(agg->agg_name));
```

**Inspect query trees in debugger**:
- Breakpoint in `rollups_planner_hook()`
- Examine `parse->targetList`, `parse->rtable`, `parse->groupClause`
- Use `pprint(node)` in LLDB (with PostgreSQL formatters)

**Compare query trees before/after**:
- Use `nodeToString(parse)` to dump query tree as string
- Log before and after rewrite to see differences

---

### Known Limitations (Stage 1)

1. **No WHERE clause handling** - Queries with filters will be rewritten but might scan entire rollup
2. **No freshness checking** - Always uses rollup even if stale
3. **Exact match only** - Cannot use day-rollup for week-query (multi-level defer to Stage 2)
4. **Single rollup per query** - Cannot combine multiple rollups
5. **No partial rewrite** - Cannot use rollup for old data + source for recent (real-time layer)

**Why acceptable**: These are all addressed in Stage 2 with custom scan provider

---

### Next Steps After Stage 1

Once basic rewriting works:

1. **Add WHERE clause support** - Rewrite filters on time column to use bucket column
2. **Freshness tracking** - Only rewrite if rollup is fresh enough
3. **Design Stage 2 (Custom Scan Provider)** - Proper architecture for production
4. **Multi-level rollups** - Use hour rollup for hour queries, day rollup for day queries

---

**Document Status**: Design complete, ready for implementation
**Stage**: 1 (planner_hook)
**Next Action**: Implement `query_rewriter.cpp`
