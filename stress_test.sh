#!/bin/bash

PROXY="http://localhost:8080"
TARGET="http://example.com"
CONCURRENT=1000

echo "========================================"
echo "Concurrency Stress Test"
echo "========================================"
echo "Proxy   : $PROXY"
echo "Target  : $TARGET"
echo "Requests: $CONCURRENT"
echo "----------------------------------------"

# Check proxy is running
if ! nc -z localhost 8080; then
  echo "[ERROR] Proxy is not running on port 8080"
  echo "Start it using: make run"
  exit 1
fi

start_time=$(date +%s)

for i in $(seq 1 $CONCURRENT); do
  curl -x $PROXY $TARGET -s -o /dev/null &
done

wait

end_time=$(date +%s)
duration=$((end_time - start_time))

echo "----------------------------------------"
echo "[PASS] Concurrency test completed"
echo "Time taken: ${duration}s"
echo "========================================"
