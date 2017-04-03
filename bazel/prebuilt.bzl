# Everything in this file is generated by bazel/gen_prebuilt.sh, do not hand
# edit. This script should be rerun on the change to any dependency.

ARES_HDRS = [
    "thirdparty_build/cares.dep/include/ares.h",
    "thirdparty_build/cares.dep/include/ares_build.h",
    "thirdparty_build/cares.dep/include/ares_dns.h",
    "thirdparty_build/cares.dep/include/ares_rules.h",
    "thirdparty_build/cares.dep/include/ares_version.h",
]

ARES_LIBS = [
    "thirdparty_build/cares.dep/lib/libcares.a",
]

CRYPTO_HDRS = [
    "thirdparty_build/boringssl.dep/include/openssl/aead.h",
    "thirdparty_build/boringssl.dep/include/openssl/aes.h",
    "thirdparty_build/boringssl.dep/include/openssl/arm_arch.h",
    "thirdparty_build/boringssl.dep/include/openssl/asn1.h",
    "thirdparty_build/boringssl.dep/include/openssl/asn1_mac.h",
    "thirdparty_build/boringssl.dep/include/openssl/asn1t.h",
    "thirdparty_build/boringssl.dep/include/openssl/base.h",
    "thirdparty_build/boringssl.dep/include/openssl/base64.h",
    "thirdparty_build/boringssl.dep/include/openssl/bio.h",
    "thirdparty_build/boringssl.dep/include/openssl/blowfish.h",
    "thirdparty_build/boringssl.dep/include/openssl/bn.h",
    "thirdparty_build/boringssl.dep/include/openssl/buf.h",
    "thirdparty_build/boringssl.dep/include/openssl/buffer.h",
    "thirdparty_build/boringssl.dep/include/openssl/bytestring.h",
    "thirdparty_build/boringssl.dep/include/openssl/cast.h",
    "thirdparty_build/boringssl.dep/include/openssl/chacha.h",
    "thirdparty_build/boringssl.dep/include/openssl/cipher.h",
    "thirdparty_build/boringssl.dep/include/openssl/cmac.h",
    "thirdparty_build/boringssl.dep/include/openssl/conf.h",
    "thirdparty_build/boringssl.dep/include/openssl/cpu.h",
    "thirdparty_build/boringssl.dep/include/openssl/crypto.h",
    "thirdparty_build/boringssl.dep/include/openssl/curve25519.h",
    "thirdparty_build/boringssl.dep/include/openssl/des.h",
    "thirdparty_build/boringssl.dep/include/openssl/dh.h",
    "thirdparty_build/boringssl.dep/include/openssl/digest.h",
    "thirdparty_build/boringssl.dep/include/openssl/dsa.h",
    "thirdparty_build/boringssl.dep/include/openssl/dtls1.h",
    "thirdparty_build/boringssl.dep/include/openssl/ec.h",
    "thirdparty_build/boringssl.dep/include/openssl/ec_key.h",
    "thirdparty_build/boringssl.dep/include/openssl/ecdh.h",
    "thirdparty_build/boringssl.dep/include/openssl/ecdsa.h",
    "thirdparty_build/boringssl.dep/include/openssl/engine.h",
    "thirdparty_build/boringssl.dep/include/openssl/err.h",
    "thirdparty_build/boringssl.dep/include/openssl/evp.h",
    "thirdparty_build/boringssl.dep/include/openssl/ex_data.h",
    "thirdparty_build/boringssl.dep/include/openssl/hkdf.h",
    "thirdparty_build/boringssl.dep/include/openssl/hmac.h",
    "thirdparty_build/boringssl.dep/include/openssl/lhash.h",
    "thirdparty_build/boringssl.dep/include/openssl/lhash_macros.h",
    "thirdparty_build/boringssl.dep/include/openssl/md4.h",
    "thirdparty_build/boringssl.dep/include/openssl/md5.h",
    "thirdparty_build/boringssl.dep/include/openssl/mem.h",
    "thirdparty_build/boringssl.dep/include/openssl/newhope.h",
    "thirdparty_build/boringssl.dep/include/openssl/nid.h",
    "thirdparty_build/boringssl.dep/include/openssl/obj.h",
    "thirdparty_build/boringssl.dep/include/openssl/obj_mac.h",
    "thirdparty_build/boringssl.dep/include/openssl/objects.h",
    "thirdparty_build/boringssl.dep/include/openssl/opensslconf.h",
    "thirdparty_build/boringssl.dep/include/openssl/opensslv.h",
    "thirdparty_build/boringssl.dep/include/openssl/ossl_typ.h",
    "thirdparty_build/boringssl.dep/include/openssl/pem.h",
    "thirdparty_build/boringssl.dep/include/openssl/pkcs12.h",
    "thirdparty_build/boringssl.dep/include/openssl/pkcs7.h",
    "thirdparty_build/boringssl.dep/include/openssl/pkcs8.h",
    "thirdparty_build/boringssl.dep/include/openssl/poly1305.h",
    "thirdparty_build/boringssl.dep/include/openssl/pool.h",
    "thirdparty_build/boringssl.dep/include/openssl/rand.h",
    "thirdparty_build/boringssl.dep/include/openssl/rc4.h",
    "thirdparty_build/boringssl.dep/include/openssl/ripemd.h",
    "thirdparty_build/boringssl.dep/include/openssl/rsa.h",
    "thirdparty_build/boringssl.dep/include/openssl/safestack.h",
    "thirdparty_build/boringssl.dep/include/openssl/sha.h",
    "thirdparty_build/boringssl.dep/include/openssl/srtp.h",
    "thirdparty_build/boringssl.dep/include/openssl/ssl.h",
    "thirdparty_build/boringssl.dep/include/openssl/ssl3.h",
    "thirdparty_build/boringssl.dep/include/openssl/stack.h",
    "thirdparty_build/boringssl.dep/include/openssl/stack_macros.h",
    "thirdparty_build/boringssl.dep/include/openssl/thread.h",
    "thirdparty_build/boringssl.dep/include/openssl/time_support.h",
    "thirdparty_build/boringssl.dep/include/openssl/tls1.h",
    "thirdparty_build/boringssl.dep/include/openssl/type_check.h",
    "thirdparty_build/boringssl.dep/include/openssl/x509.h",
    "thirdparty_build/boringssl.dep/include/openssl/x509_vfy.h",
    "thirdparty_build/boringssl.dep/include/openssl/x509v3.h",
]

CRYPTO_LIBS = [
    "thirdparty_build/boringssl.dep/lib/libcrypto.a",
]

EVENT_HDRS = [
    "thirdparty_build/libevent.dep/include/evdns.h",
    "thirdparty_build/libevent.dep/include/event.h",
    "thirdparty_build/libevent.dep/include/event2/buffer.h",
    "thirdparty_build/libevent.dep/include/event2/buffer_compat.h",
    "thirdparty_build/libevent.dep/include/event2/bufferevent.h",
    "thirdparty_build/libevent.dep/include/event2/bufferevent_compat.h",
    "thirdparty_build/libevent.dep/include/event2/bufferevent_ssl.h",
    "thirdparty_build/libevent.dep/include/event2/bufferevent_struct.h",
    "thirdparty_build/libevent.dep/include/event2/dns.h",
    "thirdparty_build/libevent.dep/include/event2/dns_compat.h",
    "thirdparty_build/libevent.dep/include/event2/dns_struct.h",
    "thirdparty_build/libevent.dep/include/event2/event-config.h",
    "thirdparty_build/libevent.dep/include/event2/event.h",
    "thirdparty_build/libevent.dep/include/event2/event_compat.h",
    "thirdparty_build/libevent.dep/include/event2/event_struct.h",
    "thirdparty_build/libevent.dep/include/event2/http.h",
    "thirdparty_build/libevent.dep/include/event2/http_compat.h",
    "thirdparty_build/libevent.dep/include/event2/http_struct.h",
    "thirdparty_build/libevent.dep/include/event2/keyvalq_struct.h",
    "thirdparty_build/libevent.dep/include/event2/listener.h",
    "thirdparty_build/libevent.dep/include/event2/rpc.h",
    "thirdparty_build/libevent.dep/include/event2/rpc_compat.h",
    "thirdparty_build/libevent.dep/include/event2/rpc_struct.h",
    "thirdparty_build/libevent.dep/include/event2/tag.h",
    "thirdparty_build/libevent.dep/include/event2/tag_compat.h",
    "thirdparty_build/libevent.dep/include/event2/thread.h",
    "thirdparty_build/libevent.dep/include/event2/util.h",
    "thirdparty_build/libevent.dep/include/event2/visibility.h",
    "thirdparty_build/libevent.dep/include/evhttp.h",
    "thirdparty_build/libevent.dep/include/evrpc.h",
    "thirdparty_build/libevent.dep/include/evutil.h",
]

EVENT_LIBS = [
    "thirdparty_build/libevent.dep/lib/libevent.a",
]

EVENT_PTHREADS_LIBS = [
    "thirdparty_build/libevent.dep/lib/libevent_pthreads.a",
]

GOOGLETEST_HDRS = [
    "thirdparty_build/googletest.dep/include/gmock/gmock-actions.h",
    "thirdparty_build/googletest.dep/include/gmock/gmock-cardinalities.h",
    "thirdparty_build/googletest.dep/include/gmock/gmock-generated-actions.h",
    "thirdparty_build/googletest.dep/include/gmock/gmock-generated-actions.h.pump",
    "thirdparty_build/googletest.dep/include/gmock/gmock-generated-function-mockers.h",
    "thirdparty_build/googletest.dep/include/gmock/gmock-generated-function-mockers.h.pump",
    "thirdparty_build/googletest.dep/include/gmock/gmock-generated-matchers.h",
    "thirdparty_build/googletest.dep/include/gmock/gmock-generated-matchers.h.pump",
    "thirdparty_build/googletest.dep/include/gmock/gmock-generated-nice-strict.h",
    "thirdparty_build/googletest.dep/include/gmock/gmock-generated-nice-strict.h.pump",
    "thirdparty_build/googletest.dep/include/gmock/gmock-matchers.h",
    "thirdparty_build/googletest.dep/include/gmock/gmock-more-actions.h",
    "thirdparty_build/googletest.dep/include/gmock/gmock-more-matchers.h",
    "thirdparty_build/googletest.dep/include/gmock/gmock-spec-builders.h",
    "thirdparty_build/googletest.dep/include/gmock/gmock.h",
    "thirdparty_build/googletest.dep/include/gmock/internal/custom/gmock-generated-actions.h",
    "thirdparty_build/googletest.dep/include/gmock/internal/custom/gmock-generated-actions.h.pump",
    "thirdparty_build/googletest.dep/include/gmock/internal/custom/gmock-matchers.h",
    "thirdparty_build/googletest.dep/include/gmock/internal/custom/gmock-port.h",
    "thirdparty_build/googletest.dep/include/gmock/internal/gmock-generated-internal-utils.h",
    "thirdparty_build/googletest.dep/include/gmock/internal/gmock-generated-internal-utils.h.pump",
    "thirdparty_build/googletest.dep/include/gmock/internal/gmock-internal-utils.h",
    "thirdparty_build/googletest.dep/include/gmock/internal/gmock-port.h",
    "thirdparty_build/googletest.dep/include/gtest/gtest-death-test.h",
    "thirdparty_build/googletest.dep/include/gtest/gtest-message.h",
    "thirdparty_build/googletest.dep/include/gtest/gtest-param-test.h",
    "thirdparty_build/googletest.dep/include/gtest/gtest-param-test.h.pump",
    "thirdparty_build/googletest.dep/include/gtest/gtest-printers.h",
    "thirdparty_build/googletest.dep/include/gtest/gtest-spi.h",
    "thirdparty_build/googletest.dep/include/gtest/gtest-test-part.h",
    "thirdparty_build/googletest.dep/include/gtest/gtest-typed-test.h",
    "thirdparty_build/googletest.dep/include/gtest/gtest.h",
    "thirdparty_build/googletest.dep/include/gtest/gtest_pred_impl.h",
    "thirdparty_build/googletest.dep/include/gtest/gtest_prod.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/custom/gtest-port.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/custom/gtest-printers.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/custom/gtest.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-death-test-internal.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-filepath.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-internal.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-linked_ptr.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-param-util-generated.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-param-util-generated.h.pump",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-param-util.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-port-arch.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-port.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-string.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-tuple.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-tuple.h.pump",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-type-util.h",
    "thirdparty_build/googletest.dep/include/gtest/internal/gtest-type-util.h.pump",
]

GOOGLETEST_LIBS = [
    "thirdparty_build/googletest.dep/lib/libgmock.a",
    "thirdparty_build/googletest.dep/lib/libgtest.a",
]

HTTP_PARSER_HDRS = [
    "thirdparty_build/http-parser.dep/include/http_parser.h",
]

HTTP_PARSER_LIBS = [
    "thirdparty_build/http-parser.dep/lib/libhttp_parser.a",
]

LIGHTSTEP_LIBS = [
    "thirdparty_build/lightstep.dep/lib/liblightstep_core_cxx11.a",
]

LIGHTSTEP_INCLUDES_HDRS = [
    "thirdparty_build/lightstep.dep/include/collector.pb.h",
    "thirdparty_build/lightstep.dep/include/lightstep/carrier.h",
    "thirdparty_build/lightstep.dep/include/lightstep/impl.h",
    "thirdparty_build/lightstep.dep/include/lightstep/options.h",
    "thirdparty_build/lightstep.dep/include/lightstep/propagation.h",
    "thirdparty_build/lightstep.dep/include/lightstep/recorder.h",
    "thirdparty_build/lightstep.dep/include/lightstep/span.h",
    "thirdparty_build/lightstep.dep/include/lightstep/tracer.h",
    "thirdparty_build/lightstep.dep/include/lightstep/util.h",
    "thirdparty_build/lightstep.dep/include/lightstep/value.h",
    "thirdparty_build/lightstep.dep/include/lightstep_carrier.pb.h",
    "thirdparty_build/lightstep.dep/include/mapbox_variant/recursive_wrapper.hpp",
    "thirdparty_build/lightstep.dep/include/mapbox_variant/variant.hpp",
]

NGHTTP2_HDRS = [
    "thirdparty_build/nghttp2.dep/include/nghttp2/nghttp2.h",
    "thirdparty_build/nghttp2.dep/include/nghttp2/nghttp2ver.h",
]

NGHTTP2_LIBS = [
    "thirdparty_build/nghttp2.dep/lib/libnghttp2.a",
]

PROTOBUF_HDRS = [
    "thirdparty_build/protobuf.dep/include/google/protobuf/any.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/any.pb.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/any.proto",
    "thirdparty_build/protobuf.dep/include/google/protobuf/api.pb.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/api.proto",
    "thirdparty_build/protobuf.dep/include/google/protobuf/arena.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/arenastring.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/code_generator.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/command_line_interface.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/cpp/cpp_generator.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/csharp/csharp_generator.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/csharp/csharp_names.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/importer.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/java/java_generator.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/java/java_names.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/javanano/javanano_generator.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/js/js_generator.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/js/well_known_types_embed.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/objectivec/objectivec_generator.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/objectivec/objectivec_helpers.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/parser.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/php/php_generator.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/plugin.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/plugin.pb.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/plugin.proto",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/python/python_generator.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/compiler/ruby/ruby_generator.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/descriptor.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/descriptor.pb.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/descriptor.proto",
    "thirdparty_build/protobuf.dep/include/google/protobuf/descriptor_database.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/duration.pb.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/duration.proto",
    "thirdparty_build/protobuf.dep/include/google/protobuf/dynamic_message.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/empty.pb.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/empty.proto",
    "thirdparty_build/protobuf.dep/include/google/protobuf/extension_set.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/field_mask.pb.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/field_mask.proto",
    "thirdparty_build/protobuf.dep/include/google/protobuf/generated_enum_reflection.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/generated_enum_util.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/generated_message_reflection.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/generated_message_util.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/has_bits.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/io/coded_stream.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/io/printer.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/io/strtod.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/io/tokenizer.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/io/zero_copy_stream.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/io/zero_copy_stream_impl.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/io/zero_copy_stream_impl_lite.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/map.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/map_entry.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/map_entry_lite.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/map_field.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/map_field_inl.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/map_field_lite.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/map_type_handler.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/message.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/message_lite.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/metadata.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/reflection.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/reflection_ops.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/repeated_field.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/service.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/source_context.pb.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/source_context.proto",
    "thirdparty_build/protobuf.dep/include/google/protobuf/struct.pb.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/struct.proto",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomic_sequence_num.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_arm64_gcc.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_arm_gcc.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_arm_qnx.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_atomicword_compat.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_generic_c11_atomic.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_generic_gcc.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_macosx.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_mips_gcc.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_power.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_ppc_gcc.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_solaris.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_tsan.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_x86_gcc.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/atomicops_internals_x86_msvc.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/bytestream.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/callback.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/casts.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/common.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/fastmem.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/hash.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/logging.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/macros.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/mutex.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/once.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/platform_macros.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/port.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/scoped_ptr.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/shared_ptr.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/singleton.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/status.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/stl_util.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/stringpiece.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/template_util.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/stubs/type_traits.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/text_format.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/timestamp.pb.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/timestamp.proto",
    "thirdparty_build/protobuf.dep/include/google/protobuf/type.pb.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/type.proto",
    "thirdparty_build/protobuf.dep/include/google/protobuf/unknown_field_set.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/util/field_comparator.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/util/field_mask_util.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/util/json_util.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/util/message_differencer.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/util/time_util.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/util/type_resolver.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/util/type_resolver_util.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/wire_format.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/wire_format_lite.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/wire_format_lite_inl.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/wrappers.pb.h",
    "thirdparty_build/protobuf.dep/include/google/protobuf/wrappers.proto",
]

PROTOBUF_LIBS = [
    "thirdparty_build/protobuf.dep/lib/libprotobuf.a",
    "thirdparty_build/protobuf.dep/lib/libprotobuf-lite.a",
    "thirdparty_build/protobuf.dep/lib/libprotoc.a",
]

RAPIDJSON_HDRS = [
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/allocators.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/document.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/encodedstream.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/encodings.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/error/en.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/error/error.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/filereadstream.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/filewritestream.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/fwd.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/internal/biginteger.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/internal/diyfp.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/internal/dtoa.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/internal/ieee754.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/internal/itoa.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/internal/meta.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/internal/pow10.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/internal/regex.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/internal/stack.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/internal/strfunc.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/internal/strtod.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/internal/swap.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/istreamwrapper.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/memorybuffer.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/memorystream.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/msinttypes/inttypes.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/msinttypes/stdint.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/ostreamwrapper.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/pointer.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/prettywriter.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/rapidjson.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/reader.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/schema.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/stream.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/stringbuffer.h",
    "thirdparty/rapidjson.dep/rapidjson-1.1.0/include/rapidjson/writer.h",
]

SPDLOG_HDRS = [
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/async_logger.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/common.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/details/async_log_helper.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/details/async_logger_impl.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/details/file_helper.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/details/log_msg.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/details/logger_impl.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/details/mpmc_bounded_q.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/details/null_mutex.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/details/os.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/details/pattern_formatter_impl.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/details/registry.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/details/spdlog_impl.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/fmt/bundled/format.cc",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/fmt/bundled/format.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/fmt/bundled/ostream.cc",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/fmt/bundled/ostream.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/fmt/bundled/printf.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/fmt/fmt.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/fmt/ostr.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/formatter.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/logger.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/sinks/android_sink.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/sinks/ansicolor_sink.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/sinks/base_sink.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/sinks/dist_sink.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/sinks/file_sinks.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/sinks/msvc_sink.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/sinks/null_sink.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/sinks/ostream_sink.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/sinks/sink.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/sinks/stdout_sinks.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/sinks/syslog_sink.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/spdlog.h",
    "thirdparty/spdlog.dep/spdlog-0.11.0/include/spdlog/tweakme.h",
]

SSL_LIBS = [
    "thirdparty_build/boringssl.dep/lib/libssl.a",
]

TCLAP_HDRS = [
    "thirdparty/tclap.dep/tclap-1.2.1/include/Makefile.am",
    "thirdparty/tclap.dep/tclap-1.2.1/include/Makefile.in",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/Arg.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/ArgException.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/ArgTraits.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/CmdLine.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/CmdLineInterface.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/CmdLineOutput.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/Constraint.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/DocBookOutput.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/HelpVisitor.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/IgnoreRestVisitor.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/Makefile.am",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/Makefile.in",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/MultiArg.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/MultiSwitchArg.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/OptionalUnlabeledTracker.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/StandardTraits.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/StdOutput.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/SwitchArg.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/UnlabeledMultiArg.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/UnlabeledValueArg.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/ValueArg.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/ValuesConstraint.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/VersionVisitor.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/Visitor.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/XorHandler.h",
    "thirdparty/tclap.dep/tclap-1.2.1/include/tclap/ZshCompletionOutput.h",
]

PROTOC_LIBS = [
    "thirdparty_build/protobuf.dep/bin/protoc",
]

GENERATED_FILES = (ARES_HDRS +
                   ARES_LIBS +
                   CRYPTO_HDRS +
                   CRYPTO_LIBS +
                   EVENT_HDRS +
                   EVENT_LIBS +
                   EVENT_PTHREADS_LIBS +
                   GOOGLETEST_HDRS +
                   GOOGLETEST_LIBS +
                   HTTP_PARSER_HDRS +
                   HTTP_PARSER_LIBS +
                   LIGHTSTEP_LIBS +
                   LIGHTSTEP_INCLUDES_HDRS +
                   NGHTTP2_HDRS +
                   NGHTTP2_LIBS +
                   PROTOBUF_HDRS +
                   PROTOBUF_LIBS +
                   RAPIDJSON_HDRS +
                   SPDLOG_HDRS +
                   SSL_LIBS +
                   TCLAP_HDRS +
                   PROTOC_LIBS)

def envoy_prebuilt_targets(prebuilts_genrule=False):
    if prebuilts_genrule:
        native.genrule(
            name = "prebuilts",
            srcs = [
                "//ci/build_container:build_and_install_deps.sh",
                "//ci/build_container:prebuilt_build_inputs",
            ],
            outs = GENERATED_FILES,
            cmd = "export GENDIR=$$(realpath $(@D)); BUILD_DISTINCT=1 TMPDIR=$$GENDIR " +
                  "THIRDPARTY_DEPS=$$GENDIR THIRDPARTY_SRC=$$GENDIR/thirdparty " +
                  "THIRDPARTY_BUILD=$$GENDIR/thirdparty_build $(location " +
                  "//ci/build_container:build_and_install_deps.sh)",
            message = "Building Envoy dependencies",
        )

    native.cc_library(
        name = "ares",
        srcs = ARES_LIBS,
        hdrs = ARES_HDRS,
        strip_include_prefix = "thirdparty_build/cares.dep/include",
    )

    native.cc_library(
        name = "crypto",
        srcs = CRYPTO_LIBS,
        hdrs = CRYPTO_HDRS,
        strip_include_prefix = "thirdparty_build/boringssl.dep/include",
    )

    native.cc_library(
        name = "event",
        srcs = EVENT_LIBS,
        hdrs = EVENT_HDRS,
        strip_include_prefix = "thirdparty_build/libevent.dep/include",
    )

    native.cc_library(
        name = "event_pthreads",
        srcs = EVENT_PTHREADS_LIBS,
        deps = [
            "event",
        ],
    )

    native.cc_library(
        name = "googletest",
        srcs = GOOGLETEST_LIBS,
        hdrs = GOOGLETEST_HDRS,
        strip_include_prefix = "thirdparty_build/googletest.dep/include",
    )

    native.cc_library(
        name = "http_parser",
        srcs = HTTP_PARSER_LIBS,
        hdrs = HTTP_PARSER_HDRS,
        strip_include_prefix = "thirdparty_build/http-parser.dep/include",
    )

    native.cc_library(
        name = "lightstep",
        srcs = LIGHTSTEP_LIBS,
        deps = [
            "lightstep_includes",
            "protobuf",
        ],
    )

    native.cc_library(
        name = "lightstep_includes",
        hdrs = LIGHTSTEP_INCLUDES_HDRS,
        strip_include_prefix = "thirdparty_build/lightstep.dep/include",
    )

    native.cc_library(
        name = "nghttp2",
        srcs = NGHTTP2_LIBS,
        hdrs = NGHTTP2_HDRS,
        strip_include_prefix = "thirdparty_build/nghttp2.dep/include",
    )

    native.cc_library(
        name = "protobuf",
        srcs = PROTOBUF_LIBS,
        hdrs = PROTOBUF_HDRS,
        strip_include_prefix = "thirdparty_build/protobuf.dep/include",
    )

    native.cc_library(
        name = "rapidjson",
        hdrs = RAPIDJSON_HDRS,
        strip_include_prefix = "thirdparty/rapidjson.dep/rapidjson-1.1.0/include",
    )

    native.cc_library(
        name = "spdlog",
        hdrs = SPDLOG_HDRS,
        strip_include_prefix = "thirdparty/spdlog.dep/spdlog-0.11.0/include",
    )

    native.cc_library(
        name = "ssl",
        srcs = SSL_LIBS,
        deps = [
            "crypto",
        ],
    )

    native.cc_library(
        name = "tclap",
        hdrs = TCLAP_HDRS,
        strip_include_prefix = "thirdparty/tclap.dep/tclap-1.2.1/include",
    )

    native.filegroup(
        name = "protoc",
        srcs = PROTOC_LIBS,
    )

def envoy_prebuilt_workspace_targets(path):
    native.bind(
        name = "ares",
        actual = path + ":ares",
    )

    native.bind(
        name = "crypto",
        actual = path + ":crypto",
    )

    native.bind(
        name = "event",
        actual = path + ":event",
    )

    native.bind(
        name = "event_pthreads",
        actual = path + ":event_pthreads",
    )

    native.bind(
        name = "googletest",
        actual = path + ":googletest",
    )

    native.bind(
        name = "http_parser",
        actual = path + ":http_parser",
    )

    native.bind(
        name = "lightstep",
        actual = path + ":lightstep",
    )

    native.bind(
        name = "lightstep_includes",
        actual = path + ":lightstep_includes",
    )

    native.bind(
        name = "nghttp2",
        actual = path + ":nghttp2",
    )

    native.bind(
        name = "protobuf",
        actual = path + ":protobuf",
    )

    native.bind(
        name = "rapidjson",
        actual = path + ":rapidjson",
    )

    native.bind(
        name = "spdlog",
        actual = path + ":spdlog",
    )

    native.bind(
        name = "ssl",
        actual = path + ":ssl",
    )

    native.bind(
        name = "tclap",
        actual = path + ":tclap",
    )

    native.bind(
        name = "protoc",
        actual = path + ":protoc",
    )

    # Used only for protobuf.bzl
    native.git_repository(
        name = "protobuf_bzl",
        # Using a non-canonical repository/branch here. This is a workaround to the lack of
        # merge on https://github.com/google/protobuf/pull/2508, which is needed for supporting
        # arbitrary CC compiler locations from the environment. The branch is
        # https://github.com/htuch/protobuf/tree/v3.2.0-default-shell-env, which is the 3.2.0
        # release with the above mentioned PR cherry picked.
        commit = "d490587268931da78c942a6372ef57bb53db80da",
        remote = "https://github.com/htuch/protobuf.git",
    )
