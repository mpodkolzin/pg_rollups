#!/bin/bash
#
# Test ProcessUtility_hook implementation
#
# This script:
# 1. Rebuilds the extension
# 2. Installs it fresh
# 3. Runs DDL commands to trigger the hook
# 4. Shows relevant log output

set -e  # Exit on error

PGDATA="$HOME/pgdev/data"
LOGFILE="$HOME/pgdev/logfile"

echo "=== Testing ProcessUtility Hook ==="
echo

# Check if PostgreSQL is running
if ! pg_ctl status -D "$PGDATA" > /dev/null 2>&1; then
    echo "ERROR: PostgreSQL is not running"
    echo "Start it with: pg_ctl start -D $PGDATA -l $LOGFILE"
    exit 1
fi

# Rebuild and install extension
echo "1. Rebuilding extension..."
make clean > /dev/null 2>&1 || true
make > /dev/null 2>&1
echo "   ✓ Built"

echo "2. Installing extension..."
make install > /dev/null 2>&1
echo "   ✓ Installed"

# Mark current position in log file (so we only show new messages)
if [ -f "$LOGFILE" ]; then
    LOG_START=$(wc -l < "$LOGFILE")
else
    LOG_START=0
fi

echo "3. Loading extension and running DDL commands..."
echo

# Run test commands in psql
psql -d rollups_test -v ON_ERROR_STOP=1 << 'SQL'
-- Drop and recreate extension to ensure fresh _PG_init() call
DROP EXTENSION IF EXISTS rollups CASCADE;
CREATE EXTENSION rollups;

-- Now run some DDL commands that should trigger the hook
CREATE TABLE test_hook (
    id SERIAL PRIMARY KEY,
    ts TIMESTAMP NOT NULL,
    value NUMERIC
);

CREATE INDEX test_hook_ts_idx ON test_hook(ts);

DROP TABLE test_hook;

-- Run a non-DDL query (shouldn't trigger ProcessUtility)
SELECT rollups.version();
SQL

echo
echo "4. Checking PostgreSQL logs for hook messages..."
echo "   (Looking for 'rollups: ProcessUtility hook fired')"
echo

# Show relevant log lines added since we started
if [ -f "$LOGFILE" ]; then
    tail -n +$((LOG_START + 1)) "$LOGFILE" | grep -i "rollups" || echo "   ⚠ No hook messages found in log"
else
    echo "   ⚠ Log file not found: $LOGFILE"
fi

echo
echo "=== Hook Test Complete ==="
echo
echo "If you don't see hook messages above, check your PostgreSQL log settings:"
echo "  psql rollups_test -c \"SHOW log_min_messages;\""
echo "  psql rollups_test -c \"SET log_min_messages = DEBUG1;\""
echo
echo "Or configure in postgresql.conf:"
echo "  log_min_messages = DEBUG1"
echo "  Then restart: pg_ctl restart -D $PGDATA -l $LOGFILE"
