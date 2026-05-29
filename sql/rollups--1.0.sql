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
    is_active BOOLEAN NOT NULL DEFAULT true,

    -- Aggregate select-list: the expressions computed per bucket.
    -- Free-form SQL text (can exceed the 63-char NAME limit), e.g.
    -- 'sum(amount) AS total, count(*) AS cnt'
    select_clause TEXT NOT NULL
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
    select_clause,
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
-- PART 4: Continuous aggregate management functions
-- ============================================================================

-- Create a continuous aggregate: records the metadata, then builds and
-- populates the materialization table in one call.
CREATE FUNCTION rollups.create_continuous_aggregate(
    p_name          text,
    p_source_table  text,
    p_time_column   text,
    p_bucket_width  interval,
    p_select_clause text
)
RETURNS oid
AS 'MODULE_PATHNAME', 'rollups_create_continuous_aggregate'
LANGUAGE C STRICT;

COMMENT ON FUNCTION rollups.create_continuous_aggregate(text, text, text, interval, text) IS
'Create a continuous aggregate and populate its materialization table.
p_select_clause holds the per-bucket aggregate expressions, e.g.
''sum(amount) AS total, count(*) AS cnt''.';

-- Refresh a continuous aggregate (Phase 3: full recompute).
CREATE FUNCTION rollups.refresh_continuous_aggregate(p_name text)
RETURNS void
AS 'MODULE_PATHNAME', 'rollups_refresh_continuous_aggregate'
LANGUAGE C STRICT;

COMMENT ON FUNCTION rollups.refresh_continuous_aggregate(text) IS
'Recompute the materialization table for a continuous aggregate.';

-- Drop a continuous aggregate: removes the materialization table and the
-- catalog entry.
CREATE FUNCTION rollups.drop_continuous_aggregate(p_name text)
RETURNS void
AS 'MODULE_PATHNAME', 'rollups_drop_continuous_aggregate'
LANGUAGE C STRICT;

COMMENT ON FUNCTION rollups.drop_continuous_aggregate(text) IS
'Drop a continuous aggregate and its materialization table.';

-- ============================================================================
-- Grant appropriate permissions
-- ============================================================================

-- Allow all users to query the info view (but not modify the metadata table)
GRANT SELECT ON rollups.rollup_info TO PUBLIC;

-- Only extension owner can create/modify rollups
-- (Implicit through function ownership)
