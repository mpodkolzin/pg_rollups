/*
 * materialization_engine.cpp
 *
 * Implementation of MaterializationEngine - builds and refreshes the
 * materialized rollup tables backing each continuous aggregate.
 *
 * All SQL is run through SPI (the Server Programming Interface). The generated
 * statements are ordinary DDL/DML; the only interesting part is build_select_query,
 * which assembles the aggregation query from the catalog metadata.
 */

extern "C" {
#include "postgres.h"
#include "fmgr.h"               // DirectFunctionCall1
#include "executor/spi.h"       // Server Programming Interface
#include "lib/stringinfo.h"     // StringInfoData for building queries
#include "utils/timestamp.h"    // Interval, GetCurrentTimestamp
#include "utils/builtins.h"     // interval_out (via utils/fmgrprotos.h)
}

#include "rollups/materialization_engine.hpp"
#include "rollups/catalog_manager.hpp"

namespace rollups {

/*
 * build_select_query - Assemble the aggregation SELECT for an aggregate
 *
 * The generated query buckets the source rows by time and applies the
 * user-supplied aggregate expressions. It is used both to populate the
 * matview initially and to refresh it.
 */
char*
MaterializationEngine::build_select_query(const ContinuousAggregateData *data)
{
    /*
     * Render the bucket interval as canonical text via interval_out, so it
     * re-parses cleanly inside the generated SQL.
     */
    char *interval_str = DatumGetCString(
        DirectFunctionCall1(interval_out, IntervalPGetDatum(data->bucket_interval)));

    StringInfoData buf;
    initStringInfo(&buf);

    /*
     * Notes on the generated query:
     * - The time column is cast to ::timestamp so a timestamptz source column
     *   also works (rollups.time_bucket only accepts timestamp in Phase 3).
     * - select_clause is inlined raw on purpose - it IS executable SQL
     *   (e.g. "sum(amount) AS total"), supplied when the aggregate was created.
     * - GROUP BY 1 groups on the bucket column (Phase 3 supports time-bucket
     *   grouping only; extra dimensions are a later addition).
     */
    appendStringInfo(&buf,
        "SELECT rollups.time_bucket(interval '%s', %s::timestamp) AS bucket, %s "
        "FROM %s "
        "GROUP BY 1",
        interval_str,
        NameStr(data->time_column),
        data->select_clause,
        NameStr(data->source_table_name));

    return buf.data;
}

/*
 * initial_populate - Create and fill the materialization table
 */
void
MaterializationEngine::initial_populate(const ContinuousAggregateData *data)
{
    int ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SPI_connect failed")));

    char *select_query = build_select_query(data);

    /*
     * CREATE TABLE AS both creates the table and fills it in one statement -
     * the column list is inferred from the SELECT.
     */
    StringInfoData ddl;
    initStringInfo(&ddl);
    appendStringInfo(&ddl, "CREATE TABLE %s AS %s",
                     NameStr(data->matview_name), select_query);

    /*
     * CREATE TABLE AS is a utility statement, so SPI reports a non-SELECT
     * success code. A negative return value is the reliable error signal
     * (an actual SQL error would have ereport'd out before returning here).
     */
    ret = SPI_execute(ddl.data, false /* not read-only */, 0);
    if (ret < 0)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("failed to create materialization table \"%s\" (SPI code %d)",
                        NameStr(data->matview_name), ret)));
    }

    SPI_finish();

    /* Mark the aggregate as materialized up to the current moment. */
    CatalogManager::update_last_refresh(data->agg_id, GetCurrentTimestamp());

    ereport(DEBUG1,
            (errmsg("rollups: populated materialization table \"%s\"",
                    NameStr(data->matview_name))));
}

/*
 * refresh - Recompute the materialization table (Phase 3: full recompute)
 */
void
MaterializationEngine::refresh(const ContinuousAggregateData *data)
{
    int ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SPI_connect failed")));

    /*
     * Phase 3 keeps refresh simple: empty the table and recompute every
     * bucket from scratch. Incremental refresh (only buckets touched since
     * last_refresh) is Phase 4.
     */
    StringInfoData truncate_sql;
    initStringInfo(&truncate_sql);
    appendStringInfo(&truncate_sql, "TRUNCATE TABLE %s",
                     NameStr(data->matview_name));

    ret = SPI_execute(truncate_sql.data, false, 0);
    if (ret < 0)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("failed to truncate materialization table \"%s\"",
                        NameStr(data->matview_name))));
    }

    char *select_query = build_select_query(data);

    StringInfoData insert_sql;
    initStringInfo(&insert_sql);
    appendStringInfo(&insert_sql, "INSERT INTO %s %s",
                     NameStr(data->matview_name), select_query);

    ret = SPI_execute(insert_sql.data, false, 0);
    if (ret < 0)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("failed to refresh materialization table \"%s\"",
                        NameStr(data->matview_name))));
    }

    SPI_finish();

    CatalogManager::update_last_refresh(data->agg_id, GetCurrentTimestamp());

    ereport(DEBUG1,
            (errmsg("rollups: refreshed materialization table \"%s\"",
                    NameStr(data->matview_name))));
}

/*
 * drop_matview - Drop the materialization table
 */
void
MaterializationEngine::drop_matview(const ContinuousAggregateData *data)
{
    int ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SPI_connect failed")));

    /* IF EXISTS makes this safe even if populate never succeeded. */
    StringInfoData ddl;
    initStringInfo(&ddl);
    appendStringInfo(&ddl, "DROP TABLE IF EXISTS %s",
                     NameStr(data->matview_name));

    ret = SPI_execute(ddl.data, false, 0);
    if (ret < 0)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("failed to drop materialization table \"%s\"",
                        NameStr(data->matview_name))));
    }

    SPI_finish();
}

} // namespace rollups
