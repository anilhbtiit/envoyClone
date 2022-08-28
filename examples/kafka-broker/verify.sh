#!/bin/bash -e

export NAME=kafka-broker
export PORT_PROXY="${KAFKA_PORT_PROXY:-11100}"
export PORT_ADMIN="${KAFKA_PORT_ADMIN:-11101}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

kafka_client () {
    docker-compose run --rm kafka-client "$@"
}

TOPIC="envoy-kafka-broker"

MESSAGE="Welcome to Envoy and Kafka Broker filter!"

run_log "Create a Kafka topic"
kafka_client kafka-topics --bootstrap-server proxy:10000 --create --topic $TOPIC

run_log "Check the Kafka topic"
kafka_client kafka-topics --bootstrap-server proxy:10000 --list | grep $TOPIC

run_log "Send a message using the Kafka producer"
kafka_client /bin/bash -c " \
    echo $MESSAGE \
    | kafka-console-producer --request-required-acks 1 --broker-list proxy:10000 --topic $TOPIC"

run_log "Receive a message using the Kafka consumer"
kafka_client kafka-console-consumer --bootstrap-server proxy:10000 --topic $TOPIC --from-beginning --max-messages 1 | grep "$MESSAGE"

run_log "Check admin kafka_broker stats"
responds_with \
    "kafka.kafka_broker.request.create_topics_request: 1" \
    "http://localhost:${PORT_ADMIN}/stats?filter=kafka.kafka_broker.request.create_topics_request"
responds_with \
    "kafka.kafka_broker.request.api_versions_request: 4" \
    "http://localhost:${PORT_ADMIN}/stats?filter=kafka.kafka_broker.request.api_versions_request"
responds_with \
    "kafka.kafka_broker.request.find_coordinator_request: 1" \
    "http://localhost:${PORT_ADMIN}/stats?filter=kafka.kafka_broker.request.find_coordinator_request"
responds_with \
    "kafka.kafka_broker.request.metadata_request: 4" \
    "http://localhost:${PORT_ADMIN}/stats?filter=kafka.kafka_broker.request.metadata_request"
responds_with \
    "kafka.kafka_broker.response.api_versions_response: 4" \
    "http://localhost:${PORT_ADMIN}/stats?filter=kafka.kafka_broker.response.api_versions_response"
responds_with \
    "kafka.kafka_broker.response.find_coordinator_response: 1" \
    "http://localhost:${PORT_ADMIN}/stats?filter=kafka.kafka_broker.response.find_coordinator_response"
responds_with \
    "kafka.kafka_broker.response.metadata_response: 4" \
    "http://localhost:${PORT_ADMIN}/stats?filter=kafka.kafka_broker.response.metadata_response"

run_log "Check admin kafka_service stats"
responds_with \
    "cluster.kafka_service.max_host_weight: 1" \
    "http://localhost:${PORT_ADMIN}/stats?filter=cluster.kafka_service.max_host_weight"
responds_with \
    "cluster.kafka_service.membership_healthy: 1" \
    "http://localhost:${PORT_ADMIN}/stats?filter=cluster.kafka_service.membership_healthy"
responds_with \
    "cluster.kafka_service.membership_total: 1" \
    "http://localhost:${PORT_ADMIN}/stats?filter=cluster.kafka_service.membership_total"
