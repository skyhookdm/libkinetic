#!/bin/bash

# ------------------------------
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

# ------------------------------
# Dependencies
scripts_topdir=$(dirname $(dirname "${0}"))
source "${scripts_topdir}/functions/keys.bash"


# ------------------------------
# Global variables and command-line args

# >> check if we're sourced or invoked directly
is_sourced=0
[[ "${BASH_SOURCE[0]}" != "${0}" ]] && is_sourced=1

# >> global variables for fixture management
fixture_name="[DELETE]"
fixture_keyspace="kctl.fixture.del."
fixture_teststate_dir="${scripts_topdir}/test-expectedstates"
declare -a fixture_testcases

# >> valgrind command with options set
valgrind_cmd="valgrind --leak-check=full --show-leak-kinds=all"
diff_cmd="/usr/bin/diff"

# >> command-line parsing; only do this if invoked directly
if [[ ${is_sourced} -eq 0 ]]; then
    usage_msg="
    Usage: ${0} -h <host-ip> [-m] [-r]

    -h: [required] IP address of kinetic server.
    -m: [optional] check memory leaks using valgrind. *Disables* debug mode.
    -r: [optional] record, instead of check, pre- and post-conditions for tests.
    -s: [optional] setup-only flag; only runs setup   phase
    -c: [optional] cleanup    flag; only runs cleanup phase
    -d: [optional] debug      flag; skips cleanup phase; prints commands (using 'set -x')
    "

    # default values for optional args
    valgrind_mode='false'
    record_mode='false'
    debug_mode='false'

    # default values for test phases
    should_run_setup='true'
    should_run_tests='true'
    should_run_cleanup='true'

    while getopts "h:mrdsc" option_symbol; do
        case "${option_symbol}" in
            h)
                kinetic_host="${OPTARG}"
                ;;
            m)
                valgrind_mode='true'
                ;;
            r)
                record_mode='true'
                ;;
            d)
                debug_mode='true'
                should_run_cleanup='false'
                ;;
            s)
                should_run_tests='false'
                should_run_cleanup='false'
                ;;
            c)
                should_run_setup='false'
                should_run_tests='false'
                ;;
            *)
                echo "${usage_msg}"
                exit 1
                ;;
        esac
    done

    # do this after arg parsing so arg order doesn't matter
    if [[ "${valgrind_mode}" = 'true' ]]; then
        debug_mode='false'
        should_run_cleanup='true'
    fi
fi


# ------------------------------
# Validation

if [[ ${is_sourced} -eq 0 ]]; then
    if [[ -z $(which kctl) ]]; then
        echo 'Please add `kctl` to PATH'
        exit 1

    else
        kctl_path=$(which kctl)
        echo "Which kctl: '${kctl_path}'"

    fi

    if [[ -z "${kinetic_host}" ]]; then
        echo "${usage_msg}"
        exit 1
    fi
fi


# ------------------------------
# Fixture Functions

function fixture_setup() {
    echo "${fixture_name} |> Fixture Setup"

    echo -e "\t>> add test keys"
    for key_ndx in $(seq -w 1 1 10); do
        kctl -h ${kinetic_host} put "${fixture_keyspace}${key_ndx}" "Put${key_ndx}"
    done
}

function fixture_cleanup() {
    echo "${fixture_name} |> Fixture Cleanup"

    echo -e "\t>> delete remaining test keys"
    for key_ndx in $(seq -w 1 1 10); do
        kctl -h ${kinetic_host} -y del "${fixture_keyspace}${key_ndx}" >/dev/null 2>/dev/null
    done
}

function fixture_inspectstate() {
    kctl -h ${kinetic_host} -y            \
         range -S "${fixture_keyspace}01" \
               -E "${fixture_keyspace}10"
}

function fixture_managestate() {
    state_filename="${1:?"Must provide path to expected state"}"
    is_pre_or_post="${2:?"Must specify [pre]condition state or [post]condition state"}"

    if [[ "${is_pre_or_post}" -ne 'pre' || "${is_pre_or_post}" -ne 'post' ]]; then
        echo "Unrecognized type of condition check: '${is_pre_or_post}'"
        exit 1
    fi

    state_filepath="${fixture_teststate_dir}/${is_pre_or_post}/${state_filename}"
    [[ ! -d $(dirname "${state_filepath}") ]] && mkdir -p $(dirname "${state_filepath}")

    if [[ "${record_mode}" = "true" ]]; then
        fixture_inspectstate > "${state_filepath}"

    else
        actualstate_filename="actual-state.delete.${$}"
        fixture_inspectstate > "${actualstate_filename}"

        ${diff_cmd} -q "${state_filepath}" "${actualstate_filename}" >/dev/null 2>/dev/null
        if [[ "${?}" -ne 0 ]]; then
            diff_filename="${actualstate_filename}.diff"
            diff_with_params="${diff_cmd} ${state_filepath} ${actualstate_filename}"

            echo "${diff_with_params}" > "${diff_filename}"
            ${diff_with_params}        > "${diff_filename}"

            echo "Unexpected initial state. Check files:"
            echo -e "\t[Actual State] >> ${actualstate_filename}"
            echo -e "\t[Diff'd State] >> ${diff_filename}"

            return 1

        else
            rm "${actualstate_filename}"

        fi
    fi

    return 0
}

# Test Cases; use a common variable name to capture which cases to run
function fixture_case_pointdelete() {
    echo "${fixture_name} |> Test Case <Point Delete>"

    use_valgrind="${1:-"false"}"
    testkey="${fixture_keyspace}01"
    test_command="kctl -h ${kinetic_host} -y del ${testkey}"
    path_to_statefile="recordedstate.delete.pointdelete"

    fixture_managestate "${path_to_statefile}" 'pre'
    passed_precondition=${?}

    if [[ "${use_valgrind}" = 'true' ]]; then
        ${valgrind_cmd} ${test_command}

    else
        [[ "${debug_mode}" = 'true' ]] && set -x

        ${test_command} >/dev/null
        #${test_command}

        [[ "${debug_mode}" = 'true' ]] && set +x
    fi

    fixture_managestate "${path_to_statefile}" 'post'
    passed_postcondition=${?}

    if [[ ${passed_precondition} -eq 0 && ${passed_postcondition} -eq 0 ]]; then
        echo "${fixture_name} |> Passed"

    else
        echo "${fixture_name} |> Failed"

    fi
}

function fixture_case_atomicdelete() {
    echo "${fixture_name} |> Test Case <Atomic Delete>"

    use_valgrind="${1:-"false"}"
    testkey="${fixture_keyspace}02"
    test_command="kctl -h ${kinetic_host} -y del -c ${testkey}"
    path_to_statefile="recordedstate.delete.atomicdelete"

    fixture_managestate "${path_to_statefile}" 'pre'
    passed_precondition=${?}

    if [[ "${use_valgrind}" = "true" ]]; then
        ${valgrind_cmd} ${test_command}

    else
        [[ "${debug_mode}" = 'true' ]] && set -x

        ${test_command} >/dev/null

        [[ "${debug_mode}" = 'true' ]] && set +x

    fi

    fixture_managestate "${path_to_statefile}" 'post'
    passed_postcondition=${?}

    if [[ ${passed_precondition} -eq 0 && ${passed_postcondition} -eq 0 ]]; then
        echo "${fixture_name} |> Passed"

    else
        echo "${fixture_name} |> Failed"

    fi
}

function fixture_case_writethroughdelete() {
    echo "${fixture_name} |> Test Case <Write-Through Delete>"

    use_valgrind="${1:-"false"}"
    testkey="${fixture_keyspace}03"
    test_command="kctl -h ${kinetic_host} -y del -p wt ${testkey}"
    path_to_statefile="recordedstate.delete.writethroughdelete"

    fixture_managestate "${path_to_statefile}" 'pre'
    passed_precondition=${?}

    if [[ "${use_valgrind}" = "true" ]]; then
        ${valgrind_cmd} ${test_command}

    else
        [[ "${debug_mode}" = 'true' ]] && set -x

        ${test_command} >/dev/null

        [[ "${debug_mode}" = 'true' ]] && set +x

    fi

    fixture_managestate "${path_to_statefile}" 'post'
    passed_postcondition=${?}

    if [[ ${passed_precondition} -eq 0 && ${passed_postcondition} -eq 0 ]]; then
        echo "${fixture_name} |> Passed"

    else
        echo "${fixture_name} |> Failed"

    fi
}

function fixture_case_rangedelete_exclusive() {
    echo "${fixture_name} |> Test Case <Range Delete>"

    use_valgrind="${1:-"false"}"
    testkey_start="${fixture_keyspace}04"
    testkey_end="${fixture_keyspace}06"
    test_command="kctl -h ${kinetic_host} -y del -s ${testkey_start} -E ${testkey_end}"
    path_to_statefile="recordedstate.delete.range-sE"

    fixture_managestate "${path_to_statefile}" 'pre'
    passed_precondition=${?}

    if [[ "${use_valgrind}" = "true" ]]; then
        ${valgrind_cmd} ${test_command}

    else
        [[ "${debug_mode}" = 'true' ]] && set -x

        ${test_command} >/dev/null

        [[ "${debug_mode}" = 'true' ]] && set +x

    fi

    fixture_managestate "${path_to_statefile}" 'post'
    passed_postcondition=${?}

    if [[ ${passed_precondition} -eq 0 && ${passed_postcondition} -eq 0 ]]; then
        echo "${fixture_name} |> Passed"

    else
        echo "${fixture_name} |> Failed"

    fi
}

function fixture_case_rangedelete_inclusive() {
    echo "${fixture_name} |> Test Case <Range Delete>"

    use_valgrind="${1:-"false"}"
    testkey_start="${fixture_keyspace}07"
    testkey_end="${fixture_keyspace}10"
    test_command="kctl -h ${kinetic_host} -y del -S ${testkey_start} -e ${testkey_end}"
    path_to_statefile="recordedstate.delete.range-Se"

    fixture_managestate "${path_to_statefile}" 'pre'
    passed_precondition=${?}

    if [[ "${use_valgrind}" = "true" ]]; then
        ${valgrind_cmd} ${test_command}

    else
        [[ "${debug_mode}" = 'true' ]] && set -x

        ${test_command} >/dev/null

        [[ "${debug_mode}" = 'true' ]] && set +x

    fi

    fixture_managestate "${path_to_statefile}" 'post'
    passed_postcondition=${?}

    if [[ ${passed_precondition} -eq 0 && ${passed_postcondition} -eq 0 ]]; then
        echo "${fixture_name} |> Passed"

    else
        echo "${fixture_name} |> Failed"

    fi
}

# Append test cases we want to "export" to `fixture_testcases` array
fixture_testcases[0]="fixture_case_pointdelete"
# fixture_testcases[1]="fixture_case_atomicdelete"
# fixture_testcases[2]="fixture_case_writethroughdelete"
# fixture_testcases[3]="fixture_case_rangedelete_exclusive"
# fixture_testcases[4]="fixture_case_rangedelete_inclusive"


# ------------------------------
# Main Logic

# >> There are 3 phases: setup, test cases, cleanup.

# set readable variables for controlling phases using mode flags
if [[ ${is_sourced} -eq 0 ]]; then
    echo "${fixture_name} |> Executing Fixture"

    # >> setup phase
    [[ "${should_run_setup}" = 'true' ]] && fixture_setup

    # >> test phase
    if [[ "${should_run_tests}" = 'true' ]]; then
        for fn_testcase in ${fixture_testcases[@]}; do
            ${fn_testcase} "${valgrind_mode}"
        done
    fi

    # >> cleanup phase
    [[ "${should_run_cleanup}" = 'true' ]] && fixture_cleanup
fi
