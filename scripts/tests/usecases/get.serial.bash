#!/bin/bash

# This script assumes that $function_dir has been sourced
function_dir="../../functions"
# source "${function_dir}/keys.bash" # sourced by "../../test-kinetic"

getkey_sequential() {
    num_keys=$1
    key_base=${2:-KEY-}

    kinetic_host=${3:-localhost}
    kinetic_port=${4:-8123}

    # this gets repetitive, so just put it in a variable
    kctl_base_cmd="kctl -h ${kinetic_host} -p ${kinetic_port}"

    # ignore kctl stdout (default); otherwise, it can be parameterized later
    kctl_output=${kctl_output:-"/dev/null"}
    kctl_output="outputs/kctl.messages.log"
    kctl_output="/dev/stdout"

    let exclusive_keycnt=${num_keys}-1
    key_ids=($(sequential_ids 0 ${exclusive_keycnt}))

    min_runtime=""
    max_runtime=""
    total_runtime=0

    # set -x

    for key_id in ${key_ids[@]}; do
        # echo $(printf "${key_base}%05d" ${key_id})

        key_name=$(printf "${key_base}%05d" ${key_id})

        echo "pid: ${$}"
        let start_time=$(date +%s%N)
        ${kctl_base_cmd} get -A ${key_name} > ${kctl_output}
        let end_time=$(date +%s%N)

        let elapsed_time=(${end_time}-${start_time})
        let total_runtime=${elapsed_time}+${total_runtime}

        if [[ -z $min_runtime || ${elapsed_time} -lt ${min_runtime} ]]; then
            min_runtime=${elapsed_time}
        fi

        if [[ -z $max_runtime || ${elapsed_time} -gt ${max_runtime} ]]; then
            max_runtime=${elapsed_time}
        fi
    done

    let avg_runtime=${total_runtime}/${num_keys}
    # set +x

    echo "${avg_runtime} ${min_runtime} ${max_runtime}"
}

getkey_random() {
    num_keys=$1
    key_base=${2:-KEY-}

    kinetic_host=${3:-localhost}
    kinetic_port=${4:-8123}

    # this gets repetitive, so just put it in a variable
    kctl_base_cmd="kctl -h ${kinetic_host} -p ${kinetic_port}"

    # ignore kctl stdout (default); otherwise, it can be parameterized later
    kctl_output=${kctl_output:-"/dev/null"}
    kctl_output="outputs/kctl.messages.log"
    kctl_output="/dev/stdout"

    key_ids=($(random_ids $num_keys))

    min_runtime=""
    max_runtime=""
    total_runtime=0

    # set -x

    for key_id in ${key_ids[@]}; do
        # keep the key ID in the existing (hard-coded) range
        key_name=$(printf "${key_base}%05d" $((key_id % 10000)))

        start_time=$(date +%s%N)
        ${kctl_base_cmd} get -A ${key_name} > ${kctl_output}
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

    let avg_runtime=${total_runtime}/${num_keys}
    # set +x

    echo "${avg_runtime} ${min_runtime} ${max_runtime}"
}
