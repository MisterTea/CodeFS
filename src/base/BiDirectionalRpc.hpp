#ifndef __BIDIRECTIONAL_RPC_H__
#define __BIDIRECTIONAL_RPC_H__

#include "Headers.hpp"

namespace codefs {
class RpcId {
 public:
  RpcId() : barrier(0), id(0) {}
  RpcId(uint64_t _barrier, uint64_t _id) : barrier(_barrier), id(_id) {}
  bool operator==(const RpcId& other) const {
    return barrier == other.barrier && id == other.id;
  }
  string str() const { return to_string(barrier) + "/" + to_string(id); }

  uint64_t barrier;
  uint64_t id;
};

class IdPayload {
 public:
  IdPayload() {}
  IdPayload(const RpcId& _id, const string& _payload)
      : id(_id), payload(_payload) {}

  RpcId id;
  string payload;
};
}  // namespace codefs

namespace std {
template <>
struct hash<codefs::RpcId> : public std::unary_function<codefs::RpcId, size_t> {
 public:
  // hash functor: hash uuid to size_t value by pseudorandomizing transform
  size_t operator()(const codefs::RpcId& rpcId) const {
    if (sizeof(size_t) > 4) {
      return size_t(rpcId.barrier ^ rpcId.id);
    } else {
      uint64_t hash64 = rpcId.barrier ^ rpcId.id;
      return size_t(uint32_t(hash64 >> 32) ^ uint32_t(hash64));
    }
  }
};
}  // namespace std

namespace codefs {
enum RpcHeader { HEARTBEAT = 1, REQUEST = 2, REPLY = 3, ACKNOWLEDGE = 4 };

class BiDirectionalRpc {
 public:
  BiDirectionalRpc(const string& address, bool bind);
  ~BiDirectionalRpc();
  void shutdown();
  void heartbeat();
  void update();
  void barrier() { onBarrier++; }

  RpcId request(const string& payload);
  void reply(const RpcId& rpcId, const string& payload);

  bool hasIncomingRequest() { return !incomingRequests.empty(); }
  IdPayload consumeIncomingRequest() {
    IdPayload idPayload = incomingRequests.front();
    incomingRequests.pop_front();
    return idPayload;
  }

  bool hasIncomingReply(const RpcId& rpcId) {
    return incomingReplies.find(rpcId) != incomingReplies.end();
  }
  string consumeIncomingReply(const RpcId& rpcId) {
    auto it = incomingReplies.find(rpcId);
    if (it == incomingReplies.end()) {
      LOG(FATAL) << "Tried to get a reply that didn't exist!";
    }
    string payload = it->second;
    incomingReplies.erase(it);
    return payload;
  }

  void setFlaky(bool _flaky) { flaky = _flaky; }

 protected:
  deque<IdPayload> delayedRequests;
  deque<IdPayload> outgoingRequests;
  deque<IdPayload> incomingRequests;

  deque<IdPayload> outgoingReplies;
  unordered_map<RpcId, string> incomingReplies;

  shared_ptr<zmq::context_t> context;
  shared_ptr<zmq::socket_t> socket;

  std::random_device rd;
  std::uniform_int_distribution<uint64_t> dist;

  uint64_t onBarrier;
  uint64_t onId;
  bool flaky;

  void tryToSendBarrier();
  void sendRequest(const IdPayload& idPayload);
  void sendReply(const IdPayload& idPayload);
  void sendAcknowledge(const RpcId& uid);
};
}  // namespace codefs

#endif  // __BIDIRECTIONAL_RPC_H__