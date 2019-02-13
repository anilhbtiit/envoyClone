#pragma once

#include "server/hot_restarting_base.h"

namespace Envoy {
namespace Server {

/**
 * The parent half of hot restarting. Listens for requests and commands from the child.
 */
class HotRestartingParent : HotRestartingBase, Logger::Loggable<Logger::Id::main> {
public:
  HotRestartingParent(int base_id, int restart_epoch);
  void initialize(Event::Dispatcher& dispatcher, Server::Instance& server);
  void shutdown();

private:
  void onSocketEvent();

  const int restart_epoch_;
  sockaddr_un child_address_;
  Event::FileEventPtr socket_event_;
  Server::Instance* server_{};
};

} // namespace Server
} // namespace Envoy
