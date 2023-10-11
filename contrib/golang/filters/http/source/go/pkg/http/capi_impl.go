/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package http

/*
// ref https://github.com/golang/go/issues/25832

#cgo CFLAGS: -I../../../../../../common/go/api -I../api
#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "api.h"

*/
import "C"
import (
	"errors"
	"reflect"
	"runtime"
	"strings"
	"sync/atomic"
	"unsafe"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/structpb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

const (
	ValueRouteName               = 1
	ValueFilterChainName         = 2
	ValueProtocol                = 3
	ValueResponseCode            = 4
	ValueResponseCodeDetails     = 5
	ValueAttemptCount            = 6
	ValueDownstreamLocalAddress  = 7
	ValueDownstreamRemoteAddress = 8
	ValueUpstreamLocalAddress    = 9
	ValueUpstreamRemoteAddress   = 10
	ValueUpstreamClusterName     = 11
	ValueVirtualClusterName      = 12

	// NOTE: this is a trade-off value.
	// When the number of header is less this value, we could use the slice on the stack,
	// otherwise, we have to allocate a new slice on the heap,
	// and the slice on the stack will be wasted.
	// So, we choose a value that many requests' number of header is less than this value.
	// But also, it should not be too large, otherwise it might be waste stack memory.
	maxStackAllocedHeaderSize = 16
	maxStackAllocedSliceLen   = maxStackAllocedHeaderSize * 2
)

type httpCApiImpl struct{}

// When the status means unexpected stage when invoke C API,
// panic here and it will be recover in the Go entry function.
func handleCApiStatus(status C.CAPIStatus) {
	switch status {
	case C.CAPIFilterIsGone,
		C.CAPIFilterIsDestroy,
		C.CAPINotInGo,
		C.CAPIInvalidPhase:
		panic(capiStatusToStr(status))
	}
}

func capiStatusToStr(status C.CAPIStatus) string {
	switch status {
	case C.CAPIFilterIsGone:
		return errRequestFinished
	case C.CAPIFilterIsDestroy:
		return errFilterDestroyed
	case C.CAPINotInGo:
		return errNotInGo
	case C.CAPIInvalidPhase:
		return errInvalidPhase
	}

	return "unknown status"
}

func capiStatusToErr(status C.CAPIStatus) error {
	switch status {
	case C.CAPIValueNotFound:
		return api.ErrValueNotFound
	case C.CAPIInternalFailure:
		return api.ErrInternalFailure
	case C.CAPISerializationFailure:
		return api.ErrSerializationFailure
	}

	return errors.New("unknown status")
}

func (c *httpCApiImpl) HttpContinue(r unsafe.Pointer, status uint64) {
	res := C.envoyGoFilterHttpContinue(r, C.int(status))
	handleCApiStatus(res)
}

// Only may panic with errRequestFinished, errFilterDestroyed or errNotInGo,
// won't panic with errInvalidPhase and others, otherwise will cause deadloop, see RecoverPanic for the details.
func (c *httpCApiImpl) HttpSendLocalReply(r unsafe.Pointer, response_code int, body_text string, headers map[string]string, grpc_status int64, details string) {
	hLen := len(headers)
	strs := make([]string, 0, hLen)
	for k, v := range headers {
		strs = append(strs, k, v)
	}
	res := C.envoyGoFilterHttpSendLocalReply(r, C.int(response_code), unsafe.Pointer(&body_text), unsafe.Pointer(&strs), C.longlong(grpc_status), unsafe.Pointer(&details))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpSendPanicReply(r unsafe.Pointer, details string) {
	res := C.envoyGoFilterHttpSendPanicReply(r, unsafe.Pointer(&details))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpGetHeader(r unsafe.Pointer, key *string, value *string) {
	res := C.envoyGoFilterHttpGetHeader(r, unsafe.Pointer(key), unsafe.Pointer(value))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpCopyHeaders(r unsafe.Pointer, num uint64, bytes uint64) map[string][]string {
	var strs []string
	if num <= maxStackAllocedHeaderSize {
		// NOTE: only const length slice may be allocated on stack.
		strs = make([]string, maxStackAllocedSliceLen)
	} else {
		// TODO: maybe we could use a memory pool for better performance,
		// since these go strings in strs, will be copied into the following map.
		strs = make([]string, num*2)
	}
	// NOTE: this buffer can not be reused safely,
	// since strings may refer to this buffer as string data, and string is const in go.
	// we have to make sure the all strings is not using before reusing,
	// but strings may be alive beyond the request life.
	buf := make([]byte, bytes)
	sHeader := (*reflect.SliceHeader)(unsafe.Pointer(&strs))
	bHeader := (*reflect.SliceHeader)(unsafe.Pointer(&buf))

	res := C.envoyGoFilterHttpCopyHeaders(r, unsafe.Pointer(sHeader.Data), unsafe.Pointer(bHeader.Data))
	handleCApiStatus(res)

	m := make(map[string][]string, num)
	for i := uint64(0); i < num*2; i += 2 {
		key := strs[i]
		value := strs[i+1]

		if v, found := m[key]; !found {
			m[key] = []string{value}
		} else {
			m[key] = append(v, value)
		}
	}
	runtime.KeepAlive(buf)
	return m
}

func (c *httpCApiImpl) HttpSetHeader(r unsafe.Pointer, key *string, value *string, add bool) {
	var act C.headerAction
	if add {
		act = C.HeaderAdd
	} else {
		act = C.HeaderSet
	}
	res := C.envoyGoFilterHttpSetHeaderHelper(r, unsafe.Pointer(key), unsafe.Pointer(value), act)
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpRemoveHeader(r unsafe.Pointer, key *string) {
	res := C.envoyGoFilterHttpRemoveHeader(r, unsafe.Pointer(key))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpGetBuffer(r unsafe.Pointer, bufferPtr uint64, length uint64) []byte {
	buf := make([]byte, length)
	bHeader := (*reflect.SliceHeader)(unsafe.Pointer(&buf))
	res := C.envoyGoFilterHttpGetBuffer(r, C.ulonglong(bufferPtr), unsafe.Pointer(bHeader.Data))
	handleCApiStatus(res)
	return buf
}

func (c *httpCApiImpl) HttpDrainBuffer(r unsafe.Pointer, bufferPtr uint64, length uint64) {
	res := C.envoyGoFilterHttpDrainBuffer(r, C.ulonglong(bufferPtr), C.uint64_t(length))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpSetBufferHelper(r unsafe.Pointer, bufferPtr uint64, value string, action api.BufferAction) {
	sHeader := (*reflect.StringHeader)(unsafe.Pointer(&value))
	c.httpSetBufferHelper(r, bufferPtr, unsafe.Pointer(sHeader.Data), C.int(sHeader.Len), action)
}

func (c *httpCApiImpl) HttpSetBytesBufferHelper(r unsafe.Pointer, bufferPtr uint64, value []byte, action api.BufferAction) {
	bHeader := (*reflect.SliceHeader)(unsafe.Pointer(&value))
	c.httpSetBufferHelper(r, bufferPtr, unsafe.Pointer(bHeader.Data), C.int(bHeader.Len), action)
}

func (c *httpCApiImpl) httpSetBufferHelper(r unsafe.Pointer, bufferPtr uint64, data unsafe.Pointer, length C.int, action api.BufferAction) {
	var act C.bufferAction
	switch action {
	case api.SetBuffer:
		act = C.Set
	case api.AppendBuffer:
		act = C.Append
	case api.PrependBuffer:
		act = C.Prepend
	}
	res := C.envoyGoFilterHttpSetBufferHelper(r, C.ulonglong(bufferPtr), data, length, act)
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpCopyTrailers(r unsafe.Pointer, num uint64, bytes uint64) map[string][]string {
	var strs []string
	if num <= maxStackAllocedHeaderSize {
		// NOTE: only const length slice may be allocated on stack.
		strs = make([]string, maxStackAllocedSliceLen)
	} else {
		// TODO: maybe we could use a memory pool for better performance,
		// since these go strings in strs, will be copied into the following map.
		strs = make([]string, num*2)
	}
	// NOTE: this buffer can not be reused safely,
	// since strings may refer to this buffer as string data, and string is const in go.
	// we have to make sure the all strings is not using before reusing,
	// but strings may be alive beyond the request life.
	buf := make([]byte, bytes)
	sHeader := (*reflect.SliceHeader)(unsafe.Pointer(&strs))
	bHeader := (*reflect.SliceHeader)(unsafe.Pointer(&buf))

	res := C.envoyGoFilterHttpCopyTrailers(r, unsafe.Pointer(sHeader.Data), unsafe.Pointer(bHeader.Data))
	handleCApiStatus(res)

	m := make(map[string][]string, num)
	for i := uint64(0); i < num*2; i += 2 {
		key := strs[i]
		value := strs[i+1]

		if v, found := m[key]; !found {
			m[key] = []string{value}
		} else {
			m[key] = append(v, value)
		}
	}
	return m
}

func (c *httpCApiImpl) HttpSetTrailer(r unsafe.Pointer, key *string, value *string, add bool) {
	var act C.headerAction
	if add {
		act = C.HeaderAdd
	} else {
		act = C.HeaderSet
	}
	res := C.envoyGoFilterHttpSetTrailer(r, unsafe.Pointer(key), unsafe.Pointer(value), act)
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpRemoveTrailer(r unsafe.Pointer, key *string) {
	res := C.envoyGoFilterHttpRemoveTrailer(r, unsafe.Pointer(key))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpGetStringValue(rr unsafe.Pointer, id int) (string, bool) {
	r := (*httpRequest)(rr)
	var value string
	// add a lock to protect filter->req_->strValue field in the Envoy side, from being writing concurrency,
	// since there might be multiple concurrency goroutines invoking this API on the Go side.
	r.mutex.Lock()
	defer r.mutex.Unlock()
	res := C.envoyGoFilterHttpGetStringValue(unsafe.Pointer(r.req), C.int(id), unsafe.Pointer(&value))
	if res == C.CAPIValueNotFound {
		return "", false
	}
	handleCApiStatus(res)
	// copy the memory from c to Go.
	return strings.Clone(value), true
}

func (c *httpCApiImpl) HttpGetIntegerValue(r unsafe.Pointer, id int) (uint64, bool) {
	var value uint64
	res := C.envoyGoFilterHttpGetIntegerValue(r, C.int(id), unsafe.Pointer(&value))
	if res == C.CAPIValueNotFound {
		return 0, false
	}
	handleCApiStatus(res)
	return value, true
}

func (c *httpCApiImpl) HttpGetDynamicMetadata(rr unsafe.Pointer, filterName string) map[string]interface{} {
	r := (*httpRequest)(rr)
	var buf []byte
	r.mutex.Lock()
	defer r.mutex.Unlock()
	r.sema.Add(1)
	res := C.envoyGoFilterHttpGetDynamicMetadata(unsafe.Pointer(r.req), unsafe.Pointer(&filterName), unsafe.Pointer(&buf))
	if res == C.CAPIYield {
		atomic.AddInt32(&r.waitingOnEnvoy, 1)
		r.sema.Wait()
	} else {
		r.sema.Done()
		handleCApiStatus(res)
	}
	// copy the memory from c to Go.
	var meta structpb.Struct
	proto.Unmarshal(buf, &meta)
	return meta.AsMap()
}

func (c *httpCApiImpl) HttpSetDynamicMetadata(r unsafe.Pointer, filterName string, key string, value interface{}) {
	v, err := structpb.NewValue(value)
	if err != nil {
		panic(err)
	}
	buf, err := proto.Marshal(v)
	if err != nil {
		panic(err)
	}
	res := C.envoyGoFilterHttpSetDynamicMetadata(r, unsafe.Pointer(&filterName), unsafe.Pointer(&key), unsafe.Pointer(&buf))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpLog(level api.LogType, message string) {
	C.envoyGoFilterLog(C.uint32_t(level), unsafe.Pointer(&message))
}

func (c *httpCApiImpl) HttpLogLevel() api.LogType {
	return api.GetLogLevel()
}

func (c *httpCApiImpl) HttpFinalize(r unsafe.Pointer, reason int) {
	C.envoyGoFilterHttpFinalize(r, C.int(reason))
}

func (c *httpCApiImpl) HttpConfigFinalize(cfg unsafe.Pointer) {
	C.envoyGoConfigHttpFinalize(cfg)
}

var cAPI api.HttpCAPI = &httpCApiImpl{}

// SetHttpCAPI for mock cAPI
func SetHttpCAPI(api api.HttpCAPI) {
	cAPI = api
}

func (c *httpCApiImpl) HttpSetStringFilterState(r unsafe.Pointer, key string, value string, stateType api.StateType, lifeSpan api.LifeSpan, streamSharing api.StreamSharing) {
	res := C.envoyGoFilterHttpSetStringFilterState(r, unsafe.Pointer(&key), unsafe.Pointer(&value), C.int(stateType), C.int(lifeSpan), C.int(streamSharing))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpGetStringFilterState(rr unsafe.Pointer, key string) string {
	r := (*httpRequest)(rr)
	var value string
	r.mutex.Lock()
	defer r.mutex.Unlock()
	r.sema.Add(1)
	res := C.envoyGoFilterHttpGetStringFilterState(unsafe.Pointer(r.req), unsafe.Pointer(&key), unsafe.Pointer(&value))
	if res == C.CAPIYield {
		atomic.AddInt32(&r.waitingOnEnvoy, 1)
		r.sema.Wait()
	} else {
		r.sema.Done()
		handleCApiStatus(res)
	}

	return strings.Clone(value)
}

func (c *httpCApiImpl) HttpGetStringProperty(rr unsafe.Pointer, key string) (string, error) {
	r := (*httpRequest)(rr)
	var value string
	var rc int
	r.mutex.Lock()
	defer r.mutex.Unlock()
	r.sema.Add(1)
	res := C.envoyGoFilterHttpGetStringProperty(unsafe.Pointer(r.req), unsafe.Pointer(&key),
		unsafe.Pointer(&value), (*C.int)(unsafe.Pointer(&rc)))
	if res == C.CAPIYield {
		atomic.AddInt32(&r.waitingOnEnvoy, 1)
		r.sema.Wait()
		res = C.CAPIStatus(rc)
	} else {
		r.sema.Done()
		handleCApiStatus(res)
	}

	if res == C.CAPIOK {
		return strings.Clone(value), nil
	}

	return "", capiStatusToErr(res)
}

func (c *httpCApiImpl) HttpDefineMetric(cfg unsafe.Pointer, metricType api.MetricType, name string) uint32 {
	var value uint32

	res := C.envoyGoFilterHttpDefineMetric(unsafe.Pointer(cfg), C.uint32_t(metricType), unsafe.Pointer(&name), unsafe.Pointer(&value))
	handleCApiStatus(res)
	return value
}

func (c *httpCApiImpl) HttpIncrementMetric(cc unsafe.Pointer, metricId uint32, offset int64) {
	cfg := (*httpConfig)(cc)
	res := C.envoyGoFilterHttpIncrementMetric(unsafe.Pointer(cfg.config), C.uint32_t(metricId), C.int64_t(offset))
	handleCApiStatus(res)
}

func (c *httpCApiImpl) HttpGetMetric(cc unsafe.Pointer, metricId uint32) uint64 {
	cfg := (*httpConfig)(cc)
	var value uint64
	res := C.envoyGoFilterHttpGetMetric(unsafe.Pointer(cfg.config), C.uint32_t(metricId), unsafe.Pointer(&value))
	handleCApiStatus(res)
	return value
}

func (c *httpCApiImpl) HttpRecordMetric(cc unsafe.Pointer, metricId uint32, value uint64) {
	cfg := (*httpConfig)(cc)
	res := C.envoyGoFilterHttpRecordMetric(unsafe.Pointer(cfg.config), C.uint32_t(metricId), C.uint64_t(value))
	handleCApiStatus(res)
}
