    powershell "./test_time_jumps_orig_no_generic_shared.exe    | tee windows_results_orig_no_generic_shared.txt"
    powershell "./test_time_jumps_orig_yes_generic_shared.exe   | tee windows_results_orig_yes_generic_shared.txt"
    powershell "./test_time_jumps_fixed_no_generic_shared.exe   | tee windows_results_fixed_no_generic_shared.txt"
    powershell "./test_time_jumps_fixed_yes_generic_shared.exe  | tee windows_results_fixed_yes_generic_shared.txt"
rem powershell "./test_time_jumps_austin_no_generic_shared.exe  | tee windows_results_austin_no_generic_shared.txt"
rem powershell "./test_time_jumps_austin_yes_generic_shared.exe | tee windows_results_austin_yes_generic_shared.txt"
