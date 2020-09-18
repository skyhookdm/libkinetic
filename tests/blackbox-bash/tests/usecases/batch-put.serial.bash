#!/bin/bash

# This script assumes that $function_dir has been sourced
function_dir="../../functions"
# source "${function_dir}/keys.bash" # sourced by "../../test-kinetic"

batchput_sequential_from_file() {
    test_count=$1
    file_path=${2:?"Error: missing required file for batched put data"}
    kinetic_host=${3:-localhost}
    kinetic_port=${4:-8123}

    # this gets repetitive, so just put it in a variable
    kctl_base_cmd="kctl -h ${kinetic_host} -p ${kinetic_port}"

    # ignore kctl stdout (default); otherwise, it can be parameterized later
    kctl_output=${kctl_output:-"/dev/null"}

    min_runtime=""
    max_runtime=""
    total_runtime=0

    # set -x

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

# TODO
batchput_sequential() {
    num_keys=$1
    key_base=${2:-Test|BatchedPuts}

    kinetic_host=${3:-localhost}
    kinetic_port=${4:-8123}

    # this gets repetitive, so just put it in a variable
    kctl_base_cmd="kctl -h ${kinetic_host} -p ${kinetic_port}"

    # ignore kctl stdout (default); otherwise, it can be parameterized later
    kctl_output=${kctl_output:-"/dev/null"}

    val_len=${val_len:-128}
    key_ids=($(sequential_ids 0 $num_keys))

    min_runtime=""
    max_runtime=""
    total_runtime=0

    # set -x
    ${kctl_base_cmd} batch -S > ${kctl_output}

    # batch puts
    for key_id in ${key_ids[@]}; do
        # echo $(printf "${key_base}%05d" ${key_id})

        key_name=$(normalize_keyname ${key_id} "%05d" ${key_base})
        key_val=$(random_keyval $val_len)

        val_hash=$(compute_crc32 ${key_val})
        val_formatted=$(binary_to_escaped_hex ${key_val})

        start_time=$(date +%s%N)

        ${kctl_base_cmd} put -p wb -s ${val_hash} ${key_name} "${val_formatted}" > ${kctl_output}

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

    ${kctl_base_cmd} -p ${kinetic_port} batch -C > ${kctl_output}

    # remove the keys so that this is replayable

    for key_id in ${key_ids[@]}; do
        key_name=$(normalize_keyname ${key_id} "%05d" ${key_base})

        ${kctl_base_cmd} -y del -p wb ${key_name} > ${kctl_output}
    done

    let avg_runtime=${total_runtime}/${num_keys}
    # set +x

    echo "${avg_runtime} ${min_runtime} ${max_runtime}"
}
