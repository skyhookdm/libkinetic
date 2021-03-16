#!/bin/bash

# This script assumes that $function_dir has been sourced
function_dir="../../functions"
# source "${function_dir}/keys.bash" # sourced by "../../test-kinetic"

putgetkey_sequential() {
    local num_keys=$1
    local key_base=${2:-AnExc33d1nglyYooniqueKeyBass-}

    local kinetic_host=${3:-localhost}
    local kinetic_port=${4:-8123}

    local val_len=${val_len:-128}

    local key_ids=($(sequential_ids 0 $num_keys))

    local min_runtime=""
    local max_runtime=""
    local total_runtime=0

    set -x
    set -e

    for key_id in ${key_ids[@]}; do
        # echo $(printf "${key_base}%05d" ${key_id})

        local key_name=$(printf "${key_base}%05d" ${key_id})
        local key_val=$(head -c ${val_len} /dev/urandom | tr -d '\0')
        local val_hash=$(echo -n ${key_val} | /usr/bin/crc32 /dev/stdin)

        local formatted_val=$(echo -n ${key_val} | xxd -p -u -c1)
        local formatted_valstr=$(perl -e 'print("\\x".join("\\x", @ARGV)."\n");' ${formatted_val})

        local start_time=$(date +%s%N)

        kctl -h ${kinetic_host}       \
             -p ${kinetic_port}       \
             put -p wb -s ${val_hash} \
             ${key_name} "${formatted_valstr}" \
             > /dev/null 2>&1

        kctl -h ${kinetic_host} \
             -p ${kinetic_port} \
             get -A ${key_name} \
             > /dev/null

        local end_time=$(date +%s%N)

        let elapsed_time=(${end_time}-${start_time})
        let total_runtime=${elapsed_time}+${total_runtime}

        if [[ -z $min_runtime || ${elapsed_time} -lt ${min_runtime} ]]; then
            min_runtime=${elapsed_time}
        fi

        if [[ -z $max_runtime || ${elapsed_time} -gt ${max_runtime} ]]; then
            max_runtime=${elapsed_time}
        fi
    done

    # remove the keys so that this is replayable
    for key_id in ${key_ids[@]}; do
        local key_name=$(printf "${key_base}%05d" ${key_id})

        kctl -h ${kinetic_host}    \
             -p ${kinetic_port}    \
             -y                    \
             del -p wb ${key_name} \
             > /dev/null
    done

    let avg_runtime=${total_runtime}/${num_keys}
    # set +x

    echo "${avg_runtime} ${min_runtime} ${max_runtime}"
}

putgetkey_random() {
    num_keys=$1
    key_base=${2:-AnExc33d1nglyYooniqueKeyBass-}

    kinetic_host=${3:-localhost}
    kinetic_port=${4:-8123}

    val_len=${val_len:-128}

    key_ids=($(random_ids $num_keys))

    min_runtime=""
    max_runtime=""
    total_runtime=0

    # set -x

    for key_id in ${key_ids[@]}; do
        # echo $(printf "${key_base}%05d" ${key_id})

        key_name=$(printf "${key_base}%05d" $((key_id % 10000)))
        key_val=$(head -c ${val_len} /dev/urandom | tr -d '\0')
        val_hash=$(echo -n ${key_val} | /usr/bin/crc32 /dev/stdin)

        formatted_val=$(echo -n ${key_val} | xxd -p -u -c1)
        formatted_valstr=$(perl -e 'print("\\x".join("\\x", @ARGV)."\n");' ${formatted_val})

        start_time=$(date +%s%N)

        kctl -h ${kinetic_host}                \
             -p ${kinetic_port}                \
             put -p wb -s ${val_hash}          \
             ${key_name} "${formatted_valstr}" \
             > /dev/null 2>&1

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

    # remove the keys so that this is replayable
    for key_id in ${key_ids[@]}; do
        key_name=$(printf "${key_base}%05d" $((key_id % 10000)))

        kctl -h ${kinetic_host}    \
             -p ${kinetic_port}    \
             -y                    \
             del -p wb ${key_name} \
             > /dev/null
    done

    let avg_runtime=${total_runtime}/${num_keys}
    # set +x

    echo "${avg_runtime} ${min_runtime} ${max_runtime}"
}
