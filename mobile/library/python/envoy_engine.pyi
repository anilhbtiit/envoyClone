from typing import Callable
from typing import Dict
from typing import List
from typing import Optional
from typing import overload


class EngineBuilder:
    @overload
    def __init__(self, config_template: str): ...
    @overload
    def __init__(self): ...
    def add_log_level(self, log_level: "LogLevel") -> "EngineBuilder": ...
    def set_on_engine_running(self, on_engine_running: Callable[[], None]) -> "EngineBuilder": ...
    def add_stats_domain(self, stats_domain: str) -> "EngineBuilder": ...
    def add_connect_timeout_seconds(self, connect_timeout_seconds: int) -> "EngineBuilder": ...
    def add_dns_refresh_seconds(self, dns_refresh_seconds: int) -> "EngineBuilder": ...
    def add_dns_failure_refresh_seconds(self, base: int, max: int) -> "EngineBuilder": ...
    def add_dns_query_timeout_seconds(self, dns_query_timeout_seconds: int) -> "EngineBuilder": ...
    def add_dns_preresolve_hostnames(self, dns_preresolve_hostnames: str) -> "EngineBuilder": ...
    def add_stats_flush_seconds(self, stats_flush_seconds: int) -> "EngineBuilder": ...
    def set_app_version(self, app_version: str) -> "EngineBuilder": ...
    def set_app_id(self, app_id: str) -> "EngineBuilder": ...
    def add_virtual_cluster(self, virtual_cluster: str) -> "EngineBuilder": ...
    def build(self) -> "Engine": ...

    # TODO: add after filter integration
    # add_platform_filter
    # add_native_filter
    # add_string_accessor


class Engine:
    def stream_client(self) -> "StreamClient": ...
    def pulse_client(self) -> "PulseClient": ...


class EnvoyError:
    error_code: int
    message: str
    attempt_count: Optional[int]
    cause: Optional[Exception]


class RequestHeaders:
    def __getitem__(self, name: string) -> List[str]: ...
    def all_headers(self) -> Dict[str, List[str]]: ...
    def request_method(self) -> "RequestMethod": ...
    def scheme(self) -> str: ...
    def authority(self) -> str: ...
    def path(self) -> str: ...
    def retry_policy(self) -> "RetryPolicy": ...
    def upstream_http_protocol(self) -> "UpstreamHttpProtocol": ...
    def to_request_headers_builder(self) -> "RequestHeadersBuilder": ...


class RequestHeadersBuilder:
    def __init__(self, request_method: "RequestMethod", scheme: str, authority: str, path: str): ...
    def add(self, name: str, value: str) -> "RequestHeadersBuilder": ...
    def set(self, name: str, values: List[str]) -> "RequestHeadersBuilder": ...
    def remove(self, name: string_view) -> "RequestHeadersBuilder": ...
    def add_retry_policy(self, retry_policy: "RetryPolicy") -> "RequestHeadersBuilder": ...
    def add_upstream_http_protocol(self, upstream_http_protocol: "UpstreamHttpProtocol") -> "RequestHeadersBuilder": ...
    def build(self) -> "RequestHeaders": ...

class RequestTrailers:
    def __getitem__(self, name: string) -> List[str]: ...
    def all_headers(self) -> Dict[str, List[str]]: ...
    def to_request_trailers_builder(self) -> "RequestTrailersBuilder": ...


class RequestTrailersBuilder:
    def __init__(self, request_method: "RequestMethod", scheme: str, authority: str, path: str): ...
    def add(self, name: str, value: str) -> "RequestTrailersBuilder": ...
    def set(self, name: str, values: List[str]) -> "RequestTrailersBuilder": ...
    def remove(self, name: string_view) -> "RequestTrailersBuilder": ...
    def build(self) -> "RequestTrailers": ...


class ResponseHeaders:
    def __getitem__(self, name: string) -> List[str]: ...
    def all_headers(self) -> Dict[str, List[str]]: ...
    def http_status(self) -> int: ...
    def to_response_headers_builder(self) -> "ResponseHeadersBuilder": ...


class ResponseHeadersBuilder:
    def __init__(self): ...
    def add(self, name: str, value: str) -> "ResponseHeadersBuilder": ...
    def set(self, name: str, values: List[str]) -> "ResponseHeadersBuilder": ...
    def remove(self, name: string_view) -> "ResponseHeadersBuilder": ...
    def add_http_status(self, http_status: int) -> "ResponseHeadersBuilder": ...
    def build(self) -> "ResponseHeaders": ...


class ResponseTrailers:
    def __getitem__(self, name: str) -> List[str]: ...
    def all_trailers(self) -> Dict[str, List[str]]: ...
    def to_response_trailers_builder(self) -> "ResponseTrailersBuilder": ...


class ResponseTrailersBuilder:
    def __init__(self): ...
    def add(self, name: str, value: str) -> "ResponseTrailersBuilder": ...
    def set(self, name: str, values: List[str]) -> "ResponseTrailersBuilder": ...
    def remove(self, name: string_view) -> "ResponseTrailersBuilder": ...
    def build(self) -> "ResponseTrailers": ...


class RetryPolicy:
    max_retry_count: int
    retry_on: List["RetryRule"]
    retry_status_codes: List[int]
    per_try_timeout_ms: Optional[int]
    total_upstream_timeout_ms: Optional[int]


class PulseClient:
    pass


class Stream:
    def send_headers(self, request_headers: RequestHeaders, end_stream: bool) -> "Stream": ...
    def send_data(self, data: bytes) -> "Stream": ...
    @overload
    def close(self, request_trailers: RequestTrailers) -> None: ...
    @overload
    def close(self, data: bytes) -> None: ...
    def cancel(self) -> None: ...


class StreamClient:
    def new_stream_prototype(self) -> "StreamPrototype": ...


class StreamPrototype:
    def start(self) -> "Stream": ...
    def set_on_headers(self, on_headers: Callable[["ResponseHeaders", bool], None]) -> "StreamPrototype": ...
    def set_on_data(self, on_data: Callable[[bytes, bool], None]) -> "StreamPrototype": ...
    def set_on_trailers(self, on_trailers: Callable[["ResponseTrailers"], None]) -> "StreamPrototype": ...
    def set_on_error(self, on_trailers: Callable[["EnvoyError"], None]) -> "StreamPrototype": ...
    def set_on_complete(self, on_complete: Callable[[], None]) -> "StreamPrototype": ...
    def set_on_cancel(self, on_cancel: Callable[[], None]) -> "StreamPrototype": ...


class LogLevel:
    Trace: "LogLevel"
    Debug: "LogLevel"
    Info: "LogLevel"
    Warn: "LogLevel"
    Error: "LogLevel"
    Critical: "LogLevel"
    Off: "LogLevel"


class RequestMethod:
    DELETE: "RequestMethod"
    GET: "RequestMethod"
    HEAD: "RequestMethod"
    OPTIONS: "RequestMethod"
    PATCH: "RequestMethod"
    POST: "RequestMethod"
    PUT: "RequestMethod"
    TRACE: "RequestMethod"

class RetryRule:
    Status5xx: "RetryRule"
    GatewayError: "RetryRule"
    ConnectFailure: "RetryRule"
    RefusedStream: "RetryRule"
    Retriable4xx: "RetryRule"
    RetriableHeaders: "RetryRule"
    Reset: "RetryRule"

class UpstreamHttpProtocol:
    HTTP1: "UpstreamHttpProtocol"
    HTTP2: "UpstreamHttpProtocol"
