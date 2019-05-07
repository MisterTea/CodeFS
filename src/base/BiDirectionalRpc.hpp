#ifndef __BIDIRECTIONAL_RPC_H__
#define __BIDIRECTIONAL_RPC_H__

#include "Headers.hpp"
#include "MessageReader.hpp"
#include "MessageWriter.hpp"
#include "PidController.hpp"
#include "RpcId.hpp"

namespace codefs {
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
  BiDirectionalRpc();
  virtual ~BiDirectionalRpc();
  void shutdown();
  void heartbeat();
  void barrier() {
    lock_guard<recursive_mutex> guard(mutex);
    onBarrier++;
  }

  RpcId request(const string& payload);
  void requestNoReply(const string& payload);
  virtual void requestWithId(const IdPayload& idPayload);
  virtual void reply(const RpcId& rpcId, const string& payload);
  inline void replyOneWay(const RpcId& rpcId) { reply(rpcId, "OK"); }

  bool hasIncomingRequest() {
    lock_guard<recursive_mutex> guard(mutex);
    return !incomingRequests.empty();
  }
  bool hasIncomingRequestWithId(const RpcId& rpcId) {
    lock_guard<recursive_mutex> guard(mutex);
    return incomingRequests.find(rpcId) != incomingRequests.end();
  }
  IdPayload getFirstIncomingRequest() {
    lock_guard<recursive_mutex> guard(mutex);
    if (!hasIncomingRequest()) {
      LOGFATAL << "Tried to get a request when one doesn't exist";
    }
    return IdPayload(incomingRequests.begin()->first,
                     incomingRequests.begin()->second);
  }

  bool hasIncomingReply() {
    lock_guard<recursive_mutex> guard(mutex);
    return !incomingReplies.empty();
  }

  IdPayload getFirstIncomingReply() {
    lock_guard<recursive_mutex> guard(mutex);
    if (incomingReplies.empty()) {
      LOGFATAL << "Tried to get reply when there was none";
    }
    IdPayload idPayload = IdPayload(incomingReplies.begin()->first,
                                    incomingReplies.begin()->second);
    incomingReplies.erase(incomingReplies.begin());
    return idPayload;
  }

  bool hasIncomingReplyWithId(const RpcId& rpcId) {
    lock_guard<recursive_mutex> guard(mutex);
    return incomingReplies.find(rpcId) != incomingReplies.end();
  }
  string consumeIncomingReplyWithId(const RpcId& rpcId) {
    lock_guard<recursive_mutex> guard(mutex);
    auto it = incomingReplies.find(rpcId);
    if (it == incomingReplies.end()) {
      LOGFATAL << "Tried to get a reply that didn't exist!";
    }
    string payload = it->second;
    incomingReplies.erase(it);
    return payload;
  }

  void setFlaky(bool _flaky) { flaky = _flaky; }

  virtual void receive(const string& message);

  bool hasWork() {
    lock_guard<recursive_mutex> guard(mutex);
    return !delayedRequests.empty() || !outgoingRequests.empty() ||
           !incomingRequests.empty() || !outgoingReplies.empty() ||
           !incomingReplies.empty();
  }

 protected:
  unordered_map<RpcId, string> delayedRequests;
  unordered_map<RpcId, string> outgoingRequests;
  unordered_map<RpcId, string> incomingRequests;
  unordered_set<RpcId> oneWayRequests;

  unordered_map<RpcId, int64_t> requestSendTimeMap;
  unordered_map<RpcId, int64_t> requestRecieveTimeMap;

  unordered_map<RpcId, string> outgoingReplies;
  unordered_map<RpcId, string> incomingReplies;

  int64_t onBarrier;
  uint64_t onId;
  bool flaky;
  recursive_mutex mutex;

  struct NetworkStats {
    int64_t offset;
    int64_t ping;
  };
  deque<NetworkStats> networkStatsQueue;
  PidController timeOffsetController;

  void handleRequest(const RpcId& rpcId, const string& payload);
  virtual void handleReply(const RpcId& rpcId, const string& payload);
  void resendRandomOutgoingMessage();
  void tryToSendBarrier();
  void sendRequest(const RpcId& id, const string& payload);
  void sendReply(const RpcId& id, const string& payload);
  void sendAcknowledge(const RpcId& uid);
  virtual void addIncomingRequest(const IdPayload& idPayload);
  virtual void addIncomingReply(const RpcId& uid, const string& payload) {
    incomingReplies.emplace(uid, payload);
  }
  void updateDrift(int64_t requestSendTime, int64_t requestReceiptTime,
                   int64_t replySendTime, int64_t replyRecieveTime);

  virtual void send(const string& message) = 0;
};
}  // namespace codefs

#endif  // __BIDIRECTIONAL_RPC_H__