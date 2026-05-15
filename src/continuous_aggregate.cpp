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

// Forward declaration (MaterializationEngine not implemented yet)
namespace rollups {
    class MaterializationEngine;
}

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
 * refresh - Incremental refresh
 *
 * TODO: Implement when MaterializationEngine is ready
 * For now, just a placeholder that reports an error
 */
void
ContinuousAggregate::refresh()
{
    ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
             errmsg("refresh() not yet implemented"),
             errhint("MaterializationEngine is coming in Phase 3")));
}

/*
 * initial_populate - First-time population
 *
 * TODO: Implement when MaterializationEngine is ready
 */
void
ContinuousAggregate::initial_populate()
{
    ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
             errmsg("initial_populate() not yet implemented"),
             errhint("MaterializationEngine is coming in Phase 3")));
}

/*
 * drop - Delete aggregate
 *
 * TODO: Implement - should drop matview and delete catalog entry
 */
void
ContinuousAggregate::drop()
{
    ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
             errmsg("drop() not yet implemented")));
}

} // namespace rollups
