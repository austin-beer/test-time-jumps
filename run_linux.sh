#!/bin/bash
systemctl stop ntpd
 ./test_time_jumps_develop1_default   | tee linux_results_develop1_default.txt
 ./test_time_jumps_develop2_v2_shared | tee linux_results_develop2_v2_shared.txt
#./test_time_jumps_feature1_default   | tee linux_results_feature1_default.txt
#./test_time_jumps_feature2_v2_shared | tee linux_results_feature2_v2_shared.txt
 ./test_time_jumps_austin1_default    | tee linux_results_austin1_default.txt
 ./test_time_jumps_austin2_v2_shared  | tee linux_results_austin2_v2_shared.txt
#./git_austin/bin.v2/libs/thread/test/gcc-5.4.0/dbg/thrdp-pthrd/thrd-mlt/test_time_jumps_1 | tee linux_results_test1.txt
#./git_austin/bin.v2/libs/thread/test/gcc-5.4.0/dbg/thrdp-pthrd/thrd-mlt/test_time_jumps_2 | tee linux_results_test2.txt
#./git_austin/bin.v2/libs/thread/test/gcc-5.4.0/dbg/thrdp-pthrd/thrd-mlt/test_time_jumps_3 | tee linux_results_test3.txt
systemctl start ntpd
