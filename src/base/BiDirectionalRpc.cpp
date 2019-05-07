#include "BiDirectionalRpc.hpp"

#include "TimeHandler.hpp"

namespace codefs {
BiDirectionalRpc::BiDirectionalRpc()
    : onBarrier(0),
      onId(0),
      flaky(false),
      timeOffsetController(1.0, 1000000, -1000000, 0.6, 1.2, 1.0) {}

BiDirectionalRpc::~BiDirectionalRpc() {}

void BiDirectionalRpc::shutdown() {}

void BiDirectionalRpc::heartbeat() {
  lock_guard<recursive_mutex> guard(mutex);
  // TODO: If the outgoingReplies/requests is high, and we have recently
  // received data, flush a lot of data out
  VLOG(1) << "BEAT: " << int64_t(this);
  if (!outgoingReplies.empty() || !outgoingRequests.empty()) {
    resendRandomOutgoingMessage();
  } else {
    VLOG(1) << "SENDING HEARTBEAT";
    string s = "0";
    s[0] = HEARTBEAT;
    send(s);
  }
}

void BiDirectionalRpc::resendRandomOutgoingMessage() {
  if (!outgoingReplies.empty() &&
      (outgoingRequests.empty() || rand() % 2 == 0)) {
    // Re-send a random reply
    DRAW_FROM_UNORDERED(it, outgoingReplies);
    sendReply(it->first, it->second);
  } else if (!outgoingRequests.empty()) {
    // Re-send a random request
    DRAW_FROM_UNORDERED(it, outgoingRequests);
    sendRequest(it->first, it->second);
  } else {
  }
}

void BiDirectionalRpc::receive(const string& message) {
  lock_guard<recursive_mutex> guard(mutex);
  VLOG(1) << "Receiving message with length " << message.length();
  MessageReader reader;
  reader.load(message);
  RpcHeader header = (RpcHeader)reader.readPrimitive<unsigned char>();
  if (flaky && rand() % 2 == 0) {
    // Pretend we never got the message
    VLOG(1) << "FLAKE";
  } else {
    if (header != HEARTBEAT) {
      VLOG(1) << "GOT PACKET WITH HEADER " << header;
    }
    switch (header) {
      case HEARTBEAT: {
        // MultiEndpointHandler deals with keepalive
      } break;
      case REQUEST: {
        while (reader.sizeRemaining()) {
          RpcId rpcId = reader.readClass<RpcId>();
          string payload = reader.readPrimitive<string>();
          handleRequest(rpcId, payload);
        }
      } break;
      case REPLY: {
        while (reader.sizeRemaining()) {
          RpcId uid = reader.readClass<RpcId>();
          int64_t requestReceiptTime = reader.readPrimitive<int64_t>();
          int64_t replySendTime = reader.readPrimitive<int64_t>();
          auto requestSendTimeIt = requestSendTimeMap.find(uid);
          if (requestSendTimeIt != requestSendTimeMap.end()) {
            int64_t requestSendTime = requestSendTimeIt->second;
            requestSendTimeMap.erase(requestSendTimeIt);
            int64_t replyRecieveTime = TimeHandler::currentTimeMicros();
            updateDrift(requestSendTime, requestReceiptTime, replySendTime,
                        replyRecieveTime);
          }
          string payload = reader.readPrimitive<string>();
          handleReply(uid, payload);
        }
      } break;
      case ACKNOWLEDGE: {
        RpcId uid = reader.readClass<RpcId>();
        VLOG(1) << "ACK UID " << uid.str();
        for (auto it = outgoingReplies.begin(); it != outgoingReplies.end();
             it++) {
          VLOG(1) << "REPLY UID " << it->first.str();
          if (it->first == uid) {
            if (requestRecieveTimeMap.find(it->first) ==
                requestRecieveTimeMap.end()) {
              LOG(INFO) << requestRecieveTimeMap.size();
              for (const auto& it2 : requestRecieveTimeMap) {
                LOG(INFO) << "XXXX: " << it2.first.str();
              }
              LOGFATAL << "Tried to remove a request receive time that we "
                          "didn't have: "
                       << it->first.str();
            }
            requestRecieveTimeMap.erase(it->first);
            outgoingReplies.erase(it);
            break;
          }
        }
      } break;
      default: {
        LOGFATAL << "Got invalid header: " << header << " in message "
                 << message;
      }
    }
  }
}

void BiDirectionalRpc::handleRequest(const RpcId& rpcId,
                                     const string& payload) {
  VLOG(1) << "GOT REQUEST: " << rpcId.str();

  bool skip = (incomingRequests.find(rpcId) != incomingRequests.end());
  if (!skip) {
    for (const auto& it : outgoingReplies) {
      if (it.first == rpcId) {
        // We already processed this request.  Send the reply again
        skip = true;
        sendReply(it.first, it.second);
        break;
      }
    }
  }
  if (!skip) {
    addIncomingRequest(IdPayload(rpcId, payload));
  }
}

void BiDirectionalRpc::handleReply(const RpcId& rpcId, const string& payload) {
  bool skip = false;
  if (incomingReplies.find(rpcId) != incomingReplies.end()) {
    // We already received this reply.  Send acknowledge again and skip.
    sendAcknowledge(rpcId);
    skip = true;
  }
  if (!skip) {
    // Stop sending the request once you get the reply
    bool deletedRequest = false;
    for (auto it = outgoingRequests.begin(); it != outgoingRequests.end();
         it++) {
      if (it->first == rpcId) {
        outgoingRequests.erase(it);
        deletedRequest = true;
        tryToSendBarrier();
        break;
      }
    }
    if (deletedRequest) {
      auto it = oneWayRequests.find(rpcId);
      if (it != oneWayRequests.end()) {
        // Remove this from the set of one way requests and don't bother
        // adding a reply.
        oneWayRequests.erase(it);
      } else {
        // Add a reply to be processed
        addIncomingReply(rpcId, payload);
      }
      sendAcknowledge(rpcId);
    } else {
      // We must have processed both this request and reply.  Send the
      // acknowledge again.
      sendAcknowledge(rpcId);
    }
  }
}

RpcId BiDirectionalRpc::request(const string& payload) {
  auto fullUuid = sole::uuid4();
  auto uuid = RpcId(onBarrier, fullUuid.cd);
  auto idPayload = IdPayload(uuid, payload);
  requestWithId(idPayload);
  return uuid;
}

void BiDirectionalRpc::requestNoReply(const string& payload) {
  lock_guard<recursive_mutex> guard(mutex);
  auto fullUuid = sole::uuid4();
  auto uuid = RpcId(onBarrier, fullUuid.cd);
  oneWayRequests.insert(uuid);
  auto idPayload = IdPayload(uuid, payload);
  requestWithId(idPayload);
}

void BiDirectionalRpc::requestWithId(const IdPayload& idPayload) {
  lock_guard<recursive_mutex> guard(mutex);
  if (outgoingRequests.empty() ||
      outgoingRequests.begin()->first.barrier == onBarrier) {
    // We can send the request immediately
    outgoingRequests[idPayload.id] = idPayload.payload;
    requestSendTimeMap[idPayload.id] = TimeHandler::currentTimeMicros();
    sendRequest(idPayload.id, idPayload.payload);
  } else {
    // We have to wait for existing requests from an older barrier
    delayedRequests[idPayload.id] = idPayload.payload;
  }
}

void BiDirectionalRpc::reply(const RpcId& rpcId, const string& payload) {
  lock_guard<recursive_mutex> guard(mutex);
  incomingRequests.erase(incomingRequests.find(rpcId));
  outgoingReplies[rpcId] = payload;
  sendReply(rpcId, payload);
}

void BiDirectionalRpc::tryToSendBarrier() {
  if (delayedRequests.empty()) {
    // Nothing to send
    return;
  }
  if (outgoingRequests.empty()) {
    // There are no outgoing requests, we can send the next barrier
    int64_t lowestBarrier = delayedRequests.begin()->first.barrier;
    for (const auto& it : delayedRequests) {
      lowestBarrier = min(lowestBarrier, it.first.barrier);
    }

    for (auto it = delayedRequests.begin(); it != delayedRequests.end();) {
      if (it->first.barrier == lowestBarrier) {
        outgoingRequests[it->first] = it->second;
        requestSendTimeMap[it->first] = TimeHandler::currentTimeMicros();
        sendRequest(it->first, it->second);
        it = delayedRequests.erase(it);
      } else {
        it++;
      }
    }
  }
}

void BiDirectionalRpc::sendRequest(const RpcId& id, const string& payload) {
  VLOG(1) << "SENDING REQUEST: " << id.str();
  MessageWriter writer;
  writer.start();
  set<RpcId> rpcsSent;

  rpcsSent.insert(id);
  writer.writePrimitive<unsigned char>(REQUEST);
  writer.writeClass<RpcId>(id);
  writer.writePrimitive<string>(payload);
  // Try to attach more requests to this packet
  int i = 0;
  while (!outgoingRequests.empty() &&
         rpcsSent.size() < outgoingRequests.size()) {
    DRAW_FROM_UNORDERED(it, outgoingRequests);
    if (rpcsSent.find(it->first) != rpcsSent.end()) {
      // Drew an rpc that's already in the packet.  Just bail for now, maybe in
      // the future do something more clever.
      break;
    }
    int size = sizeof(RpcId) + it->second.length();
    if (size + writer.size() > 400) {
      // Too big
      break;
    }
    i++;
    rpcsSent.insert(it->first);
    writer.writeClass<RpcId>(it->first);
    writer.writePrimitive<string>(it->second);
  }
  VLOG(1) << "Attached " << i << " extra packets";
  send(writer.finish());
}

void BiDirectionalRpc::sendReply(const RpcId& id, const string& payload) {
  lock_guard<recursive_mutex> guard(mutex);
  VLOG(1) << "SENDING REPLY: " << id.str();
  set<RpcId> rpcsSent;

  rpcsSent.insert(id);
  MessageWriter writer;
  writer.start();
  writer.writePrimitive<unsigned char>(REPLY);
  writer.writeClass<RpcId>(id);
  auto receiveTimeIt = requestRecieveTimeMap.find(id);
  if (receiveTimeIt == requestRecieveTimeMap.end()) {
    LOGFATAL << "Got a request with no receive time: " << id.str() << " "
             << requestRecieveTimeMap.size();
  }
  writer.writePrimitive<int64_t>(receiveTimeIt->second);
  writer.writePrimitive<int64_t>(TimeHandler::currentTimeMicros());
  writer.writePrimitive<string>(payload);
  // Try to attach more replies to this packet
  int i = 0;
  while (!outgoingReplies.empty() && rpcsSent.size() < outgoingReplies.size()) {
    DRAW_FROM_UNORDERED(it, outgoingReplies);
    if (rpcsSent.find(it->first) != rpcsSent.end()) {
      // Drew an rpc that's already in the packet.  Just bail for now, maybe in
      // the future do something more clever.
      break;
    }
    int size = sizeof(RpcId) + it->second.length();
    if (size + writer.size() > 400) {
      // Too big
      break;
    }
    i++;
    rpcsSent.insert(it->first);
    writer.writeClass<RpcId>(it->first);
    receiveTimeIt = requestRecieveTimeMap.find(it->first);
    if (receiveTimeIt == requestRecieveTimeMap.end()) {
      LOGFATAL << "Got a request with no receive time";
    }
    writer.writePrimitive<int64_t>(receiveTimeIt->second);
    writer.writePrimitive<int64_t>(TimeHandler::currentTimeMicros());
    writer.writePrimitive<string>(it->second);
  }
  VLOG(1) << "Attached " << i << " extra packets";
  send(writer.finish());
}

void BiDirectionalRpc::sendAcknowledge(const RpcId& uid) {
  MessageWriter writer;
  writer.start();
  writer.writePrimitive<unsigned char>(ACKNOWLEDGE);
  writer.writeClass<RpcId>(uid);
  send(writer.finish());
}

void BiDirectionalRpc::addIncomingRequest(const IdPayload& idPayload) {
  lock_guard<recursive_mutex> guard(mutex);
  if (requestRecieveTimeMap.find(idPayload.id) != requestRecieveTimeMap.end()) {
    LOGFATAL << "Already created receive time for id: " << idPayload.id.str();
  }
  requestRecieveTimeMap[idPayload.id] = TimeHandler::currentTimeMicros();
  incomingRequests.insert(make_pair(idPayload.id, idPayload.payload));
}

void BiDirectionalRpc::updateDrift(int64_t requestSendTime,
                                   int64_t requestReceiptTime,
                                   int64_t replySendTime,
                                   int64_t replyRecieveTime) {
  int64_t timeOffset = ((requestReceiptTime - requestSendTime) +
                        (replySendTime - replyRecieveTime)) /
                       2;
  int64_t ping = (replyRecieveTime - requestSendTime) -
                 (replySendTime - requestReceiptTime);
  networkStatsQueue.push_back({timeOffset, ping});
  VLOG(2) << "Time Sync Info: " << timeOffset << " " << ping << " "
          << (replyRecieveTime - requestSendTime) << " "
          << (replySendTime - requestReceiptTime);
  if (networkStatsQueue.size() >= 100) {
    LOG(INFO) << "Time Sync Info: " << timeOffset << " " << ping << " "
              << (replyRecieveTime - requestSendTime) << " "
              << (replySendTime - requestReceiptTime);
    int64_t sumShift = 0;
    int64_t shiftCount = 0;
    for (int i = 0; i < networkStatsQueue.size(); i++) {
      sumShift += networkStatsQueue.at(i).offset;
      shiftCount++;
    }
    if (shiftCount) {
      VLOG(2) << "New shift: " << (sumShift / shiftCount);
      auto shift =
          std::chrono::microseconds{sumShift / shiftCount / int64_t(5)};
      VLOG(2) << "TIME CHANGE: " << TimeHandler::currentTimeMicros();
      TimeHandler::initialTime -= shift;
      VLOG(2) << "TIME CHANGE: " << TimeHandler::currentTimeMicros();
    }
  }
  // auto shift = std::chrono::microseconds{
  //     int64_t(timeOffsetController.calculate(0, double(timeOffset)))};
  // TimeHandler::initialTime += shift;
  networkStatsQueue.clear();
}

}  // namespace codefs
