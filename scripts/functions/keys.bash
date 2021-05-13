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


random_ids() {
    num_ids=${1}

    let start_val=${RANDOM}
    let step_val=${RANDOM}
    stop_val=$(( (${step_val} * ${num_ids}) + ${start_val} - 1 ))

    echo $(seq ${start_val} ${step_val} ${stop_val})
}

sequential_ids() {
    start_id=${1}
    num_ids=${2}

    echo $(seq -w ${start_id} 1 ${num_ids})
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
