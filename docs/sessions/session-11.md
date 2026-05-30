# Session 11: Query Rewriting Stage 1 (planner_hook)
**Date**: 2026-05-29
**Phase**: Phase 3 - Core Rollup Engine
**Focus**: Automatic query rewriting to use materialized rollup tables

---

## Goals

Implement transparent query rewriting so users can query the source table and PostgreSQL automatically uses the rollup table when possible.

**Two-stage approach**:
- **Stage 1 (this session)**: planner_hook for quick win, validate pattern matching
- **Stage 2 (future)**: Custom scan provider for proper architecture

---

## Work Completed

### 1. Design Documentation
- Created `docs/QUERY_REWRITING.md` with comprehensive design for both stages
- Documented pattern matching rules, query tree structures, and testing strategy
- Defined two-stage approach: planner_hook (Stage 1) → custom scan provider (Stage 2)

### 2. Implementation Started: Query Rewriter Header

**Files created**:
- `include/rollups/query_rewriter.hpp` - Function declarations and interface

**Interface defined**:
- `rollups_planner_hook()` - Main entry point (extern "C" for PostgreSQL)
- `is_rollup_eligible_query()` - Pattern matching to detect rewritable queries
- `find_matching_rollup()` - Catalog lookup for matching rollup definition
- `rewrite_query_for_rollup()` - Query tree surgery to swap tables

### 3. Files Still Needed (Next Session)

**Implementation file**:
- `src/query_rewriter.cpp` - Implement all functions declared in header

**Modifications needed**:
- `src/rollups.cpp` - Register planner_hook in _PG_init()/_PG_fini()
- `src/catalog_manager.cpp` - Add helper `find_by_source_and_bucket()`
- `CMakeLists.txt` - Add query_rewriter.cpp to sources

**Testing**:
- SQL test script to verify rewriting works
- EXPLAIN output comparison (before/after rewrite)
- Debug logging to confirm hook fires

---

## PostgreSQL Concepts Learned

### Query Tree Structures
- `Query` struct (commandType, rtable, targetList, groupClause)
- `RangeTblEntry` (RTE) - represents tables in FROM clause
- `TargetEntry` - SELECT list items
- `Aggref` - aggregate function calls
- `FuncExpr` - function calls (time_bucket)
- `Var` - column references

### Hook Mechanics
- `planner_hook` fires after parse/analyze, before optimization
- Hook chaining pattern (save previous, call in our hook)
- Calling `standard_planner()` to continue normal flow

### Query Tree Manipulation
- Walking lists with `foreach(lc, list)`
- Type checking with `IsA(node, NodeTag)`
- Modifying query tree in-place (uses query memory context)

---

## Testing

[To be added as implementation progresses]

---

## Next Session: Implementation Checklist

### 1. Implement Query Rewriter (`src/query_rewriter.cpp`)
**Functions to implement**:
- [ ] `rollups_planner_hook()` - Main hook, detect eligible queries, rewrite, chain to standard_planner
- [ ] `is_rollup_eligible_query()` - Check commandType, groupClause, hasAggs, rtable length
- [ ] `find_matching_rollup()` - Extract source table OID, find time_bucket interval, query catalog
- [ ] `rewrite_query_for_rollup()` - Swap RTE, rewrite target list, update GROUP BY

**PostgreSQL APIs to use**:
- `linitial(list)`, `foreach(lc, list)` - List iteration
- `IsA(node, Aggref)`, `IsA(node, FuncExpr)` - Node type checking
- `get_rel_name(Oid)` - Table name from OID
- `standard_planner()` - Chain to normal planner

### 2. Register Hook (`src/rollups.cpp`)
- [ ] Add `static planner_hook_type prev_planner_hook = NULL;`
- [ ] In `_PG_init()`: save previous hook, install ours
- [ ] In `_PG_fini()`: restore previous hook

### 3. Add Catalog Helper (`src/catalog_manager.cpp`)
- [ ] `CatalogManager::find_by_source_and_bucket(Oid source_oid, const char *bucket_width)`
- [ ] Query `rollups.continuous_aggregates` with WHERE clause
- [ ] Return `ContinuousAggregateData*` or NULL

### 4. Update Build System
- [ ] Add `src/query_rewriter.cpp` to `CMakeLists.txt`
- [ ] Rebuild: `make clean && make && make install`

### 5. Testing
- [ ] Create test rollup
- [ ] Enable debug logging: `SET client_min_messages = DEBUG1;`
- [ ] Run matching query, verify "Rewrote query to use rollup" message
- [ ] Run EXPLAIN, verify it scans rollup table not source
- [ ] Run non-matching query, verify no rewrite

**Start in Code Mode** - design is complete, just implement what's documented.

---

## Notes & Insights

**Why staged approach?**
- Stage 1 proves concept quickly, validates pattern matching logic
- Stage 2 teaches "the right way" with custom scan providers
- Natural refactoring exercise (common in real projects)
- Both approaches have value for learning PostgreSQL internals

**Query rewriting complexity**:
- Must handle various query shapes (GROUP BY, aggregates, filters)
- Need to preserve query semantics exactly
- Fallback to source table if unsure (conservative approach)

---

**Session Status**: Paused (header complete, implementation file next)
**Next Session**: Start in Code Mode - implement query_rewriter.cpp, register hook, test
**Resume at**: Implementation checklist above
