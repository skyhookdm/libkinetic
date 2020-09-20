#!/bin/bash

# This script assumes that $function_dir has been sourced

run_workload_from_file() {
    test_count=$1
    file_path=${2:?"Error: missing required file of kctl commands"}
    kinetic_host=${3:-localhost}
    kinetic_port=${4:-8123}

    # this gets repetitive, so just put it in a variable
    kctl_base_cmd="kctl -h ${kinetic_host} -p ${kinetic_port}"

    # ignore kctl stdout (default); otherwise, it can be parameterized later
    kctl_output=${kctl_output:-"/dev/null"}
    kctl_output="/dev/stdout"

    min_runtime=""
    max_runtime=""
    total_runtime=0

     #set -x

    for iter_count in $(seq 1 1 ${test_count}); do
        start_time=$(date +%s%N)
        ${kctl_base_cmd} -y < ${file_path} > ${kctl_output} 2>&1
        end_time=$(date +%s%N)

        let elapsed_time=(${end_time}-${start_time})
        let total_runtime=${elapsed_time}+${total_runtime}

        if [[ -z $min_runtime || ${elapsed_time} -lt ${min_runtime} ]]; then
            min_runtime=${elapsed_time}
        fi

        if [[ -z $max_runtime || ${elapsed_time} -gt ${max_runtime} ]]; then
            max_runtime=${elapsed_time}
        fi
    done

    let avg_runtime=${total_runtime}/${test_count}
    # set +x

    echo "${avg_runtime} ${min_runtime} ${max_runtime}"
}
