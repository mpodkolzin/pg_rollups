#!/usr/bin/env python3
"""
LLDB Type Formatters for PostgreSQL Types

This script provides custom display formatters for PostgreSQL internal types,
making debugging much easier by showing readable content instead of raw pointers.

Usage:
    In LLDB: command script import scripts/lldb_postgres_formatters.py
    Or add to .lldbinit to load automatically

Supported types:
    - text* / varchar* / bytea* (varlena types)
    - Timestamp / TimestampTz
    - Interval
    - Datum (with type hints)
"""

import lldb
import struct


def text_summary(valobj, internal_dict):
    """
    Format PostgreSQL text* (and other varlena types) as readable strings

    PostgreSQL text is stored as:
        struct varlena {
            int32 vl_len_;   // Length header (includes header itself)
            char vl_dat[];   // Actual data
        }

    This formatter extracts the string from vl_dat and displays it nicely.
    """
    try:
        # Get the pointer value
        addr = valobj.GetValueAsUnsigned()
        if addr == 0:
            return "NULL"

        # Read the length header (first 4 bytes)
        error = lldb.SBError()
        process = valobj.GetProcess()

        # Read vl_len_ (4 bytes, little endian)
        len_bytes = process.ReadMemory(addr, 4, error)
        if error.Fail():
            return f"<error reading length: {error}>"

        vl_len = struct.unpack('<I', len_bytes)[0]

        # The length includes the header itself, so data length is vl_len - 4
        # But actually, let's just read a reasonable amount and null-terminate
        data_len = min(vl_len - 4, 200)  # Cap at 200 chars for display

        # Read the actual string data (starts at offset 4)
        str_bytes = process.ReadMemory(addr + 4, data_len, error)
        if error.Fail():
            return f"<error reading data: {error}>"

        # Decode as UTF-8 (PostgreSQL's default encoding)
        try:
            text = str_bytes.decode('utf-8', errors='replace')
            # Find first null byte if any
            null_pos = text.find('\0')
            if null_pos != -1:
                text = text[:null_pos]
            return f'"{text}"'
        except:
            return f"<binary data, {data_len} bytes>"

    except Exception as e:
        return f"<error: {e}>"


def timestamp_summary(valobj, internal_dict):
    """
    Format PostgreSQL Timestamp as human-readable date/time

    PostgreSQL Timestamp is int64 microseconds since 2000-01-01 00:00:00
    """
    try:
        # Timestamp is just an int64
        ts_us = valobj.GetValueAsSigned()

        if ts_us == 0x7FFFFFFFFFFFFFFF:  # PG_INT64_MAX = infinity
            return "infinity"
        if ts_us == -0x8000000000000000:  # PG_INT64_MIN = -infinity
            return "-infinity"

        # Convert to seconds and microseconds
        ts_sec = ts_us // 1000000
        us = ts_us % 1000000

        # PostgreSQL epoch is 2000-01-01, Unix epoch is 1970-01-01
        # Difference: 946684800 seconds (30 years)
        unix_ts = ts_sec + 946684800

        # Format as ISO 8601
        import datetime
        dt = datetime.datetime.fromtimestamp(unix_ts, tz=datetime.timezone.utc)
        return f'{dt.strftime("%Y-%m-%d %H:%M:%S")}.{us:06d} UTC'

    except Exception as e:
        return f"<error: {e}>"


def interval_summary(valobj, internal_dict):
    """
    Format PostgreSQL Interval structure

    Interval has three components:
        int64 time;   // microseconds
        int32 day;    // days
        int32 month;  // months
    """
    try:
        time_us = valobj.GetChildMemberWithName('time').GetValueAsSigned()
        days = valobj.GetChildMemberWithName('day').GetValueAsSigned()
        months = valobj.GetChildMemberWithName('month').GetValueAsSigned()

        parts = []
        if months != 0:
            years = months // 12
            remaining_months = months % 12
            if years != 0:
                parts.append(f"{years} year{'s' if years != 1 else ''}")
            if remaining_months != 0:
                parts.append(f"{remaining_months} month{'s' if remaining_months != 1 else ''}")

        if days != 0:
            parts.append(f"{days} day{'s' if days != 1 else ''}")

        if time_us != 0:
            hours = time_us // 3600000000
            minutes = (time_us % 3600000000) // 60000000
            seconds = (time_us % 60000000) // 1000000
            us = time_us % 1000000

            time_parts = []
            if hours != 0:
                time_parts.append(f"{hours} hour{'s' if hours != 1 else ''}")
            if minutes != 0:
                time_parts.append(f"{minutes} min")
            if seconds != 0 or us != 0:
                if us != 0:
                    time_parts.append(f"{seconds}.{us:06d} sec")
                else:
                    time_parts.append(f"{seconds} sec")

            if time_parts:
                parts.extend(time_parts)

        if not parts:
            return "00:00:00"

        return " ".join(parts)

    except Exception as e:
        return f"<error: {e}>"


def datum_summary(valobj, internal_dict):
    """
    Format Datum (PostgreSQL's generic type)

    Datum is just a uintptr_t, so we can't know the actual type.
    We'll show it as both a number and potential pointer.
    """
    try:
        val = valobj.GetValueAsUnsigned()
        if val == 0:
            return "0 (NULL or false)"

        # Show as both integer and potential pointer
        return f"0x{val:016x} (or {val} as int)"

    except Exception as e:
        return f"<error: {e}>"


def __lldb_init_module(debugger, internal_dict):
    """
    Called automatically when this script is imported by LLDB.
    Registers type summaries for PostgreSQL types.
    """

    # Register formatters for varlena types (text, varchar, bytea, etc.)
    debugger.HandleCommand('type summary add -F lldb_postgres_formatters.text_summary "text"')
    debugger.HandleCommand('type summary add -F lldb_postgres_formatters.text_summary "text *"')
    debugger.HandleCommand('type summary add -F lldb_postgres_formatters.text_summary "VarChar"')
    debugger.HandleCommand('type summary add -F lldb_postgres_formatters.text_summary "VarChar *"')
    debugger.HandleCommand('type summary add -F lldb_postgres_formatters.text_summary "bytea"')
    debugger.HandleCommand('type summary add -F lldb_postgres_formatters.text_summary "bytea *"')

    # Register formatter for Timestamp
    debugger.HandleCommand('type summary add -F lldb_postgres_formatters.timestamp_summary "Timestamp"')
    debugger.HandleCommand('type summary add -F lldb_postgres_formatters.timestamp_summary "TimestampTz"')

    # Register formatter for Interval
    debugger.HandleCommand('type summary add -F lldb_postgres_formatters.interval_summary "Interval"')
    debugger.HandleCommand('type summary add -F lldb_postgres_formatters.interval_summary "Interval *"')

    # Register formatter for Datum
    debugger.HandleCommand('type summary add -F lldb_postgres_formatters.datum_summary "Datum"')

    print("PostgreSQL type formatters loaded successfully!")
    print("Supported types: text*, Timestamp, Interval, Datum")
