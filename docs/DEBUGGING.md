# Debugging PostgreSQL Extensions with LLDB (macOS)

**Target**: Debug our `rollups` extension using LLDB on macOS
**Prerequisites**: PostgreSQL built with debug symbols (`--enable-debug`, `-ggdb -O0`)

---

## Why LLDB (Not GDB) on macOS

- **GDB on macOS is broken**: Code signing issues, poor integration with modern macOS
- **LLDB is the native debugger**: Built by Apple, works seamlessly with macOS
- **Same concepts**: If you know GDB, LLDB is very similar (just different command syntax)

---

## Quick Reference: GDB vs LLDB Commands

| Task                     | GDB                          | LLDB                                |
|--------------------------|------------------------------|-------------------------------------|
| Attach to process        | `gdb -p <pid>`               | `lldb -p <pid>`                     |
| Set breakpoint (function)| `break function_name`        | `breakpoint set --name function_name` or `b function_name` |
| Set breakpoint (file:line)| `break file.c:42`           | `breakpoint set --file file.c --line 42` or `b file.c:42` |
| List breakpoints         | `info breakpoints`           | `breakpoint list`                   |
| Delete breakpoint        | `delete 1`                   | `breakpoint delete 1`               |
| Continue execution       | `continue` or `c`            | `continue` or `c`                   |
| Step over (next line)    | `next` or `n`                | `next` or `n`                       |
| Step into (enter function)| `step` or `s`               | `step` or `s`                       |
| Finish current function  | `finish`                     | `finish`                            |
| Print variable           | `print variable`             | `print variable` or `p variable`    |
| Print all locals         | `info locals`                | `frame variable` or `fr v`          |
| Backtrace (stack trace)  | `backtrace` or `bt`          | `thread backtrace` or `bt`          |
| Quit debugger            | `quit`                       | `quit`                              |

**Pro tip**: Many GDB commands work in LLDB (like `p`, `bt`, `c`, `n`, `s`) for convenience!

---

## Debugging Workflow Overview

The typical debugging workflow for PostgreSQL extensions:

```
1. Start PostgreSQL
2. Connect with psql to trigger backend creation
3. Find the backend process ID (PID)
4. Attach LLDB to that backend
5. Set breakpoints in your extension code
6. Run SQL commands in psql to trigger breakpoints
7. Step through code, inspect variables
8. Detach debugger when done
```

**Key insight**: Each `psql` connection gets its own backend process. You debug ONE specific connection.

---

## Step 1: Find Your Backend PID

### Option A: From within psql (easiest)

```sql
-- Connect to your database
psql rollups_test

-- Get the PID of YOUR current connection
SELECT pg_backend_pid();
```

Output example:
```
 pg_backend_pid
----------------
          12345
```

That's your PID! Keep this terminal open.

### Option B: From command line

```bash
# Show all PostgreSQL processes
ps aux | grep postgres

# Look for lines like:
# postgres: <user> rollups_test [local] idle
```

The number after "postgres" in the output is the PID.

### Option C: From pg_stat_activity (useful for finding other connections)

```sql
SELECT pid, usename, datname, application_name, state, query
FROM pg_stat_activity
WHERE datname = 'rollups_test';
```

---

## Step 2: Attach LLDB to Backend

**Important**: Keep your `psql` session open in one terminal, use another terminal for LLDB.

```bash
# Open a NEW terminal window
# Attach LLDB to the backend process
lldb -p <PID>

# Example:
lldb -p 12345
```

You should see output like:
```
(lldb) process attach --pid 12345
Process 12345 stopped
* thread #1, queue = 'com.apple.main-thread', stop reason = signal SIGSTOP
...
(lldb)
```

**If you get "Operation not permitted"**: See Troubleshooting section below.

---

## Step 3: Set Breakpoints in Our Extension

Now we'll set breakpoints in our `rollups` extension functions.

### Breakpoint in time_bucket() function

```lldb
# Set breakpoint by function name
(lldb) breakpoint set --name time_bucket_timestamp
# or shorter:
(lldb) b time_bucket_timestamp

# Verify breakpoint was set
(lldb) breakpoint list
```

Output should show:
```
Current breakpoints:
1: name = 'time_bucket_timestamp', locations = 1
```

**Note**: We use the C function name `time_bucket_timestamp`, not the SQL function name `time_bucket`.

### Breakpoint in version() function

```lldb
(lldb) b rollups_version

# Verify
(lldb) breakpoint list
```

### Breakpoint by file and line number

```lldb
# Set breakpoint at specific line in rollups.cpp
(lldb) breakpoint set --file rollups.cpp --line 145
# or shorter:
(lldb) b rollups.cpp:145
```

### Continue execution after setting breakpoints

```lldb
(lldb) continue
# or
(lldb) c
```

Your backend process is now running and waiting for SQL commands.

---

## Step 4: Trigger Breakpoints from psql

Go back to your `psql` terminal and run SQL commands:

```sql
-- This should hit the breakpoint in rollups_version()
SELECT rollups.version();
```

Your LLDB terminal should now show:
```
Process 12345 stopped
* thread #1, stop reason = breakpoint 1.1
    frame #0: 0x00000001234abcde rollups.so`rollups_version(fcinfo=0x...) at rollups.cpp:67
   64    */
   65   extern "C" Datum rollups_version(PG_FUNCTION_ARGS) {
   66       // Return a text value
-> 67       const char *version_str = "Rollups Extension 1.0";
   68       text *result = cstring_to_text(version_str);
   69       PG_RETURN_TEXT_P(result);
   70   }
(lldb)
```

🎉 **You're now debugging your extension!**

---

## Step 5: Inspect Variables and Step Through Code

### Print variables

```lldb
# Print the version string (won't exist until line 67 executes)
(lldb) next  # Execute line 67
(lldb) print version_str
(const char *) $0 = 0x00000001234abcde "Rollups Extension 1.0"

# Print the result text pointer
(lldb) next  # Execute line 68
(lldb) print result
(text *) $1 = 0x00007f8a5c000100
```

### Step through code

```lldb
# Execute current line, move to next line
(lldb) next
# or
(lldb) n

# Step INTO a function call
(lldb) step
# or
(lldb) s

# Finish current function and return to caller
(lldb) finish
```

### View all local variables

```lldb
(lldb) frame variable
# or shorter:
(lldb) fr v
```

### View the call stack (backtrace)

```lldb
(lldb) thread backtrace
# or
(lldb) bt
```

This shows how you got to this function (useful for understanding PostgreSQL's call chain):
```
* thread #1
  * frame #0: rollups.so`rollups_version(fcinfo=0x...) at rollups.cpp:67
    frame #1: postgres`ExecInterpExpr(...) at execExprInterp.c:1234
    frame #2: postgres`evaluate_expr(...) at clauses.c:4567
    ...
```

### Continue to next breakpoint

```lldb
(lldb) continue
# or
(lldb) c
```

Your `psql` session will now complete the query and show results.

---

## Practical Example: Debug time_bucket()

Let's debug our time bucketing function to see how it works.

### 1. Set breakpoint

```lldb
(lldb) b time_bucket_timestamp
(lldb) c
```

### 2. Trigger from psql

```sql
SELECT rollups.time_bucket('1 hour', '2024-01-15 14:37:22'::timestamp);
```

### 3. LLDB should stop at the breakpoint

```
(lldb)
Process 12345 stopped
* frame #0: rollups.so`time_bucket_timestamp(fcinfo=0x...) at rollups.cpp:145
```

### 4. Inspect function arguments

```lldb
# Print the function call info structure
(lldb) print fcinfo
(FunctionCallInfo) $0 = 0x00007f8a5c000200

# We can't directly access fcinfo->args easily, but we can step through
(lldb) next  # Step to line that extracts interval_text
(lldb) next  # Step to line that extracts timestamp
```

After stepping to line ~150 where we extract the timestamp:

```lldb
(lldb) print timestamp
(Timestamp) $1 = 822841042000000

# This is microseconds since 2000-01-01
# Let's see the interval string
(lldb) print interval_text
(text *) $2 = 0x00007f8a5c000300
```

### 5. Step through the parsing logic

```lldb
# Step through interval parsing
(lldb) next
(lldb) next
# ... keep stepping to see how we convert "1 hour" to microseconds

# When we reach the bucket_us variable:
(lldb) print bucket_us
(int64) $3 = 3600000000  # 1 hour = 3.6 billion microseconds
```

### 6. See the bucketing arithmetic

```lldb
# Step to the bucketing calculation line
(lldb) next
(lldb) print timestamp / bucket_us
(int64) $4 = 228567  # Number of complete buckets since epoch

(lldb) print (timestamp / bucket_us) * bucket_us
(int64) $5 = 822361200000000  # Timestamp truncated to hour boundary
```

### 7. Continue to see the result

```lldb
(lldb) finish  # Complete the function

# Back in psql, you should see:
# 2024-01-15 14:00:00
```

---

## Step 6: Detach When Done

When you're finished debugging:

```lldb
# Detach from the process (backend keeps running)
(lldb) detach

# Or quit LLDB entirely (this will terminate the backend!)
(lldb) quit
```

**Important**:
- `detach` - Backend continues normally, you can keep using psql
- `quit` - Backend is killed, psql connection is lost

---

## VS Code Integration

Create `.vscode/launch.json` in your project:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Attach to PostgreSQL Backend",
      "type": "lldb",
      "request": "attach",
      "program": "${env:HOME}/pgdev/postgres/bin/postgres",
      "pid": "${command:pickProcess}",
      "sourceMap": {
        "/path/to/source/during/build": "${workspaceFolder}"
      }
    },
    {
      "name": "Attach to Backend (Manual PID)",
      "type": "lldb",
      "request": "attach",
      "program": "${env:HOME}/pgdev/postgres/bin/postgres",
      "pid": "${input:backendPID}"
    }
  ],
  "inputs": [
    {
      "id": "backendPID",
      "type": "promptString",
      "description": "Backend PID (get from SELECT pg_backend_pid())",
      "default": ""
    }
  ]
}
```

**Prerequisites**: Install the "CodeLLDB" extension in VS Code.

### How to use:

1. Get backend PID from psql: `SELECT pg_backend_pid();`
2. In VS Code: Press F5 or Run > Start Debugging
3. Choose "Attach to Backend (Manual PID)"
4. Enter the PID when prompted
5. Set breakpoints by clicking left of line numbers
6. Run SQL in psql to trigger breakpoints

---

## Common Debugging Patterns

### Pattern 1: Debugging Crashes (ereport, segfaults)

If your extension crashes, you want to see WHERE it crashed:

```bash
# After crash, if backend didn't exit, LLDB should show the crash location
# If backend exited, check PostgreSQL logs:
tail -f ~/pgdev/logfile

# Look for lines like:
# server process (PID 12345) was terminated by signal 11: Segmentation fault
```

To debug a crash:

1. Attach LLDB BEFORE running the crashing SQL
2. Let it crash in the debugger (don't set breakpoints)
3. When it crashes, LLDB stops at the crash location
4. Use `bt` to see the call stack
5. Use `frame select <n>` to jump to different stack frames
6. Use `fr v` to see local variables at each level

```lldb
# After crash:
(lldb) bt
* thread #1
  * frame #0: rollups.so`buggy_function + 42  # <-- Crashed here!
    frame #1: postgres`FunctionCall1...
    ...

# Jump to the crash frame
(lldb) frame select 0
(lldb) fr v  # See all local variables
```

### Pattern 2: Debugging ereport() Errors

When your extension calls `ereport(ERROR, ...)`, PostgreSQL throws an exception:

```cpp
if (bucket_us <= 0) {
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
             errmsg("time bucket must be positive")));
}
```

To debug:

```lldb
# Set breakpoint on the errfinish function (called by ereport)
(lldb) b errfinish

# When it hits, check the backtrace to see where ereport was called
(lldb) bt
* frame #0: postgres`errfinish
  * frame #1: rollups.so`time_bucket_timestamp + 234  # <-- Your ereport call
    frame #2: ...
```

### Pattern 3: Inspecting PostgreSQL Internals

Sometimes you need to inspect PostgreSQL's internal structures:

```lldb
# Inspect a memory context
(lldb) print *CurrentMemoryContext
(MemoryContextData) $0 = {
  type = T_AllocSetContext
  name = 0x00001234 "MessageContext"
  ...
}

# Inspect a Datum (PostgreSQL's generic value type)
(lldb) print my_datum
(Datum) $1 = 12345678

# To interpret a Datum, you need to know its type:
# - If it's an int: (int32)my_datum
# - If it's a pointer: (void*)my_datum
# - If it's text: (text*)DatumGetPointer(my_datum)

# Example: inspect a text* value
(lldb) print *((text*)my_datum)
(text) $2 = {
  vl_len_ = 24  # VARSIZE
  vl_dat = "hello world"
}
```

### Pattern 4: Conditional Breakpoints

Set a breakpoint that only triggers when a condition is true:

```lldb
# Break only when bucket_us is 0 (likely a bug)
(lldb) b rollups.cpp:180
(lldb) breakpoint modify 1 --condition 'bucket_us == 0'

# Break only when timestamp is negative
(lldb) b time_bucket_timestamp
(lldb) breakpoint modify 2 --condition 'timestamp < 0'
```

### Pattern 5: Watchpoints (Break on Variable Change)

Watch a variable and break whenever it's modified:

```lldb
# Break when a variable's value changes
(lldb) watchpoint set variable timestamp
(lldb) c

# Break when memory at an address changes
(lldb) watchpoint set expression -- 0x12345678
```

---

## Troubleshooting

### Issue: "Operation not permitted" when attaching LLDB

**Cause**: macOS System Integrity Protection (SIP) and code signing restrictions.

**Solution**:

**Option 1 (Recommended)**: Add Terminal to Developer Tools

1. Open System Settings → Privacy & Security → Developer Tools
2. Add your terminal app (Terminal.app or iTerm.app)
3. Try attaching LLDB again

**Option 2**: Sign the debugger (more complex)

```bash
# Create an entitlements file
cat > lldb-entitlements.xml <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.cs.debugger</key>
    <true/>
</dict>
</plist>
EOF

# Re-sign LLDB with entitlements
sudo codesign --entitlements lldb-entitlements.xml -fs gdb-cert $(which lldb)
```

**Option 3**: Disable SIP (NOT RECOMMENDED for security reasons)

Only as a last resort: https://developer.apple.com/documentation/security/disabling_and_enabling_system_integrity_protection

---

### Issue: "No symbols loaded" or can't find function names

**Symptoms**:
- Breakpoints don't work
- Function names not found
- Source code not shown

**Causes**:
- PostgreSQL not built with debug symbols
- Extension not built with debug symbols
- Wrong path to source code

**Solutions**:

```bash
# Check if PostgreSQL has debug symbols
file ~/pgdev/postgres/bin/postgres
# Should say: "with debug_info, not stripped"

# Check if extension has debug symbols
file build/rollups.so
# Should say: "with debug_info, not stripped"

# Rebuild PostgreSQL with debug symbols if needed
cd ~/pgdev/postgresql-source
./configure --enable-debug --enable-cassert CFLAGS="-ggdb -O0 -g3"
make clean
make -j$(sysctl -n hw.ncpu)
make install

# Rebuild extension with debug symbols
cd ~/dev/cpp/rollups
make clean
make
```

---

### Issue: Breakpoint set but never hit

**Possible causes**:

1. **Function not called**: SQL command doesn't actually call your function
2. **Wrong function name**: Use C function name, not SQL function name
3. **Extension not loaded**: Run `CREATE EXTENSION rollups;` first
4. **Attached to wrong backend**: Each psql connection is a different backend PID

**Debug steps**:

```lldb
# List all breakpoints
(lldb) breakpoint list

# Check if symbol exists
(lldb) image lookup --name time_bucket_timestamp

# If not found, the function isn't loaded or has a different name
# Try listing all symbols in the extension:
(lldb) image list  # Find rollups.so
(lldb) image dump symtab rollups.so  # List all symbols
```

---

### Issue: Source code not shown, only assembly

**Cause**: LLDB can't find the source files.

**Solution**: Tell LLDB where to find source code:

```lldb
# Set source path mapping
(lldb) settings set target.source-map /old/path /new/path

# Example:
(lldb) settings set target.source-map /tmp/build /Users/maxim.podkolzin/dev/cpp/rollups

# Or in VS Code launch.json, add:
"sourceMap": {
  "/old/path": "${workspaceFolder}"
}
```

---

### Issue: Variables show "<optimized out>"

**Cause**: Code was compiled with optimizations (`-O2`, `-O3`).

**Solution**: Rebuild with `-O0` (no optimization):

```bash
# Our CMakeLists.txt should already have this for DEBUG build
cd ~/dev/cpp/rollups
make clean
make
```

Check `CMakeLists.txt` has:
```cmake
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -ggdb3")
```

---

### Issue: Debugger breaks at "random" places

**Cause**: Compiler optimizations reordered code, or you're stepping through inlined functions.

**Solution**:
- Use `finish` to exit the current function
- Use `continue` to go to the next breakpoint
- Rebuild with `-O0` to disable inlining

---

## Advanced: Debugging PostgreSQL Core

Sometimes you need to debug PostgreSQL's own code (not just your extension):

```lldb
# Set breakpoint in PostgreSQL planner
(lldb) b standard_planner

# Set breakpoint in executor
(lldb) b ExecutorRun

# Set breakpoint in memory allocator
(lldb) b palloc

# This is useful for understanding how PostgreSQL calls your extension
```

**Example**: See how PostgreSQL calls your function:

```lldb
(lldb) b ExecInterpExpr  # Executor's expression evaluation
(lldb) c
# Now run your SQL query
# LLDB will stop in the executor before calling your function
# Use 'step' to step into your function call
```

---

## Tips and Best Practices

### 1. Use a debugging script

Create `scripts/debug_attach.sh`:

```bash
#!/bin/bash
# Attach LLDB to a PostgreSQL backend

if [ -z "$1" ]; then
    echo "Usage: $0 <backend_pid>"
    echo "Get PID from: psql -c 'SELECT pg_backend_pid()'"
    exit 1
fi

echo "Attaching LLDB to PostgreSQL backend $1..."
echo "Tip: Use 'b time_bucket_timestamp' to set a breakpoint"
echo "     Use 'c' to continue execution"
lldb -p $1
```

### 2. Keep a LLDB command file

Create `.lldbinit` in your project:

```lldb
# Project-specific LLDB configuration for rollups extension

# Common breakpoints (commented out, uncomment as needed)
# b time_bucket_timestamp
# b rollups_version

# Display settings
settings set stop-disassembly-display never
settings set stop-line-count-after 3
settings set stop-line-count-before 3

# Aliases for common commands
command alias reload process detach && process attach --pid %1

echo Rollups extension debugging initialized
echo Common commands:
echo   b time_bucket_timestamp  - Break in time bucketing function
echo   b rollups_version        - Break in version function
echo   fr v                     - Show local variables
echo   bt                       - Show call stack
```

### 3. Use psql variables for PID

In psql, save your PID for easy reference:

```sql
\set mypid `echo $$ `
SELECT pg_backend_pid() AS backend_pid \gset

-- Now you can reference :backend_pid in scripts
\echo 'Attach debugger to backend: :backend_pid'
```

### 4. Debug logging alongside LLDB

Enable verbose PostgreSQL logging in another terminal:

```bash
tail -f ~/pgdev/logfile
```

This way you see log messages while debugging.

### 5. Test in a transaction

Wrap your test SQL in a transaction so you can rollback:

```sql
BEGIN;
SELECT rollups.time_bucket('1 hour', now()::timestamp);
-- Debug, inspect results
ROLLBACK;  -- Undo any changes
```

---

## Summary: Quick Debugging Workflow

**The 5-minute debug session**:

```bash
# Terminal 1: Start psql and get PID
psql rollups_test
SELECT pg_backend_pid();  -- Note the PID, e.g., 12345

# Terminal 2: Attach LLDB
lldb -p 12345
(lldb) b time_bucket_timestamp
(lldb) c

# Terminal 1: Trigger breakpoint
SELECT rollups.time_bucket('1 hour', now()::timestamp);

# Terminal 2: Debug
(lldb) fr v  -- See variables
(lldb) n     -- Step through
(lldb) c     -- Continue

# Terminal 1: See result in psql

# Terminal 2: Detach when done
(lldb) detach
```

**That's it!** You're now debugging your PostgreSQL extension.

---

## Next Steps

Now that you can debug:

1. **Practice**: Set breakpoints in `rollups_version()` and `time_bucket_timestamp()`
2. **Explore**: Step through PostgreSQL's call chain (ExecutorRun → your function)
3. **Learn PostgreSQL internals**: Set breakpoints in planner, executor, memory allocator
4. **Get ready for hooks**: When we implement hooks, you'll be able to debug them!

---

**Happy debugging! 🐛🔍**

*This guide is part of the Rollups Extension project - see CLAUDE.md for project overview*
