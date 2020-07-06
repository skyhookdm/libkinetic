cmake_minimum_required(VERSION 3.10)


# ------------------------------
# Project level configuration

# project name
project(
    KineticPrototype
    VERSION     0.1.0
    DESCRIPTION "A prototype of the kinetic library. Work-in-progress."
    LANGUAGES   C
)

# variables
set(STATIC_PREFIX      ${PROJECT_BINARY_DIR}/vendor)
set(STATIC_LIB_DIR     ${STATIC_PREFIX}/lib)
set(STATIC_INCLUDE_DIR ${STATIC_PREFIX}/include)

# subdirectories
add_subdirectory(src)


# ------------------------------
# Main compilation
include_directories(
    # header files
    include
    ${STATIC_INCLUDE_DIR}
    # source files
    src
    ${PROJECT_SOURCE_DIR}/src/protocol
)


