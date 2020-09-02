import timeit

run_count = 1000
results  = timeit.timeit(
    setup='from ktest import tools; kctl_cmd = tools.KCtl.test_getcmd()',
    stmt='kctl_proc = kctl_cmd.run(key_name="pak")',
    number=run_count
)

print(f'Get "pak" runs in {results:04f} seconds, average over {run_count} iterations')
