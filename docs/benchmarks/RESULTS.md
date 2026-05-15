# Performance results

Auto-generated from `docs/benchmarks/perf.baseline.<host>.json`
by `tools/render_perf_results.py`. Do not edit by hand — re-run
the script after a baseline refresh.

## Summary — cpp/c decode ratio per host

Values ≥ 1.0 mean zserio-c is faster than upstream cpp at the
host's defined allocator strategy (libc-vs-libc by default;
the `_arena` workloads use a bump arena vs cpp's `std::pmr`).

| Workload | `pi4` | `primary` |
| -------- | ---: | ---: |
| `addressbook` | 0.975 | 0.971 |
| `addressbook_align` | 0.790 | 0.775 |
| `addressbook_align_arena` | 2.075 | 1.212 |
| `addressbook_packed` | 0.867 | 0.767 |
| `apollo` | 1.010 | 0.888 |
| `carsales` | 1.189 | 1.157 |
| `carsales_align` | 1.290 | 1.274 |
| `carsales_packed` | 1.124 | 1.088 |
| `large_array` | 1.584 | 2.285 |
| `medium_nested` | 1.022 | 0.942 |
| `simpletrace` | 0.956 | 0.883 |
| `simpletrace_packed` | 1.340 | 1.160 |
| `simpletrace_packed_arena` | 1.332 | 1.169 |
| `small_flat` | 1.012 | 0.988 |

**Headline:** `pi4` beats or ties cpp on 10 / 14 workloads; `primary` beats or ties cpp on 7 / 14 workloads.

### Host `primary`

Decode `ns/op` per workload (median of 5 runs, sorted by ratio).

| Workload | zserio_c ns | zserio_cpp ns | cpp/c | Verdict |
| -------- | ----------: | ------------: | ----: | ------- |
| `large_array` | 281,454 | 643,261 | 2.285 | clear win |
| `carsales_align` | 895,569 | 1,140,972 | 1.274 | win |
| `addressbook_align_arena` | 341,722 | 414,182 | 1.212 | win |
| `simpletrace_packed_arena` | 46,666 | 54,563 | 1.169 | win |
| `simpletrace_packed` | 48,729 | 56,527 | 1.160 | win |
| `carsales` | 1,100,423 | 1,273,711 | 1.157 | win |
| `carsales_packed` | 1,196,694 | 1,302,257 | 1.088 | win |
| `small_flat` | 11 | 11 | 0.988 | parity |
| `addressbook` | 1,467,452 | 1,424,916 | 0.971 | parity |
| `medium_nested` | 181 | 170 | 0.942 | narrow loss |
| `apollo` | 128,480 | 114,119 | 0.888 | narrow loss |
| `simpletrace` | 130,729 | 115,435 | 0.883 | narrow loss |
| `addressbook_align` | 1,205,274 | 934,073 | 0.775 | loss |
| `addressbook_packed` | 1,511,521 | 1,158,925 | 0.767 | loss |

### Host `pi4`

Decode `ns/op` per workload (median of 5 runs, sorted by ratio).

| Workload | zserio_c ns | zserio_cpp ns | cpp/c | Verdict |
| -------- | ----------: | ------------: | ----: | ------- |
| `addressbook_align_arena` | 1,287,797 | 2,672,095 | 2.075 | clear win |
| `large_array` | 1,640,669 | 2,599,548 | 1.584 | clear win |
| `simpletrace_packed` | 204,234 | 273,596 | 1.340 | win |
| `simpletrace_packed_arena` | 203,952 | 271,589 | 1.332 | win |
| `carsales_align` | 3,840,053 | 4,952,682 | 1.290 | win |
| `carsales` | 4,690,249 | 5,575,878 | 1.189 | win |
| `carsales_packed` | 5,190,316 | 5,832,858 | 1.124 | win |
| `medium_nested` | 621 | 635 | 1.022 | parity |
| `small_flat` | 44 | 44 | 1.012 | parity |
| `apollo` | 449,681 | 454,392 | 1.010 | parity |
| `addressbook` | 5,860,196 | 5,715,078 | 0.975 | parity |
| `simpletrace` | 525,903 | 502,567 | 0.956 | parity |
| `addressbook_packed` | 6,120,604 | 5,305,752 | 0.867 | narrow loss |
| `addressbook_align` | 4,872,713 | 3,848,954 | 0.790 | loss |
