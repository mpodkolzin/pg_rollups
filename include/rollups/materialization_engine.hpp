/*
 * materialization_engine.hpp
 *
 * MaterializationEngine - builds and refreshes the materialized rollup tables.
 *
 * This is a stateless manager class (all static methods, no instances),
 * following the same pattern as CatalogManager. It uses PostgreSQL's SPI
 * to run the generated DDL/DML against the database.
 *
 * Design pattern: Stateless manager with static methods
 */

#ifndef ROLLUPS_MATERIALIZATION_ENGINE_HPP
#define ROLLUPS_MATERIALIZATION_ENGINE_HPP

#include "types.hpp"

namespace rollups {

/*
 * MaterializationEngine - Populates and refreshes rollup materialization tables
 *
 * A continuous aggregate is backed by an ordinary table (the "matview") that
 * holds the pre-computed per-bucket rows. This engine owns the lifecycle of
 * that table: create+fill it, recompute it, and drop it.
 *
 * Error handling: all methods use ereport(ERROR, ...) on failure.
 */
class MaterializationEngine {
public:
    /*
     * initial_populate - Create the materialization table and fill it
     *
     * Runs `CREATE TABLE <matview> AS <generated query>`, which creates the
     * table and populates it from the source table in a single statement,
     * then advances the last_refresh watermark.
     *
     * Should be called exactly once, right after the aggregate is created.
     *
     * Errors: ereport(ERROR) if the table already exists or the query fails.
     */
    static void initial_populate(const ContinuousAggregateData *data);

    /*
     * refresh - Recompute the materialization table
     *
     * Phase 3 strategy: full recompute (TRUNCATE + INSERT ... SELECT).
     * Incremental refresh from the watermark is planned for Phase 4.
     *
     * Errors: ereport(ERROR) if the matview is missing or the query fails.
     */
    static void refresh(const ContinuousAggregateData *data);

    /*
     * drop_matview - Drop the materialization table
     *
     * Uses `DROP TABLE IF EXISTS` so it is safe to call even if the table
     * was never successfully created.
     *
     * Errors: ereport(ERROR) if the drop fails.
     */
    static void drop_matview(const ContinuousAggregateData *data);

private:
    /* No instances - this is a pure static class. */
    MaterializationEngine() = delete;
    ~MaterializationEngine() = delete;
    MaterializationEngine(const MaterializationEngine&) = delete;
    MaterializationEngine& operator=(const MaterializationEngine&) = delete;

    /*
     * build_select_query - Generate the aggregation SELECT for an aggregate
     *
     * Produces:
     *   SELECT rollups.time_bucket(interval 'X', <time_col>::timestamp) AS bucket,
     *          <select_clause>
     *   FROM <source_table>
     *   GROUP BY 1
     *
     * Returns: palloc'd query string in CurrentMemoryContext.
     */
    static char* build_select_query(const ContinuousAggregateData *data);
};

} // namespace rollups

#endif /* ROLLUPS_MATERIALIZATION_ENGINE_HPP */
