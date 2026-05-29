/*
 * continuous_aggregate.cpp
 *
 * Implementation of ContinuousAggregate wrapper class
 *
 * This file is simple - mostly just delegation to other managers.
 * The real work happens in CatalogManager and MaterializationEngine.
 */

extern "C" {
#include "postgres.h"
#include "fmgr.h"
}

#include "rollups/continuous_aggregate.hpp"
#include "rollups/catalog_manager.hpp"
#include "rollups/materialization_engine.hpp"

namespace rollups {

/*
 * Constructor: Load from catalog by name
 *
 * Delegates to CatalogManager::load()
 */
ContinuousAggregate::ContinuousAggregate(const char *name)
{
    data_ = CatalogManager::load(name);
}

/*
 * Constructor: Wrap existing data
 *
 * Simple assignment - no catalog access
 */
ContinuousAggregate::ContinuousAggregate(ContinuousAggregateData *data)
    : data_(data)
{
    /* Validate that data is not NULL */
    if (data_ == nullptr)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("cannot create ContinuousAggregate with NULL data")));
    }
}

/*
 * refresh - Recompute the materialization table
 *
 * Delegates to MaterializationEngine, which also advances the watermark.
 */
void
ContinuousAggregate::refresh()
{
    MaterializationEngine::refresh(data_);
}

/*
 * initial_populate - First-time population of the materialization table
 */
void
ContinuousAggregate::initial_populate()
{
    MaterializationEngine::initial_populate(data_);
}

/*
 * drop - Delete the aggregate and its materialization table
 *
 * Drop the data table first, then remove the catalog row. If the table drop
 * fails, the catalog entry is left intact so the aggregate stays consistent.
 */
void
ContinuousAggregate::drop()
{
    MaterializationEngine::drop_matview(data_);
    CatalogManager::delete_agg(data_->agg_id);
}

} // namespace rollups
