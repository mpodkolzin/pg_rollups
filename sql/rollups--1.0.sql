-- rollups--1.0.sql
-- SQL installation script for rollups extension version 1.0
--
-- This script runs when: CREATE EXTENSION rollups;
-- It defines all SQL-visible objects (functions, tables, views, etc.)

-- Complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION rollups" to load this file. \quit

-- ============================================================================
-- SCHEMA CREATION
-- ============================================================================
-- The schema 'rollups' is automatically created by PostgreSQL because we
-- specified 'schema = rollups' in rollups.control. We don't need to (and
-- shouldn't) create it explicitly here.

-- ============================================================================
-- PART 1: Basic utility functions (learning examples)
-- ============================================================================

-- Simple C function: returns extension version
CREATE FUNCTION rollups.version()
RETURNS text
AS 'MODULE_PATHNAME', 'rollups_version'
LANGUAGE C IMMUTABLE STRICT;

COMMENT ON FUNCTION rollups.version() IS
'Returns the version of the rollups extension';

-- Time bucket function: core functionality for rollups
CREATE FUNCTION rollups.time_bucket(bucket_width interval, ts timestamp)
RETURNS timestamp
AS 'MODULE_PATHNAME', 'rollups_time_bucket'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION rollups.time_bucket(interval, timestamp) IS
'Round timestamp down to the start of a time bucket.
Example: time_bucket(''1 hour'', ''2024-01-15 14:32:00'') returns ''2024-01-15 14:00:00''';

-- ============================================================================
-- PART 2: Metadata tables (for rollup definitions)
-- ============================================================================

-- Create sequence for agg_id (must be created BEFORE table that uses it)
CREATE SEQUENCE rollups.continuous_aggregates_agg_id_seq;

-- Table to store continuous aggregate definitions
-- Schema matches ContinuousAggregateData struct in types.hpp
CREATE TABLE rollups.continuous_aggregates (
    -- Unique identifier (auto-generated OID)
    agg_id OID PRIMARY KEY DEFAULT nextval('rollups.continuous_aggregates_agg_id_seq'::regclass),

    -- User-friendly name (NameData = max 63 chars)
    agg_name NAME NOT NULL UNIQUE,

    -- View name (same as agg_name for now)
    view_name NAME NOT NULL,

    -- Source table (by OID and name for redundancy/performance)
    source_table_oid OID NOT NULL,
    source_table_name NAME NOT NULL,

    -- Time column in source table
    time_column NAME NOT NULL,

    -- Time bucketing configuration
    bucket_interval INTERVAL NOT NULL,

    -- Materialization table name (e.g., rollups._materialized_hourly_sales)
    matview_name NAME NOT NULL UNIQUE,

    -- Lifecycle timestamps
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    last_refresh TIMESTAMPTZ NOT NULL DEFAULT '-infinity'::timestamptz,

    -- Is this aggregate active?
    is_active BOOLEAN NOT NULL DEFAULT true
);

COMMENT ON TABLE rollups.continuous_aggregates IS
'Stores metadata for all continuous aggregate definitions. Schema matches ContinuousAggregateData struct in C++ code.';

-- Index on source table OID for fast lookups
CREATE INDEX continuous_aggregates_source_oid_idx
ON rollups.continuous_aggregates(source_table_oid);

-- Index on agg_name for fast name lookups
CREATE INDEX continuous_aggregates_name_idx
ON rollups.continuous_aggregates(agg_name);

-- ============================================================================
-- PART 3: Helper views
-- ============================================================================

-- View to see all rollups in a human-readable format
CREATE VIEW rollups.rollup_info AS
SELECT
    agg_id,
    agg_name,
    view_name,
    source_table_name AS source_table,
    time_column,
    bucket_interval,
    matview_name,
    CASE
        WHEN last_refresh = '-infinity'::timestamptz THEN 'Never refreshed'
        ELSE 'Last refresh: ' || last_refresh::text
    END AS status,
    is_active,
    created_at
FROM rollups.continuous_aggregates
ORDER BY created_at DESC;

COMMENT ON VIEW rollups.rollup_info IS
'Human-readable view of all continuous aggregates';

-- ============================================================================
-- PART 4: Test functions for CatalogManager
-- ============================================================================

-- Test function: Create a continuous aggregate entry
-- This exercises CatalogManager::create() from SQL
CREATE FUNCTION rollups.test_create_aggregate(
    p_agg_name text,
    p_source_table text,
    p_time_column text,
    p_bucket_interval interval DEFAULT '1 hour'::interval
)
RETURNS oid
AS 'MODULE_PATHNAME', 'rollups_test_create_aggregate'
LANGUAGE C STRICT;

COMMENT ON FUNCTION rollups.test_create_aggregate(text, text, text, interval) IS
'Test function to create a continuous aggregate entry via CatalogManager::create()';

-- Test function: Load aggregate by name
-- Returns JSON representation of the aggregate
CREATE FUNCTION rollups.test_load_aggregate(p_agg_name text)
RETURNS jsonb
AS 'MODULE_PATHNAME', 'rollups_test_load_aggregate'
LANGUAGE C STRICT;

COMMENT ON FUNCTION rollups.test_load_aggregate(text) IS
'Test function to load aggregate metadata via CatalogManager::load()';

-- Test function: Check if aggregate exists
CREATE FUNCTION rollups.test_aggregate_exists(p_agg_name text)
RETURNS boolean
AS 'MODULE_PATHNAME', 'rollups_test_aggregate_exists'
LANGUAGE C STRICT;

COMMENT ON FUNCTION rollups.test_aggregate_exists(text) IS
'Test function to check aggregate existence via CatalogManager::exists()';

-- ============================================================================
-- Grant appropriate permissions
-- ============================================================================

-- Allow all users to query the info view (but not modify the metadata table)
GRANT SELECT ON rollups.rollup_info TO PUBLIC;

-- Only extension owner can create/modify rollups
-- (Implicit through function ownership)
