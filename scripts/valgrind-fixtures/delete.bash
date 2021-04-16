#!/bin/bash

# ------------------------------
# Global variables and command-line args

# check if we're sourced or invoked directly
echo "bash source: ${BASH_SOURCE[0]}"
echo "First arg: ${0}"
is_sourced=0
[[ "${BASH_SOURCE[0]}" != "${0}" ]] && is_sourced=1

# valgrind command
valgrind_cmd="valgrind --leak-check=full --show-leak-kinds=all -v"

# general global variables
fixture_name="[DELETE]"
fixture_keyspace="kctl.fixture.del."
declare -a fixture_testcases

# command-line parsing, if invoked directly
usage_msg="
Usage: ${0} [host-ip]
"

if [[ ${is_sourced} -eq 0 ]]; then
    kinetic_host="${1:?"${usage_msg}"}"
fi


# ------------------------------
# Validation

if [[ -z $(which kctl) ]]; then
    echo 'Please add `kctl` to PATH'

else
    kctl_path=$(which kctl)
    echo "Which kctl: '${kctl_path}'"
fi


# ------------------------------
# Fixture Functions

function fixture_setup() {
    echo "${fixture_name} |> Fixture Setup"

    echo -e "\t>> add test keys"
    for key_ndx in $(seq 1 10); do
        kctl -h ${kinetic_host} put "${fixture_keyspace}${key_ndx}" "Put${key_ndx}"
    done
}

function fixture_cleanup() {
    echo "${fixture_name} |> Fixture Cleanup"

    echo -e "\t>> delete remaining test keys"
    for key_ndx in $(seq 1 10); do
        echo "y" | kctl -h ${kinetic_host} del "${fixture_keyspace}${key_ndx}" 2>/dev/null
    done
}

# Test Cases; use a common variable name to capture which cases to run
function fixture_case_pointdelete() {
    echo "${fixture_name} |> Test Case <Point Delete>"

    echo "y" | ${valgrind_cmd} kctl -h ${kinetic_host} del "${fixture_keyspace}1"
}
fixture_testcases[0]="fixture_case_pointdelete"

function fixture_case_atomicdelete() {
    echo "${fixture_name} |> Test Case <Atomic Delete>"
}
fixture_testcases[1]="fixture_case_atomicdelete"

function fixture_case_writethroughdelete() {
    echo "${fixture_name} |> Test Case <Write-Through Delete>"
}
fixture_testcases[2]="fixture_case_writethroughdelete"

function fixture_case_rangedelete() {
    echo "${fixture_name} |> Test Case <Range Delete>"
}
fixture_testcases[3]="fixture_case_rangedelete"

# ------------------------------
# Main Logic

if [[ ${is_sourced} -eq 0 ]]; then
    echo "${fixture_name} |> Executing Fixture"

    fixture_setup

    for fn_testcase in ${fixture_testcases[@]}; do
        ${fn_testcase}
    done

    fixture_cleanup
fi
