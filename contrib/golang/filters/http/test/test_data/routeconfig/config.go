package main

import (
	xds "github.com/cncf/xds/go/xds/type/v3"
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

const Name = "routeconfig"

func init() {
	http.RegisterHttpFilterConfigFactory(Name, configFactory)
	http.RegisterHttpFilterConfigParser(&parser{})
}

func configFactory(c interface{}) api.StreamFilterFactory {
	conf, ok := c.(*config)
	if !ok {
		panic("unexpected config type")
	}
	return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
		return &filter{
			config:    conf,
			callbacks: callbacks,
		}
	}
}

type config struct {
	removeHeader string
	setHeader    string
}

type parser struct {
}

func (p *parser) Parse(any *anypb.Any) (interface{}, error) {
	configStruct := &xds.TypedStruct{}
	if err := any.UnmarshalTo(configStruct); err != nil {
		return nil, err
	}

	v := configStruct.Value
	conf := &config{}
	if remove, ok := v.AsMap()["remove"].(string); ok {
		conf.removeHeader = remove
	}
	if set, ok := v.AsMap()["set"].(string); ok {
		conf.setHeader = set
	}
	return conf, nil
}

func (p *parser) Merge(parent interface{}, child interface{}) interface{} {
	parentConfig := parent.(*config)
	childConfig := child.(*config)

	// copy one
	newConfig := *parentConfig
	if childConfig.removeHeader != "" {
		newConfig.removeHeader = childConfig.removeHeader
	}
	if childConfig.setHeader != "" {
		newConfig.setHeader = childConfig.setHeader
	}
	return &newConfig
}

func main() {
}
