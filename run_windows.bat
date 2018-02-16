net stop w32time
    powershell ".\test_time_jumps_develop1_default.exe         | tee windows_results_develop1_default.txt"
    powershell ".\test_time_jumps_develop2_generic_shared.exe  | tee windows_results_develop2_generic_shared.txt"
    powershell ".\test_time_jumps_develop3_v2_shared.exe       | tee windows_results_develop3_v2_shared.txt"
rem powershell ".\test_time_jumps_feature1_default.exe         | tee windows_results_feature1_default.txt"
rem powershell ".\test_time_jumps_feature2_generic_shared.exe  | tee windows_results_feature2_generic_shared.txt"
rem powershell ".\test_time_jumps_feature3_v2_shared.exe       | tee windows_results_feature3_v2_shared.txt"
    powershell ".\test_time_jumps_austin1_default.exe          | tee windows_results_austin1_default.txt"
    powershell ".\test_time_jumps_austin2_generic_shared.exe   | tee windows_results_austin2_generic_shared.txt"
    powershell ".\test_time_jumps_austin3_v2_shared.exe        | tee windows_results_austin3_v2_shared.txt"
rem powershell ".\git_austin\bin.v2\libs\thread\test\msvc-14.1\dbg\lnk-sttc\thrdp-wn32\thrd-mlt\test_time_jumps_1.exe | tee windows_results_test1.txt"
rem powershell ".\git_austin\bin.v2\libs\thread\test\msvc-14.1\dbg\lnk-sttc\thrdp-wn32\thrd-mlt\test_time_jumps_2.exe | tee windows_results_test2.txt"
rem powershell ".\git_austin\bin.v2\libs\thread\test\msvc-14.1\dbg\lnk-sttc\thrdp-wn32\thrd-mlt\test_time_jumps_3.exe | tee windows_results_test3.txt"
net start w32time
