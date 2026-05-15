/*
 * continuous_aggregate.hpp
 *
 * ContinuousAggregate - Main wrapper class for continuous aggregates
 *
 * This is a lightweight wrapper around ContinuousAggregateData*.
 * It provides a clean C++ interface for working with continuous aggregates.
 *
 * Design pattern: Stack-allocated wrapper around palloc'd data
 * Memory: The wrapper lives on stack, the data it points to is palloc'd
 */

#ifndef ROLLUPS_CONTINUOUS_AGGREGATE_HPP
#define ROLLUPS_CONTINUOUS_AGGREGATE_HPP

#include "types.hpp"

namespace rollups {

/*
 * ContinuousAggregate - Wrapper for continuous aggregate operations
 *
 * Lifetime: Stack-allocated, short-lived (function scope)
 * Memory: Wraps a ContinuousAggregateData* that lives in PostgreSQL memory context
 *
 * The destructor is a no-op - PostgreSQL's memory context handles cleanup.
 *
 * Usage:
 *   rollups::ContinuousAggregate agg("my_rollup");  // Loads from catalog
 *   if (agg.is_active()) {
 *       agg.refresh();  // Incremental refresh
 *   }
 *   // Object goes out of scope, no cleanup needed
 */
class ContinuousAggregate {
public:
    /*
     * Constructor: Load aggregate from catalog by name
     *
     * Calls CatalogManager::load() internally.
     *
     * Parameters:
     *   name - Name of the aggregate to load
     *
     * Errors: ereport(ERROR) if aggregate doesn't exist
     *
     * Usage:
     *   ContinuousAggregate agg("hourly_sales");
     */
    explicit ContinuousAggregate(const char *name);

    /*
     * Constructor: Wrap existing data (no catalog load)
     *
     * Use this when you already have the data (e.g., from list_all()).
     *
     * Parameters:
     *   data - Pointer to aggregate data (must be valid, non-NULL)
     *
     * Usage:
     *   ContinuousAggregateData *data = CatalogManager::load("my_agg");
     *   ContinuousAggregate agg(data);
     */
    explicit ContinuousAggregate(ContinuousAggregateData *data);

    /*
     * Destructor: No-op (memory context handles cleanup)
     *
     * The data_ pointer remains valid as long as its memory context is alive.
     * We don't call pfree() because the context will reset automatically.
     */
    ~ContinuousAggregate() = default;

    /*
     * Copy operations: Deleted to prevent accidental copies
     *
     * Copying would be expensive (need to copy all data) and confusing
     * (two wrappers pointing to same data - who owns it?).
     *
     * If you need to pass around aggregates, use references or pointers.
     */
    ContinuousAggregate(const ContinuousAggregate&) = delete;
    ContinuousAggregate& operator=(const ContinuousAggregate&) = delete;

    /*
     * Move operations: Default (cheap pointer copy)
     *
     * Moving just copies the pointer - no data is moved.
     * The moved-from object becomes invalid (data_ = nullptr).
     */
    ContinuousAggregate(ContinuousAggregate&&) = default;
    ContinuousAggregate& operator=(ContinuousAggregate&&) = default;

    /*
     * Accessors: Inline for zero-cost abstraction
     *
     * These provide convenient access to the underlying data fields.
     * Being inline means they compile to the same code as direct field access.
     */

    inline Oid get_id() const {
        return data_->agg_id;
    }

    inline const char* get_name() const {
        return NameStr(data_->agg_name);
    }

    inline const char* get_view_name() const {
        return NameStr(data_->view_name);
    }

    inline Oid get_source_table_oid() const {
        return data_->source_table_oid;
    }

    inline const char* get_source_table() const {
        return NameStr(data_->source_table_name);
    }

    inline const char* get_time_column() const {
        return NameStr(data_->time_column);
    }

    inline Interval* get_bucket_interval() const {
        return data_->bucket_interval;
    }

    inline const char* get_matview_name() const {
        return NameStr(data_->matview_name);
    }

    inline TimestampTz get_created_at() const {
        return data_->created_at;
    }

    inline TimestampTz get_last_refresh() const {
        return data_->last_refresh;
    }

    inline bool is_active() const {
        return data_->is_active;
    }

    /*
     * Operations: Delegate to MaterializationEngine
     *
     * These are the main operations you can perform on an aggregate.
     */

    /*
     * refresh - Incremental refresh from last watermark
     *
     * Only processes data added since last_refresh timestamp.
     * Updates the watermark after successful refresh.
     *
     * Errors: ereport(ERROR) if refresh fails
     */
    void refresh();

    /*
     * initial_populate - First-time population of all data
     *
     * Creates the materialization table and populates it with
     * all historical data from the source table.
     *
     * Should only be called once, right after creating the aggregate.
     *
     * Errors: ereport(ERROR) if population fails
     */
    void initial_populate();

    /*
     * drop - Delete the aggregate and its materialization table
     *
     * 1. Drops the materialization table
     * 2. Deletes the catalog entry
     *
     * After calling this, the aggregate no longer exists.
     * The ContinuousAggregate object becomes invalid.
     *
     * Errors: ereport(ERROR) if drop fails
     */
    void drop();

    /*
     * get_data - Access underlying data pointer
     *
     * Use this when you need to pass the raw data to C code
     * or other functions that work directly with the struct.
     *
     * Returns: Pointer to the underlying ContinuousAggregateData
     *
     * Warning: Don't pfree() this pointer! It's managed by memory context.
     */
    inline ContinuousAggregateData* get_data() const {
        return data_;
    }

private:
    /*
     * The actual data (palloc'd, owned by memory context)
     *
     * This wrapper just holds a pointer to it.
     * Lifetime is managed by PostgreSQL's memory context system.
     */
    ContinuousAggregateData *data_;
};

} // namespace rollups

#endif /* ROLLUPS_CONTINUOUS_AGGREGATE_HPP */
