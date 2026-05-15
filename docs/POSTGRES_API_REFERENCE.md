# PostgreSQL Developer API Reference

A curated guide to PostgreSQL's C API documentation and resources for extension development.

---

## Official PostgreSQL Documentation

### 1. Part V - Server Programming
**URL**: https://www.postgresql.org/docs/current/server-programming.html

Your main reference for extension development.

**Key Chapters**:
- **Chapter 38 - Extending SQL**: Custom functions, types, operators
- **Chapter 39 - Triggers**: Trigger functions and event triggers
- **Chapter 40 - Event Triggers**: DDL command hooks
- **Chapter 41 - PL/pgSQL**: (Less relevant for C extensions, but useful for testing)

### 2. Part VII - Internals
**URL**: https://www.postgresql.org/docs/current/internals.html

Deep dive into PostgreSQL architecture.

**Essential Chapters**:
- **Chapter 53 - Overview of PostgreSQL Internals**: Query processing pipeline (parser → planner → executor)
- **Chapter 56 - Writing a Procedural Language Handler**: Function call protocol details
- **Chapter 57 - Writing a Foreign Data Wrapper**: Table access methods
- **Chapter 58 - Writing a Table Sampling Method**: Custom scan providers
- **Chapter 73 - System Catalogs**: Understanding pg_class, pg_attribute, pg_proc, etc.

### 3. C Language Functions
**URL**: https://www.postgresql.org/docs/current/xfunc-c.html

Core API reference for C extension functions.

**Topics Covered**:
- `PG_FUNCTION_INFO_V1()` and version 1 calling convention
- Datum type system (universal value container)
- Memory management (palloc/pfree, memory contexts)
- Error handling (ereport, elog)
- NULL handling (PG_ARGISNULL, PG_RETURN_NULL)
- Function attributes (IMMUTABLE, STRICT, PARALLEL SAFE)

---

## PostgreSQL Source Code Documentation

The **best** reference is the PostgreSQL source itself. You already have it installed!

### Source Tree Location
```bash
cd ~/pgdev/postgresql-source/
```

### Key Directories

```
src/include/          # All header files (your main API reference)
src/backend/          # Implementation code (learn by reading)
contrib/              # Example extensions (learn by example)
src/tutorial/         # Simple examples (good starting point)
```

---

## Important Header Files

These headers contain inline documentation and are your primary API reference.

### Core API Headers

| Header | Purpose | Key Contents |
|--------|---------|--------------|
| `postgres.h` | **Must include first** | Core types, platform macros |
| `fmgr.h` | Function manager | Datum, PG_FUNCTION_*, PG_GETARG_*, PG_RETURN_* |
| `utils/builtins.h` | Built-in functions | Declarations for all built-in PostgreSQL functions |

**Location**: `src/include/`

### Type System Headers

| Header | Purpose | Key Types/Functions |
|--------|---------|---------------------|
| `utils/timestamp.h` | Timestamp operations | Timestamp, TimestampTz, timestamp operations |
| `utils/date.h` | Date/time utilities | Date, Time, Interval types |
| `catalog/pg_type.h` | Type OIDs | INT4OID, TIMESTAMPOID, etc. |
| `utils/array.h` | Array types | Array construction, deconstruction |
| `utils/jsonb.h` | JSONB type | JSONB manipulation |

**Location**: `src/include/utils/`, `src/include/catalog/`

### Memory Management Headers

| Header | Purpose | Key Functions |
|--------|---------|---------------|
| `utils/memutils.h` | Memory contexts | AllocSetContextCreate, MemoryContextSwitchTo |
| `utils/palloc.h` | Memory allocation | palloc, pfree, repalloc |

**Location**: `src/include/utils/`

### Error Handling Headers

| Header | Purpose | Key Macros/Functions |
|--------|---------|----------------------|
| `utils/elog.h` | Error reporting | ereport, elog, errcode, errmsg |
| `postgres_ext.h` | Error codes | ERRCODE_* constants |

**Location**: `src/include/utils/`

### Hook Headers

| Header | Purpose | Hook Types |
|--------|---------|------------|
| `commands/explain.h` | Planner hooks | planner_hook, ExplainOneQuery_hook |
| `executor/executor.h` | Executor hooks | ExecutorStart_hook, ExecutorRun_hook |
| `tcop/utility.h` | Utility hooks | ProcessUtility_hook (for DDL) |

**Location**: `src/include/commands/`, `src/include/executor/`, `src/include/tcop/`

### Reading Header Files

```bash
# Example: Understand timestamps
less ~/pgdev/postgresql-source/src/include/utils/timestamp.h

# Example: See all Datum conversion macros
less ~/pgdev/postgresql-source/src/include/fmgr.h

# Example: Learn about memory contexts
less ~/pgdev/postgresql-source/src/include/utils/memutils.h
```

---

## Example Extensions (Learning by Example)

Your source tree has excellent example extensions to study.

### Location
```bash
cd ~/pgdev/postgresql-source/contrib/
```

### Recommended Examples

#### Simple Extensions (Start Here)
- **`pg_stat_statements/`**: Hooks, shared memory, query tracking
- **`citext/`**: Custom text type (case-insensitive)
- **`hstore/`**: Key-value type, custom operators

#### Intermediate
- **`postgres_fdw/`**: Foreign data wrapper, custom scan nodes
- **`pg_trgm/`**: Trigram indexing, custom index access method
- **`btree_gist/`**: Custom index operator class

#### Advanced
- **`worker_spi/`**: Background worker example
- **`test_decoding/`**: Logical decoding (change capture)

### How to Study Extensions

```bash
# Pick an extension
cd ~/pgdev/postgresql-source/contrib/pg_stat_statements/

# Look at structure
ls -la
# Key files:
# - pg_stat_statements.c      (C implementation)
# - pg_stat_statements.control (metadata)
# - pg_stat_statements--*.sql (SQL installation script)
# - Makefile                   (PGXS build)

# Read implementation
less pg_stat_statements.c

# Build and install to experiment
make
make install
```

---

## Quick API Reference

### Function Declaration Pattern

```c
#include "postgres.h"
#include "fmgr.h"

PG_MODULE_MAGIC;  // Required for all extensions

// Declare function (matches SQL CREATE FUNCTION)
PG_FUNCTION_INFO_V1(my_function);

// Implement function
Datum my_function(PG_FUNCTION_ARGS)
{
    // Get arguments
    int32 arg1 = PG_GETARG_INT32(0);

    // Do work...
    int32 result = arg1 * 2;

    // Return result
    PG_RETURN_INT32(result);
}
```

### Getting Function Arguments

```c
// From: src/include/fmgr.h (lines ~240-400)

// Integer types
PG_GETARG_INT16(n)
PG_GETARG_INT32(n)
PG_GETARG_INT64(n)

// Floating point
PG_GETARG_FLOAT4(n)
PG_GETARG_FLOAT8(n)

// Boolean
PG_GETARG_BOOL(n)

// Text (variable-length)
PG_GETARG_TEXT_PP(n)     // Most common, supports TOAST
PG_GETARG_TEXT_P(n)      // Alternative
PG_GETARG_CSTRING(n)     // C string (null-terminated)

// Date/Time
PG_GETARG_TIMESTAMP(n)
PG_GETARG_TIMESTAMPTZ(n)
PG_GETARG_DATE(n)
PG_GETARG_INTERVAL_P(n)

// Arrays
PG_GETARG_ARRAYTYPE_P(n)

// Pointers (generic)
PG_GETARG_POINTER(n)
PG_GETARG_DATUM(n)       // Raw Datum

// NULL checking
PG_ARGISNULL(n)          // Returns true if argument is SQL NULL
```

### Returning Values

```c
// From: src/include/fmgr.h

// Integer types
PG_RETURN_INT16(x)
PG_RETURN_INT32(x)
PG_RETURN_INT64(x)

// Floating point
PG_RETURN_FLOAT4(x)
PG_RETURN_FLOAT8(x)

// Boolean
PG_RETURN_BOOL(x)

// Text
PG_RETURN_TEXT_P(x)
PG_RETURN_CSTRING(x)

// Date/Time
PG_RETURN_TIMESTAMP(x)
PG_RETURN_TIMESTAMPTZ(x)
PG_RETURN_DATE(x)
PG_RETURN_INTERVAL_P(x)

// Pointers
PG_RETURN_POINTER(x)
PG_RETURN_DATUM(x)

// NULL
PG_RETURN_NULL()
```

### Memory Management

```c
// From: src/include/utils/palloc.h

void *palloc(Size size);              // Allocate in current memory context
void *palloc0(Size size);             // Allocate and zero
void pfree(void *pointer);            // Free memory
void *repalloc(void *pointer, Size size); // Realloc

// NEVER use malloc/free in PostgreSQL code!
// NEVER use C++ new/delete in PostgreSQL-callable code!
// Always use palloc/pfree
```

### Memory Contexts

```c
// From: src/include/utils/memutils.h

// Current context
extern MemoryContext CurrentMemoryContext;

// Built-in contexts
TopMemoryContext          // Lives for entire backend lifetime
CacheMemoryContext        // For cache data
MessageContext            // For error messages

// Create custom context
MemoryContext ctx = AllocSetContextCreate(
    CurrentMemoryContext,
    "My Context",
    ALLOCSET_DEFAULT_SIZES
);

// Switch context
MemoryContext old = MemoryContextSwitchTo(ctx);
// ... allocations here go into ctx ...
MemoryContextSwitchTo(old);

// Delete context (frees all allocations in it)
MemoryContextDelete(ctx);
```

### Error Handling

```c
// From: src/include/utils/elog.h

// Report error (aborts transaction)
ereport(ERROR,
    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
     errmsg("invalid value: %d", value),
     errdetail("Value must be positive."),
     errhint("Use a positive integer.")));

// Log messages (don't abort)
ereport(LOG,
    (errmsg("processing %d rows", count)));

// Levels: DEBUG5, DEBUG4, DEBUG3, DEBUG2, DEBUG1, LOG, NOTICE, WARNING, ERROR, FATAL, PANIC

// Simple logging (for debugging)
elog(WARNING, "debug value: %d", value);

// Error codes from: src/include/postgres_ext.h
ERRCODE_INVALID_PARAMETER_VALUE
ERRCODE_DIVISION_BY_ZERO
ERRCODE_OUT_OF_MEMORY
// ... many more
```

### String Operations

```c
// From: src/include/utils/builtins.h

// text* to C string
char *text_to_cstring(const text *t);

// C string to text*
text *cstring_to_text(const char *s);

// String length
#define VARSIZE(PTR)         // Total size including header
#define VARDATA(PTR)         // Pointer to actual data
#define VARSIZE_ANY_EXHDR(PTR) // Data length

// Example usage
text *input = PG_GETARG_TEXT_PP(0);
char *str = text_to_cstring(input);
// ... use str ...
pfree(str);  // Don't forget to free!
```

### Type Conversion

```c
// From: src/include/fmgr.h

// Manual Datum conversions (if not using PG_GETARG_* macros)
Int32GetDatum(int32 x)
DatumGetInt32(Datum d)

Int64GetDatum(int64 x)
DatumGetInt64(Datum d)

Float8GetDatum(float8 x)
DatumGetFloat8(Datum d)

BoolGetDatum(bool x)
DatumGetBool(Datum d)

PointerGetDatum(void *x)
DatumGetPointer(Datum d)

// Check if Datum is pass-by-value or pass-by-reference
// (Usually don't need this, but good to know)
DatumGetPointer(d)   // Assumes pass-by-reference
```

---

## PostgreSQL Wiki & Community Resources

### PostgreSQL Wiki
**URL**: https://wiki.postgresql.org/wiki/Main_Page

**Key Pages**:
- **Developer FAQ**: https://wiki.postgresql.org/wiki/Developer_FAQ
- **So You Want to Be a Developer**: https://wiki.postgresql.org/wiki/So,_you_want_to_be_a_developer%3F
- **Coding Conventions**: https://wiki.postgresql.org/wiki/Coding_Conventions
- **Extension Building**: https://wiki.postgresql.org/wiki/ExtensionBuilding

### Mailing Lists
- **pgsql-hackers**: For internals discussions
- **pgsql-general**: For general PostgreSQL questions

Archives: https://www.postgresql.org/list/

### Community Extensions (Learn from)

**Modern C++ Extension**:
- **pgvector**: https://github.com/pgvector/pgvector
  - Uses C++, custom index access method, great example

**Background Workers**:
- **pg_cron**: https://github.com/citusdata/pg_cron
  - Cron-like job scheduling, uses bgworkers

**Advanced Hooks**:
- **TimescaleDB**: https://github.com/timescale/timescaledb
  - Planner/executor hooks, custom scan nodes, continuous aggregates

**Logical Decoding**:
- **wal2json**: https://github.com/eulerto/wal2json
  - Change data capture using logical decoding

---

## Finding Information with grep

The PostgreSQL source is massive. Use grep effectively:

### Find Function Definitions
```bash
cd ~/pgdev/postgresql-source

# Find where a function is defined
grep -rn "^Datum timestamp_" src/backend/utils/adt/

# Find all uses of a macro
grep -rn "PG_GETARG_TIMESTAMP" src/backend/

# Find error code usage
grep -rn "ERRCODE_INVALID_PARAMETER_VALUE" contrib/
```

### Find Type Definitions
```bash
# Find struct definitions
grep -rn "^typedef struct.*Timestamp" src/include/

# Find all OID definitions
grep -n "OID" src/include/catalog/pg_type.h
```

### Find Hook Definitions
```bash
# Find where hooks are declared
grep -rn "_hook" src/include/

# Find where hooks are called
grep -rn "planner_hook" src/backend/
```

### Find Examples
```bash
# Find extensions that use specific features
cd ~/pgdev/postgresql-source/contrib/

# Which extensions use background workers?
grep -l "RegisterBackgroundWorker" */*.c

# Which extensions use hooks?
grep -l "planner_hook" */*.c
```

---

## Understanding Datum (Deep Dive)

### What is Datum?

```c
// From: src/include/postgres.h
typedef uintptr_t Datum;  // Platform-specific unsigned integer (pointer-sized)
```

Datum is PostgreSQL's **universal type** for passing any value between C code and the database.

### Why Datum Exists

PostgreSQL's type system is:
- **Dynamic**: Types determined at runtime
- **Extensible**: Users can create custom types
- **C-based**: No templates or overloading

Datum provides **type erasure** - one type that can hold any PostgreSQL type.

### How Datum Works

Datum stores data in **two ways**:

#### 1. Pass-by-Value (small types, ≤8 bytes on 64-bit)
Value stored directly in the Datum:
```c
int32 value = 42;
Datum d = Int32GetDatum(value);  // Value copied
int32 result = DatumGetInt32(d); // Value extracted
```

#### 2. Pass-by-Reference (large or variable-length types)
Datum holds a **pointer** to the data:
```c
text *str = cstring_to_text("hello");
Datum d = PointerGetDatum(str);  // Pointer stored
text *result = DatumGetTextP(d); // Pointer extracted
```

### Determining Pass-by-Value vs Pass-by-Reference

Check `src/include/catalog/pg_type.h`:
```c
// Pass-by-value types (typbyval = true)
int16, int32, int64, float4, float8, bool, char, oid

// Pass-by-reference types (typbyval = false)
text, varchar, bytea, numeric, arrays, composite types
```

### When to Use Datum Directly

**Usually never!** Always use the macros:
```c
// DON'T DO THIS:
Datum d = fcinfo->args[0];  // Raw access
int32 value = *((int32*)&d); // Manual extraction

// DO THIS:
int32 value = PG_GETARG_INT32(0);  // Safe macro
```

**Only use raw Datum when**:
- Implementing generic container functions
- Working with polymorphic functions (anyelement)
- Building dynamic queries

---

## Common Patterns

### Pattern 1: Simple Function
```c
PG_FUNCTION_INFO_V1(add_one);
Datum add_one(PG_FUNCTION_ARGS)
{
    int32 input = PG_GETARG_INT32(0);
    PG_RETURN_INT32(input + 1);
}
```

### Pattern 2: NULL Handling
```c
PG_FUNCTION_INFO_V1(safe_divide);
Datum safe_divide(PG_FUNCTION_ARGS)
{
    // Check for NULL arguments
    if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
        PG_RETURN_NULL();

    int32 a = PG_GETARG_INT32(0);
    int32 b = PG_GETARG_INT32(1);

    if (b == 0)
        ereport(ERROR,
            (errcode(ERRCODE_DIVISION_BY_ZERO),
             errmsg("division by zero")));

    PG_RETURN_INT32(a / b);
}
```

### Pattern 3: Working with Text
```c
PG_FUNCTION_INFO_V1(text_length);
Datum text_length(PG_FUNCTION_ARGS)
{
    text *input = PG_GETARG_TEXT_PP(0);

    // Get length (excluding header)
    int32 len = VARSIZE_ANY_EXHDR(input);

    PG_RETURN_INT32(len);
}
```

### Pattern 4: Memory Context Switching
```c
PG_FUNCTION_INFO_V1(cache_data);
Datum cache_data(PG_FUNCTION_ARGS)
{
    // Switch to long-lived context
    MemoryContext oldcontext = MemoryContextSwitchTo(CacheMemoryContext);

    // Allocate in cache context
    char *cached = palloc(1024);
    strcpy(cached, "persistent data");

    // Switch back
    MemoryContextSwitchTo(oldcontext);

    PG_RETURN_POINTER(cached);
}
```

---

## C++ Integration Notes

When writing C++ extensions (like our rollups project):

### Required Patterns

```cpp
// 1. PostgreSQL functions must be extern "C"
extern "C" {
    PG_MODULE_MAGIC;

    PG_FUNCTION_INFO_V1(my_function);
    Datum my_function(PG_FUNCTION_ARGS);
}

// 2. Implementation can be C++
Datum my_function(PG_FUNCTION_ARGS)
{
    // Can use C++ here (classes, STL, etc.)
    std::vector<int> vec = {1, 2, 3};

    // But must use palloc for PostgreSQL-visible memory
    int32 result = vec.size();
    PG_RETURN_INT32(result);
}
```

### Restrictions

- **No C++ exceptions**: PostgreSQL uses setjmp/longjmp
- **Use palloc, not new**: For PostgreSQL-managed memory
- **extern "C"**: All PostgreSQL-callable functions
- **Careful with destructors**: May not run if ereport(ERROR) is called

### Safe C++ Features

✅ **Safe to use**:
- Classes and namespaces
- STL containers (with caution about memory)
- Templates, auto, lambdas
- Modern C++ features (C++17)

❌ **Avoid**:
- Exceptions (use ereport instead)
- new/delete for PostgreSQL memory (use palloc/pfree)
- Global constructors/destructors (may not run as expected)

---

## Quick Start Checklist

When implementing a new extension function:

- [ ] Include `postgres.h` first
- [ ] Include `fmgr.h` for Datum/PG_FUNCTION_*
- [ ] Declare with `PG_FUNCTION_INFO_V1(name)`
- [ ] Use `PG_GETARG_*` to extract arguments
- [ ] Check for NULLs with `PG_ARGISNULL()` if needed
- [ ] Use `palloc`/`pfree`, never `malloc`/`free`
- [ ] Use `ereport(ERROR, ...)` for errors
- [ ] Return with `PG_RETURN_*` macros
- [ ] Test with `CREATE EXTENSION` and SQL queries

---

## Further Reading

### Books
- **PostgreSQL Server Programming** (Packt)
- **The Internals of PostgreSQL** (online): http://www.interdb.jp/pg/

### Conference Talks
- Search YouTube for "PGCon" talks on extensions
- "Writing PostgreSQL Extensions" talks from various PG conferences

### Source Code Tours
- Read `src/backend/executor/README` for executor overview
- Read `src/backend/optimizer/README` for planner overview
- Study `contrib/` extensions matching your interests

---

## TL;DR - Essential Bookmarks

1. **C Functions Guide**: https://www.postgresql.org/docs/current/xfunc-c.html
2. **Server Programming**: https://www.postgresql.org/docs/current/server-programming.html
3. **Internals Overview**: https://www.postgresql.org/docs/current/internals.html
4. **Your Local Headers**: `~/pgdev/postgresql-source/src/include/`
5. **Example Extensions**: `~/pgdev/postgresql-source/contrib/`

---

**Last Updated**: 2026-03-13
**PostgreSQL Version**: 16.6
**Target Audience**: C/C++ extension developers learning PostgreSQL internals
