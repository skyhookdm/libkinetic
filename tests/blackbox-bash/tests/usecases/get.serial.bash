#!/bin/bash

# This script assumes that $function_dir has been sourced
function_dir="../../functions"
# source "${function_dir}/keys.bash" # sourced by "../../test-kinetic"

getkey_sequential() {
    num_keys=$1
    key_base=${2:-KEY-}

    kinetic_host=${3:-localhost}
    kinetic_port=${4:-8123}

    key_ids=($(sequential_ids 0 $num_keys))

    min_runtime=""
    max_runtime=""
    total_runtime=0

    # set -x

    for key_id in ${key_ids[@]}; do
        # echo $(printf "${key_base}%05d" ${key_id})

        key_name=$(printf "${key_base}%05d" ${key_id})

        start_time=$(date +%s%N)

        kctl -h ${kinetic_host} \
             -p ${kinetic_port} \
             get -A ${key_name} \
             > /dev/null

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

getkey_random() {
    num_keys=$1
    key_base=${2:-KEY-}

    kinetic_host=${3:-localhost}
    kinetic_port=${4:-8123}

    key_ids=($(random_ids $num_keys))

    min_runtime=""
    max_runtime=""
    total_runtime=0

    # set -x

    for key_id in ${key_ids[@]}; do
        # keep the key ID in the existing (hard-coded) range
        key_name=$(printf "${key_base}%05d" $((key_id % 10000)))

        start_time=$(date +%s%N)

        kctl -h ${kinetic_host} \
             -p ${kinetic_port} \
             get -A ${key_name} \
             > /dev/null

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
