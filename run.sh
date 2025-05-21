#!/bin/bash
count=20 # Target number of SUCCESSFUL runs
protocol=PSM1


if [ ! -d "./build" ];then
    echo "Build directory not found. Running CMake..."
    cmake -B build
    if [ $? -ne 0 ]; then echo "CMake configuration failed."; exit 1; fi
    cmake --build build -j8
    if [ $? -ne 0 ]; then echo "CMake build failed."; exit 1; fi
fi


if [ $# -ge 1 ];then
    if [[ $1 =~ ^[0-9]+$ ]]; then
        count=$1; shift
        if [ $# -ge 1 ]; then protocol=$1; fi
    else
        protocol=$1; shift
        if [ $# -ge 1 ]; then
            if [[ $1 =~ ^[0-9]+$ ]]; then count=$1; else
                 echo "Usage: $0 [<count>] [<protocol>] or $0 [<protocol>] [<count>]"; exit 1; fi
        fi
    fi
fi
echo "Attempting to achieve $count successful runs with protocol $protocol"
echo "Logs will be appended to server.log and client.log"


echo "--- Script Started: $(date) ---" > server.log
echo "--- Script Started: $(date) ---" > client.log



total_time_oprf1_client=0.0; total_time_oprf2_client=0.0; total_time_hint_comp_client=0.0; total_time_psm_client=0.0; total_time_runtime_client=0.0
total_time_oprf1_server=0.0; total_time_oprf2_server=0.0; total_time_hint_comp_server=0.0; total_time_psm_server=0.0; total_time_runtime_server=0.0


current_server_log="current_server.tmp.log"
current_client_log="current_client.tmp.log"


successful_runs=0
current_attempt=1

# --- Helper function for average calculation ---
calculate_average() {
    local total=$1; local num_runs=$2
    if [ $num_runs -eq 0 ] || [ $(echo "$num_runs <= 0" | bc) -eq 1 ]; then echo "0.0000"; else
        printf "%.4f" $(echo "scale=10; $total / $num_runs" | bc -l); fi
}


accumulate_time() {
    local total_var_ref=$1; local temp_file=$2; local pattern=$3
    if [ ! -r "$temp_file" ]; then echo "Warning: Cannot read '$temp_file' for attempt $current_attempt."; return; fi
    local current_total=$(eval echo \$$total_var_ref)
    local extracted_value=$(grep -oP "$pattern" "$temp_file")
    if [[ -z "$extracted_value" ]]; then
        if [ ! -s "$temp_file" ] || [ $(wc -c <"$temp_file") -lt 10 ]; then echo "Warning: '$temp_file' empty/small. Pattern '$pattern' not found (Attempt $current_attempt). Adding 0."; else
             echo "Warning: Pattern '$pattern' not found in '$temp_file' (Attempt $current_attempt). Adding 0."; fi; extracted_value=0
    elif ! [[ "$extracted_value" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then echo "Warning: Extracted '$extracted_value' not number ('$pattern', '$temp_file'). Adding 0."; extracted_value=0; fi
    local new_total=$(echo "$current_total + $extracted_value" | bc)
    eval $total_var_ref=$new_total
}

# --- Main Loop ---
while [ $successful_runs -lt $count ]
do
    echo "--- Starting Attempt $current_attempt (Target: $((successful_runs + 1))/$count Successful Runs) ---"
    server_pid="" # 重置 PID
    client_pid=""


    ./build/bin/gcf_psi -r 0 -p 31000 -c 1 -s 4096 -n 8 -y $protocol > "$current_server_log" 2>&1 &
    server_pid=$!
    echo "Server started (PID: $server_pid) for attempt $current_attempt."


    sleep 0.5


    if ! kill -0 $server_pid > /dev/null 2>&1; then
         echo "Error: Server (PID $server_pid) seems to have terminated prematurely before client start. Skipping attempt."

         echo "" >> server.log; echo "--- FAILED Attempt $current_attempt Server Output (Premature Exit) ---" >> server.log; cat "$current_server_log" >> server.log; echo "--- End FAILED Attempt $current_attempt Server ---" >> server.log
         rm -f "$current_server_log" "$current_client_log" 
         current_attempt=$((current_attempt + 1))
         sleep 3 # 
         continue # 
    fi

    echo "Starting client for attempt $current_attempt..."
    ./build/bin/gcf_psi -r 1 -a 127.0.0.1 -p 31000 -c 1 -s 4096 -n 8 -y $protocol > "$current_client_log" 2>&1 &
    client_pid=$!
    echo "Client started (PID: $client_pid) for attempt $current_attempt."


    wait $client_pid
    client_exit_code=$?
    echo "Client process (PID: $client_pid) finished with exit code: $client_exit_code."


    server_exit_code=0 
    valid_run=true

    if [ $client_exit_code -ne 0 ]; then

        valid_run=false
        if [ $client_exit_code -eq 139 ]; then echo "Error: Client failed with Segmentation Fault (Exit Code: 139) on attempt $current_attempt."; else
             echo "Error: Client failed (Exit Code: $client_exit_code) on attempt $current_attempt."; fi


        echo "Client failed. Terminating server (PID: $server_pid) forcefully..."
        if kill -0 $server_pid > /dev/null 2>&1; then 
            kill -9 $server_pid
            server_exit_code=9 
            echo "Server (PID: $server_pid) terminated."
 
            sleep 0.2
 
            if kill -0 $server_pid > /dev/null 2>&1; then
                 echo "Warning: Failed to kill server process $server_pid forcefully. Manual intervention might be needed."
            fi
        else
            echo "Server (PID: $server_pid) already terminated or not found."


            server_exit_code=1 
        fi
    else

        echo "Client finished successfully. Waiting for server (PID: $server_pid)..."
        wait $server_pid
        server_exit_code=$?
        echo "Server process (PID: $server_pid) finished with exit code: $server_exit_code."
        if [ $server_exit_code -ne 0 ]; then
             echo "Error: Server exited abnormally (Exit Code: $server_exit_code) even after successful client run."
             valid_run=false 
    fi


    if $valid_run; then

        successful_runs=$((successful_runs + 1))
        echo "Attempt $current_attempt successful (Run $successful_runs/$count). Processing logs..."

        accumulate_time total_time_oprf1_server "$current_server_log" 'Time for OPRF1 \K[0-9\.]+'
        accumulate_time total_time_oprf2_server "$current_server_log" 'Time for OPRF2 \K[0-9\.]+'
        accumulate_time total_time_hint_comp_server "$current_server_log" 'Time for hint computation \K[0-9\.]+'
        accumulate_time total_time_psm_server "$current_server_log" 'Timing for PSM \K[0-9\.]+'
        accumulate_time total_time_runtime_server "$current_server_log" 'Total runtime w/o base OTs:\K[0-9\.]+'
        accumulate_time total_time_oprf1_client "$current_client_log" 'Time for OPRF1 \K[0-9\.]+'
        accumulate_time total_time_oprf2_client "$current_client_log" 'Time for OPRF2 \K[0-9\.]+'
        accumulate_time total_time_hint_comp_client "$current_client_log" 'Time for hint computation \K[0-9\.]+'
        accumulate_time total_time_psm_client "$current_client_log" 'Timing for PSM \K[0-9\.]+'
        accumulate_time total_time_runtime_client "$current_client_log" 'Total runtime w/o base OTs:\K[0-9\.]+'


        echo "" >> server.log; echo "--- SUCCESSFUL Run $successful_runs (Attempt $current_attempt) Server Output (PID: $server_pid, Exit: $server_exit_code) ---" >> server.log; cat "$current_server_log" >> server.log; echo "--- End Run $successful_runs Server ---" >> server.log
        echo "" >> client.log; echo "--- SUCCESSFUL Run $successful_runs (Attempt $current_attempt) Client Output (PID: $client_pid, Exit: $client_exit_code) ---" >> client.log; cat "$current_client_log" >> client.log; echo "--- End Run $successful_runs Client ---" >> client.log
    else

        echo "Attempt $current_attempt failed. Cleaning up recorded logs..."



        echo "" >> server.log; echo "--- FAILED Attempt $current_attempt Server Output (PID: $server_pid, Exit: $server_exit_code) ---" >> server.log; cat "$current_server_log" >> server.log; echo "--- End FAILED Attempt $current_attempt Server ---" >> server.log
        echo "" >> client.log; echo "--- FAILED Attempt $current_attempt Client Output (PID: $client_pid, Exit: $client_exit_code) ---" >> client.log; cat "$current_client_log" >> client.log; echo "--- End FAILED Attempt $current_attempt Client ---" >> client.log

        echo "Retrying after a pause..."
        sleep 3 
    fi


    rm -f "$current_server_log" "$current_client_log"

    current_attempt=$((current_attempt + 1))


    if [ $current_attempt -gt $(($count * 3 + 5)) ]; then 
       echo "Error: Exceeded maximum number of attempts ($(($current_attempt -1))). Aborting."
       break
    fi


    if [ $successful_runs -lt $count ]; then
       sleep 1
    fi
done


echo "--- Loop Finished ---"


if [ $successful_runs -lt $count ]; then
    echo "Warning: Only $successful_runs out of $count desired runs completed successfully after $(($current_attempt -1)) attempts."
fi



average_oprf1_server=$(calculate_average $total_time_oprf1_server $successful_runs)
average_oprf2_server=$(calculate_average $total_time_oprf2_server $successful_runs)
average_hint_comp_server=$(calculate_average $total_time_hint_comp_server $successful_runs)
average_psm_server=$(calculate_average $total_time_psm_server $successful_runs)
average_runtime_server=$(calculate_average $total_time_runtime_server $successful_runs)
average_oprf1_client=$(calculate_average $total_time_oprf1_client $successful_runs)
average_oprf2_client=$(calculate_average $total_time_oprf2_client $successful_runs)
average_hint_comp_client=$(calculate_average $total_time_hint_comp_client $successful_runs)
average_psm_client=$(calculate_average $total_time_psm_client $successful_runs)
average_runtime_client=$(calculate_average $total_time_runtime_client $successful_runs)


echo ""
echo "============================================="
echo "Average Server Times ($successful_runs successful runs from $(($current_attempt -1)) attempts):"
echo "============================================="
echo "OPRF1: $average_oprf1_server ms"
echo "OPRF2: $average_oprf2_server ms"
echo "Hint Computation: $average_hint_comp_server ms"
echo "PSM: $average_psm_server ms"
echo "Total Runtime w/o Base OTs: $average_runtime_server ms"
echo ""
echo "============================================="
echo "Average Client Times ($successful_runs successful runs from $(($current_attempt -1)) attempts):"
echo "============================================="
echo "OPRF1: $average_oprf1_client ms"
echo "OPRF2: $average_oprf2_client ms"
echo "Hint Computation: $average_hint_comp_client ms"
echo "PSM: $average_psm_client ms"
echo "Total Runtime w/o Base OTs: $average_runtime_client ms"
echo "============================================="
echo ""
echo "Full logs are available in server.log and client.log"
echo "Script finished."


if [ $successful_runs -lt $count ]; then exit 1; else exit 0; fi
