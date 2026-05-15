/*
 * rollups.c
 *
 * Core implementation of the rollups extension
 *
 * This file demonstrates fundamental PostgreSQL extension concepts:
 * - PG_MODULE_MAGIC (required for all extensions)
 * - Function calling conventions (PG_FUNCTION_INFO_V1, PG_RETURN_*, PG_GETARG_*)
 * - Datum type system (PostgreSQL's internal value representation)
 * - Memory management (palloc, pfree, memory contexts)
 * - Type conversions (timestamp handling, intervals)
 */

#include "postgres.h"
#include "fmgr.h"              /* Function manager interface */
#include "utils/timestamp.h"   /* Timestamp type definitions */
#include "utils/datetime.h"    /* Date/time utilities */
#include "datatype/timestamp.h" /* More timestamp functions */

/* Required for all PostgreSQL extensions - provides versioning info */
PG_MODULE_MAGIC;

/*
 * Extension version string
 * This will be returned by rollups.version()
 */
#define ROLLUPS_VERSION "1.0.0"

/* ========================================================================
 * PART 1: Version function (simplest possible example)
 * ========================================================================
 */

/*
 * rollups_version - Returns the extension version as text
 *
 * This demonstrates the basic structure of a C function in PostgreSQL:
 * 1. PG_FUNCTION_INFO_V1 macro declares the function to PostgreSQL
 * 2. Function name must match what's in the CREATE FUNCTION statement
 * 3. Always takes PG_FUNCTION_ARGS (even if no arguments)
 * 4. Returns Datum (PostgreSQL's generic value type)
 * 5. Must use PG_RETURN_* macros to return values
 */
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
	 */
	text *result = cstring_to_text(ROLLUPS_VERSION);

	/* Return as Datum (PostgreSQL's polymorphic type) */
	PG_RETURN_TEXT_P(result);
}

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
 */
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
	 */
	ts_us = (int64) ts;
	bucket_start_us = (ts_us / bucket_width_us) * bucket_width_us;
	result = (Timestamp) bucket_start_us;

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
 *
 * 4. ERROR HANDLING
 *    - ereport(ERROR, ...): throw exception, rollback transaction
 *    - ereport(WARNING, ...): print warning, continue execution
 *    - ereport(DEBUG1-5, ...): debug messages (controlled by log level)
 *    - Never use return to indicate errors - use ereport(ERROR, ...)
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
 * NEXT STEPS FOR LEARNING:
 * - Hook into query planner (planner_hook)
 * - Create custom scan nodes
 * - Implement background workers
 * - Use shared memory and LWLocks
 * - Understand MVCC and transaction visibility
 */
