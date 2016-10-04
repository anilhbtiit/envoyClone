# NOTE: These are all of the third party requirements required to build Envoy. We realize this is
#       not the cleanest cmake way of doing things and we welcome patches from cmake experts to
#       make it better.

# https://github.com/sakra/cotire
# Last tested with 1.7.8
set(ENVOY_COTIRE_MODULE_DIR "" CACHE FILEPATH "location of cotire cmake module")

# https://github.com/gabime/spdlog
# Has no releases. Last tested with 319a62
set(ENVOY_SPDLOG_INCLUDE_DIR "" CACHE FILEPATH "location of spdlog includes")

# https://github.com/nodejs/http-parser
# Last tested with 2.7.0
set(ENVOY_HTTP_PARSER_INCLUDE_DIR "" CACHE FILEPATH "location of http-parser includes")

# https://github.com/nghttp2/nghttp2
# Last tested with 1.14.0
set(ENVOY_NGHTTP2_INCLUDE_DIR "" CACHE FILEPATH "location of nghttp2 includes")

# http://libevent.org/
# Last tested with 2.0.22
set(ENVOY_LIBEVENT_INCLUDE_DIR "" CACHE FILEPATH "location of libevent includes")

# http://tclap.sourceforge.net/
# Last tested with 1.2.1
set(ENVOY_TCLAP_INCLUDE_DIR "" CACHE FILEPATH "location of tclap includes")

# https://github.com/gperftools/gperftools
# Last tested with 2.5.0
set(ENVOY_GPERFTOOLS_INCLUDE_DIR "" CACHE FILEPATH "location of gperftools includes")

# https://github.com/akheron/jansson
# Last tested with 2.7
set(ENVOY_JANSSON_INCLUDE_DIR "" CACHE FILEPATH "location of jansson includes")

# https://www.openssl.org/
# Last tested with 1.0.2h
set(ENVOY_OPENSSL_INCLUDE_DIR "" CACHE FILEPATH "location of openssl includes")

# https://github.com/google/protobuf
# Last tested with 3.0.0
set(ENVOY_PROTOBUF_INCLUDE_DIR "" CACHE FILEPATH "location of protobuf includes")
set(ENVOY_PROTOBUF_PROTOC "" CACHE FILEPATH "location of protoc")

# http://lightstep.com/
# Last tested with lightstep-tracer-cpp-0.16
set(ENVOY_LIGHTSTEP_TRACER_INCLUDE_DIR "" CACHE FILEPATH "location of lighstep tracer includes")

# Extra linker flags required to properly link envoy with all of the above libraries.
set(ENVOY_EXE_EXTRA_LINKER_FLAGS "" CACHE STRING "envoy extra linker flags")

#
# Test Requirements
#

# https://github.com/google/googletest
# Last tested with 1.8.0
set(ENVOY_GTEST_INCLUDE_DIR "" CACHE FILEPATH "location of gtest includes")
set(ENVOY_GMOCK_INCLUDE_DIR "" CACHE FILEPATH "location of gmock includes")

# http://gcovr.com/
# Last tested with 3.3
set(ENVOY_GCOVR "" CACHE FILEPATH "location of gcovr")
set(ENVOY_GCOVR_EXTRA_ARGS "" CACHE STRING "extra arguments to pass to gcovr")

# Extra linker flags required to properly link envoy-test with all of the above libraries.
set(ENVOY_TEST_EXTRA_LINKER_FLAGS "" CACHE STRING "envoy-test extra linker flags")
