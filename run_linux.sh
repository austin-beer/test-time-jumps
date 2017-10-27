#!/bin/bash
 systemctl stop ntpd
 ./test_time_jumps_orig1_default       | tee linux_results_orig1_default.txt
 ./test_time_jumps_orig2_mono_condattr | tee linux_results_orig2_mono_condattr.txt
 ./test_time_jumps_fixed1_default      | tee linux_results_fixed1_default.txt
 ./test_time_jumps_fixed2_v2_shared    | tee linux_results_fixed2_v2_shared.txt
 ./test_time_jumps_austin1_default     | tee linux_results_austin1_default.txt
 ./test_time_jumps_austin2_v2_shared   | tee linux_results_austin2_v2_shared.txt
 systemctl start ntpd
