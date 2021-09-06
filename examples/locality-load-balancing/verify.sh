#!/bin/bash -e

export NAME=locality-load-balancing
export DELAY=5

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

check_health() {
    docker-compose exec -T client-envoy curl -s localhost:8001/clusters | grep health_flags
}

request_backend() {
    docker-compose exec -T client-envoy python3 client.py http://localhost:3000/ 100
}

bring_up_backend() {
    local server
    server="$1"

    docker-compose exec -T client-envoy curl -s "$server":8000/healthy
}

bring_down_backend() {
    local server
    server="$1"

    docker-compose exec -T client-envoy curl -s "$server":8000/unhealthy
}

run_log "=== Demo setup
client  -> backend-local-1      [priority: 0, weight: 1]
        -> backend-local-2      [priority: 1, weight: 1]
        -> backend-remote-1     [priority: 1, weight: 1]
        -> backend-remote-2     [priority: 2, weight: 1]
"

run_log "=== Scenario 1: one replica per locality"

run_log "Send requests to backend."
check_health
request_backend

run_log "Bring down backend-local-1 then snooze for ${DELAY}s"
bring_down_backend "${NAME}"_backend-local-1_1
sleep ${DELAY}

run_log "Send requests to backend."
check_health
request_backend

run_log "Bring down backend-local-2 then snooze for ${DELAY}s"
bring_down_backend "${NAME}"_backend-local-2_1
sleep ${DELAY}

run_log "Traffic is load balanced goes to remote only."
check_health
request_backend

run_log "=== Conclusion:
When the highest priority locality is completely unhealthy, the remaining localities will share the traffic."

run_log "=== Scenario 2: multiple replica per subset"

run_log "Recover local-1 and local-2 then snooze for ${DELAY}s"
bring_up_backend "${NAME}"_backend-local-1_1
bring_up_backend "${NAME}"_backend-local-2_1
sleep ${DELAY}

run_log "Scale backend-local-1 to 5 replicas then snooze for ${DELAY}s"
docker-compose -p ${NAME} up --scale backend-local-1=5 -d
sleep ${DELAY}

run_log "Bring down 4 replicas in backend-local-1 then snooze for ${DELAY}s"
bring_down_backend "${NAME}"_backend-local-1_2
bring_down_backend "${NAME}"_backend-local-1_3
bring_down_backend "${NAME}"_backend-local-1_4
bring_down_backend "${NAME}"_backend-local-1_5
sleep ${DELAY}

run_log "Send requests to backend."
check_health
request_backend

run_log "Bring down endpoints of priority 1"
bring_down_backend "${NAME}"_backend-local-2_1
bring_down_backend "${NAME}"_backend-remote-1_1
sleep ${DELAY}

run_log "Send requests to backend."
check_health
request_backend

run_log "=== Conclusion:
When the highest priority locality is partially unhealthy, the traffic will be gradually shifted to other localities. The default overprovisioning factor is 1.4, it means that the shifting threshold is at around 71% of the health rate."
