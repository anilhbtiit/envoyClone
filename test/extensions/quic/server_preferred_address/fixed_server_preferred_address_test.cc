#include "source/common/network/utility.h"
#include "source/extensions/quic/server_preferred_address/fixed_server_preferred_address_config.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

class FixedServerPreferredAddressConfigTest : public ::testing::Test {
public:
  FixedServerPreferredAddressConfigFactory factory_;
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> visitor_;
};

TEST_F(FixedServerPreferredAddressConfigTest, Validation) {
  {
    // Bad address_and_port.
    envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig cfg;
    cfg.mutable_ipv4_config()->mutable_address()->set_address("not an address");
    cfg.mutable_ipv4_config()->mutable_address()->set_port_value(1);
    EXPECT_THROW_WITH_REGEX(factory_.createServerPreferredAddressConfig(cfg, visitor_, {}),
                            EnvoyException, ".*malformed IP address: not an address.*");
  }
  {
    // Bad address.
    envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig cfg;
    cfg.set_ipv4_address("not an address");
    EXPECT_THROW_WITH_REGEX(factory_.createServerPreferredAddressConfig(cfg, visitor_, {}),
                            EnvoyException, ".*bad v4 server preferred address: not an address.*");
  }
  {
    // Non-zero port not supported in dnat address.
    envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig cfg;
    cfg.mutable_ipv4_config()->mutable_address()->set_address("127.0.0.1");
    cfg.mutable_ipv4_config()->mutable_address()->set_port_value(1);
    cfg.mutable_ipv4_config()->mutable_dnat_address()->set_address("127.0.0.1");
    cfg.mutable_ipv4_config()->mutable_dnat_address()->set_port_value(1);
    EXPECT_THROW_WITH_REGEX(factory_.createServerPreferredAddressConfig(cfg, visitor_, {}),
                            EnvoyException,
                            ".*port must be 0 in this version of Envoy in address '127.0.0.1:1'.*");
  }
  {
    // Cannot set dnat address but not spa address.
    envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig cfg;
    cfg.mutable_ipv4_config()->mutable_dnat_address()->set_address("127.0.0.1");
    cfg.mutable_ipv4_config()->mutable_dnat_address()->set_port_value(1);
    EXPECT_THROW_WITH_REGEX(
        factory_.createServerPreferredAddressConfig(cfg, visitor_, {}), EnvoyException,
        ".*'dnat_address' but not 'address' is set in server preferred address for v4.*");
  }
  {
    // v6 address in v4 field.
    envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig cfg;
    cfg.mutable_ipv4_config()->mutable_address()->set_address("::1");
    cfg.mutable_ipv4_config()->mutable_address()->set_port_value(1);
    EXPECT_THROW_WITH_REGEX(factory_.createServerPreferredAddressConfig(cfg, visitor_, {}),
                            EnvoyException,
                            ".*wrong address type for v4 server preferred address.*");
  }
  {
    // v4 address in v6 field.
    envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig cfg;
    cfg.mutable_ipv6_config()->mutable_address()->set_address("127.0.0.1");
    cfg.mutable_ipv6_config()->mutable_address()->set_port_value(1);
    EXPECT_THROW_WITH_REGEX(factory_.createServerPreferredAddressConfig(cfg, visitor_, {}),
                            EnvoyException,
                            ".*wrong address type for v6 server preferred address.*");
  }
}

TEST_F(FixedServerPreferredAddressConfigTest, AddressGetsCombinedWithPort) {
  envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig cfg;
  cfg.set_ipv4_address("1.2.3.4");
  auto obj = factory_.createServerPreferredAddressConfig(cfg, visitor_, {});
  auto addresses = obj->getServerPreferredAddresses(
      Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 1234));
  EXPECT_EQ(addresses.ipv4_.ToString(), "1.2.3.4:1234");
}

TEST_F(FixedServerPreferredAddressConfigTest, AddressAndPortIgnoresListenerPort) {
  envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig cfg;
  cfg.mutable_ipv4_config()->mutable_address()->set_address("1.2.3.4");
  cfg.mutable_ipv4_config()->mutable_address()->set_port_value(5);
  auto obj = factory_.createServerPreferredAddressConfig(cfg, visitor_, {});
  auto addresses = obj->getServerPreferredAddresses(
      Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 1234));
  EXPECT_EQ(addresses.ipv4_.ToString(), "1.2.3.4:5");
}

TEST_F(FixedServerPreferredAddressConfigTest, AddressAndZeroPortUsesListenerPort) {
  envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig cfg;
  cfg.mutable_ipv4_config()->mutable_address()->set_address("1.2.3.4");
  cfg.mutable_ipv4_config()->mutable_address()->set_port_value(0);
  auto obj = factory_.createServerPreferredAddressConfig(cfg, visitor_, {});
  auto addresses = obj->getServerPreferredAddresses(
      Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 1234));
  EXPECT_EQ(addresses.ipv4_.ToString(), "1.2.3.4:1234");
}

} // namespace Quic
} // namespace Envoy
