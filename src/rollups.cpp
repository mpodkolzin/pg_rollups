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
		 * Phase 3 uses a function-based API for managing aggregates
		 * (rollups.create_continuous_aggregate(...) etc.), so creation does
		 * NOT go through this hook.
		 *
		 * Why not a custom CREATE CONTINUOUS AGGREGATE statement? This hook
		 * runs AFTER the parser. PostgreSQL's grammar is fixed, so a brand-new
		 * keyword sequence is a syntax error before the hook is ever reached -
		 * adding real keywords would mean forking PostgreSQL's grammar.
		 *
		 * The hook is kept here for observability now, and as the future home
		 * for intercepting CREATE MATERIALIZED VIEW ... WITH (rollups.*) and
		 * DROP TABLE cascades.
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
 * PART 3: Continuous aggregate management functions
 * ========================================================================
 *
 * These are the user-facing entry points for the function-based API:
 *   rollups.create_continuous_aggregate(...)
 *   rollups.refresh_continuous_aggregate(name)
 *   rollups.drop_continuous_aggregate(name)
 *
 * Each is a thin extern "C" wrapper: it converts the SQL arguments to C
 * types and delegates to the C++ CatalogManager / ContinuousAggregate
 * classes, which do the real SPI work.
 */
extern "C" {

PG_FUNCTION_INFO_V1(rollups_create_continuous_aggregate);

Datum
rollups_create_continuous_aggregate(PG_FUNCTION_ARGS)
{
	/* Convert the SQL arguments to C types. */
	char *agg_name      = text_to_cstring(PG_GETARG_TEXT_PP(0));
	char *source_table  = text_to_cstring(PG_GETARG_TEXT_PP(1));
	char *time_column   = text_to_cstring(PG_GETARG_TEXT_PP(2));
	Interval *bucket    = PG_GETARG_INTERVAL_P(3);
	char *select_clause = text_to_cstring(PG_GETARG_TEXT_PP(4));

	/*
	 * Step 1: record the aggregate in the catalog. CatalogManager::create()
	 * also resolves the source table OID and generates the matview name.
	 */
	Oid agg_id = rollups::CatalogManager::create(
		agg_name, source_table, time_column, bucket, select_clause);

	/*
	 * Step 2: build and populate the materialization table. We construct the
	 * wrapper from the name so the engine works from the canonical catalog
	 * row (resolved OIDs, generated matview name) rather than local copies.
	 */
	rollups::ContinuousAggregate agg(agg_name);
	agg.initial_populate();

	PG_RETURN_OID(agg_id);
}

PG_FUNCTION_INFO_V1(rollups_refresh_continuous_aggregate);

Datum
rollups_refresh_continuous_aggregate(PG_FUNCTION_ARGS)
{
	char *agg_name = text_to_cstring(PG_GETARG_TEXT_PP(0));

	/* The constructor loads the aggregate from the catalog. */
	rollups::ContinuousAggregate agg(agg_name);
	agg.refresh();

	PG_RETURN_VOID();
}

PG_FUNCTION_INFO_V1(rollups_drop_continuous_aggregate);

Datum
rollups_drop_continuous_aggregate(PG_FUNCTION_ARGS)
{
	char *agg_name = text_to_cstring(PG_GETARG_TEXT_PP(0));

	/* The constructor loads the aggregate from the catalog. */
	rollups::ContinuousAggregate agg(agg_name);
	agg.drop();

	PG_RETURN_VOID();
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
