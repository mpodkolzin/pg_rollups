/*
 * catalog_manager.cpp
 *
 * Implementation of CatalogManager - CRUD operations on catalog table
 *
 * This file uses PostgreSQL's SPI (Server Programming Interface) to execute SQL.
 * SPI is the standard way for extensions to query system catalogs and tables.
 *
 * Key SPI functions used:
 * - SPI_connect() / SPI_finish() - Start/end SPI session
 * - SPI_execute() - Execute SQL query
 * - SPI_getvalue() - Extract field value from result tuple
 * - SPI_processed - Number of rows affected/returned
 * - SPI_tuptable - Result set from query
 */

extern "C" {
#include "postgres.h"
#include "fmgr.h"
#include "executor/spi.h"        // Server Programming Interface
#include "lib/stringinfo.h"      // StringInfoData for building queries
#include "utils/builtins.h"      // text_to_cstring, quote_literal_cstr
#include "utils/lsyscache.h"     // get_namespace_name
#include "utils/timestamp.h"     // TimestampTz, Interval, interval_out
#include "catalog/namespace.h"   // RangeVarGetRelid
#include "nodes/makefuncs.h"     // makeRangeVar
#include "catalog/pg_type.h"     // Type OIDs (TEXTOID, INT4OID, etc.)
#include "access/htup_details.h" // HeapTuple macros
#include "utils/syscache.h"      // SearchSysCache
}

#include "rollups/catalog_manager.hpp"
#include <cstring>  // For sprintf

namespace rollups {

/*
 * load - Load aggregate by name from catalog
 *
 * Algorithm:
 * 1. Connect to SPI
 * 2. Execute SELECT query with name filter
 * 3. Check if row exists
 * 4. Convert HeapTuple to ContinuousAggregateData*
 * 5. Disconnect SPI
 * 6. Return result
 *
 * SPI Notes:
 * - Must call SPI_connect() before any SPI operations
 * - SPI_finish() must be called before returning (even on error)
 * - SPI allocates memory in SPI context, we copy to CurrentMemoryContext
 */
ContinuousAggregateData*
CatalogManager::load(const char *agg_name)
{
    int ret;
    char query[512];

    /*
     * Remember the caller's memory context BEFORE connecting to SPI.
     *
     * SPI_connect() switches CurrentMemoryContext to a private SPI context
     * that SPI_finish() destroys. Anything we palloc while connected lives in
     * that doomed context - so the result struct must be built in the caller's
     * context instead, or it becomes a dangling pointer the moment we return.
     */
    MemoryContext caller_ctx = CurrentMemoryContext;

    /* Connect to SPI - required before any SPI operations */
    ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SPI_connect failed: %d", ret)));

    /*
     * Build SELECT query.
     *
     * quote_literal_cstr() escapes the aggregate name into a safe SQL string
     * literal (quotes included), so an embedded quote can't break the query
     * or inject SQL.
     */
    snprintf(query, sizeof(query),
             "SELECT agg_id, agg_name, view_name, source_table_oid, "
             "       source_table_name, time_column, bucket_interval, "
             "       matview_name, created_at, last_refresh, is_active, "
             "       select_clause "
             "FROM rollups.continuous_aggregates "
             "WHERE agg_name = %s",
             quote_literal_cstr(agg_name));

    /*
     * Execute query.
     *
     * read_only = false: this takes a fresh snapshot, so the SELECT sees rows
     * inserted earlier in the SAME transaction (e.g. create() inserts the
     * catalog row, then we immediately load it back). With read_only = true
     * SPI would reuse the outer statement's snapshot and miss them.
     */
    ret = SPI_execute(query, false, 1 /* max rows */);
    if (ret != SPI_OK_SELECT)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SELECT from rollups.continuous_aggregates failed: %d", ret)));
    }

    /* Check if aggregate exists */
    if (SPI_processed == 0)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_OBJECT),
                 errmsg("continuous aggregate \"%s\" does not exist", agg_name),
                 errhint("Check rollups.rollup_info view for available aggregates.")));
    }

    /*
     * Convert tuple to our struct, building the result in the caller's
     * context so it survives SPI_finish().
     */
    HeapTuple tuple = SPI_tuptable->vals[0];
    TupleDesc tupdesc = SPI_tuptable->tupdesc;

    MemoryContext spi_ctx = MemoryContextSwitchTo(caller_ctx);
    ContinuousAggregateData *data = tuple_to_data(tuple, tupdesc);
    MemoryContextSwitchTo(spi_ctx);

    /* Disconnect SPI */
    SPI_finish();

    return data;
}

/*
 * load_by_oid - Load aggregate by OID from catalog
 *
 * Same logic as load() but filters by agg_id instead of agg_name
 */
ContinuousAggregateData*
CatalogManager::load_by_oid(Oid agg_oid)
{
    int ret;
    char query[512];

    /* See load() for why we capture the caller's context here. */
    MemoryContext caller_ctx = CurrentMemoryContext;

    ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SPI_connect failed: %d", ret)));

    snprintf(query, sizeof(query),
             "SELECT agg_id, agg_name, view_name, source_table_oid, "
             "       source_table_name, time_column, bucket_interval, "
             "       matview_name, created_at, last_refresh, is_active, "
             "       select_clause "
             "FROM rollups.continuous_aggregates "
             "WHERE agg_id = %u",
             agg_oid);

    /* read_only = false: see also the note in load(). */
    ret = SPI_execute(query, false, 1);
    if (ret != SPI_OK_SELECT)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SELECT from rollups.continuous_aggregates failed")));
    }

    if (SPI_processed == 0)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_OBJECT),
                 errmsg("continuous aggregate with OID %u does not exist", agg_oid)));
    }

    HeapTuple tuple = SPI_tuptable->vals[0];
    TupleDesc tupdesc = SPI_tuptable->tupdesc;

    MemoryContext spi_ctx = MemoryContextSwitchTo(caller_ctx);
    ContinuousAggregateData *data = tuple_to_data(tuple, tupdesc);
    MemoryContextSwitchTo(spi_ctx);

    SPI_finish();

    return data;
}

/*
 * exists - Check if aggregate exists
 *
 * Simple existence check - returns bool, doesn't throw error if not found
 */
bool
CatalogManager::exists(const char *agg_name)
{
    int ret;
    char query[256];
    bool result;

    ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SPI_connect failed")));

    snprintf(query, sizeof(query),
             "SELECT 1 FROM rollups.continuous_aggregates WHERE agg_name = %s",
             quote_literal_cstr(agg_name));

    /* read_only = false: see also the note in load(). */
    ret = SPI_execute(query, false, 1);
    if (ret != SPI_OK_SELECT)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SELECT from rollups.continuous_aggregates failed")));
    }

    result = (SPI_processed > 0);

    SPI_finish();

    return result;
}

/*
 * create - Create new aggregate in catalog
 *
 * This is more complex than other methods because we need to:
 * 1. Look up the source table OID
 * 2. Generate a unique matview name
 * 3. Insert the new row
 * 4. Return the generated agg_id
 *
 * Note: PostgreSQL will auto-generate agg_id (it's a SERIAL column)
 */
Oid
CatalogManager::create(
    const char *agg_name,
    const char *source_table,
    const char *time_column,
    Interval *bucket_interval,
    const char *select_clause)
{
    int ret;
    Oid source_table_oid;
    char *matview_name;
    Oid new_agg_id;

    /* Check if aggregate already exists */
    if (exists(agg_name))
    {
        ereport(ERROR,
                (errcode(ERRCODE_DUPLICATE_OBJECT),
                 errmsg("continuous aggregate \"%s\" already exists", agg_name),
                 errhint("Use a different name or drop the existing aggregate first.")));
    }

    /* Look up source table OID */
    source_table_oid = lookup_table_oid(source_table);

    /* Generate materialization table name */
    matview_name = generate_matview_name(agg_name);

    /*
     * Render the Interval* as canonical text via the type's output function.
     * interval_out is the same routine PostgreSQL uses to display an interval,
     * so the result re-parses cleanly back inside the INSERT.
     */
    char *interval_str = DatumGetCString(
        DirectFunctionCall1(interval_out, IntervalPGetDatum(bucket_interval)));

    ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SPI_connect failed")));

    /*
     * Build the INSERT with StringInfo: select_clause is free-form text and
     * may be longer than any fixed buffer. quote_literal_cstr() escapes every
     * user-supplied value into a safe SQL string literal, so embedded quotes
     * cannot break the statement or inject SQL. RETURNING agg_id hands back
     * the sequence-generated id.
     */
    StringInfoData buf;
    initStringInfo(&buf);
    appendStringInfo(&buf,
             "INSERT INTO rollups.continuous_aggregates "
             "(agg_name, view_name, source_table_oid, source_table_name, "
             " time_column, bucket_interval, matview_name, created_at, "
             " last_refresh, is_active, select_clause) "
             "VALUES (%s, %s, %u, %s, %s, interval %s, %s, "
             "        now(), '-infinity'::timestamptz, true, %s) "
             "RETURNING agg_id",
             quote_literal_cstr(agg_name),
             quote_literal_cstr(agg_name),
             source_table_oid,
             quote_literal_cstr(source_table),
             quote_literal_cstr(time_column),
             quote_literal_cstr(interval_str),
             quote_literal_cstr(matview_name),
             quote_literal_cstr(select_clause));

    /* Execute INSERT */
    ret = SPI_execute(buf.data, false /* not read-only */, 0);
    if (ret != SPI_OK_INSERT_RETURNING)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("INSERT into rollups.continuous_aggregates failed: %d", ret)));
    }

    /* Get the generated agg_id from RETURNING clause */
    if (SPI_processed != 1)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("INSERT returned unexpected number of rows: %lu", SPI_processed)));
    }

    HeapTuple tuple = SPI_tuptable->vals[0];
    bool isnull;
    Datum oid_datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc, 1, &isnull);

    if (isnull)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("INSERT RETURNING returned NULL agg_id")));
    }

    new_agg_id = DatumGetObjectId(oid_datum);

    SPI_finish();

    return new_agg_id;
}

/*
 * update_last_refresh - Update watermark timestamp
 *
 * Simple UPDATE statement
 */
void
CatalogManager::update_last_refresh(Oid agg_oid, TimestampTz refresh_time)
{
    int ret;
    char query[256];

    /*
     * Render the timestamp via its output function so it re-parses exactly
     * (no precision loss, correct timezone handling).
     */
    char *ts_str = DatumGetCString(
        DirectFunctionCall1(timestamptz_out, TimestampTzGetDatum(refresh_time)));

    ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SPI_connect failed")));

    snprintf(query, sizeof(query),
             "UPDATE rollups.continuous_aggregates "
             "SET last_refresh = %s::timestamptz "
             "WHERE agg_id = %u",
             quote_literal_cstr(ts_str), agg_oid);

    ret = SPI_execute(query, false, 0);
    if (ret != SPI_OK_UPDATE)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("UPDATE rollups.continuous_aggregates failed")));
    }

    SPI_finish();
}

/*
 * delete_agg - Remove an aggregate's catalog row
 *
 * Deletes only the metadata row. The caller is responsible for dropping the
 * materialization table (see MaterializationEngine::drop_matview).
 */
void
CatalogManager::delete_agg(Oid agg_oid)
{
    int ret;
    char query[256];

    ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SPI_connect failed")));

    snprintf(query, sizeof(query),
             "DELETE FROM rollups.continuous_aggregates WHERE agg_id = %u",
             agg_oid);

    ret = SPI_execute(query, false, 0);
    if (ret != SPI_OK_DELETE)
    {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("DELETE from rollups.continuous_aggregates failed")));
    }

    SPI_finish();
}

/*
 * tuple_to_data - Convert HeapTuple to ContinuousAggregateData*
 *
 * This extracts all fields from the SPI result tuple and builds our struct.
 *
 * SPI field extraction:
 * - SPI_getbinval() - Get field value as Datum
 * - SPI_getvalue() - Get field value as C string (text representation)
 * - Must check 'isnull' to see if field is NULL
 * - Field numbers are 1-based (not 0-based!)
 *
 * Memory: Result is palloc'd in CurrentMemoryContext (will survive SPI_finish)
 */
ContinuousAggregateData*
CatalogManager::tuple_to_data(HeapTuple tuple, TupleDesc tupdesc)
{
    /* Allocate the struct */
    ContinuousAggregateData *data =
        (ContinuousAggregateData *) palloc0(sizeof(ContinuousAggregateData));

    bool isnull;

    /* Extract fields (1-based indexing!) */

    /* agg_id (field 1) */
    Datum oid_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
    data->agg_id = isnull ? InvalidOid : DatumGetObjectId(oid_datum);

    /* agg_name (field 2) */
    char *agg_name = SPI_getvalue(tuple, tupdesc, 2);
    if (agg_name)
        namestrcpy(&data->agg_name, agg_name);

    /* view_name (field 3) */
    char *view_name = SPI_getvalue(tuple, tupdesc, 3);
    if (view_name)
        namestrcpy(&data->view_name, view_name);

    /* source_table_oid (field 4) */
    oid_datum = SPI_getbinval(tuple, tupdesc, 4, &isnull);
    data->source_table_oid = isnull ? InvalidOid : DatumGetObjectId(oid_datum);

    /* source_table_name (field 5) */
    char *source_table = SPI_getvalue(tuple, tupdesc, 5);
    if (source_table)
        namestrcpy(&data->source_table_name, source_table);

    /* time_column (field 6) */
    char *time_col = SPI_getvalue(tuple, tupdesc, 6);
    if (time_col)
        namestrcpy(&data->time_column, time_col);

    /* bucket_interval (field 7) - more complex, need to parse */
    Datum interval_datum = SPI_getbinval(tuple, tupdesc, 7, &isnull);
    if (!isnull)
    {
        /* Copy the interval to palloc'd memory */
        Interval *src_interval = DatumGetIntervalP(interval_datum);
        data->bucket_interval = (Interval *) palloc(sizeof(Interval));
        memcpy(data->bucket_interval, src_interval, sizeof(Interval));
    }
    else
    {
        data->bucket_interval = NULL;
    }

    /* matview_name (field 8) */
    char *matview = SPI_getvalue(tuple, tupdesc, 8);
    if (matview)
        namestrcpy(&data->matview_name, matview);

    /* created_at (field 9) */
    Datum ts_datum = SPI_getbinval(tuple, tupdesc, 9, &isnull);
    data->created_at = isnull ? 0 : DatumGetTimestampTz(ts_datum);

    /* last_refresh (field 10) */
    ts_datum = SPI_getbinval(tuple, tupdesc, 10, &isnull);
    data->last_refresh = isnull ? 0 : DatumGetTimestampTz(ts_datum);

    /* is_active (field 11) */
    Datum bool_datum = SPI_getbinval(tuple, tupdesc, 11, &isnull);
    data->is_active = isnull ? false : DatumGetBool(bool_datum);

    /*
     * select_clause (field 12) - free-form text, stored as a separate
     * palloc'd string. pstrdup copies it into CurrentMemoryContext (the
     * caller's context, set by load()/load_by_oid()).
     */
    char *select_clause = SPI_getvalue(tuple, tupdesc, 12);
    data->select_clause = select_clause ? pstrdup(select_clause) : NULL;

    return data;
}

/*
 * lookup_table_oid - Get OID of a table by name
 *
 * Uses PostgreSQL's RangeVarGetRelid which handles:
 * - Unqualified names (searches search_path)
 * - Qualified names (schema.table)
 * - Checks if relation exists and is accessible
 */
Oid
CatalogManager::lookup_table_oid(const char *table_name)
{
    RangeVar *rv;
    Oid relid;

    /*
     * makeRangeVarFromNameList would be ideal here, but for simplicity
     * we'll use makeRangeVar with NULL schema (searches search_path)
     *
     * TODO: Parse schema.table syntax properly
     */
    rv = makeRangeVar(NULL /* schema */, pstrdup(table_name), -1);

    /*
     * RangeVarGetRelid looks up the table OID
     * NoLock means we don't need a table lock (just looking up metadata)
     * false means "don't skip if table is missing" (throw error instead)
     */
    relid = RangeVarGetRelid(rv, NoLock, false);

    return relid;
}

/*
 * generate_matview_name - Create unique materialization table name
 *
 * Format: rollups._materialized_<agg_name>
 *
 * The underscore prefix makes it an internal table (convention)
 */
char*
CatalogManager::generate_matview_name(const char *agg_name)
{
    char *result = (char *) palloc(256);
    snprintf(result, 256, "rollups._materialized_%s", agg_name);
    return result;
}

} // namespace rollups
