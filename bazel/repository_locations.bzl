REPOSITORY_LOCATIONS = dict(
    bazel_gazelle = dict(
        sha256 = "be9296bfd64882e3c08e3283c58fcb461fa6dd3c171764fcc4cf322f60615a9b",
        urls = ["https://github.com/bazelbuild/bazel-gazelle/releases/download/0.18.1/bazel-gazelle-0.18.1.tar.gz"],
    ),
    bazel_toolchains = dict(
        sha256 = "b72e7a911436b2900b05759a1fcd735070edbd4442f0a3506ef021fdcd6e15b3",
        strip_prefix = "bazel-toolchains-0.28.5",
        urls = ["https://github.com/bazelbuild/bazel-toolchains/archive/0.28.5.tar.gz"],
    ),
    boringssl = dict(
        sha256 = "c712766ddc844de2a38e686e1cdd7288795e9a6fe7f699c6636f1b76703db84e",
        strip_prefix = "boringssl-265728decec4370cd02b941f72fba9f0735e2923",
        # To update BoringSSL, which tracks Chromium releases:
        # 1. Open https://omahaproxy.appspot.com/ and note <current_version> of linux/beta release.
        # 2. Open https://chromium.googlesource.com/chromium/src/+/refs/tags/<current_version>/DEPS and note <boringssl_revision>.
        # 3. Find a commit in BoringSSL's "master-with-bazel" branch that merges <boringssl_revision>.
        #
        # chromium-77.0.3865.35 (BETA)
        urls = ["https://github.com/google/boringssl/archive/265728decec4370cd02b941f72fba9f0735e2923.tar.gz"],
    ),
    boringssl_fips = dict(
        sha256 = "b12ad676ee533824f698741bd127f6fbc82c46344398a6d78d25e62c6c418c73",
        # fips-20180730
        urls = ["https://commondatastorage.googleapis.com/chromium-boringssl-docs/fips/boringssl-66005f41fbc3529ffe8d007708756720529da20d.tar.xz"],
    ),
    com_google_absl = dict(
        sha256 = "3df5970908ed9a09ba51388d04661803a6af18c373866f442cede7f381e0b94a",
        strip_prefix = "abseil-cpp-14550beb3b7b97195e483fb74b5efb906395c31e",
        # 2019-07-31
        urls = ["https://github.com/abseil/abseil-cpp/archive/14550beb3b7b97195e483fb74b5efb906395c31e.tar.gz"],
    ),
    com_github_apache_thrift = dict(
        sha256 = "7d59ac4fdcb2c58037ebd4a9da5f9a49e3e034bf75b3f26d9fe48ba3d8806e6b",
        strip_prefix = "thrift-0.11.0",
        urls = ["https://files.pythonhosted.org/packages/c6/b4/510617906f8e0c5660e7d96fbc5585113f83ad547a3989b80297ac72a74c/thrift-0.11.0.tar.gz"],
    ),
    com_github_c_ares_c_ares = dict(
        sha256 = "bbaab13d6ad399a278d476f533e4d88a7ec7d729507348bb9c2e3b207ba4c606",
        strip_prefix = "c-ares-d7e070e7283f822b1d2787903cce3615536c5610",
        # 2019-06-19
        # 27 new commits from release-1.15.0. Upgrade for commit 7d3591ee8a1a63e7748e68e6d880bd1763a32885 "getaddrinfo enhancements" and follow up fixes.
        # Use getaddrinfo to query DNS record and TTL.
        # TODO(crazyxy): Update to release-1.16.0 when it is released.
        urls = ["https://github.com/c-ares/c-ares/archive/d7e070e7283f822b1d2787903cce3615536c5610.tar.gz"],
    ),
    com_github_circonus_labs_libcircllhist = dict(
        sha256 = "8165aa25e529d7d4b9ae849d3bf30371255a99d6db0421516abcff23214cdc2c",
        strip_prefix = "libcircllhist-63a16dd6f2fc7bc841bb17ff92be8318df60e2e1",
        # 2019-02-11
        urls = ["https://github.com/circonus-labs/libcircllhist/archive/63a16dd6f2fc7bc841bb17ff92be8318df60e2e1.tar.gz"],
    ),
    com_github_cyan4973_xxhash = dict(
        sha256 = "b34792646d5e19964bb7bba24f06cb13aecaac623ab91a54da08aa19d3686d7e",
        strip_prefix = "xxHash-0.7.0",
        urls = ["https://github.com/Cyan4973/xxHash/archive/v0.7.0.tar.gz"],
    ),
    com_github_envoyproxy_sqlparser = dict(
        sha256 = "425dfee0c4fe9aff8acf2365cde3dd2ba7fb878d2ba37562d33920e34c40c05e",
        strip_prefix = "sql-parser-5f50c68bdf5f107692bb027d1c568f67597f4d7f",
        urls = ["https://github.com/envoyproxy/sql-parser/archive/5f50c68bdf5f107692bb027d1c568f67597f4d7f.tar.gz"],
    ),
    com_github_eile_tclap = dict(
        sha256 = "f0ede0721dddbb5eba3a47385a6e8681b14f155e1129dd39d1a959411935098f",
        strip_prefix = "tclap-tclap-1-2-1-release-final",
        urls = ["https://github.com/eile/tclap/archive/tclap-1-2-1-release-final.tar.gz"],
    ),
    com_github_fmtlib_fmt = dict(
        sha256 = "4c0741e10183f75d7d6f730b8708a99b329b2f942dad5a9da3385ab92bb4a15c",
        strip_prefix = "fmt-5.3.0",
        urls = ["https://github.com/fmtlib/fmt/releases/download/5.3.0/fmt-5.3.0.zip"],
    ),
    com_github_gabime_spdlog = dict(
        sha256 = "160845266e94db1d4922ef755637f6901266731c4cb3b30b45bf41efa0e6ab70",
        strip_prefix = "spdlog-1.3.1",
        urls = ["https://github.com/gabime/spdlog/archive/v1.3.1.tar.gz"],
    ),
    com_github_google_libprotobuf_mutator = dict(
        sha256 = "97b3639630040f41c45f45838ab00b78909e6b4cb69c8028e01302bea5b79495",
        strip_prefix = "libprotobuf-mutator-c3d2faf04a1070b0b852b0efdef81e1a81ba925e",
        # 2018-03-06
        urls = ["https://github.com/google/libprotobuf-mutator/archive/c3d2faf04a1070b0b852b0efdef81e1a81ba925e.tar.gz"],
    ),
    com_github_gperftools_gperftools = dict(
        # TODO(cmluciano): Bump to release 2.8
        # This sha is specifically chosen to fix ppc64le builds that require inclusion
        # of asm/ptrace.h
        sha256 = "18574813a062eee487bc1b761e8024a346075a7cb93da19607af362dc09565ef",
        strip_prefix = "gperftools-fc00474ddc21fff618fc3f009b46590e241e425e",
        urls = ["https://github.com/gperftools/gperftools/archive/fc00474ddc21fff618fc3f009b46590e241e425e.tar.gz"],
    ),
    com_github_grpc_grpc = dict(
        sha256 = "bcb01ac7029a7fb5219ad2cbbc4f0a2df3ef32db42e236ce7814597f4b04b541",
        strip_prefix = "grpc-79a8b5289e3122d2cea2da3be7151d37313d6f46",
        # Commit from 2019-05-30
        urls = ["https://github.com/grpc/grpc/archive/79a8b5289e3122d2cea2da3be7151d37313d6f46.tar.gz"],
    ),
    com_github_luajit_luajit = dict(
        sha256 = "409f7fe570d3c16558e594421c47bdd130238323c9d6fd6c83dedd2aaeb082a8",
        strip_prefix = "LuaJIT-2.1.0-beta3",
        urls = ["https://github.com/LuaJIT/LuaJIT/archive/v2.1.0-beta3.tar.gz"],
    ),
    com_github_nanopb_nanopb = dict(
        sha256 = "5fb4dab0b7f6a239908407fe07c9d03877cd0502abb637e38c41091cb9c1d438",
        strip_prefix = "nanopb-0.3.9.3",
        urls = ["https://github.com/nanopb/nanopb/archive/0.3.9.3.tar.gz"],
    ),
    com_github_nghttp2_nghttp2 = dict(
        sha256 = "25b623cd04dc6a863ca3b34ed6247844effe1aa5458229590b3f56a6d53cd692",
        strip_prefix = "nghttp2-1.39.1",
        urls = ["https://github.com/nghttp2/nghttp2/releases/download/v1.39.1/nghttp2-1.39.1.tar.gz"],
    ),
    io_opentracing_cpp = dict(
        sha256 = "015c4187f7a6426a2b5196f0ccd982aa87f010cf61f507ae3ce5c90523f92301",
        strip_prefix = "opentracing-cpp-1.5.1",
        urls = ["https://github.com/opentracing/opentracing-cpp/archive/v1.5.1.tar.gz"],
    ),
    com_lightstep_tracer_cpp = dict(
        sha256 = "defbf471facfebde6523ca1177529b63784893662d4ef2c60db074be8aef0634",
        strip_prefix = "lightstep-tracer-cpp-0.8.0",
        urls = ["https://github.com/lightstep/lightstep-tracer-cpp/archive/v0.8.0.tar.gz"],
    ),
    com_github_datadog_dd_opentracing_cpp = dict(
        sha256 = "f7fb2ad541f812c36fd78f9a38e4582d87dadb563ab80bee3f7c3a2132a425c5",
        strip_prefix = "dd-opentracing-cpp-1.0.1",
        urls = ["https://github.com/DataDog/dd-opentracing-cpp/archive/v1.0.1.tar.gz"],
    ),
    com_github_google_benchmark = dict(
        sha256 = "3c6a165b6ecc948967a1ead710d4a181d7b0fbcaa183ef7ea84604994966221a",
        strip_prefix = "benchmark-1.5.0",
        urls = ["https://github.com/google/benchmark/archive/v1.5.0.tar.gz"],
    ),
    com_github_libevent_libevent = dict(
        sha256 = "549d34065eb2485dfad6c8de638caaa6616ed130eec36dd978f73b6bdd5af113",
        # This SHA includes the new "prepare" and "check" watchers, used for event loop performance
        # stats (see https://github.com/libevent/libevent/pull/793) and the fix for a race condition
        # in the watchers (see https://github.com/libevent/libevent/pull/802).
        # This also includes the fixes for https://github.com/libevent/libevent/issues/806
        # and https://github.com/lyft/envoy-mobile/issues/215.
        # TODO(mergeconflict): Update to v2.2 when it is released.
        strip_prefix = "libevent-0d7d85c2083f7a4c9efe01c061486f332b576d28",
        # 2019-07-02
        urls = ["https://github.com/libevent/libevent/archive/0d7d85c2083f7a4c9efe01c061486f332b576d28.tar.gz"],
    ),
    net_zlib = dict(
        sha256 = "629380c90a77b964d896ed37163f5c3a34f6e6d897311f1df2a7016355c45eff",
        strip_prefix = "zlib-1.2.11",
        urls = ["https://github.com/madler/zlib/archive/v1.2.11.tar.gz"],
    ),
    com_github_jbeder_yaml_cpp = dict(
        sha256 = "53dcffd55f3433b379fcc694f45c54898711c0e29159a7bd02e82a3e0253bac3",
        strip_prefix = "yaml-cpp-0f9a586ca1dc29c2ecb8dd715a315b93e3f40f79",
        urls = ["https://github.com/jbeder/yaml-cpp/archive/0f9a586ca1dc29c2ecb8dd715a315b93e3f40f79.tar.gz"],
    ),
    com_github_msgpack_msgpack_c = dict(
        sha256 = "fbaa28c363a316fd7523f31d1745cf03eab0d1e1ea5a1c60aa0dffd4ce551afe",
        strip_prefix = "msgpack-3.2.0",
        urls = ["https://github.com/msgpack/msgpack-c/releases/download/cpp-3.2.0/msgpack-3.2.0.tar.gz"],
    ),
    com_github_google_jwt_verify = dict(
        sha256 = "8ab9a0b3f8b7eab5f1cd059920e81fdc138cd4ee657c1412af891652929885c5",
        strip_prefix = "jwt_verify_lib-6356535ae83a3f1820b6b06dae80cd6a0a03e7f2",
        # 2019-07-01
        urls = ["https://github.com/google/jwt_verify_lib/archive/6356535ae83a3f1820b6b06dae80cd6a0a03e7f2.tar.gz"],
    ),
    com_github_nodejs_http_parser = dict(
        sha256 = "ef26268c54c8084d17654ba2ed5140bffeffd2a040a895ffb22a6cca3f6c613f",
        strip_prefix = "http-parser-2.9.0",
        urls = ["https://github.com/nodejs/http-parser/archive/v2.9.0.tar.gz"],
    ),
    com_github_pallets_jinja = dict(
        sha256 = "e9baab084b8d84b511c75aca98bba8585041dbe971d5476ee53d9c6eea1b58b3",
        strip_prefix = "jinja-2.10.1",
        urls = ["https://github.com/pallets/jinja/archive/2.10.1.tar.gz"],
    ),
    com_github_pallets_markupsafe = dict(
        sha256 = "222a10e3237d92a9cd45ed5ea882626bc72bc5e0264d3ed0f2c9129fa69fc167",
        strip_prefix = "markupsafe-1.1.1/src",
        urls = ["https://github.com/pallets/markupsafe/archive/1.1.1.tar.gz"],
    ),
    com_github_tencent_rapidjson = dict(
        sha256 = "bf7ced29704a1e696fbccf2a2b4ea068e7774fa37f6d7dd4039d0787f8bed98e",
        strip_prefix = "rapidjson-1.1.0",
        urls = ["https://github.com/Tencent/rapidjson/archive/v1.1.0.tar.gz"],
    ),
    com_github_twitter_common_lang = dict(
        sha256 = "56d1d266fd4767941d11c27061a57bc1266a3342e551bde3780f9e9eb5ad0ed1",
        strip_prefix = "twitter.common.lang-0.3.9/src",
        urls = ["https://files.pythonhosted.org/packages/08/bc/d6409a813a9dccd4920a6262eb6e5889e90381453a5f58938ba4cf1d9420/twitter.common.lang-0.3.9.tar.gz"],
    ),
    com_github_twitter_common_rpc = dict(
        sha256 = "0792b63fb2fb32d970c2e9a409d3d00633190a22eb185145fe3d9067fdaa4514",
        strip_prefix = "twitter.common.rpc-0.3.9/src",
        urls = ["https://files.pythonhosted.org/packages/be/97/f5f701b703d0f25fbf148992cd58d55b4d08d3db785aad209255ee67e2d0/twitter.common.rpc-0.3.9.tar.gz"],
    ),
    com_github_twitter_common_finagle_thrift = dict(
        sha256 = "1e3a57d11f94f58745e6b83348ecd4fa74194618704f45444a15bc391fde497a",
        strip_prefix = "twitter.common.finagle-thrift-0.3.9/src",
        urls = ["https://files.pythonhosted.org/packages/f9/e7/4f80d582578f8489226370762d2cf6bc9381175d1929eba1754e03f70708/twitter.common.finagle-thrift-0.3.9.tar.gz"],
    ),
    com_google_googletest = dict(
        sha256 = "cbd251a40485fddd44cdf641af6df2953d45695853af6d68aeb11c7efcde6771",
        strip_prefix = "googletest-d7003576dd133856432e2e07340f45926242cc3a",
        # 2019-07-16
        # TODO(akonradi): Switch this back to a released version later than 1.8.1 once there is
        # one available.
        urls = ["https://github.com/google/googletest/archive/d7003576dd133856432e2e07340f45926242cc3a.tar.gz"],
    ),
    com_google_protobuf = dict(
        sha256 = "b7220b41481011305bf9100847cf294393973e869973a9661046601959b2960b",
        strip_prefix = "protobuf-3.8.0",
        urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v3.8.0/protobuf-all-3.8.0.tar.gz"],
    ),
    grpc_httpjson_transcoding = dict(
        sha256 = "dedd76b0169eb8c72e479529301a1d9b914a4ccb4d2b5ddb4ebe92d63a7b2152",
        strip_prefix = "grpc-httpjson-transcoding-64d6ac985360b624d8e95105701b64a3814794cd",
        # 2018-12-19
        urls = ["https://github.com/grpc-ecosystem/grpc-httpjson-transcoding/archive/64d6ac985360b624d8e95105701b64a3814794cd.tar.gz"],
    ),
    io_bazel_rules_go = dict(
        sha256 = "96b1f81de5acc7658e1f5a86d7dc9e1b89bc935d83799b711363a748652c471a",
        urls = ["https://github.com/bazelbuild/rules_go/releases/download/0.19.2/rules_go-0.19.2.tar.gz"],
    ),
    rules_foreign_cc = dict(
        sha256 = "c957e6663094a1478c43330c1bbfa71afeaf1ab86b7565233783301240c7a0ab",
        strip_prefix = "rules_foreign_cc-a209b642c7687a8894c19b3dd40e43e6d3f38e83",
        # 2019-07-17
        urls = ["https://github.com/bazelbuild/rules_foreign_cc/archive/a209b642c7687a8894c19b3dd40e43e6d3f38e83.tar.gz"],
    ),
    six_archive = dict(
        sha256 = "105f8d68616f8248e24bf0e9372ef04d3cc10104f1980f54d57b2ce73a5ad56a",
        urls = ["https://files.pythonhosted.org/packages/b3/b2/238e2590826bfdd113244a40d9d3eb26918bd798fc187e2360a8367068db/six-1.10.0.tar.gz"],
    ),
    io_opencensus_cpp = dict(
        sha256 = "b5fcc36a994a4ecb6e53c901e33579ed1ac238cff9975807db760918a15f43ff",
        strip_prefix = "opencensus-cpp-8058a1b8efe6a63bd18673abc51223917d12d45d",
        # 2019-08-22
        urls = ["https://github.com/census-instrumentation/opencensus-cpp/archive/8058a1b8efe6a63bd18673abc51223917d12d45d.tar.gz"],
    ),
    grpc_java = dict(
        sha256 = "9618a6f4ec0f2bdb77d9b6e01865af9796f370e63e1352210798bacfc99ccdac",
        strip_prefix = "grpc-java-1.23.0",
        urls = ["https://github.com/grpc/grpc-java/archive/v1.23.0.tar.gz"],
    ),
    com_github_curl = dict(
        sha256 = "4376ac72b95572fb6c4fbffefb97c7ea0dd083e1974c0e44cd7e49396f454839",
        strip_prefix = "curl-7.65.3",
        urls = ["https://github.com/curl/curl/releases/download/curl-7_65_3/curl-7.65.3.tar.gz"],
    ),
    com_googlesource_quiche = dict(
        # Static snapshot of https://quiche.googlesource.com/quiche/+archive/2a930469533c3b541443488a629fe25cd8ff53d0.tar.gz
        sha256 = "fcdebf54c89d839ffa7eefae166c8e4b551c765559db13ff15bff98047f344fb",
        urls = ["https://storage.googleapis.com/quiche-envoy-integration/2a930469533c3b541443488a629fe25cd8ff53d0.tar.gz"],
    ),
    com_google_cel_cpp = dict(
        sha256 = "f027c551d57d38fb9f0b5e4f21a2b0b8663987119e23b1fd8dfcc7588e9a2350",
        strip_prefix = "cel-cpp-d9d02b20ab85da2444dbdd03410bac6822141364",
        urls = ["https://github.com/google/cel-cpp/archive/d9d02b20ab85da2444dbdd03410bac6822141364.tar.gz"],
    ),
    com_googlesource_code_re2 = dict(
        sha256 = "f31db9cd224d018a7e4fe88ef84aaa874b0b3ed91d4d98ee5a1531101d3fdc64",
        strip_prefix = "re2-87e2ad45e7b18738e1551474f7ee5886ff572059",
        urls = ["https://github.com/google/re2/archive/87e2ad45e7b18738e1551474f7ee5886ff572059.tar.gz"],
    ),
)
