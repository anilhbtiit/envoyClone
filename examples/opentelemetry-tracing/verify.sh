#!/bin/bash -e

export NAME=opentelemetry
export PORT_PROXY="${OPENTELEMETRY_PORT_PROXY:-12600}"
export PORT_UI="${OPENTELEMETRY_PORT_UI:-12601}"
export PORT_COLLECTOR_ZPAGE="${OPENTELEMETRY_PORT_COLLECTOR_ZPAGE:-12602}"
export PORT_HEALTH_CHECK="${OPENTELEMETRY_PORT_HEALTH_CHECK:-12603}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Health check OpenTelemetry"
wait_for 10 bash -c "responds_with 'Server available' http://localhost:${PORT_HEALTH_CHECK} | jq '.status'"

run_log "Make a request to service-1"
responds_with \
    "Hello from behind Envoy (service 1)!" \
    "http://localhost:${PORT_PROXY}/trace/1"

run_log "Make a request to service-2"
responds_with \
    "Hello from behind Envoy (service 2)!" \
    "http://localhost:${PORT_PROXY}/trace/2"

run_log "Wait for sending traces"
sleep 5

run_log "Count the traces that have >0s in OpenTelemetry UI"
curl -sf "http://localhost:${PORT_COLLECTOR_ZPAGE}/debug/tracez?zspanname=opentelemetry.proto.collector.trace.v1.TraceService%2fExport&ztype=1&zlatencybucket=0" | grep trace_id -c
