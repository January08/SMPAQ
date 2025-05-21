#!/bin/bash
count=20 # Target number of SUCCESSFUL runs
protocol=PSM1

# --- 构建检查 ---
if [ ! -d "./build" ];then
    echo "Build directory not found. Running CMake..."
    cmake -B build
    if [ $? -ne 0 ]; then echo "CMake configuration failed."; exit 1; fi
    cmake --build build -j8
    if [ $? -ne 0 ]; then echo "CMake build failed."; exit 1; fi
fi

# --- 参数解析 ---
# (参数解析逻辑不变)
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

# --- 初始化主日志文件 ---
echo "--- Script Started: $(date) ---" > server.log
echo "--- Script Started: $(date) ---" > client.log

# --- 初始化统计变量 ---
# (统计变量初始化不变)
total_time_oprf1_client=0.0; total_time_oprf2_client=0.0; total_time_hint_comp_client=0.0; total_time_psm_client=0.0; total_time_runtime_client=0.0
total_time_oprf1_server=0.0; total_time_oprf2_server=0.0; total_time_hint_comp_server=0.0; total_time_psm_server=0.0; total_time_runtime_server=0.0

# --- 临时文件名 ---
current_server_log="current_server.tmp.log"
current_client_log="current_client.tmp.log"

# --- 运行循环 ---
successful_runs=0
current_attempt=1

# --- Helper function for average calculation ---
calculate_average() {
    local total=$1; local num_runs=$2
    if [ $num_runs -eq 0 ] || [ $(echo "$num_runs <= 0" | bc) -eq 1 ]; then echo "0.0000"; else
        printf "%.4f" $(echo "scale=10; $total / $num_runs" | bc -l); fi
}

# --- 安全地提取和累加函数 ---
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

    # --- 启动服务器 (后台) ---
    ./build/bin/gcf_psi -r 0 -p 31000 -c 1 -s 4096 -n 8 -y $protocol > "$current_server_log" 2>&1 &
    server_pid=$!
    echo "Server started (PID: $server_pid) for attempt $current_attempt."

    # 短暂等待服务器启动和端口绑定
    sleep 0.5

    # --- 启动客户端 (后台) ---
    # 检查服务器是否已意外退出 (例如端口冲突)
    if ! kill -0 $server_pid > /dev/null 2>&1; then
         echo "Error: Server (PID $server_pid) seems to have terminated prematurely before client start. Skipping attempt."
         # 记录服务器日志
         echo "" >> server.log; echo "--- FAILED Attempt $current_attempt Server Output (Premature Exit) ---" >> server.log; cat "$current_server_log" >> server.log; echo "--- End FAILED Attempt $current_attempt Server ---" >> server.log
         rm -f "$current_server_log" "$current_client_log" # 清理可能的空文件
         current_attempt=$((current_attempt + 1))
         sleep 3 # 暂停后重试
         continue # 进入下一次循环
    fi

    echo "Starting client for attempt $current_attempt..."
    ./build/bin/gcf_psi -r 1 -a 127.0.0.1 -p 31000 -c 1 -s 4096 -n 8 -y $protocol > "$current_client_log" 2>&1 &
    client_pid=$!
    echo "Client started (PID: $client_pid) for attempt $current_attempt."

    # --- 首先等待客户端结束 ---
    wait $client_pid
    client_exit_code=$?
    echo "Client process (PID: $client_pid) finished with exit code: $client_exit_code."

    # --- 根据客户端结果处理 ---
    server_exit_code=0 # 初始假设
    valid_run=true

    if [ $client_exit_code -ne 0 ]; then
        # --- 客户端失败 ---
        valid_run=false
        if [ $client_exit_code -eq 139 ]; then echo "Error: Client failed with Segmentation Fault (Exit Code: 139) on attempt $current_attempt."; else
             echo "Error: Client failed (Exit Code: $client_exit_code) on attempt $current_attempt."; fi

        # **关键：客户端失败，立即强制终止服务器**
        echo "Client failed. Terminating server (PID: $server_pid) forcefully..."
        if kill -0 $server_pid > /dev/null 2>&1; then # 检查服务器是否还在运行
            kill -9 $server_pid
            server_exit_code=9 # 标记为被强制杀死
            echo "Server (PID: $server_pid) terminated."
            # 短暂等待确保 kill 完成
            sleep 0.2
            # 再次检查是否真的被杀死
            if kill -0 $server_pid > /dev/null 2>&1; then
                 echo "Warning: Failed to kill server process $server_pid forcefully. Manual intervention might be needed."
            fi
        else
            echo "Server (PID: $server_pid) already terminated or not found."
            # 尝试从日志判断服务器状态，或设一个通用错误码
            # grep -q "Error" "$current_server_log" # 这是一个简单的例子
            server_exit_code=1 # 标记为失败
        fi
    else
        # --- 客户端成功 ---
        echo "Client finished successfully. Waiting for server (PID: $server_pid)..."
        wait $server_pid
        server_exit_code=$?
        echo "Server process (PID: $server_pid) finished with exit code: $server_exit_code."
        if [ $server_exit_code -ne 0 ]; then
             echo "Error: Server exited abnormally (Exit Code: $server_exit_code) even after successful client run."
             valid_run=false # 如果服务器在客户端成功后异常退出，也标记为失败
        fi
    fi

    # --- 处理日志 或 清理 ---
    if $valid_run; then
        # --- 成功运行 ---
        successful_runs=$((successful_runs + 1))
        echo "Attempt $current_attempt successful (Run $successful_runs/$count). Processing logs..."
        # (数据累加逻辑不变)
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

        # 追加成功日志
        echo "" >> server.log; echo "--- SUCCESSFUL Run $successful_runs (Attempt $current_attempt) Server Output (PID: $server_pid, Exit: $server_exit_code) ---" >> server.log; cat "$current_server_log" >> server.log; echo "--- End Run $successful_runs Server ---" >> server.log
        echo "" >> client.log; echo "--- SUCCESSFUL Run $successful_runs (Attempt $current_attempt) Client Output (PID: $client_pid, Exit: $client_exit_code) ---" >> client.log; cat "$current_client_log" >> client.log; echo "--- End Run $successful_runs Client ---" >> client.log
    else
        # --- 失败运行 ---
        echo "Attempt $current_attempt failed. Cleaning up recorded logs..."
        # (服务器应该已经被上面的逻辑杀死了)

        # 追加失败日志
        echo "" >> server.log; echo "--- FAILED Attempt $current_attempt Server Output (PID: $server_pid, Exit: $server_exit_code) ---" >> server.log; cat "$current_server_log" >> server.log; echo "--- End FAILED Attempt $current_attempt Server ---" >> server.log
        echo "" >> client.log; echo "--- FAILED Attempt $current_attempt Client Output (PID: $client_pid, Exit: $client_exit_code) ---" >> client.log; cat "$current_client_log" >> client.log; echo "--- End FAILED Attempt $current_attempt Client ---" >> client.log

        echo "Retrying after a pause..."
        sleep 3 # 失败后暂停长一点
    fi

    # --- 清理临时文件 ---
    rm -f "$current_server_log" "$current_client_log"

    # --- 增加尝试次数 ---
    current_attempt=$((current_attempt + 1))

    # --- 最大尝试次数保护 ---
    if [ $current_attempt -gt $(($count * 3 + 5)) ]; then # 例如，允许目标次数3倍+5次额外尝试
       echo "Error: Exceeded maximum number of attempts ($(($current_attempt -1))). Aborting."
       break
    fi

    # --- 每次尝试间暂停 ---
    if [ $successful_runs -lt $count ]; then
       sleep 1
    fi
done

# --- 循环结束 ---
echo "--- Loop Finished ---"

# --- 检查是否达到目标次数 ---
if [ $successful_runs -lt $count ]; then
    echo "Warning: Only $successful_runs out of $count desired runs completed successfully after $(($current_attempt -1)) attempts."
fi

# --- 计算平均值 ---
# (计算平均值逻辑不变)
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

# --- 输出平均结果 ---
# (输出结果逻辑不变, 但使用 $current_attempt - 1 作为总尝试次数)
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

# --- 设置最终退出码 ---
if [ $successful_runs -lt $count ]; then exit 1; else exit 0; fi