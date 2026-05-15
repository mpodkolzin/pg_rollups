# CMakeLists.txt - PostgreSQL Rollups Extension
#
# Why CMake instead of PGXS?
# - Better IDE integration (CLion, VS Code CMake Tools)
# - More flexible build configuration
# - Easier to add C++ features and external libraries
# - Better for learning: explicit control over compilation
# - Cross-platform support (though we're focused on macOS)
#
# Trade-offs:
# - More verbose than PGXS
# - Need to manually handle PostgreSQL-specific installation paths
# - Less "standard" for PostgreSQL extensions (most use PGXS)
# - But: Better for a learning project with potential complexity

cmake_minimum_required(VERSION 3.20)

project(rollups
    VERSION 1.0.0
    DESCRIPTION "Continuous rollup aggregations for PostgreSQL"
    LANGUAGES CXX C
)

# ============================================================================
# Build Configuration
# ============================================================================

# Use C++17 for modern features (optional, auto, range-based for, etc.)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # Use -std=c++17, not -std=gnu++17

# PostgreSQL C headers are C-only, so we need C99 for compatibility
set(CMAKE_C_STANDARD 99)

# Build type: Debug by default for learning
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Enable compile_commands.json for IDE tooling (clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Find PostgreSQL
# ============================================================================

# Find pg_config (must be in PATH)
find_program(PG_CONFIG pg_config
    HINTS
        ENV PATH
        /usr/local/bin
        /opt/homebrew/bin
        $ENV{HOME}/pgdev/postgres/bin
)

if(NOT PG_CONFIG)
    message(FATAL_ERROR "pg_config not found. Please install PostgreSQL development headers or add pg_config to PATH")
endif()

message(STATUS "Found pg_config: ${PG_CONFIG}")

# Get PostgreSQL configuration
execute_process(COMMAND ${PG_CONFIG} --version OUTPUT_VARIABLE PG_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${PG_CONFIG} --includedir-server OUTPUT_VARIABLE PG_INCLUDEDIR_SERVER OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${PG_CONFIG} --includedir OUTPUT_VARIABLE PG_INCLUDEDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${PG_CONFIG} --pkglibdir OUTPUT_VARIABLE PG_PKGLIBDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${PG_CONFIG} --sharedir OUTPUT_VARIABLE PG_SHAREDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${PG_CONFIG} --cflags OUTPUT_VARIABLE PG_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${PG_CONFIG} --ldflags OUTPUT_VARIABLE PG_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)

message(STATUS "PostgreSQL version: ${PG_VERSION}")
message(STATUS "PostgreSQL include (server): ${PG_INCLUDEDIR_SERVER}")
message(STATUS "PostgreSQL include: ${PG_INCLUDEDIR}")
message(STATUS "PostgreSQL pkglibdir: ${PG_PKGLIBDIR}")
message(STATUS "PostgreSQL sharedir: ${PG_SHAREDIR}")

# Verify headers exist
if(NOT EXISTS "${PG_INCLUDEDIR_SERVER}/postgres.h")
    message(FATAL_ERROR "PostgreSQL server headers not found at ${PG_INCLUDEDIR_SERVER}")
endif()

# ============================================================================
# Compiler Flags
# ============================================================================

# Debug flags for learning (maximum debug info, no optimization)
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -fno-omit-frame-pointer -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "-g3 -O0 -fno-omit-frame-pointer -Wall -Wextra")

# Release flags (if we ever need them)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# PostgreSQL-specific flags
# Note: PostgreSQL headers expect certain macros to be defined
add_compile_definitions(
    # Prevent conflicts between C and C++ headers
    __STDC_LIMIT_MACROS
    __STDC_FORMAT_MACROS
)

# macOS-specific: Build as bundle (required for PostgreSQL extensions)
if(APPLE)
    set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")
    set(CMAKE_SHARED_MODULE_SUFFIX ".so")
    # Use -bundle instead of -dynamiclib for PostgreSQL extensions
    set(CMAKE_SHARED_MODULE_CREATE_CXX_FLAGS "-bundle -flat_namespace -undefined dynamic_lookup")
endif()

# ============================================================================
# Extension Library
# ============================================================================

# Source files
set(ROLLUPS_SOURCES
    src/rollups.cpp
    src/catalog_manager.cpp
    src/continuous_aggregate.cpp
)

# Create shared module (not shared library - important for PostgreSQL)
add_library(rollups MODULE ${ROLLUPS_SOURCES})

# Include directories
target_include_directories(rollups PRIVATE
    ${PG_INCLUDEDIR_SERVER}
    ${PG_INCLUDEDIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include  # Our C++ headers
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link flags (PostgreSQL typically doesn't need explicit linking on macOS)
if(NOT APPLE)
    target_link_libraries(rollups PRIVATE ${PG_LDFLAGS})
endif()

# Set output name (must be exactly 'rollups.so' to match MODULE_PATHNAME in SQL)
set_target_properties(rollups PROPERTIES
    PREFIX ""  # No 'lib' prefix
    OUTPUT_NAME "rollups"
)

# ============================================================================
# Installation
# ============================================================================

# Install the shared library to PostgreSQL's extension directory
install(TARGETS rollups
    LIBRARY DESTINATION ${PG_PKGLIBDIR}
)

# Install control file
install(FILES rollups.control
    DESTINATION ${PG_SHAREDIR}/extension
)

# Install SQL scripts
install(FILES sql/rollups--1.0.sql
    DESTINATION ${PG_SHAREDIR}/extension
)

# ============================================================================
# Custom Targets
# ============================================================================

# Target: show PostgreSQL configuration
add_custom_target(show-pg-config
    COMMAND ${CMAKE_COMMAND} -E echo "PostgreSQL Configuration:"
    COMMAND ${CMAKE_COMMAND} -E echo "  Version: ${PG_VERSION}"
    COMMAND ${CMAKE_COMMAND} -E echo "  Include: ${PG_INCLUDEDIR_SERVER}"
    COMMAND ${CMAKE_COMMAND} -E echo "  Library: ${PG_PKGLIBDIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "  Share:   ${PG_SHAREDIR}"
    VERBATIM
)

# Target: create extension in database (requires PostgreSQL running)
add_custom_target(create-extension
    COMMAND psql rollups_test -c "DROP EXTENSION IF EXISTS rollups CASCADE$<SEMICOLON> CREATE EXTENSION rollups$<SEMICOLON>"
    DEPENDS rollups
    COMMENT "Creating extension in rollups_test database"
    VERBATIM
)

# Target: test extension
add_custom_target(test-extension
    COMMAND psql rollups_test -c "SELECT rollups.version()$<SEMICOLON>"
    COMMAND psql rollups_test -c "SELECT rollups.time_bucket('1 hour', now())$<SEMICOLON>"
    DEPENDS create-extension
    COMMENT "Testing extension functions"
    VERBATIM
)

# ============================================================================
# Development Helpers
# ============================================================================

# Print build information
message(STATUS "")
message(STATUS "Build Configuration:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  C standard: C${CMAKE_C_STANDARD}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Build commands:")
message(STATUS "  mkdir build && cd build")
message(STATUS "  cmake ..")
message(STATUS "  make")
message(STATUS "  make install  (or sudo make install if needed)")
message(STATUS "")
message(STATUS "Test commands:")
message(STATUS "  make create-extension  # Drop and recreate extension")
message(STATUS "  make test-extension    # Run basic tests")
message(STATUS "")

# ============================================================================
# LEARNING NOTES: CMake for PostgreSQL Extensions
# ============================================================================
#
# Key Differences from PGXS:
#
# 1. LIBRARY TYPE:
#    - Use MODULE, not SHARED (MODULE is for dlopen, SHARED is for linking)
#    - PostgreSQL uses dlopen to load extensions at runtime
#
# 2. macOS BUNDLE:
#    - PostgreSQL extensions need -bundle, not -dynamiclib
#    - Use -flat_namespace -undefined dynamic_lookup
#    - This allows unresolved symbols (PostgreSQL provides them)
#
# 3. INCLUDE PATHS:
#    - Must include both includedir-server (postgres.h, fmgr.h)
#    - And includedir (libpq-fe.h, if needed)
#
# 4. NO PREFIX:
#    - Set PREFIX "" so we get 'rollups.so', not 'librollups.so'
#    - MODULE_PATHNAME in SQL expects exactly 'rollups'
#
# 5. INSTALLATION:
#    - Extensions go to pkglibdir (e.g., ~/pgdev/postgres/lib/)
#    - Control and SQL go to sharedir/extension
#
# 6. C++ CONSIDERATIONS:
#    - PostgreSQL headers are C, not C++
#    - Need extern "C" in source files
#    - Be careful with name mangling
#    - Use C-compatible types in function signatures
#
# WHY THIS APPROACH?
#
# Pros:
# - Full control over build process
# - Easy to add C++ features (classes, STL, templates)
# - Better IDE support (CLion, VS Code)
# - Easier to integrate external libraries
# - Learning: understand what PGXS does automatically
#
# Cons:
# - More boilerplate than PGXS
# - Need to maintain pg_config queries manually
# - Less "standard" for PostgreSQL community
# - More complex for simple extensions
#
# For this learning project, the trade-offs are worth it:
# - We'll likely add complex C++ logic later
# - Better debugging experience with CMake + IDE
# - Valuable to understand both approaches (PGXS and manual)
#
# ============================================================================
