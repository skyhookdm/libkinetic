#!/bin/bash

random_ids() {
    num_ids=$1

    let start_val=$RANDOM step_val=$RANDOM
    let stop_val=($step_val*$num_ids)+$start_val-1

    echo $(seq $start_val $step_val $stop_val)
}

sequential_ids() {
    start_id=$1
    num_ids=$2

    echo $(seq $start_id 1 $num_ids)
}

normalize_keyname() {
    key_id=${1:-""}
    key_id_format=${2:-"%05d"}
    key_base=${3:-KEY-}

    printf "${key_base}${key_id_format}" ${key_id}
}

random_keyval() {
    val_len=${1:-32}

    head -c ${val_len} /dev/urandom | tr -d '\0'
}
