#### Session 10: 2026-05-20 - Function API and MaterializationEngine (Phase 3)
- **Status**: ✅ End-to-end create / refresh / drop of continuous aggregates working
- **Design decision (key finding)**:
  - A custom `CREATE CONTINUOUS AGGREGATE` statement is **not feasible** as an
    extension. `ProcessUtility_hook` runs *after* the parser; PostgreSQL's
    fixed Bison grammar rejects unknown keyword sequences at raw-parse time, so
    the hook never sees them. Real new keywords would require forking the
    grammar (rejected by Design Decision #1).
  - Chose a **function-based API** over piggybacking on `CREATE MATERIALIZED
    VIEW ... WITH (...)` (TimescaleDB's approach). Function API is the simplest
    correct option and unblocks the materialization engine immediately. The
    matview-DDL route remains a possible future upgrade.
  - Recorded as Design Decision #9 in CLAUDE.md.
- **Completed**:
  - ✅ **SQL surface** (`sql/rollups--1.0.sql`):
    - Added `select_clause TEXT NOT NULL` column to `continuous_aggregates`
      and to the `rollup_info` view.
    - Replaced the temporary `test_*` functions with the real API:
      `create_continuous_aggregate`, `refresh_continuous_aggregate`,
      `drop_continuous_aggregate`.
  - ✅ **types.hpp**: added `char *select_clause` to `ContinuousAggregateData`;
    removed the now-unused `CreateContinuousAggregateStmt` struct (no DDL
    parsing in the function-API design, so no QueryParser was built).
  - ✅ **CatalogManager**:
    - `create()` takes `select_clause`; real `Interval*`→text conversion via
      `interval_out` (was hardcoded `'1 hour'`).
    - `update_last_refresh()` now uses the passed timestamp (via
      `timestamptz_out`) instead of `now()`.
    - Implemented `delete_agg()`.
    - `tuple_to_data()` reads `select_clause` (field 12).
  - ✅ **MaterializationEngine** (NEW — `materialization_engine.{hpp,cpp}`):
    - `build_select_query()` generates
      `SELECT rollups.time_bucket(interval 'X', col::timestamp) AS bucket,
       <select_clause> FROM <source> GROUP BY 1`.
    - `initial_populate()` runs `CREATE TABLE <matview> AS <query>`.
    - `refresh()` does a Phase 3 full recompute (`TRUNCATE` + `INSERT ... SELECT`).
    - `drop_matview()` runs `DROP TABLE IF EXISTS`.
    - `ContinuousAggregate::refresh/initial_populate/drop` wired to it.
  - ✅ **rollups.cpp**: replaced the 3 `test_*` extern "C" functions with the 3
    real management functions; removed now-unused `jsonb.h`/`stringinfo.h`
    includes; updated the stale `ProcessUtility_hook` TODO comment.
  - ✅ **CMakeLists.txt**: added `src/materialization_engine.cpp`.
- **Bugs fixed along the way**:
  - **SPI memory-context bug** in `CatalogManager::load()`/`load_by_oid()`:
    `tuple_to_data()` palloc'd the result in SPI's procedure context, which
    `SPI_finish()` destroys — a latent use-after-free. Now the result is built
    in the caller's context (capture `CurrentMemoryContext`, switch around the
    `tuple_to_data()` call). Worked by luck in Session 9's quick tests.
  - **SPI snapshot bug**: `load()`/`exists()` used `read_only = true`, reusing
    the outer statement's snapshot, so they could not see the catalog row
    `create()` had just inserted in the same transaction. Switched to
    `read_only = false` (fresh snapshot per call).
  - **Missing header**: `interval_out`/`timestamptz_out` live in
    `utils/fmgrprotos.h` (pulled in via `utils/builtins.h`).
- **Security**: all user-supplied string values in catalog SQL are escaped
  with `quote_literal_cstr()` (no SQL injection via `select_clause` etc.).
- **Verified in `rollups_test`**:
  - create → matview built with correct bucketed aggregates (SUM, COUNT)
  - refresh → recomputes, picks up new rows and new buckets
  - drop → matview and catalog row both removed
  - error paths: duplicate create, refresh of missing aggregate
  - `timestamptz` source column works via the `::timestamp` cast
- **Phase 3 deliberate limitations** (not over-built):
  - Grouping is time-bucket only (no extra `GROUP BY` dimensions yet)
  - Refresh is full recompute (incremental refresh is Phase 4)
  - No automatic query rewriting yet (users query the matview directly)
- **Known stale artifact**: `scripts/test_catalog.sh` still references the
  removed `test_*` functions — needs updating or removal.
- **Next Steps**:
  - Query rewriting so queries against the source transparently use the matview
  - Phase 4: incremental refresh from the `last_refresh` watermark
