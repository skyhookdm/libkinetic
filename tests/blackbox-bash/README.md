# Overview

ktest-runner defines what tests to run.
The logic for each test is defined in tests/usecases.
Some general, re-usable functions are defined in functions/.
Some test data are defiend in tests/data. At the moment, this only includes a specific workload for
testing batched puts.

# Running the tests
ktest-runner should be a runnable script that invokes tests with default parameters.

Just run:

    `bash ktest-runner [<number of test iterations>]`

or:

    `./ktest-runner [<number of test iterations>]`
