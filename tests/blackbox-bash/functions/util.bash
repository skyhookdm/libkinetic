#!/bin/bash

nanoseconds_to_seconds() {
    nanoseconds=$1

    echo $(perl -e 'print($ARGV[0]/1e9."\n");' ${nanoseconds})
}

random_keyval() {
    val_len=${1:-32}

    key_val=""

    for val_ndx in {1..$val_len}; do
        let val_symbol=$((val_ndx % 26))+65
        key_val="${key_val}${val_symbol}"
    done

    echo ${key_val}
}
