set -ex

./latency \
    --test_ms 20000 \
    --sm_verbose 0 \
    --num_processes 2 \
    --process_id 1
