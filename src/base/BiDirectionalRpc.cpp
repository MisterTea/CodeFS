#include "BiDirectionalRpc.hpp"

#include "TimeHandler.hpp"

namespace codefs {
BiDirectionalRpc::BiDirectionalRpc(bool _reliable)
    : onBarrier(0), onId(0), flaky(false), reliable(_reliable) {}

BiDirectionalRpc::~BiDirectionalRpc() {}

void BiDirectionalRpc::shutdown() {}

void BiDirectionalRpc::heartbeat() {
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
    sendReply(*it);
  } else if (!outgoingRequests.empty()) {
    // Re-send a random request
    DRAW_FROM_UNORDERED(it, outgoingRequests);
    sendRequest(*it);
  } else {
  }
}

void BiDirectionalRpc::receive(const string& message) {
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
          string payload = reader.readPrimitive<string>();
          handleReply(uid, payload);
        }
      } break;
      case ACKNOWLEDGE: {
        RpcId uid = reader.readClass<RpcId>();
        VLOG(1) << "ACK UID " << uid.str();
        for (auto it = outgoingReplies.begin(); it != outgoingReplies.end();
             it++) {
          VLOG(1) << "REPLY UID " << it->id.str();
          if (it->id == uid) {
            outgoingReplies.erase(it);

            // When we complete an RPC, try to send a new message
            // resendRandomOutgoingMessage();
            break;
          }
        }
      } break;
    }
  }
}

void BiDirectionalRpc::handleRequest(const RpcId& rpcId,
                                     const string& payload) {
  bool skip = false;
  for (auto it : incomingRequests) {
    if (it.first == rpcId) {
      // We already received this request.  Skip
      skip = true;
      break;
    }
  }
  if (!skip) {
    for (const IdPayload& it : outgoingReplies) {
      if (it.id == rpcId) {
        // We already processed this request.  Send the reply again
        skip = true;
        sendReply(it);
        break;
      }
    }
  }
  if (!skip) {
    VLOG(1) << "GOT REQUEST: " << rpcId.str();
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
      if (it->id == rpcId) {
        auto reqTimeIt = requestTime.find(it->id);
        if (reqTimeIt == requestTime.end()) {
          LOG(FATAL) << "We think a request is just replied to, but it's time "
                        "is not found";
        }
        rpcTime.push_back(TimeHandler::currentTimeMs() - reqTimeIt->second);
        requestTime.erase(reqTimeIt);

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

    // When we complete an reply, try to send a new message
    // resendRandomOutgoingMessage();
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
  auto fullUuid = sole::uuid4();
  auto uuid = RpcId(onBarrier, fullUuid.cd);
  oneWayRequests.insert(uuid);
  auto idPayload = IdPayload(uuid, payload);
  requestWithId(idPayload);
}

void BiDirectionalRpc::requestWithId(const IdPayload& idPayload) {
  if (outgoingRequests.empty() ||
      outgoingRequests.begin()->id.barrier == onBarrier) {
    // We can send the request immediately
    outgoingRequests.push_back(idPayload);
    requestTime[idPayload.id] = TimeHandler::currentTimeMs();
    sendRequest(outgoingRequests.back());
  } else {
    // We have to wait for existing requests from an older barrier
    delayedRequests.push_back(idPayload);
  }
}

void BiDirectionalRpc::reply(const RpcId& rpcId, const string& payload) {
  incomingRequests.erase(incomingRequests.find(rpcId));
  outgoingReplies.push_back(IdPayload(rpcId, payload));
  sendReply(outgoingReplies.back());
}

void BiDirectionalRpc::tryToSendBarrier() {
  if (delayedRequests.empty()) {
    // Nothing to send
    return;
  }
  if (outgoingRequests.empty()) {
    // There are no outgoing requests, we can send the next barrier
    outgoingRequests.push_back(delayedRequests.front());
    requestTime[delayedRequests.front().id] = TimeHandler::currentTimeMs();
    delayedRequests.pop_front();
    sendRequest(outgoingRequests.back());

    while (!delayedRequests.empty() &&
           delayedRequests.front().id.barrier ==
               outgoingRequests.begin()->id.barrier) {
      // Part of the same barrier, keep sending
      requestTime[delayedRequests.front().id] = TimeHandler::currentTimeMs();
      outgoingRequests.push_back(delayedRequests.front());
      delayedRequests.pop_front();
      sendRequest(outgoingRequests.back());
    }
  }
}

void BiDirectionalRpc::sendRequest(const IdPayload& idPayload) {
  VLOG(1) << "SENDING REQUEST: " << idPayload.id.str();
  MessageWriter writer;
  writer.start();
  set<RpcId> rpcsSent;

  rpcsSent.insert(idPayload.id);
  writer.writePrimitive<unsigned char>(REQUEST);
  writer.writeClass<RpcId>(idPayload.id);
  writer.writePrimitive<string>(idPayload.payload);
  if (!reliable) {
    // Try to attach more requests to this packet
    int i = 0;
    while (!outgoingRequests.empty() &&
           rpcsSent.size() < outgoingRequests.size()) {
      DRAW_FROM_UNORDERED(it, outgoingRequests);
      if (rpcsSent.find(it->id) != rpcsSent.end()) {
        // Drew an rpc that's already in the packet.  Just bail for now, maybe
        // in the future do something more clever.
        break;
      }
      int size = sizeof(RpcId) + it->payload.length();
      if (size + writer.size() > 400) {
        // Too big
        break;
      }
      i++;
      rpcsSent.insert(it->id);
      writer.writeClass<RpcId>(it->id);
      writer.writePrimitive<string>(it->payload);
    }
    VLOG(1) << "Attached " << i << " extra packets";
  }
  send(writer.finish());
}

void BiDirectionalRpc::sendReply(const IdPayload& idPayload) {
  VLOG(1) << "SENDING REPLY: " << idPayload.id.str();
  set<RpcId> rpcsSent;

  rpcsSent.insert(idPayload.id);
  MessageWriter writer;
  writer.start();
  writer.writePrimitive<unsigned char>(REPLY);
  writer.writeClass<RpcId>(idPayload.id);
  writer.writePrimitive<string>(idPayload.payload);
  if (!reliable) {
    // Try to attach more requests to this packet
    int i = 0;
    while (!outgoingReplies.empty() &&
           rpcsSent.size() < outgoingReplies.size()) {
      DRAW_FROM_UNORDERED(it, outgoingReplies);
      if (rpcsSent.find(it->id) != rpcsSent.end()) {
        // Drew an rpc that's already in the packet.  Just bail for now, maybe
        // in the future do something more clever.
        break;
      }
      int size = sizeof(RpcId) + it->payload.length();
      if (size + writer.size() > 400) {
        // Too big
        break;
      }
      i++;
      rpcsSent.insert(it->id);
      writer.writeClass<RpcId>(it->id);
      writer.writePrimitive<string>(it->payload);
    }
    VLOG(1) << "Attached " << i << " extra packets";
  }
  send(writer.finish());
}

void BiDirectionalRpc::sendAcknowledge(const RpcId& uid) {
  MessageWriter writer;
  writer.start();
  writer.writePrimitive<unsigned char>(ACKNOWLEDGE);
  writer.writeClass<RpcId>(uid);
  send(writer.finish());
}

}  // namespace codefs
