# C++ Class Design for Rollups Extension

**Date**: 2026-05-15  
**Status**: Approved, ready for implementation  
**Related**: Session 7, Phase 2-3 transition

## Overview

This document describes the C++ class hierarchy for the rollups extension, implementing continuous rollup aggregations for PostgreSQL. The design follows ServiceNow's production-proven approach of using C++ wrapper classes around PostgreSQL's C data structures.

## Design Philosophy

**Key Principle**: Respect PostgreSQL's memory model while leveraging C++ for better code organization.

### Approach: Hybrid C++/C (ServiceNow-style)

- **Data Storage**: C structs (palloc'd, managed by PostgreSQL memory contexts)
- **Operations**: C++ classes (stack-allocated wrappers or stateless managers)
- **No RAII cleanup**: Destructors are no-ops; PostgreSQL memory contexts handle cleanup
- **No exceptions**: Use `ereport(ERROR)` instead of throw/catch
- **No new/delete**: Always use palloc/pfree for PostgreSQL-managed memory

### Why This Approach?

1. **Proven in production** - ServiceNow's custom PostgreSQL uses this pattern extensively
2. **Safe** - Doesn't fight PostgreSQL's memory management or error handling
3. **Better than pure C** - Namespaces, type safety, inline methods, cleaner code organization
4. **Learning value** - Understand PostgreSQL constraints deeply while using modern C++

### Alternatives Considered

**Pure C** (TimescaleDB approach)
- Pros: Simplest, most compatible, standard for PostgreSQL extensions
- Cons: Verbose, harder to organize complex logic, no modern language features
- Why not: We're building this to learn, and C++ teaches more about the constraints

**Full C++ with custom allocators** (aggressive)
- Pros: Can use STL containers, smart pointers, full C++ ecosystem
- Cons: Complex, risky, easy to mess up memory contexts, overkill for learning
- Why not: Too much complexity, fighting PostgreSQL too hard

## Architecture Overview

### File Structure

```
src/
├── rollups.cpp              # Entry point, hooks, SQL functions (existing)
├── catalog_manager.cpp      # Metadata CRUD operations (NEW)
├── materialization.cpp      # Populate and refresh logic (NEW)
├── query_parser.cpp         # Parse CREATE CONTINUOUS AGGREGATE (NEW)
└── continuous_aggregate.cpp # Core wrapper class (NEW)

include/rollups/
├── catalog_manager.hpp      # CatalogManager class
├── materialization.hpp      # MaterializationEngine class
├── query_parser.hpp         # QueryParser class
├── continuous_aggregate.hpp # ContinuousAggregate class
└── types.hpp               # C structs for data storage
```

### Namespace Organization

```cpp
namespace rollups {
    // Core classes
    class ContinuousAggregate;      // Wrapper for continuous aggregate data
    class CatalogManager;           // Metadata persistence
    class MaterializationEngine;    // Refresh operations
    class QueryParser;              // DDL parsing
    
    // Utility namespace (future)
    namespace util {
        // Helper functions for common operations
    }
}
```

### Component Diagram

```
┌─────────────────────────────────────────────────────────────┐
│  ProcessUtility_hook (rollups.cpp)                          │
│  Intercepts CREATE CONTINUOUS AGGREGATE                     │
└───────────────┬─────────────────────────────────────────────┘
                │
                v
┌─────────────────────────────────────────────────────────────┐
│  QueryParser                                                │
│  parse() -> CreateContinuousAggregateStmt                   │
└───────────────┬─────────────────────────────────────────────┘
                │
                v
┌─────────────────────────────────────────────────────────────┐
│  CatalogManager                                             │
│  create() -> Oid                                            │
│  Writes to rollups.continuous_aggregates table              │
└───────────────┬─────────────────────────────────────────────┘
                │
                v
┌─────────────────────────────────────────────────────────────┐
│  ContinuousAggregate (wrapper)                              │
│  Loads ContinuousAggregateData from catalog                 │
└───────────────┬─────────────────────────────────────────────┘
                │
                v
┌─────────────────────────────────────────────────────────────┐
│  MaterializationEngine                                      │
│  initial_populate() or incremental_refresh()                │
│  Executes SQL and populates materialization table           │
└─────────────────────────────────────────────────────────────┘
```

## Data Structures

### ContinuousAggregateData (C struct, palloc'd)

**File**: `include/rollups/types.hpp`

```cpp
typedef struct ContinuousAggregateData {
    Oid         agg_id;              // Row ID in catalog table
    NameData    agg_name;            // User-visible name (63 chars max)
    NameData    view_name;           // View name (same as agg_name initially)
    Oid         source_table_oid;    // Source table OID
    NameData    source_table_name;   // Source table name (denormalized for convenience)
    NameData    time_column;         // Which column to bucket on
    Interval   *bucket_interval;     // Bucket width (palloc'd separately)
    NameData    matview_name;        // Materialization table name
    TimestampTz created_at;          // Creation timestamp
    TimestampTz last_refresh;        // Last refresh timestamp (watermark)
    bool        is_active;           // Whether the aggregate is enabled
} ContinuousAggregateData;
```

**Design Notes**:
- Uses PostgreSQL types: `Oid`, `NameData`, `Interval*`, `TimestampTz`, `bool`
- `NameData` is a fixed-size array (64 bytes), no need to palloc strings separately
- `bucket_interval` is a pointer because `Interval` is a complex structure
- Denormalized `source_table_name` to avoid catalog lookups during refresh
- `last_refresh` is the watermark - only process data after this timestamp

### CreateContinuousAggregateStmt (parse result)

**File**: `include/rollups/query_parser.hpp`

```cpp
typedef struct CreateContinuousAggregateStmt {
    char     *agg_name;           // palloc'd
    char     *source_table;       // palloc'd
    char     *time_column;        // palloc'd
    Interval *bucket_interval;    // palloc'd
    char     *aggregation_query;  // palloc'd (the SELECT ... GROUP BY part)
} CreateContinuousAggregateStmt;
```

**Design Notes**:
- Result of parsing `CREATE CONTINUOUS AGGREGATE` statement
- All fields palloc'd (live in current memory context)
- Short-lived - only exists during DDL command execution
- `aggregation_query` will be used in Phase 3 for query rewriting

## Class Designs

### 1. ContinuousAggregate (Wrapper Class)

**File**: `include/rollups/continuous_aggregate.hpp`, `src/continuous_aggregate.cpp`

**Purpose**: Primary interface for working with continuous aggregates. Wraps `ContinuousAggregateData*`.

**Lifetime**: Stack-allocated, short-lived (function scope)

**Memory**: Wraps a `ContinuousAggregateData*` that lives in PostgreSQL memory context

```cpp
namespace rollups {

class ContinuousAggregate {
public:
    // Constructor: loads from catalog by name
    explicit ContinuousAggregate(const char *name);
    
    // Constructor: wraps existing data (no catalog load)
    explicit ContinuousAggregate(ContinuousAggregateData *data);
    
    // No-op destructor (data lives in memory context)
    ~ContinuousAggregate() = default;
    
    // Prevent copying (pointer semantics)
    ContinuousAggregate(const ContinuousAggregate&) = delete;
    ContinuousAggregate& operator=(const ContinuousAggregate&) = delete;
    
    // Allow moving (cheap pointer copy)
    ContinuousAggregate(ContinuousAggregate&&) = default;
    ContinuousAggregate& operator=(ContinuousAggregate&&) = default;
    
    // Accessors (inline for performance)
    inline Oid get_id() const { return data_->agg_id; }
    inline const char* get_name() const { return NameStr(data_->agg_name); }
    inline const char* get_source_table() const { return NameStr(data_->source_table_name); }
    inline const char* get_time_column() const { return NameStr(data_->time_column); }
    inline Interval* get_bucket_interval() const { return data_->bucket_interval; }
    inline TimestampTz get_last_refresh() const { return data_->last_refresh; }
    inline bool is_active() const { return data_->is_active; }
    
    // Operations
    void refresh();                  // Incremental refresh from last watermark
    void initial_populate();         // First-time population of all data
    void drop();                    // Delete the aggregate and its matview
    
    // Get underlying data (for passing to C code or other classes)
    inline ContinuousAggregateData* get_data() const { return data_; }

private:
    ContinuousAggregateData *data_;  // Owned by PostgreSQL memory context
};

} // namespace rollups
```

**Usage Example**:
```cpp
// Load aggregate from catalog
rollups::ContinuousAggregate agg("my_hourly_rollup");

// Check properties
if (agg.is_active()) {
    // Refresh it
    agg.refresh();
}

// Object goes out of scope - no cleanup needed (data lives in memory context)
```

**Design Rationale**:
- **Deleted copy constructor** - Prevent accidental copies (expensive, can confuse ownership)
- **Default move constructor** - Cheap to move (just copies pointer)
- **Inline accessors** - Zero-cost abstraction for field access
- **No-op destructor** - Memory context cleanup handles the data
- **Two constructors** - Load from catalog or wrap existing data

---

### 2. CatalogManager (Stateless Manager)

**File**: `include/rollups/catalog_manager.hpp`, `src/catalog_manager.cpp`

**Purpose**: CRUD operations on `rollups.continuous_aggregates` catalog table.

**Pattern**: All static methods, no instances (pure namespace with class syntax)

```cpp
namespace rollups {

class CatalogManager {
public:
    // Load aggregate metadata from catalog
    static ContinuousAggregateData* load(const char *agg_name);
    static ContinuousAggregateData* load_by_oid(Oid agg_oid);
    
    // Save aggregate metadata to catalog (update existing row)
    static void save(const ContinuousAggregateData *data);
    
    // Create new aggregate entry in catalog
    static Oid create(
        const char *agg_name,
        const char *source_table,
        const char *time_column,
        Interval *bucket_interval
    );
    
    // Delete aggregate entry from catalog
    static void delete_agg(Oid agg_oid);
    
    // Update last refresh timestamp (watermark)
    static void update_last_refresh(Oid agg_oid, TimestampTz refresh_time);
    
    // Check if aggregate exists
    static bool exists(const char *agg_name);
    
    // List all aggregates (returns palloc'd array)
    static ContinuousAggregateData** list_all(int *count_out);

private:
    // Helper: convert SPI tuple to ContinuousAggregateData*
    static ContinuousAggregateData* tuple_to_data(HeapTuple tuple, TupleDesc tupdesc);
    
    // Helper: build INSERT/UPDATE SQL
    static char* build_insert_sql(const ContinuousAggregateData *data);
    static char* build_update_sql(const ContinuousAggregateData *data);
    
    // No instances allowed
    CatalogManager() = delete;
    ~CatalogManager() = delete;
};

} // namespace rollups
```

**Implementation Notes**:
- Uses PostgreSQL's SPI (Server Programming Interface) for SQL execution
- All SQL queries use prepared statements (safe from SQL injection)
- Memory allocated with palloc (lives in current memory context)
- Errors reported with ereport(ERROR, ...) not exceptions

**Usage Example**:
```cpp
// Check if aggregate exists
if (rollups::CatalogManager::exists("my_rollup")) {
    // Load it
    auto *data = rollups::CatalogManager::load("my_rollup");
    // Use data...
}

// Create new aggregate
Oid new_id = rollups::CatalogManager::create(
    "hourly_sales",
    "sales_transactions",
    "created_at",
    bucket_interval
);
```

---

### 3. MaterializationEngine (Stateless Manager)

**File**: `include/rollups/materialization.hpp`, `src/materialization.cpp`

**Purpose**: Populate and refresh materialization tables with aggregated data.

**Pattern**: All static methods, no instances

```cpp
namespace rollups {

class MaterializationEngine {
public:
    // Initial population: compute all time buckets from source table
    static void initial_populate(const ContinuousAggregate &agg);
    
    // Incremental refresh: compute only new/changed buckets since last watermark
    static void incremental_refresh(const ContinuousAggregate &agg);
    
    // Refresh a specific time range (for backfilling or fixing data)
    static void refresh_range(
        const ContinuousAggregate &agg,
        TimestampTz start_time,
        TimestampTz end_time
    );
    
    // Drop materialization table
    static void drop_matview(const ContinuousAggregate &agg);

private:
    // Build the aggregation SQL query
    static char* build_aggregation_query(
        const ContinuousAggregate &agg,
        TimestampTz start_time,
        TimestampTz end_time
    );
    
    // Execute aggregation and insert into matview
    // Returns number of rows inserted
    static int64 execute_and_materialize(
        const char *query,
        const char *matview_name
    );
    
    // Create the materialization table schema
    static void create_matview_table(const ContinuousAggregate &agg);
    
    // No instances allowed
    MaterializationEngine() = delete;
    ~MaterializationEngine() = delete;
};

} // namespace rollups
```

**Algorithm Overview**:

**Initial Populate**:
1. Create materialization table (if not exists)
2. Query: `SELECT time_bucket(interval, time_col) AS bucket, AGG_FUNCS FROM source GROUP BY bucket`
3. Execute query and insert all results into matview
4. Update watermark to current time

**Incremental Refresh**:
1. Get last watermark from catalog
2. Query: Same as above but with `WHERE time_col > last_watermark`
3. Execute query and insert new buckets (or UPDATE existing buckets if overlap)
4. Update watermark to current time

**Usage Example**:
```cpp
rollups::ContinuousAggregate agg("hourly_sales");

// First time setup
agg.initial_populate();  // Calls MaterializationEngine::initial_populate(agg)

// Later, incremental updates
agg.refresh();  // Calls MaterializationEngine::incremental_refresh(agg)
```

---

### 4. QueryParser (Stateless Parser)

**File**: `include/rollups/query_parser.hpp`, `src/query_parser.cpp`

**Purpose**: Parse `CREATE CONTINUOUS AGGREGATE` DDL syntax.

**Pattern**: All static methods, no instances

```cpp
namespace rollups {

// Result of parsing CREATE CONTINUOUS AGGREGATE
typedef struct CreateContinuousAggregateStmt {
    char     *agg_name;
    char     *source_table;
    char     *time_column;
    Interval *bucket_interval;
    char     *aggregation_query;  // Future use for query rewriting
} CreateContinuousAggregateStmt;

class QueryParser {
public:
    // Parse CREATE CONTINUOUS AGGREGATE query string
    // Returns palloc'd struct (lives in current memory context)
    static CreateContinuousAggregateStmt* parse(const char *queryString);
    
    // Validate the parsed statement (checks table exists, column exists, etc.)
    static void validate(const CreateContinuousAggregateStmt *stmt);

private:
    // Helper: skip whitespace
    static const char* skip_whitespace(const char *str);
    
    // Helper: extract identifier (quoted or unquoted)
    static char* extract_identifier(const char **query_ptr);
    
    // Helper: extract interval literal
    static Interval* extract_interval(const char **query_ptr);
    
    // Helper: check if table exists
    static bool table_exists(const char *table_name);
    
    // Helper: check if column exists in table
    static bool column_exists(const char *table_name, const char *column_name);
    
    // No instances allowed
    QueryParser() = delete;
    ~QueryParser() = delete;
};

} // namespace rollups
```

**Syntax to Parse** (Phase 3):
```sql
CREATE CONTINUOUS AGGREGATE hourly_sales
ON sales_transactions(created_at)
WITH (bucket_width = '1 hour')
AS
SELECT 
    time_bucket('1 hour', created_at) AS bucket,
    COUNT(*) AS sale_count,
    SUM(amount) AS total_amount
FROM sales_transactions
GROUP BY bucket;
```

**Parsing Strategy**:
- **Simple string parsing** for v1.0 (no need to extend PostgreSQL grammar)
- Look for keywords: `CREATE CONTINUOUS AGGREGATE`, `ON`, `WITH`, `AS`
- Extract identifiers and intervals using helper functions
- Validate against system catalogs (pg_class, pg_attribute)

**Error Handling**:
- Syntax errors: `ereport(ERROR, (errmsg("syntax error in CREATE CONTINUOUS AGGREGATE")))`
- Semantic errors: `ereport(ERROR, (errmsg("table 'foo' does not exist")))`

**Usage Example**:
```cpp
const char *ddl = "CREATE CONTINUOUS AGGREGATE hourly_sales ...";

auto *stmt = rollups::QueryParser::parse(ddl);
rollups::QueryParser::validate(stmt);

// Use stmt fields...
Oid agg_oid = rollups::CatalogManager::create(
    stmt->agg_name,
    stmt->source_table,
    stmt->time_column,
    stmt->bucket_interval
);
```

## Integration with Existing Code

### Update rollups.cpp ProcessUtility Hook

**File**: `src/rollups.cpp`

```cpp
static void
rollups_ProcessUtility(PlannedStmt *pstmt,
                      const char *queryString,
                      bool readOnlyTree,
                      ProcessUtilityContext context,
                      ParamListInfo params,
                      QueryEnvironment *queryEnv,
                      DestReceiver *dest,
                      QueryCompletion *qc)
{
    // Check for CREATE CONTINUOUS AGGREGATE
    if (queryString && strncmp(queryString, "CREATE CONTINUOUS AGGREGATE", 27) == 0)
    {
        // Parse the DDL
        rollups::CreateContinuousAggregateStmt *stmt = 
            rollups::QueryParser::parse(queryString);
        
        // Validate (checks table exists, column exists, etc.)
        rollups::QueryParser::validate(stmt);
        
        // Create catalog entry
        Oid agg_oid = rollups::CatalogManager::create(
            stmt->agg_name,
            stmt->source_table,
            stmt->time_column,
            stmt->bucket_interval
        );
        
        // Load the aggregate and do initial population
        rollups::ContinuousAggregate agg(stmt->agg_name);
        agg.initial_populate();
        
        // Report success to user
        ereport(NOTICE,
                (errmsg("continuous aggregate \"%s\" created and populated",
                        stmt->agg_name)));
        
        return;  // Don't call standard_ProcessUtility
    }
    
    // Not our command, pass through to next hook or standard handler
    if (prev_ProcessUtility_hook)
        prev_ProcessUtility_hook(pstmt, queryString, readOnlyTree,
                                context, params, queryEnv, dest, qc);
    else
        standard_ProcessUtility(pstmt, queryString, readOnlyTree,
                               context, params, queryEnv, dest, qc);
}
```

## Memory Management Strategy

### PostgreSQL Memory Contexts

PostgreSQL uses **memory contexts** for automatic cleanup:
- **CurrentMemoryContext**: Where palloc allocates by default
- **TopMemoryContext**: Lives for entire backend session
- **MessageContext**: Reset after each query
- **Per-tuple context**: Reset after processing each row

### Our Strategy

1. **Short-lived data** (function scope):
   - Use default `CurrentMemoryContext`
   - Example: `ContinuousAggregate` wrapper object (stack), parsed DDL statements

2. **Long-lived data** (catalog entries):
   - Data loaded from catalog lives in `CurrentMemoryContext` at load time
   - Caller is responsible for switching to appropriate context before loading

3. **No manual cleanup**:
   - Never call pfree explicitly (let context reset handle it)
   - Destructors are no-ops

### Example

```cpp
// BAD: Don't do this
ContinuousAggregateData *data = palloc(sizeof(ContinuousAggregateData));
// ... use data ...
pfree(data);  // Manual free - unnecessary and error-prone

// GOOD: Let memory context handle it
ContinuousAggregateData *data = palloc(sizeof(ContinuousAggregateData));
// ... use data ...
// When function returns or transaction ends, memory context resets automatically
```

## Error Handling Strategy

### Use ereport, Never Exceptions

```cpp
// BAD: C++ exceptions don't work with PostgreSQL
if (!table_exists(table_name)) {
    throw std::runtime_error("Table does not exist");  // WRONG!
}

// GOOD: Use ereport
if (!table_exists(table_name)) {
    ereport(ERROR,
            (errcode(ERRCODE_UNDEFINED_TABLE),
             errmsg("table \"%s\" does not exist", table_name),
             errhint("Create the table before creating a continuous aggregate on it.")));
}
```

### Error Codes

Use PostgreSQL's error codes from `utils/errcodes.h`:
- `ERRCODE_UNDEFINED_TABLE` - Table doesn't exist
- `ERRCODE_UNDEFINED_COLUMN` - Column doesn't exist
- `ERRCODE_DUPLICATE_OBJECT` - Aggregate already exists
- `ERRCODE_INVALID_PARAMETER_VALUE` - Invalid bucket interval
- `ERRCODE_FEATURE_NOT_SUPPORTED` - Feature not yet implemented

## Testing Strategy

### Unit Tests (Phase 3)

Each class will have tests:

1. **CatalogManager**:
   - Test create/read/update/delete operations
   - Test duplicate name handling
   - Test OID lookup

2. **MaterializationEngine**:
   - Test initial_populate with sample data
   - Test incremental_refresh with new rows
   - Test time range queries

3. **QueryParser**:
   - Test valid DDL syntax parsing
   - Test error cases (missing keywords, invalid syntax)
   - Test quoted identifiers

4. **ContinuousAggregate**:
   - Test wrapper methods
   - Test refresh operations

### Integration Tests

- End-to-end: `CREATE CONTINUOUS AGGREGATE` -> populate -> query -> refresh
- Performance: measure refresh time on different data sizes
- Correctness: compare aggregate results with manual GROUP BY queries

## Future Enhancements

### Phase 4: Incremental Updates via Triggers

- Add trigger-based delta tracking
- Update `MaterializationEngine` to process deltas

### Phase 5: Background Workers

- Add `BackgroundWorker` class for scheduled refreshes
- Implement refresh policies (cron-like scheduling)

### Phase 6: Query Rewriting

- Add `QueryRewriter` class to intercept SELECT queries
- Automatically use materialization table when possible

## References

- **ServiceNow Tembo DB**: `/Users/maxim.podkolzin/dev/snc/tembo/db/src/types/sncvarchar.cpp`
- **PostgreSQL SPI Documentation**: https://www.postgresql.org/docs/current/spi.html
- **PostgreSQL Memory Contexts**: https://www.postgresql.org/docs/current/spi-memory.html
- **PostgreSQL Error Reporting**: https://www.postgresql.org/docs/current/error-message-reporting.html

---

**Document Version**: 1.0  
**Last Updated**: 2026-05-15  
**Next Review**: After Phase 3 implementation
