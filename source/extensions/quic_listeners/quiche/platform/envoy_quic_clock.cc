#include "extensions/quic_listeners/quiche/platform/envoy_quic_clock.h"

namespace Envoy {
namespace Quic {

quic::QuicTime EnvoyQuicClock::ApproximateNow() const {
  return quic::QuicTime::Zero() + quic::QuicTime::Delta::FromMicroseconds(microsecondsSinceEpoch(
                                      dispatcher_.approximateMonotonicTime()));
}

quic::QuicTime EnvoyQuicClock::Now() const {
  return quic::QuicTime::Zero() + quic::QuicTime::Delta::FromMicroseconds(microsecondsSinceEpoch(
                                      dispatcher_.timeSource().monotonicTime()));
}

quic::QuicWallTime EnvoyQuicClock::WallNow() const {
  return quic::QuicWallTime::FromUNIXMicroseconds(
      microsecondsSinceEpoch(dispatcher_.timeSource().systemTime()));
}

} // namespace Quic
} // namespace Envoy
