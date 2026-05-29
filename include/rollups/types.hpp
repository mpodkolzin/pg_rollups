/*
 * types.hpp
 *
 * C struct definitions for rollups extension data structures
 *
 * These are pure C structs (not C++ classes) because they need to be:
 * 1. Allocated with palloc (not new)
 * 2. Stored in PostgreSQL memory contexts
 * 3. Passed to C code if needed
 *
 * Design pattern: Data in C structs, operations in C++ classes
 */

#ifndef ROLLUPS_TYPES_HPP
#define ROLLUPS_TYPES_HPP

extern "C" {
#include "postgres.h"
#include "c.h"             // For NameData
#include "utils/timestamp.h"  // For TimestampTz, Interval
}

/*
 * ContinuousAggregateData - Metadata for a continuous aggregate
 *
 * This struct is stored in the rollups.continuous_aggregates catalog table.
 * Each field corresponds to a column in that table.
 *
 * Memory: Allocated with palloc, lives in a PostgreSQL memory context.
 * The struct itself is palloc'd, and bucket_interval is separately palloc'd.
 *
 * Why not a C++ class?
 * - Needs to be POD (Plain Old Data) for PostgreSQL compatibility
 * - Can't have constructors/destructors (memory context manages lifetime)
 * - May be passed to C code
 * - PostgreSQL's SPI returns HeapTuples that we convert to this struct
 */
typedef struct ContinuousAggregateData {
    /*
     * Unique identifier (primary key in catalog table)
     * Corresponds to: rollups.continuous_aggregates.agg_id
     */
    Oid agg_id;

    /*
     * User-visible name of the aggregate
     * NameData is a fixed-size array (NAMEDATALEN = 64 bytes)
     * Corresponds to: rollups.continuous_aggregates.agg_name
     */
    NameData agg_name;

    /*
     * View name (same as agg_name for now, might differ in future)
     * Corresponds to: rollups.continuous_aggregates.view_name
     */
    NameData view_name;

    /*
     * OID of the source table
     * Corresponds to: rollups.continuous_aggregates.source_table_oid
     */
    Oid source_table_oid;

    /*
     * Name of the source table (denormalized for convenience)
     * Avoids catalog lookups during refresh operations
     * Corresponds to: rollups.continuous_aggregates.source_table_name
     */
    NameData source_table_name;

    /*
     * Name of the time column to bucket on
     * Must be a timestamp or timestamptz column in the source table
     * Corresponds to: rollups.continuous_aggregates.time_column
     */
    NameData time_column;

    /*
     * Bucket width interval (e.g., '1 hour', '1 day')
     * This is a POINTER because Interval is a complex struct with multiple fields
     * Must be separately palloc'd
     * Corresponds to: rollups.continuous_aggregates.bucket_interval
     */
    Interval *bucket_interval;

    /*
     * Name of the materialization table where aggregated data is stored
     * Format: rollups._materialized_<agg_name>
     * Corresponds to: rollups.continuous_aggregates.matview_name
     */
    NameData matview_name;

    /*
     * Timestamp when the aggregate was created
     * Corresponds to: rollups.continuous_aggregates.created_at
     */
    TimestampTz created_at;

    /*
     * Last refresh timestamp (watermark)
     * Incremental refresh only processes data after this timestamp
     * Corresponds to: rollups.continuous_aggregates.last_refresh
     */
    TimestampTz last_refresh;

    /*
     * Whether the aggregate is active/enabled
     * Disabled aggregates are not refreshed
     * Corresponds to: rollups.continuous_aggregates.is_active
     */
    bool is_active;

    /*
     * Aggregate select-list: the expressions computed per bucket.
     * Example: "sum(amount) AS total, count(*) AS cnt"
     *
     * This is a separately palloc'd char* (not NameData) because it is
     * free-form SQL text and can exceed the 63-byte NameData limit.
     * Corresponds to: rollups.continuous_aggregates.select_clause
     */
    char *select_clause;

} ContinuousAggregateData;

#endif /* ROLLUPS_TYPES_HPP */
