package go_build_test

import (
	"testing"

	_ "github.com/envoyproxy/go-control-plane/v2/envoy/api/v2"
	_ "github.com/envoyproxy/go-control-plane/v2/envoy/api/v2/auth"
	_ "github.com/envoyproxy/go-control-plane/v2/envoy/config/bootstrap/v2"
	_ "github.com/envoyproxy/go-control-plane/v2/envoy/service/accesslog/v2"
	_ "github.com/envoyproxy/go-control-plane/v2/envoy/service/discovery/v2"
	_ "github.com/envoyproxy/go-control-plane/v2/envoy/service/metrics/v2"
	_ "github.com/envoyproxy/go-control-plane/v2/envoy/service/ratelimit/v2"
	_ "github.com/envoyproxy/go-control-plane/v2/envoy/service/trace/v2"
)

func TestNoop(t *testing.T) {
	// Noop test that verifies the successful importation of Envoy V2 API protos
}
