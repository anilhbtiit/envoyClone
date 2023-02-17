package io.envoyproxy.envoymobile.engine;

import java.util.Collections;
import java.util.List;
import java.util.ArrayList;
import java.util.Map;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.lang.StringBuilder;
import javax.annotation.Nullable;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory;
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;
import io.envoyproxy.envoymobile.engine.types.EnvoyKeyValueStore;
import io.envoyproxy.envoymobile.engine.JniLibrary;

/* Typed configuration that may be used for starting Envoy. */
public class EnvoyConfiguration {
  // Peer certificate verification mode.
  // Must match the CertificateValidationContext.TrustChainVerification proto enum.
  public enum TrustChainVerification {
    // Perform default certificate verification (e.g., against CA / verification lists)
    VERIFY_TRUST_CHAIN,
    // Connections where the certificate fails verification will be permitted.
    // For HTTP connections, the result of certificate verification can be used in route matching.
    // Used for testing.
    ACCEPT_UNTRUSTED;
  }

  public final Boolean adminInterfaceEnabled;
  public final String grpcStatsDomain;
  public final Integer connectTimeoutSeconds;
  public final Integer dnsRefreshSeconds;
  public final Integer dnsFailureRefreshSecondsBase;
  public final Integer dnsFailureRefreshSecondsMax;
  public final Integer dnsQueryTimeoutSeconds;
  public final Integer dnsMinRefreshSeconds;
  public final List<String> dnsPreresolveHostnames;
  public final Boolean enableDNSCache;
  public final Integer dnsCacheSaveIntervalSeconds;
  public final Boolean enableDrainPostDnsRefresh;
  public final Boolean enableHttp3;
  public final Boolean enableGzipDecompression;
  public final Boolean enableBrotliDecompression;
  public final Boolean enableSocketTagging;
  public final Boolean enableHappyEyeballs;
  public final Boolean enableInterfaceBinding;
  public final Integer h2ConnectionKeepaliveIdleIntervalMilliseconds;
  public final Integer h2ConnectionKeepaliveTimeoutSeconds;
  public final Integer maxConnectionsPerHost;
  public final List<EnvoyHTTPFilterFactory> httpPlatformFilterFactories;
  public final Integer statsFlushSeconds;
  public final Integer streamIdleTimeoutSeconds;
  public final Integer perTryIdleTimeoutSeconds;
  public final String appVersion;
  public final String appId;
  public final TrustChainVerification trustChainVerification;
  public final List<String> virtualClusters;
  public final List<EnvoyNativeFilterConfig> nativeFilterChain;
  public final Map<String, EnvoyStringAccessor> stringAccessors;
  public final Map<String, EnvoyKeyValueStore> keyValueStores;
  public final List<String> statSinks;
  public final Boolean enablePlatformCertificatesValidation;
  public final Boolean enableSkipDNSLookupForProxiedRequests;

  private static final Pattern UNRESOLVED_KEY_PATTERN = Pattern.compile("\\{\\{ (.+) \\}\\}");

  /**
   * Create a new instance of the configuration.
   *
   * @param adminInterfaceEnabled                         whether admin interface should be enabled
   *     or not.
   * @param grpcStatsDomain                               the domain to flush stats to.
   * @param connectTimeoutSeconds                         timeout for new network connections to
   *     hosts in
   *                                                      the cluster.
   * @param dnsRefreshSeconds                             default rate in seconds at which to
   *     refresh DNS.
   * @param dnsFailureRefreshSecondsBase                  base rate in seconds to refresh DNS on
   *     failure.
   * @param dnsFailureRefreshSecondsMax                   max rate in seconds to refresh DNS on
   *     failure.
   * @param dnsQueryTimeoutSeconds                        rate in seconds to timeout DNS queries.
   * @param dnsMinRefreshSeconds                          minimum rate in seconds at which to
   *     refresh DNS.
   * @param dnsPreresolveHostnames                        hostnames to preresolve on Envoy Client
   *     construction.
   * @param enableDNSCache                                whether to enable DNS cache.
   * @param dnsCacheSaveIntervalSeconds                   the interval at which to save results to
   *     the configured key value store.
   * @param enableDrainPostDnsRefresh                     whether to drain connections after soft
   *     DNS refresh.
   * @param enableHttp3                                   whether to enable experimental support for
   *     HTTP/3 (QUIC).
   * @param enableGzipDecompression                       whether to enable response gzip
   *     decompression.
   *     compression.
   * @param enableBrotliDecompression                     whether to enable response brotli
   *     decompression.
   *     compression.
   * @param enableSocketTagging                           whether to enable socket tagging.
   * @param enableHappyEyeballs                           whether to enable RFC 6555 handling for
   *     IPv4/IPv6.
   * @param enableInterfaceBinding                        whether to allow interface binding.
   * @param h2ConnectionKeepaliveIdleIntervalMilliseconds rate in milliseconds seconds to send h2
   *                                                      pings on stream creation.
   * @param h2ConnectionKeepaliveTimeoutSeconds           rate in seconds to timeout h2 pings.
   * @param maxConnectionsPerHost                         maximum number of connections to open to a
   *                                                      single host.
   * @param statsFlushSeconds                             interval at which to flush Envoy stats.
   * @param streamIdleTimeoutSeconds                      idle timeout for HTTP streams.
   * @param perTryIdleTimeoutSeconds                      per try idle timeout for HTTP streams.
   * @param appVersion                                    the App Version of the App using this
   *     Envoy Client.
   * @param appId                                         the App ID of the App using this Envoy
   *     Client.
   * @param trustChainVerification                        whether to mute TLS Cert verification -
   *     for tests.
   * @param virtualClusters                               the JSON list of virtual cluster configs.
   * @param nativeFilterChain                             the configuration for native filters.
   * @param httpPlatformFilterFactories                   the configuration for platform filters.
   * @param stringAccessors                               platform string accessors to register.
   * @param keyValueStores                                platform key-value store implementations.
   * @param enableSkipDNSLookupForProxiedRequests         whether to skip waiting on DNS response
   *     for proxied requests.
   * @param enablePlatformCertificatesValidation          whether to use the platform verifier.
   */
  public EnvoyConfiguration(
      boolean adminInterfaceEnabled, String grpcStatsDomain, int connectTimeoutSeconds,
      int dnsRefreshSeconds, int dnsFailureRefreshSecondsBase, int dnsFailureRefreshSecondsMax,
      int dnsQueryTimeoutSeconds, int dnsMinRefreshSeconds, List<String> dnsPreresolveHostnames,
      boolean enableDNSCache, int dnsCacheSaveIntervalSeconds, boolean enableDrainPostDnsRefresh,
      boolean enableHttp3, boolean enableGzipDecompression, boolean enableBrotliDecompression,
      boolean enableSocketTagging, boolean enableHappyEyeballs, boolean enableInterfaceBinding,
      int h2ConnectionKeepaliveIdleIntervalMilliseconds, int h2ConnectionKeepaliveTimeoutSeconds,
      int maxConnectionsPerHost, int statsFlushSeconds, int streamIdleTimeoutSeconds,
      int perTryIdleTimeoutSeconds, String appVersion, String appId,
      TrustChainVerification trustChainVerification, List<String> virtualClusters,
      List<EnvoyNativeFilterConfig> nativeFilterChain,
      List<EnvoyHTTPFilterFactory> httpPlatformFilterFactories,
      Map<String, EnvoyStringAccessor> stringAccessors,
      Map<String, EnvoyKeyValueStore> keyValueStores, List<String> statSinks,
      Boolean enableSkipDNSLookupForProxiedRequests, boolean enablePlatformCertificatesValidation) {
    JniLibrary.load();
    this.adminInterfaceEnabled = adminInterfaceEnabled;
    this.grpcStatsDomain = grpcStatsDomain;
    this.connectTimeoutSeconds = connectTimeoutSeconds;
    this.dnsRefreshSeconds = dnsRefreshSeconds;
    this.dnsFailureRefreshSecondsBase = dnsFailureRefreshSecondsBase;
    this.dnsFailureRefreshSecondsMax = dnsFailureRefreshSecondsMax;
    this.dnsQueryTimeoutSeconds = dnsQueryTimeoutSeconds;
    this.dnsMinRefreshSeconds = dnsMinRefreshSeconds;
    this.dnsPreresolveHostnames = dnsPreresolveHostnames;
    this.enableDNSCache = enableDNSCache;
    this.dnsCacheSaveIntervalSeconds = dnsCacheSaveIntervalSeconds;
    this.enableDrainPostDnsRefresh = enableDrainPostDnsRefresh;
    this.enableHttp3 = enableHttp3;
    this.enableGzipDecompression = enableGzipDecompression;
    this.enableBrotliDecompression = enableBrotliDecompression;
    this.enableSocketTagging = enableSocketTagging;
    this.enableHappyEyeballs = enableHappyEyeballs;
    this.enableInterfaceBinding = enableInterfaceBinding;
    this.h2ConnectionKeepaliveIdleIntervalMilliseconds =
        h2ConnectionKeepaliveIdleIntervalMilliseconds;
    this.h2ConnectionKeepaliveTimeoutSeconds = h2ConnectionKeepaliveTimeoutSeconds;
    this.maxConnectionsPerHost = maxConnectionsPerHost;
    this.statsFlushSeconds = statsFlushSeconds;
    this.streamIdleTimeoutSeconds = streamIdleTimeoutSeconds;
    this.perTryIdleTimeoutSeconds = perTryIdleTimeoutSeconds;
    this.appVersion = appVersion;
    this.appId = appId;
    this.trustChainVerification = trustChainVerification;
    this.virtualClusters = virtualClusters;
    int index = 0;
    // Insert in this order to preserve prior ordering constraints.
    for (EnvoyHTTPFilterFactory filterFactory : httpPlatformFilterFactories) {
      String config =
          "{'@type': type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge, platform_filter_name: " +
          filterFactory.getFilterName() + "}";
      EnvoyNativeFilterConfig ins =
          new EnvoyNativeFilterConfig("envoy.filters.http.platform_bridge", config);
      nativeFilterChain.add(index++, ins);
    }
    this.nativeFilterChain = nativeFilterChain;

    this.httpPlatformFilterFactories = httpPlatformFilterFactories;
    this.stringAccessors = stringAccessors;
    this.keyValueStores = keyValueStores;
    this.statSinks = statSinks;
    this.enablePlatformCertificatesValidation = enablePlatformCertificatesValidation;
    this.enableSkipDNSLookupForProxiedRequests = enableSkipDNSLookupForProxiedRequests;
  }

  // TODO(alyssawilk) move this to the test only JNI library.
  String createYaml() {
    Boolean enforceTrustChainVerification =
        trustChainVerification == EnvoyConfiguration.TrustChainVerification.VERIFY_TRUST_CHAIN;
    List<EnvoyNativeFilterConfig> reverseFilterChain = new ArrayList<>(nativeFilterChain);
    Collections.reverse(reverseFilterChain);

    byte[][] filter_chain = JniBridgeUtility.toJniBytes(reverseFilterChain);
    byte[][] clusters = JniBridgeUtility.stringsToJniBytes(virtualClusters);
    byte[][] stats_sinks = JniBridgeUtility.stringsToJniBytes(statSinks);
    byte[][] dns_preresolve = JniBridgeUtility.stringsToJniBytes(dnsPreresolveHostnames);

    return JniLibrary.createYaml(
        grpcStatsDomain, adminInterfaceEnabled, connectTimeoutSeconds, dnsRefreshSeconds,
        dnsFailureRefreshSecondsBase, dnsFailureRefreshSecondsMax, dnsQueryTimeoutSeconds,
        dnsMinRefreshSeconds, dns_preresolve, enableDNSCache, dnsCacheSaveIntervalSeconds,
        enableDrainPostDnsRefresh, enableHttp3, enableGzipDecompression, enableBrotliDecompression,
        enableSocketTagging, enableHappyEyeballs, enableInterfaceBinding,
        h2ConnectionKeepaliveIdleIntervalMilliseconds, h2ConnectionKeepaliveTimeoutSeconds,
        maxConnectionsPerHost, statsFlushSeconds, streamIdleTimeoutSeconds,
        perTryIdleTimeoutSeconds, appVersion, appId, enforceTrustChainVerification, clusters,
        filter_chain, stats_sinks, enablePlatformCertificatesValidation,
        enableSkipDNSLookupForProxiedRequests);
  }

  long createBootstrap() {
    Boolean enforceTrustChainVerification =
        trustChainVerification == EnvoyConfiguration.TrustChainVerification.VERIFY_TRUST_CHAIN;
    List<EnvoyNativeFilterConfig> reverseFilterChain = new ArrayList<>(nativeFilterChain);
    Collections.reverse(reverseFilterChain);

    byte[][] filter_chain = JniBridgeUtility.toJniBytes(reverseFilterChain);
    byte[][] clusters = JniBridgeUtility.stringsToJniBytes(virtualClusters);
    byte[][] stats_sinks = JniBridgeUtility.stringsToJniBytes(statSinks);
    byte[][] dns_preresolve = JniBridgeUtility.stringsToJniBytes(dnsPreresolveHostnames);
    return JniLibrary.createBootstrap(
        grpcStatsDomain, adminInterfaceEnabled, connectTimeoutSeconds, dnsRefreshSeconds,
        dnsFailureRefreshSecondsBase, dnsFailureRefreshSecondsMax, dnsQueryTimeoutSeconds,
        dnsMinRefreshSeconds, dns_preresolve, enableDNSCache, dnsCacheSaveIntervalSeconds,
        enableDrainPostDnsRefresh, enableHttp3, enableGzipDecompression, enableBrotliDecompression,
        enableSocketTagging, enableHappyEyeballs, enableInterfaceBinding,
        h2ConnectionKeepaliveIdleIntervalMilliseconds, h2ConnectionKeepaliveTimeoutSeconds,
        maxConnectionsPerHost, statsFlushSeconds, streamIdleTimeoutSeconds,
        perTryIdleTimeoutSeconds, appVersion, appId, enforceTrustChainVerification, clusters,
        filter_chain, stats_sinks, enablePlatformCertificatesValidation,
        enableSkipDNSLookupForProxiedRequests);
  }

  static class ConfigurationException extends RuntimeException {
    ConfigurationException(String unresolvedKey) {
      super("Unresolved template key: " + unresolvedKey);
    }
  }
}
