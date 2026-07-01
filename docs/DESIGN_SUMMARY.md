# Design Summary (Through Session 11)

A consolidated overview of the approach, architecture, and decisions taken so
far. This is a snapshot for quick onboarding — the authoritative sources remain
`CLAUDE.md` (Design Decisions & Rationale) and the per-session notes in
`docs/sessions/`.

**As of**: Session 11 (2026-05-29), Phase 3 — Core Rollup Engine
**Status**: Query rewriting Stage 1 in progress (header defined, implementation next)

---

## The Big Picture

A **PostgreSQL extension** implementing **continuous rollup aggregations** —
TimescaleDB/ClickHouse-style materialized, time-bucketed aggregates. The primary
goal is **learning PostgreSQL internals** through hands-on implementation, so
decisions consistently favor understanding over shipping speed.

Phases 1–2 (foundation, scaffolding, catalog design) are largely complete.
Phase 3 (time bucketing, rollup definitions, materialization, query rewriting)
is the current focus.

---

## Foundational Decisions

| Area | Decision | Rationale |
|------|----------|-----------|
| **Delivery** | Extension, not a fork (#1) | Portable, community best-practice, low adoption barrier |
| **Language** | C++17 with `extern "C"` boundaries (#2) | Namespaces, type safety, STL — but no exceptions, no `new`/`delete`, `palloc` only |
| **Build** | CMake + Makefile wrapper, not PGXS (#6) | IDE integration, `compile_commands.json`, learning value |
| **Dev env** | Native install, debug build, LLDB (#7) | Step through PG core alongside extension code; `-O0 -g3 --enable-cassert` |
| **Metadata** | Custom catalog table `rollups.continuous_aggregates` | Store rollup config as real catalog rows |

---

## Class Architecture — "Hybrid Wrapper" (Decision #8, Session 7)

The core structural pattern, validated against ServiceNow's production PostgreSQL:

- **Data lives in C structs** (e.g. `ContinuousAggregateData`), palloc'd and
  owned by PostgreSQL memory contexts.
- **Operations live in C++ classes** — either stack-allocated thin wrappers or
  stateless static managers.

**Concrete classes**:
- `ContinuousAggregate` — wrapper over `ContinuousAggregateData`
- `CatalogManager` — catalog CRUD, static methods only (no instances)
- `MaterializationEngine` — populate and refresh operations

**Constraints respected throughout**:
- No RAII cleanup (destructors are no-ops)
- No C++ exceptions (use `ereport`)
- No `new`/`delete` (use `palloc`/`pfree`)
- No STL with default allocators

See `docs/CLASS_DESIGN.md` for the full design.

---

## Creation API — SQL Functions, not Custom DDL (Decision #9, Session 10)

**The constraint discovered**: custom DDL keywords cannot be intercepted.
`ProcessUtility_hook` fires *after* the Bison parser, so a brand-new keyword
sequence like `CREATE CONTINUOUS AGGREGATE ...` is a raw-parse syntax error that
never reaches the hook. Real new keywords would require forking PostgreSQL's
grammar — rejected by Decision #1.

**The resulting API** (arguments arrive pre-split, so no DDL string parsing):
- `rollups.create_continuous_aggregate(name, source_table, time_column, bucket_width, select_clause)`
- `rollups.refresh_continuous_aggregate(name)`
- `rollups.drop_continuous_aggregate(name)`

**Future escape hatch** (not chosen now): piggyback on
`CREATE MATERIALIZED VIEW ... WITH (rollups.continuous, ...)`, which *is* valid
grammar so the hook fires — this is TimescaleDB's actual approach.

---

## What Works Today (Phase 3)

- **Time bucketing** — `rollups.time_bucket`, timestamp → bucket via integer
  division. Day-based granularities only (month/year deferred; timezone
  handling partial, currently casts to `::timestamp`).
- **Rollup definitions** — created via the function API, metadata persisted by
  `CatalogManager` into `rollups.continuous_aggregates`.
- **Initial materialization** — `MaterializationEngine` runs `CREATE TABLE AS`
  with the user-supplied `select_clause`.

---

## In Progress — Query Rewriting (Session 11)

**Goal**: transparency — the user queries the source table, PostgreSQL silently
swaps in the rollup table when a query matches a rollup definition.

**Deliberately two-staged**:

- **Stage 1 (in progress)** — `planner_hook`. Intercept after analyze /before
  optimization, pattern-match the `Query` tree, do in-place tree surgery (swap
  the `RangeTblEntry.relid`, rewrite the target list and GROUP BY), then chain
  to `standard_planner()`. Conservative by design: only rewrites single-table
  `SELECT` + `GROUP BY` + aggregate queries with an exact bucket-width match.
  Silently falls through on JOINs, WHERE clauses, subqueries, and mismatched
  intervals.
- **Stage 2 (future)** — a proper **custom scan provider** with cost-based
  selection. Treated as a natural refactoring exercise and a way to learn "the
  right way."

**Concrete state at pause**:
- ✅ `docs/QUERY_REWRITING.md` — full design for both stages
- ✅ `include/rollups/query_rewriter.hpp` — interface defined
- ⬜ `src/query_rewriter.cpp` — implementation (next)
- ⬜ Hook registration in `src/rollups.cpp` (`_PG_init`/`_PG_fini`)
- ⬜ `CatalogManager::find_by_source_and_bucket()` helper
- ⬜ Add `query_rewriter.cpp` to `CMakeLists.txt`
- ⬜ SQL tests + EXPLAIN before/after verification

**Known Stage 1 limitations** (all deferred to Stage 2): no WHERE handling, no
freshness checking, exact-match only, single rollup per query, no partial
rewrite (rollup for old data + source for recent).

---

## Deferred / Still Open

- **Storage strategy** (#3) — still TBD (separate tables vs. custom AM vs.
  columnar); researching TimescaleDB's hypertable+chunk approach.
- **Incremental updates** (Phase 4) — decided to start with triggers, evolve to
  background workers (#4); not yet implemented.
- **Materialization policies** (Phase 5), **multi-level rollups** (Phase 6),
  **compression/retention & advanced aggregates** (Phase 7) — future phases.
- Open questions remain around concurrency during refresh, consistency
  guarantees, schema changes on source tables, and cross-version compatibility.

---

## Key File Map

- `rollups.control`, `sql/rollups--1.0.sql` — extension metadata & SQL install
- `src/rollups.cpp` — entry points, `_PG_init`/`_PG_fini`, hooks
- `src/catalog_manager.cpp` — metadata catalog CRUD
- `src/continuous_aggregate.cpp` — aggregate wrapper
- `src/materialization_engine.cpp` — populate/refresh
- `include/rollups/*.hpp` — headers (incl. `query_rewriter.hpp`)
- `docs/CLASS_DESIGN.md`, `docs/QUERY_REWRITING.md`, `docs/HOOKS_GUIDE.md`,
  `docs/DEBUGGING.md`, `docs/BUILDING.md`
- `docs/sessions/session-01.md … session-11.md` — per-session notes
