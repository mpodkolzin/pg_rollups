/*
 * query_rewriter.hpp
 *
 * Query rewriting via planner_hook - automatically use rollup tables
 * when queries match rollup definitions.
 *
 * Stage 1: planner_hook approach (simple, quick win)
 * Stage 2: Custom scan provider (future - proper architecture)
 */

#ifndef ROLLUPS_QUERY_REWRITER_HPP
#define ROLLUPS_QUERY_REWRITER_HPP

extern "C" {
#include "postgres.h"
#include "nodes/plannodes.h"
#include "nodes/parsenodes.h"
#include "optimizer/planner.h"
}

namespace rollups {

/*
 * Planner hook function - intercepts queries and rewrites them to use
 * rollup tables when possible.
 *
 * Called by PostgreSQL after parse/analyze, before optimization.
 * We pattern match the Query tree, find matching rollups, rewrite in place,
 * then chain to standard_planner().
 */
extern "C" PlannedStmt *rollups_planner_hook(Query *parse,
                                              const char *query_string,
                                              int cursorOptions,
                                              ParamListInfo boundParams);

/*
 * Check if a query is eligible for rollup rewriting.
 *
 * Returns true if:
 * - SELECT with GROUP BY
 * - Has aggregates
 * - Exactly one table (no JOINs)
 * - No subqueries/CTEs
 *
 * Conservative: better to skip than rewrite incorrectly.
 */
bool is_rollup_eligible_query(Query *parse);

/*
 * Find a rollup that matches this query.
 *
 * Matches on:
 * - Source table OID
 * - time_bucket() interval in GROUP BY
 *
 * Returns NULL if no match found.
 */
struct ContinuousAggregateData *find_matching_rollup(Query *parse);

/*
 * Rewrite query tree in-place to use rollup table instead of source.
 *
 * Modifies:
 * - RangeTblEntry: swap source table OID for rollup table OID
 * - TargetEntry list: map aggregates to rollup columns
 * - GROUP BY clause: reference rollup columns
 *
 * Does NOT modify WHERE clause in Stage 1 (deferred to Stage 2).
 */
void rewrite_query_for_rollup(Query *parse, ContinuousAggregateData *agg);

} // namespace rollups

#endif // ROLLUPS_QUERY_REWRITER_HPP
