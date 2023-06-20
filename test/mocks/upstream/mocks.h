#pragma once

// NOLINT(namespace-envoy)

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/http/async_client.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/callback_impl.h"
#include "source/common/upstream/health_discovery_service.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/basic_resource_limit.h"
#include "test/mocks/upstream/cds_api.h"
#include "test/mocks/upstream/cluster.h"
#include "test/mocks/upstream/cluster_discovery_callback_handle.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_info_factory.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_manager_factory.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/cluster_real_priority_set.h"
#include "test/mocks/upstream/cluster_update_callbacks.h"
#include "test/mocks/upstream/cluster_update_callbacks_handle.h"
#include "test/mocks/upstream/health_check_event_logger.h"
#include "test/mocks/upstream/health_checker.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/load_balancer.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/od_cds_api.h"
#include "test/mocks/upstream/od_cds_api_handle.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/mocks/upstream/retry_host_predicate.h"
#include "test/mocks/upstream/retry_priority.h"
#include "test/mocks/upstream/retry_priority_factory.h"
#include "test/mocks/upstream/test_retry_host_predicate_factory.h"
#include "test/mocks/upstream/thread_aware_load_balancer.h"
#include "test/mocks/upstream/thread_local_cluster.h"
