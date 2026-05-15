/*
 * rollups.cpp
 *
 * Core implementation of the rollups extension (C++)
 *
 * This file demonstrates fundamental PostgreSQL extension concepts using C++:
 * - PG_MODULE_MAGIC (required for all extensions)
 * - Function calling conventions (PG_FUNCTION_INFO_V1, PG_RETURN_*, PG_GETARG_*)
 * - Datum type system (PostgreSQL's internal value representation)
 * - Memory management (palloc, pfree, memory contexts)
 * - Type conversions (timestamp handling, intervals)
 * - C++ extern "C" linkage for PostgreSQL compatibility
 *
 * WHY C++ FOR A POSTGRESQL EXTENSION?
 *
 * Pros:
 * - Better abstractions (classes, namespaces, RAII)
 * - Type safety and compile-time checks
 * - STL containers and algorithms
 * - Easier to write complex logic (we'll need this for rollup algorithms)
 * - Modern C++ features (auto, lambdas, smart pointers)
 *
 * Cons:
 * - PostgreSQL API is pure C (need extern "C")
 * - More complex build setup
 * - Need to be careful with exceptions (PostgreSQL uses setjmp/longjmp)
 * - Larger binary size
 *
 * For this learning project, C++ is worth it for the better code organization
 * and modern language features we'll use for complex rollup logic.
 */

// PostgreSQL headers are C, so include them in extern "C" block
extern "C" {
#include "postgres.h"
#include "fmgr.h"              /* Function manager interface */
#include "utils/builtins.h"    /* Built-in type conversions (cstring_to_text, etc.) */
#include "utils/timestamp.h"   /* Timestamp type definitions */
#include "utils/datetime.h"    /* Date/time utilities */
#include "datatype/timestamp.h" /* More timestamp functions */
#include "tcop/utility.h"      /* ProcessUtility_hook for DDL commands */
#include "commands/explain.h"  /* QueryCompletion and related types */
#include "lib/stringinfo.h"    /* StringInfoData for string building */
#include "utils/jsonb.h"       /* JSONB type and functions */
}

// We can now use C++ headers safely
#include <cstdint>  // For explicit integer types (though PostgreSQL has its own)
#include <string>   // For future use (not needed yet, but available)

// Our extension headers
#include "rollups/catalog_manager.hpp"
#include "rollups/continuous_aggregate.hpp"

/*
 * Required for all PostgreSQL extensions - provides versioning info
 *
 * IMPORTANT: PG_MODULE_MAGIC must be in extern "C" block
 * This macro creates a struct that PostgreSQL checks at load time
 */
extern "C" {
PG_MODULE_MAGIC;
}

/*
 * Extension version string
 * This will be returned by rollups.version()
 *
 * Note: In C++, we could use constexpr for this, but #define works fine
 * and is more consistent with PostgreSQL conventions
 */
#define ROLLUPS_VERSION "1.0.0-cpp"

/* ========================================================================
 * HOOKS: PostgreSQL Extension Points
 * ========================================================================
 *
 * Hooks allow extensions to intercept and modify PostgreSQL's behavior.
 * We use ProcessUtility_hook to intercept DDL commands like
 * CREATE CONTINUOUS AGGREGATE (which we'll implement later).
 *
 * HOOK PATTERN (critical for correctness):
 * 1. Save previous hook value (for hook chaining)
 * 2. Install our hook in _PG_init()
 * 3. In our hook function:
 *    a) Do our custom work
 *    b) Call prev_hook (if exists) OR standard_function
 *    c) Return result
 *
 * This allows multiple extensions to use the same hook safely.
 */

extern "C" {

/* Static storage for previous hook value (for chaining) */
static ProcessUtility_hook_type prev_ProcessUtility_hook = NULL;

/* Forward declarations */
static void rollups_ProcessUtility(PlannedStmt *pstmt,
								  const char *queryString,
								  bool readOnlyTree,
								  ProcessUtilityContext context,
								  ParamListInfo params,
								  QueryEnvironment *queryEnv,
								  DestReceiver *dest,
								  QueryCompletion *qc);

/*
 * _PG_init - Extension initialization function
 *
 * Called when the extension is loaded into a backend process.
 * For extensions in shared_preload_libraries, this happens at PostgreSQL startup.
 * For other extensions, this happens when first used in a session.
 *
 * This is where we install our hooks.
 *
 * C++ NOTE: Must be extern "C" - PostgreSQL calls this by name.
 */
void
_PG_init(void)
{
	/*
	 * Install ProcessUtility hook for intercepting DDL commands
	 *
	 * CRITICAL: Save previous hook value first!
	 * If another extension already installed a hook, we must call it.
	 * This is called "hook chaining" and allows multiple extensions
	 * to use the same hook.
	 */
	prev_ProcessUtility_hook = ProcessUtility_hook;
	ProcessUtility_hook = rollups_ProcessUtility;

	/*
	 * Log that we've initialized (visible in PostgreSQL logs)
	 * DEBUG1 level - only shows if log_min_messages = DEBUG1 or lower
	 */
	ereport(DEBUG1,
			(errmsg("rollups extension initialized"),
			 errdetail("ProcessUtility_hook installed for DDL interception")));
}

/*
 * rollups_ProcessUtility - Our ProcessUtility hook implementation
 *
 * This hook is called for ALL DDL commands:
 * - CREATE TABLE, DROP TABLE, ALTER TABLE
 * - CREATE INDEX, DROP INDEX
 * - CREATE EXTENSION, DROP EXTENSION
 * - And our custom: CREATE CONTINUOUS AGGREGATE (future)
 *
 * For now, we just log the commands and pass through to the next hook.
 * Later, we'll parse CREATE CONTINUOUS AGGREGATE here.
 *
 * Parameters:
 *   pstmt - Parsed statement (includes the Node tree)
 *   queryString - Original SQL text (this is what we'll parse for custom DDL)
 *   readOnlyTree - Whether we can modify the parse tree
 *   context - Where this utility command is being called from
 *   params - Query parameters (if any)
 *   queryEnv - Query environment
 *   dest - Where to send results
 *   qc - Query completion info
 *
 * C++ NOTE: Must be extern "C" and match the hook signature exactly.
 */
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
	/*
	 * Log the DDL command for learning purposes
	 *
	 * DEBUG1 level - only visible if log_min_messages is DEBUG1 or lower
	 * In production, we'd remove this or make it DEBUG5
	 *
	 * Note: queryString might be NULL in some cases, so we check first
	 */
	if (queryString)
	{
		ereport(DEBUG1,
				(errmsg("rollups: ProcessUtility hook fired"),
				 errdetail("Query: %s", queryString)));

		/*
		 * TODO (Phase 3): Detect CREATE CONTINUOUS AGGREGATE here
		 *
		 * Pseudocode for future implementation:
		 *
		 * if (strncmp(queryString, "CREATE CONTINUOUS AGGREGATE", 27) == 0)
		 * {
		 *     // Our custom DDL command!
		 *     ParseCreateContinuousAggregate(queryString);
		 *     CreateRollupMetadata(...);
		 *     CreateMaterializationTable(...);
		 *     return;  // Don't call standard_ProcessUtility for our command
		 * }
		 */
	}

	/*
	 * Not our command (or we haven't implemented parsing yet)
	 * Pass through to the next hook in the chain, or standard_ProcessUtility
	 *
	 * CRITICAL: This is the hook chaining pattern
	 * - If another extension installed a hook before us, call it
	 * - Otherwise, call PostgreSQL's standard_ProcessUtility
	 *
	 * This ensures all extensions work together correctly.
	 */
	if (prev_ProcessUtility_hook)
		prev_ProcessUtility_hook(pstmt, queryString, readOnlyTree,
								context, params, queryEnv, dest, qc);
	else
		standard_ProcessUtility(pstmt, queryString, readOnlyTree,
							   context, params, queryEnv, dest, qc);
}

} // extern "C"

/* ========================================================================
 * PART 1: Version function (simplest possible example)
 * ========================================================================
 */

/*
 * rollups_version - Returns the extension version as text
 *
 * This demonstrates the basic structure of a C++ function for PostgreSQL:
 * 1. PG_FUNCTION_INFO_V1 macro declares the function to PostgreSQL
 * 2. Function must be in extern "C" block (no C++ name mangling)
 * 3. Function name must match what's in the CREATE FUNCTION statement
 * 4. Always takes PG_FUNCTION_ARGS (even if no arguments)
 * 5. Returns Datum (PostgreSQL's generic value type)
 * 6. Must use PG_RETURN_* macros to return values
 *
 * C++ NOTE: We can use C++ features inside the function body, but:
 * - The function signature must be C-compatible
 * - Can't throw C++ exceptions (PostgreSQL uses ereport for errors)
 * - Return type must be Datum
 */
extern "C" {

PG_FUNCTION_INFO_V1(rollups_version);

Datum
rollups_version(PG_FUNCTION_ARGS)
{
	/*
	 * cstring_to_text() converts a C string to PostgreSQL's TEXT type
	 *
	 * Memory management note:
	 * - PostgreSQL uses "memory contexts" for automatic cleanup
	 * - Data returned from functions is copied by PostgreSQL
	 * - No need to manually free the result
	 *
	 * C++ NOTE: We could use std::string internally, but PostgreSQL
	 * API expects char* and text*, so we stick with C types for now.
	 */
	text *result = cstring_to_text(ROLLUPS_VERSION);

	/* Return as Datum (PostgreSQL's polymorphic type) */
	PG_RETURN_TEXT_P(result);
}

} // extern "C"

/* ========================================================================
 * PART 2: Time bucketing function (core rollup functionality)
 * ========================================================================
 */

/*
 * rollups_time_bucket - Round timestamp down to time bucket boundary
 *
 * This is the fundamental operation for time-series rollups:
 * - Takes a bucket_width (e.g., '1 hour', '1 day')
 * - Takes a timestamp
 * - Returns the start of the bucket containing that timestamp
 *
 * Example:
 *   time_bucket('1 hour', '2024-01-15 14:32:17')
 *   → '2024-01-15 14:00:00'
 *
 * Algorithm:
 *   1. Convert timestamp to microseconds since epoch
 *   2. Convert bucket_width to microseconds
 *   3. Integer division: bucket_start = (timestamp / bucket_width) * bucket_width
 *   4. Convert back to timestamp
 *
 * This demonstrates:
 * - Fetching function arguments with type checking
 * - Working with PostgreSQL's internal time representations
 * - Integer arithmetic on timestamps (they're just int64 microseconds)
 * - Error handling with ereport()
 *
 * C++ NOTE: In future versions, we might create a TimeBucket class to
 * encapsulate this logic. For now, keeping it simple to learn the basics.
 */
extern "C" {

PG_FUNCTION_INFO_V1(rollups_time_bucket);

Datum
rollups_time_bucket(PG_FUNCTION_ARGS)
{
	/*
	 * Extract arguments
	 *
	 * PG_GETARG_* macros:
	 * - Extract typed arguments from the function call
	 * - Perform type checking (type must match CREATE FUNCTION declaration)
	 * - STRICT functions (declared in SQL) never receive NULL arguments
	 *   (PostgreSQL returns NULL automatically if any arg is NULL)
	 */
	Interval   *bucket_width = PG_GETARG_INTERVAL_P(0);  /* First argument */
	Timestamp   ts = PG_GETARG_TIMESTAMP(1);              /* Second argument */

	/* Variables for computation */
	Timestamp   result;
	int64       bucket_width_us;  /* Bucket width in microseconds */
	int64       ts_us;            /* Timestamp in microseconds */
	int64       bucket_start_us;  /* Computed bucket start in microseconds */

	/*
	 * Convert interval to microseconds
	 *
	 * Interval structure (see utils/timestamp.h):
	 *   - time: microseconds component
	 *   - day: days component
	 *   - month: months component
	 *
	 * For time bucketing, we need total duration in microseconds.
	 * We'll use a simplified conversion for learning purposes.
	 *
	 * C++ NOTE: PostgreSQL defines these types as C structs. We access
	 * fields directly (no getters). Later, we might create wrapper classes.
	 */
	bucket_width_us = bucket_width->time +
	                  (bucket_width->day * USECS_PER_DAY);

	/*
	 * Error checking: bucket width must be positive
	 *
	 * ereport() is PostgreSQL's error reporting mechanism:
	 * - ERROR level throws an exception (rolls back transaction)
	 * - errcode() provides a SQL error code
	 * - errmsg() provides the user-visible message
	 *
	 * IMPORTANT C++ NOTE: DO NOT use C++ exceptions (throw/catch)!
	 * PostgreSQL uses setjmp/longjmp for error handling, which doesn't
	 * work with C++ exceptions. Always use ereport(ERROR, ...) instead.
	 */
	if (bucket_width_us <= 0)
	{
		ereport(ERROR,
		        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
		         errmsg("bucket_width must be positive"),
		         errhint("Try an interval like '1 hour' or '1 day'")));
	}

	/*
	 * Handle month component separately (not supported in v1.0 for simplicity)
	 *
	 * Reason: Months have variable length (28-31 days), making bucketing ambiguous
	 * TimescaleDB handles this by converting to fixed durations (1 month = 30 days)
	 * We'll add proper support in a later version.
	 */
	if (bucket_width->month != 0)
	{
		ereport(ERROR,
		        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		         errmsg("month-based time buckets not yet supported"),
		         errhint("Use day-based intervals (e.g., '30 days') instead")));
	}

	/*
	 * Core algorithm: round down to bucket boundary
	 *
	 * Timestamp is stored as int64 microseconds since 2000-01-01 (PostgreSQL epoch)
	 * Integer division automatically rounds toward zero (which is "down" for positive values)
	 *
	 * C++ NOTE: We could use static_cast<int64_t>(ts) instead of C-style cast,
	 * but PostgreSQL's int64 is just a typedef, so either works fine.
	 */
	ts_us = static_cast<int64>(ts);
	bucket_start_us = (ts_us / bucket_width_us) * bucket_width_us;
	result = static_cast<Timestamp>(bucket_start_us);

	/*
	 * Return the result
	 *
	 * PG_RETURN_* macros:
	 * - Convert C types to Datum
	 * - Handle pass-by-value vs pass-by-reference types
	 * - Include return statement (so function ends here)
	 */
	PG_RETURN_TIMESTAMP(result);
}

} // extern "C"

/* ========================================================================
 * PART 3: Test functions for CatalogManager (Phase 2)
 * ========================================================================
 */

/*
 * rollups_test_create_aggregate - Test function for CatalogManager::create()
 *
 * Creates a continuous aggregate entry in the catalog table.
 * Returns the generated agg_id (OID).
 *
 * This exercises the full SPI workflow:
 * - Looking up source table OID
 * - Generating matview name
 * - Inserting into catalog table
 * - Returning the generated ID
 *
 * Parameters:
 *   p_agg_name: Name for the aggregate
 *   p_source_table: Source table name
 *   p_time_column: Time column name
 *   p_bucket_interval: Bucket width (optional, default '1 hour')
 *
 * Returns: OID of the created aggregate
 *
 * C++ NOTE: Demonstrates calling C++ static methods from C function.
 */
extern "C" {

PG_FUNCTION_INFO_V1(rollups_test_create_aggregate);

Datum
rollups_test_create_aggregate(PG_FUNCTION_ARGS)
{
	/*
	 * Extract arguments
	 * - text_to_cstring: Convert PostgreSQL text* to C string
	 * - PG_GETARG_INTERVAL_P: Get interval* argument
	 */
	text *agg_name_text = PG_GETARG_TEXT_PP(0);
	text *source_table_text = PG_GETARG_TEXT_PP(1);
	text *time_column_text = PG_GETARG_TEXT_PP(2);
	Interval *bucket_interval = PG_GETARG_INTERVAL_P(3);

	/* Convert text to C strings */
	char *agg_name = text_to_cstring(agg_name_text);
	char *source_table = text_to_cstring(source_table_text);
	char *time_column = text_to_cstring(time_column_text);

	/*
	 * Call C++ CatalogManager::create()
	 *
	 * This demonstrates mixing C and C++ in PostgreSQL extensions:
	 * - The extern "C" function is callable from SQL
	 * - It calls a C++ static method internally
	 * - The C++ method does the complex SPI work
	 * - If CatalogManager::create() calls ereport(ERROR), control
	 *   never returns here (PostgreSQL longjmp to error handler)
	 */
	Oid result = rollups::CatalogManager::create(
		agg_name,
		source_table,
		time_column,
		bucket_interval
	);

	/* Return the OID */
	PG_RETURN_OID(result);
}

/*
 * rollups_test_load_aggregate - Test function for CatalogManager::load()
 *
 * Loads aggregate metadata by name and returns it as JSON.
 *
 * This demonstrates:
 * - Loading data from catalog via C++ CatalogManager
 * - Converting C++ struct to JSON for SQL visibility
 * - Using PostgreSQL's JSON type
 *
 * Parameters:
 *   p_agg_name: Name of the aggregate to load
 *
 * Returns: JSONB representation of the aggregate
 *
 * C++ NOTE: We manually build the JSON here. In future versions,
 * we might use a C++ JSON library (like nlohmann/json) for cleaner code.
 */
PG_FUNCTION_INFO_V1(rollups_test_load_aggregate);

Datum
rollups_test_load_aggregate(PG_FUNCTION_ARGS)
{
	/* Extract argument */
	text *agg_name_text = PG_GETARG_TEXT_PP(0);
	char *agg_name = text_to_cstring(agg_name_text);

	/*
	 * Load aggregate from catalog
	 *
	 * CatalogManager::load() returns ContinuousAggregateData* or
	 * throws ereport(ERROR) if not found.
	 *
	 * Note: ContinuousAggregateData is at global scope (not in rollups::)
	 * because it's a C struct, not a C++ class.
	 */
	ContinuousAggregateData *data =
		rollups::CatalogManager::load(agg_name);

	/*
	 * Build JSON representation
	 *
	 * For simplicity, we'll build a JSON string manually.
	 * PostgreSQL will parse it into jsonb type.
	 *
	 * Note: NameStr() macro converts NameData to char*
	 * Format: {"agg_id": 12345, "agg_name": "...", ...}
	 */
	StringInfoData buf;
	initStringInfo(&buf);

	appendStringInfo(&buf, "{");
	appendStringInfo(&buf, "\"agg_id\": %u,", data->agg_id);
	appendStringInfo(&buf, "\"agg_name\": \"%s\",", NameStr(data->agg_name));
	appendStringInfo(&buf, "\"view_name\": \"%s\",", NameStr(data->view_name));
	appendStringInfo(&buf, "\"source_table_oid\": %u,", data->source_table_oid);
	appendStringInfo(&buf, "\"source_table_name\": \"%s\",", NameStr(data->source_table_name));
	appendStringInfo(&buf, "\"time_column\": \"%s\",", NameStr(data->time_column));
	appendStringInfo(&buf, "\"matview_name\": \"%s\",", NameStr(data->matview_name));
	appendStringInfo(&buf, "\"is_active\": %s", data->is_active ? "true" : "false");
	appendStringInfo(&buf, "}");

	/*
	 * Convert string to jsonb
	 *
	 * DirectFunctionCall1: Call a PostgreSQL function directly
	 * jsonb_in: PostgreSQL's JSON parser
	 * cstring_to_text: Convert C string to text* for jsonb_in
	 */
	Datum json_datum = DirectFunctionCall1(jsonb_in,
		CStringGetDatum(buf.data));

	PG_RETURN_DATUM(json_datum);
}

/*
 * rollups_test_aggregate_exists - Test function for CatalogManager::exists()
 *
 * Simple existence check - returns boolean.
 *
 * Parameters:
 *   p_agg_name: Name to check
 *
 * Returns: true if exists, false if not
 */
PG_FUNCTION_INFO_V1(rollups_test_aggregate_exists);

Datum
rollups_test_aggregate_exists(PG_FUNCTION_ARGS)
{
	/* Extract argument */
	text *agg_name_text = PG_GETARG_TEXT_PP(0);
	char *agg_name = text_to_cstring(agg_name_text);

	/* Call C++ CatalogManager::exists() */
	bool result = rollups::CatalogManager::exists(agg_name);

	/* Return boolean */
	PG_RETURN_BOOL(result);
}

} // extern "C"

/* ========================================================================
 * LEARNING NOTES & KEY CONCEPTS
 * ========================================================================
 *
 * 1. DATUM TYPE SYSTEM
 *    - Datum: Generic type that can hold any PostgreSQL value
 *    - Small types (int, float, bool): passed by value
 *    - Large types (text, arrays): passed by reference (pointer)
 *
 * 2. FUNCTION CALLING CONVENTION
 *    - Version 1 calling convention (hence PG_FUNCTION_INFO_V1)
 *    - All functions take PG_FUNCTION_ARGS (macro for FunctionCallInfo)
 *    - Arguments accessed via PG_GETARG_* macros (not regular C parameters)
 *    - Return via PG_RETURN_* macros (not plain return statement)
 *
 * 3. MEMORY MANAGEMENT
 *    - PostgreSQL uses memory contexts (automatic cleanup)
 *    - palloc(size): like malloc, but in current memory context
 *    - pfree(ptr): free memory (but usually not needed - context cleanup handles it)
 *    - CurrentMemoryContext: where palloc allocates
 *    - Per-tuple context: reset after each row in a query
 *    - C++ NOTE: Don't use new/delete for PostgreSQL-managed memory!
 *      Use palloc/pfree instead. C++ RAII doesn't work well with PostgreSQL's
 *      memory contexts and setjmp/longjmp error handling.
 *
 * 4. ERROR HANDLING
 *    - ereport(ERROR, ...): throw exception, rollback transaction
 *    - ereport(WARNING, ...): print warning, continue execution
 *    - ereport(DEBUG1-5, ...): debug messages (controlled by log level)
 *    - Never use return to indicate errors - use ereport(ERROR, ...)
 *    - C++ NOTE: NEVER use throw/catch! PostgreSQL uses setjmp/longjmp,
 *      which will skip C++ destructors and cause memory leaks or worse.
 *
 * 5. STRICT FUNCTIONS
 *    - Declared as STRICT in CREATE FUNCTION
 *    - PostgreSQL automatically returns NULL if any argument is NULL
 *    - C function never sees NULL arguments
 *    - Simplifies null-checking logic
 *
 * 6. PARALLEL SAFETY
 *    - PARALLEL SAFE: can run in parallel workers (no global state modification)
 *    - PARALLEL RESTRICTED: parallel-safe during parallel mode, unsafe afterward
 *    - PARALLEL UNSAFE: cannot run in parallel workers at all
 *    - Our time_bucket is PARALLEL SAFE (pure function, no side effects)
 *
 * 7. IMMUTABILITY
 *    - IMMUTABLE: always returns same output for same input (like pure functions)
 *    - STABLE: same result within a transaction (e.g., now())
 *    - VOLATILE: result can change between calls (e.g., random())
 *    - Immutability enables optimizations (constant folding, indexing)
 *
 * 8. C++ SPECIFIC CONSIDERATIONS
 *    - All PostgreSQL-callable functions MUST be in extern "C" blocks
 *    - No C++ exceptions - use ereport(ERROR, ...) instead
 *    - Be careful with C++ objects - they might not be cleaned up on ereport(ERROR)
 *    - Use palloc, not new (for PostgreSQL-managed memory)
 *    - C++ features (classes, STL) are fine for internal logic
 *    - Name mangling: extern "C" prevents C++ from changing function names
 *
 * WHEN TO USE C++ FEATURES:
 *    ✓ Internal helper functions (not called from SQL)
 *    ✓ Data structures for algorithms (as long as they use palloc)
 *    ✓ Type safety and compile-time checks
 *    ✓ Namespaces for code organization
 *    ✗ C++ exceptions (use ereport instead)
 *    ✗ new/delete (use palloc/pfree)
 *    ✗ RAII for resource cleanup (won't work with ereport longjmp)
 *    ✗ C++ features in PostgreSQL-callable function signatures
 *
 * NEXT STEPS FOR LEARNING:
 * - Hook into query planner (planner_hook)
 * - Create custom scan nodes
 * - Implement background workers
 * - Use shared memory and LWLocks
 * - Understand MVCC and transaction visibility
 * - Create C++ helper classes for rollup algorithms
 * - Use STL containers for efficient data structures (with PostgreSQL allocators)
 */
