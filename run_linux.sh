#!/bin/bash
 ./test_time_jumps_orig_no_monotonic    | tee linux_results_orig_no_monotonic.txt
 ./test_time_jumps_orig_yes_monotonic   | tee linux_results_orig_yes_monotonic.txt
 ./test_time_jumps_fixed_no_monotonic   | tee linux_results_fixed_no_monotonic.txt
 ./test_time_jumps_fixed_yes_monotonic  | tee linux_results_fixed_yes_monotonic.txt
 ./test_time_jumps_austin_no_monotonic  | tee linux_results_austin_no_monotonic.txt
 ./test_time_jumps_austin_yes_monotonic | tee linux_results_austin_yes_monotonic.txt
