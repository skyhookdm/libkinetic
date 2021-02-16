## Copyright 2020-2021 Seagate Technology LLC.
#
# This Source Code Form is subject to the terms of the Mozilla
# Public License, v. 2.0. If a copy of the MPL was not
# distributed with this file, You can obtain one at
# https://mozilla.org/MP:/2.0/.
#
# This program is distributed in the hope that it will be useful,
# but is provided AS-IS, WITHOUT ANY WARRANTY; including without
# the implied warranty of MERCHANTABILITY, NON-INFRINGEMENT or
# FITNESS FOR A PARTICULAR PURPOSE. See the Mozilla Public
# License for more details.
#
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


