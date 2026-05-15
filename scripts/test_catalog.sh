#!/bin/bash
#
# test_catalog.sh - Test CatalogManager implementation
#
# This script:
# 1. Rebuilds the extension
# 2. Reinstalls it to PostgreSQL
# 3. Recreates the extension in the database
# 4. Tests the catalog functions
#

set -e  # Exit on error

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Testing CatalogManager Implementation${NC}"
echo -e "${BLUE}========================================${NC}"

# 1. Check PostgreSQL is running
echo -e "\n${BLUE}[1/5] Checking PostgreSQL...${NC}"
if ! pg_ctl status -D ~/pgdev/data > /dev/null 2>&1; then
    echo -e "${RED}PostgreSQL is not running!${NC}"
    echo "Start it with: pg_ctl start -D ~/pgdev/data"
    exit 1
fi
echo -e "${GREEN}✓ PostgreSQL is running${NC}"

# 2. Rebuild extension
echo -e "\n${BLUE}[2/5] Rebuilding extension...${NC}"
cd /Users/maxim.podkolzin/dev/cpp/rollups
make
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Build successful${NC}"

# 3. Install extension
echo -e "\n${BLUE}[3/5] Installing extension...${NC}"
make install
if [ $? -ne 0 ]; then
    echo -e "${RED}Install failed!${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Install successful${NC}"

# 4. Recreate extension in database
echo -e "\n${BLUE}[4/5] Recreating extension in rollups_test database...${NC}"
psql rollups_test -c "DROP EXTENSION IF EXISTS rollups CASCADE;" > /dev/null
psql rollups_test -c "CREATE EXTENSION rollups;" > /dev/null
echo -e "${GREEN}✓ Extension recreated${NC}"

# 5. Run tests
echo -e "\n${BLUE}[5/5] Running catalog tests...${NC}"

# Test 1: Check that no aggregates exist initially
echo -e "\n${BLUE}Test 1: Check empty catalog${NC}"
psql rollups_test -c "SELECT * FROM rollups.rollup_info;"

# Test 2: Create a test source table
echo -e "\n${BLUE}Test 2: Create test source table${NC}"
psql rollups_test <<EOF
-- Create table in public schema (default search_path)
CREATE TABLE IF NOT EXISTS events (
    id SERIAL PRIMARY KEY,
    event_time TIMESTAMP NOT NULL,
    event_type TEXT,
    value INTEGER
);
EOF
echo -e "${GREEN}✓ Test table created${NC}"

# Test 3: Test exists() - should return false
echo -e "\n${BLUE}Test 3: Test exists() - should return false${NC}"
result=$(psql rollups_test -t -A -c "SELECT rollups.test_aggregate_exists('hourly_events');")
if [ "$result" = "f" ]; then
    echo -e "${GREEN}✓ Correctly returns false${NC}"
else
    echo -e "${RED}✗ Expected false, got: $result${NC}"
fi

# Test 4: Create aggregate entry
echo -e "\n${BLUE}Test 4: Create aggregate entry${NC}"
psql rollups_test -c "
SELECT rollups.test_create_aggregate(
    'hourly_events',
    'events',
    'event_time',
    '1 hour'::interval
) AS agg_id;
"

# Test 5: Test exists() - should return true now
echo -e "\n${BLUE}Test 5: Test exists() - should return true${NC}"
result=$(psql rollups_test -t -A -c "SELECT rollups.test_aggregate_exists('hourly_events');")
if [ "$result" = "t" ]; then
    echo -e "${GREEN}✓ Correctly returns true${NC}"
else
    echo -e "${RED}✗ Expected true, got: $result${NC}"
fi

# Test 6: Load aggregate and show JSON
echo -e "\n${BLUE}Test 6: Load aggregate (as JSON)${NC}"
psql rollups_test -c "
SELECT jsonb_pretty(rollups.test_load_aggregate('hourly_events'));
"

# Test 7: View via rollup_info
echo -e "\n${BLUE}Test 7: View via rollup_info${NC}"
psql rollups_test -c "SELECT * FROM rollups.rollup_info;"

# Test 8: Test error handling - try to create duplicate
echo -e "\n${BLUE}Test 8: Test duplicate detection (should fail)${NC}"
psql rollups_test -c "
SELECT rollups.test_create_aggregate(
    'hourly_events',
    'events',
    'event_time',
    '1 hour'::interval
);
" 2>&1 | grep -q "already exists" && echo -e "${GREEN}✓ Correctly detected duplicate${NC}" || echo -e "${RED}✗ Duplicate detection failed${NC}"

# Test 9: Test non-existent aggregate load (should fail)
echo -e "\n${BLUE}Test 9: Load non-existent aggregate (should fail)${NC}"
psql rollups_test -c "
SELECT rollups.test_load_aggregate('nonexistent');
" 2>&1 | grep -q "does not exist" && echo -e "${GREEN}✓ Correctly detected missing aggregate${NC}" || echo -e "${RED}✗ Missing aggregate detection failed${NC}"

echo -e "\n${BLUE}========================================${NC}"
echo -e "${GREEN}All tests completed!${NC}"
echo -e "${BLUE}========================================${NC}"

# Cleanup
echo -e "\n${BLUE}Cleanup: Dropping test data...${NC}"
psql rollups_test <<EOF > /dev/null
DELETE FROM rollups.continuous_aggregates WHERE agg_name = 'hourly_events';
DROP TABLE IF EXISTS events;
EOF
echo -e "${GREEN}✓ Cleanup complete${NC}"
