#!/bin/bash

nanoseconds_to_seconds() {
    nanoseconds=$1

    echo $(perl -e 'print($ARGV[0]/1e9."\n");' ${nanoseconds})
}

binary_to_hex() {
    input_val=${1:?"Error: No binary input provided to convert to hex"}

    echo -n ${input_val} | xxd -p -u -c1
}

binary_to_escaped_hex() {
    input_val=${1:?"Error: No binary input provided to convert to escaped hex"}

    formatted_val=$(echo -n ${input_val} | xxd -p -u -c1)

    # use perl to join each array element with the string "\x" to denote a hex string
    perl -e 'print("\\x".join("\\x", @ARGV));' ${formatted_val}
}

compute_crc32() {
    input_val=${1:?"Error: CRC32 checksum requires an input value"}

    if [[ -z $(which /usr/bin/crc32) ]]; then
        echo "Error: Unable to find '/usr/bin/crc32'"
        return 1
    fi

    echo -n ${input_val} | /usr/bin/crc32 /dev/stdin
}
