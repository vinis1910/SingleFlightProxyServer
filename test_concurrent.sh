PROXY_HOST="127.0.0.1"
PROXY_PORT="6000"
DB_USER="${1:-postgres}"
DB_NAME="${2:-postgres}"
NUM_CONNECTIONS="${3:-100}"

echo "Concurrent Test - SingleFlight Proxy"
echo "Proxy: ${PROXY_HOST}:${PROXY_PORT}"
echo "User: ${DB_USER}"
echo "Database: ${DB_NAME}"
echo "Number of simultaneous connections: ${NUM_CONNECTIONS}"
echo ""

if ! command -v psql &> /dev/null; then
    exit 1
fi

echo "Step 1: Testing initial connection"
psql -h "${PROXY_HOST}" -p "${PROXY_PORT}" -U "${DB_USER}" -d "${DB_NAME}" \
    -c "SELECT 1;" > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "Error: Failed to connect to proxy"
    exit 1
fi

echo "Connection successful"

QUERY="SELECT pg_sleep(1.0), version();"

echo "Step 2: Sending ${NUM_CONNECTIONS} simultaneous queries"

BARRIER_FILE="/tmp/singleflight_barrier_$$"
rm -f "${BARRIER_FILE}"

for i in $(seq 1 ${NUM_CONNECTIONS}); do
    (
        while [ ! -f "${BARRIER_FILE}" ]; do
            sleep 0.001
        done
        
        START_TIME=$(date +%s%N)
        psql -h "${PROXY_HOST}" -p "${PROXY_PORT}" -U "${DB_USER}" -d "${DB_NAME}" \
            -c "${QUERY}" > /tmp/singleflight_test_${i}.out 2>&1
        END_TIME=$(date +%s%N)
        ELAPSED=$(( (END_TIME - START_TIME) / 1000000 ))
        
        if [ $? -eq 0 ]; then
            echo "[Connection $i] Success (${ELAPSED}ms)"
        else
            echo "[Connection $i] Error (check /tmp/singleflight_test_${i}.out)"
        fi
    ) &
done

sleep 0.1

touch "${BARRIER_FILE}"
wait

rm -f "${BARRIER_FILE}"

echo "Test completed"
echo "Check proxy logs for:"
echo "  - '[SingleFlight] Session is leader' (should appear once)"
echo "  - '[SingleFlight] Session waiting for key' (should appear for waiters)"
echo "  - '[SingleFlight] Notified X waiters' (X should be > 0)"
echo ""
