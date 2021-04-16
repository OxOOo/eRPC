set -ex

./large_rpc_tput \
    --test_ms 30000 \
    --req_size 10485760 \
    --resp_size 32 \
    --num_processes 2 \
    --num_proc_0_threads 1 \
    --num_proc_other_threads 1 \
    --concurrency 8 \
    --drop_prob 0.0 \
    --profile incast \
    --throttle 0 \
    --throttle_fraction 0.9 \
    --process_id 0
