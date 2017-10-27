    net stop w32time
    powershell "./test_time_jumps_orig1_default.exe          | tee windows_results_orig1_default.txt"
    powershell "./test_time_jumps_orig2_generic_shared.exe   | tee windows_results_orig2_generic_shared.txt"
    powershell "./test_time_jumps_fixed1_default.exe         | tee windows_results_fixed1_default.txt"
    powershell "./test_time_jumps_fixed2_generic_shared.exe  | tee windows_results_fixed2_generic_shared.txt"
    powershell "./test_time_jumps_fixed3_v2_shared.exe       | tee windows_results_fixed3_v2_shared.txt"
    powershell "./test_time_jumps_austin1_default.exe        | tee windows_results_austin1_default.txt"
    powershell "./test_time_jumps_austin2_generic_shared.exe | tee windows_results_austin2_generic_shared.txt"
    powershell "./test_time_jumps_austin3_v2_shared.exe      | tee windows_results_austin3_v2_shared.txt"
    net start w32time
