#!/bin/bash
# Script to test SingleFlight with multiple simultaneous connections
# 
# Usage: ./test_concurrent.sh [user] [database] [num_connections]

PROXY_HOST="127.0.0.1"
PROXY_PORT="6000"
DB_USER="${1:-postgres}"
DB_NAME="${2:-postgres}"
NUM_CONNECTIONS="${3:-5}"

echo "Concurrent Test - SingleFlight Proxy"
echo "Connecting to proxy at ${PROXY_HOST}:${PROXY_PORT}"
echo "User: ${DB_USER}"
echo "Database: ${DB_NAME}"
echo "Number of simultaneous connections: ${NUM_CONNECTIONS}"
echo ""

if ! command -v psql &> /dev/null; then
    echo "   Error: psql not found"
    echo "   Install PostgreSQL client:"
    echo "   Ubuntu/Debian: sudo apt-get install postgresql-client"
    exit 1
fi

echo "Testing initial connection"
psql -h "${PROXY_HOST}" -p "${PROXY_PORT}" -U "${DB_USER}" -d "${DB_NAME}" -c "SELECT 1;" > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "Connection error"
    exit 1
fi

QUERY="SELECT version();"

for i in $(seq 1 ${NUM_CONNECTIONS}); do
    (
        echo "[Connection $i] Starting"
        psql -h "${PROXY_HOST}" -p "${PROXY_PORT}" -U "${DB_USER}" -d "${DB_NAME}" \
            -c "${QUERY}" > /tmp/singleflight_test_${i}.out 2>&1
        if [ $? -eq 0 ]; then
            echo "[Connection $i] Success"
        else
            echo "[Connection $i] Error"
        fi
    ) &
done

wait

echo ""
echo "Test completed"
echo ""
