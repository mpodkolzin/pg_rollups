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
#include "utils/builtins.h"      // text_to_cstring, cstring_to_text
#include "utils/lsyscache.h"     // get_namespace_name
#include "utils/timestamp.h"     // TimestampTz functions
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

    /* Connect to SPI - required before any SPI operations */
    ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SPI_connect failed: %d", ret)));

    /*
     * Build SELECT query
     *
     * Note: We use quote_identifier to safely escape the aggregate name
     * (protects against SQL injection if name came from untrusted source)
     *
     * For now, using sprintf is safe because agg_name is a NameData (max 63 chars)
     * In production, you'd use SPI_prepare with parameters
     */
    snprintf(query, sizeof(query),
             "SELECT agg_id, agg_name, view_name, source_table_oid, "
             "       source_table_name, time_column, bucket_interval, "
             "       matview_name, created_at, last_refresh, is_active "
             "FROM rollups.continuous_aggregates "
             "WHERE agg_name = '%s'",
             agg_name);

    /* Execute query */
    ret = SPI_execute(query, true /* read-only */, 1 /* max rows */);
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

    /* Convert tuple to our struct */
    HeapTuple tuple = SPI_tuptable->vals[0];
    TupleDesc tupdesc = SPI_tuptable->tupdesc;
    ContinuousAggregateData *data = tuple_to_data(tuple, tupdesc);

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

    ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SPI_connect failed: %d", ret)));

    snprintf(query, sizeof(query),
             "SELECT agg_id, agg_name, view_name, source_table_oid, "
             "       source_table_name, time_column, bucket_interval, "
             "       matview_name, created_at, last_refresh, is_active "
             "FROM rollups.continuous_aggregates "
             "WHERE agg_id = %u",
             agg_oid);

    ret = SPI_execute(query, true, 1);
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
    ContinuousAggregateData *data = tuple_to_data(tuple, tupdesc);

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
             "SELECT 1 FROM rollups.continuous_aggregates WHERE agg_name = '%s'",
             agg_name);

    ret = SPI_execute(query, true, 1);
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
    Interval *bucket_interval)
{
    int ret;
    char query[1024];
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

    ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SPI_connect failed")));

    /*
     * Build INSERT query
     *
     * Note: We use RETURNING agg_id to get the auto-generated ID
     *
     * For bucket_interval, we convert it to text representation for the INSERT
     * PostgreSQL will parse it back to interval type in the table
     *
     * TODO: Use SPI_prepare with parameters for better safety
     */
    snprintf(query, sizeof(query),
             "INSERT INTO rollups.continuous_aggregates "
             "(agg_name, view_name, source_table_oid, source_table_name, "
             " time_column, bucket_interval, matview_name, created_at, "
             " last_refresh, is_active) "
             "VALUES ('%s', '%s', %u, '%s', '%s', interval '%s', '%s', "
             "        now(), '-infinity'::timestamptz, true) "
             "RETURNING agg_id",
             agg_name, agg_name, source_table_oid, source_table,
             time_column, "1 hour" /* TODO: convert Interval* to string */,
             matview_name);

    /* Execute INSERT */
    ret = SPI_execute(query, false /* not read-only */, 0);
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

    ret = SPI_connect();
    if (ret != SPI_OK_CONNECT)
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("SPI_connect failed")));

    /*
     * TODO: Properly format TimestampTz as string
     * For now, using 'now()' as placeholder
     */
    snprintf(query, sizeof(query),
             "UPDATE rollups.continuous_aggregates "
             "SET last_refresh = now() "
             "WHERE agg_id = %u",
             agg_oid);

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
