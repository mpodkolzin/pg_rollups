/*
 * catalog_manager.hpp
 *
 * CatalogManager - CRUD operations on rollups.continuous_aggregates catalog table
 *
 * This is a stateless manager class (all static methods, no instances).
 * It uses PostgreSQL's SPI (Server Programming Interface) to execute SQL
 * queries on the catalog table.
 *
 * Design pattern: Stateless manager with static methods
 * Memory: All returned data is palloc'd and lives in current memory context
 */

#ifndef ROLLUPS_CATALOG_MANAGER_HPP
#define ROLLUPS_CATALOG_MANAGER_HPP

#include "types.hpp"

/* Forward declarations for PostgreSQL types (used in private methods) */
typedef struct HeapTupleData *HeapTuple;
typedef struct TupleDescData *TupleDesc;

namespace rollups {

/*
 * CatalogManager - Manages continuous aggregate metadata
 *
 * All methods are static - this class is just a namespace with functions.
 * No instances can be created (constructor/destructor are deleted).
 *
 * Thread safety: Not thread-safe, but PostgreSQL backends are single-threaded
 * so this is fine.
 *
 * Error handling: All methods use ereport(ERROR, ...) on failure
 * (never return error codes or throw exceptions).
 */
class CatalogManager {
public:
    /*
     * load - Load aggregate metadata from catalog by name
     *
     * Returns a palloc'd ContinuousAggregateData* or throws ERROR if not found.
     *
     * Memory: Result is palloc'd in CurrentMemoryContext
     * Errors: ereport(ERROR) if aggregate doesn't exist
     *
     * Usage:
     *   auto *data = CatalogManager::load("my_aggregate");
     *   // Use data...
     *   // No need to free - memory context handles it
     */
    static ContinuousAggregateData* load(const char *agg_name);

    /*
     * load_by_oid - Load aggregate metadata by OID
     *
     * Similar to load() but looks up by agg_id instead of name.
     *
     * Memory: Result is palloc'd in CurrentMemoryContext
     * Errors: ereport(ERROR) if aggregate doesn't exist
     */
    static ContinuousAggregateData* load_by_oid(Oid agg_oid);

    /*
     * save - Update existing aggregate metadata in catalog
     *
     * Updates all fields except agg_id (primary key).
     * The aggregate must already exist (use create() for new aggregates).
     *
     * Parameters:
     *   data - Aggregate data to save (must have valid agg_id)
     *
     * Errors: ereport(ERROR) if aggregate doesn't exist or update fails
     */
    static void save(const ContinuousAggregateData *data);

    /*
     * create - Create new aggregate entry in catalog
     *
     * Inserts a new row into rollups.continuous_aggregates table.
     * Generates a unique matview_name automatically.
     *
     * Parameters:
     *   agg_name - Name of the aggregate (must be unique)
     *   source_table - Name of the source table
     *   time_column - Name of the time column
     *   bucket_interval - Bucket width (palloc'd)
     *
     * Returns: OID of the newly created aggregate (agg_id)
     *
     * Errors: ereport(ERROR) if aggregate already exists or creation fails
     */
    static Oid create(
        const char *agg_name,
        const char *source_table,
        const char *time_column,
        Interval *bucket_interval
    );

    /*
     * delete_agg - Delete aggregate entry from catalog
     *
     * Removes the row from rollups.continuous_aggregates table.
     * Does NOT drop the materialization table (caller should do that separately).
     *
     * Parameters:
     *   agg_oid - OID of the aggregate to delete
     *
     * Errors: ereport(ERROR) if aggregate doesn't exist or deletion fails
     */
    static void delete_agg(Oid agg_oid);

    /*
     * update_last_refresh - Update the last_refresh watermark
     *
     * Used after successful refresh to mark the progress.
     * This is frequently called, so it's a separate method (not full save()).
     *
     * Parameters:
     *   agg_oid - OID of the aggregate
     *   refresh_time - New watermark timestamp
     *
     * Errors: ereport(ERROR) if update fails
     */
    static void update_last_refresh(Oid agg_oid, TimestampTz refresh_time);

    /*
     * exists - Check if aggregate exists by name
     *
     * Returns true if an aggregate with this name exists, false otherwise.
     * Does not throw errors.
     *
     * Parameters:
     *   agg_name - Name to check
     *
     * Returns: true if exists, false if not
     */
    static bool exists(const char *agg_name);

    /*
     * list_all - List all continuous aggregates
     *
     * Returns an array of all aggregates in the catalog.
     *
     * Parameters:
     *   count_out - (output) Number of aggregates returned
     *
     * Returns: palloc'd array of ContinuousAggregateData* (or NULL if empty)
     *
     * Memory: Array and all elements are palloc'd in CurrentMemoryContext
     *
     * Usage:
     *   int count;
     *   auto **aggs = CatalogManager::list_all(&count);
     *   for (int i = 0; i < count; i++) {
     *       // Use aggs[i]...
     *   }
     */
    static ContinuousAggregateData** list_all(int *count_out);

private:
    /*
     * No instances allowed - this is a pure static class
     * Deleted constructors ensure compile-time error if someone tries to instantiate
     */
    CatalogManager() = delete;
    ~CatalogManager() = delete;
    CatalogManager(const CatalogManager&) = delete;
    CatalogManager& operator=(const CatalogManager&) = delete;

    /*
     * Helper: Convert SPI HeapTuple to ContinuousAggregateData*
     *
     * Extracts all fields from the tuple and builds a palloc'd struct.
     *
     * Parameters:
     *   tuple - HeapTuple from SPI query result
     *   tupdesc - Tuple descriptor (schema information)
     *
     * Returns: palloc'd ContinuousAggregateData*
     */
    static ContinuousAggregateData* tuple_to_data(HeapTuple tuple, TupleDesc tupdesc);

    /*
     * Helper: Look up source table OID by name
     *
     * Parameters:
     *   table_name - Unqualified or qualified table name
     *
     * Returns: OID of the table
     *
     * Errors: ereport(ERROR) if table doesn't exist
     */
    static Oid lookup_table_oid(const char *table_name);

    /*
     * Helper: Generate unique materialization table name
     *
     * Format: rollups._materialized_<agg_name>
     *
     * Parameters:
     *   agg_name - Name of the aggregate
     *
     * Returns: palloc'd table name
     */
    static char* generate_matview_name(const char *agg_name);
};

} // namespace rollups

#endif /* ROLLUPS_CATALOG_MANAGER_HPP */
