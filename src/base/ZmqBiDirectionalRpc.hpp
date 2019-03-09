#ifndef __ZMQ_BIDIRECTIONAL_RPC_H__
#define __ZMQ_BIDIRECTIONAL_RPC_H__

#include "BiDirectionalRpc.hpp"

namespace codefs {
class ZmqBiDirectionalRpc : public BiDirectionalRpc {
 public:
  ZmqBiDirectionalRpc(const string& address, bool bind);
  virtual ~ZmqBiDirectionalRpc();
  void shutdown();
  void update();

  void reconnect();

 protected:
  shared_ptr<zmq::context_t> context;
  shared_ptr<zmq::socket_t> socket;

  string address;
  bool bind;

  virtual void send(const string& message);
};
}  // namespace codefs

#endif  // __BIDIRECTIONAL_RPC_H__