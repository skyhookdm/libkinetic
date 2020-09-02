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
