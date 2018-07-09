#ifndef __BIDIRECTIONAL_RPC_H__
#define __BIDIRECTIONAL_RPC_H__

#include "Headers.hpp"

namespace codefs {
enum RpcHeader { HEARTBEAT = 1, REQUEST = 2, REPLY = 3, ACKNOWLEDGE = 4 };

class BiDirectionalRpc {
 public:
  class Handler {
   public:
    virtual pair<bool, string> request(const string& payload) = 0;
  };

  BiDirectionalRpc(Handler* _handler, const string& address, bool bind);
  void heartbeat();
  void update();
  sole::uuid request(const string& payload);
  bool hasReply(const sole::uuid& rpcId) {
    return incomingReplies.find(rpcId) != incomingReplies.end();
  }
  string getReply(const sole::uuid& rpcId) {
    auto it = incomingReplies.find(rpcId);
    if (it == incomingReplies.end()) {
      LOG(FATAL) << "Tried to get a reply that didn't exist!";
    }
    string payload = it->second;
    incomingReplies.erase(it);
    return payload;
  }

 protected:
  Handler* handler;

  unordered_map<sole::uuid, string> outgoingRequests;
  unordered_map<sole::uuid, string> outgoingReplies;

  unordered_map<sole::uuid, string> incomingReplies;

  shared_ptr<zmq::context_t> context;
  shared_ptr<zmq::socket_t> socket;

  void sendRequest(const sole::uuid& uid);
  void sendReply(const sole::uuid& uid);
  void sendAcknowledge(const sole::uuid& uid);
};
}  // namespace codefs

#endif  // __BIDIRECTIONAL_RPC_H__