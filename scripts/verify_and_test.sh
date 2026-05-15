#!/bin/bash
# Quick verification and testing script for rollups extension

set -e  # Exit on error

echo "=== Rollups Extension Test Script ==="
echo ""

# 1. Check if PostgreSQL is running
echo "1. Checking PostgreSQL status..."
if pg_ctl status -D ~/pgdev/data > /dev/null 2>&1; then
    echo "   ✓ PostgreSQL is running"
else
    echo "   ✗ PostgreSQL is not running"
    echo "   Starting PostgreSQL..."
    pg_ctl start -D ~/pgdev/data -l ~/pgdev/logfile
    sleep 2
fi
echo ""

# 2. Check if rollups_test database exists
echo "2. Checking for rollups_test database..."
if psql -lqt | cut -d \| -f 1 | grep -qw rollups_test; then
    echo "   ✓ rollups_test database exists"
else
    echo "   ✗ rollups_test database not found"
    echo "   Creating rollups_test database..."
    createdb rollups_test
fi
echo ""

# 3. Install the extension
echo "3. Installing rollups extension..."
cd /Users/maxim.podkolzin/dev/cpp/rollups
make install
echo ""

# 4. Create extension in database
echo "4. Loading extension in database..."
psql rollups_test -c "DROP EXTENSION IF EXISTS rollups CASCADE;" 2>/dev/null || true
psql rollups_test -c "CREATE EXTENSION rollups;"
echo ""

# 5. Test basic functions
echo "5. Testing extension functions..."
echo ""
echo "   Testing rollups.version():"
psql rollups_test -c "SELECT rollups.version();"
echo ""
echo "   Testing rollups.time_bucket():"
psql rollups_test -c "SELECT rollups.time_bucket('1 hour', now()::timestamp);"
echo ""
echo "   Testing rollups.time_bucket() with specific timestamp:"
psql rollups_test -c "SELECT rollups.time_bucket('1 hour', '2024-01-15 14:37:22'::timestamp);"
echo ""

# 6. Check metadata tables
echo "6. Checking metadata schema..."
psql rollups_test -c "\d rollups.*"
echo ""

echo "=== All tests passed! ==="
echo ""
echo "Extension is ready for development."
echo "Connect with: psql rollups_test"
