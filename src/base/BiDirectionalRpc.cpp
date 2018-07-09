#include "BiDirectionalRpc.hpp"

namespace codefs {
BiDirectionalRpc::BiDirectionalRpc(const string& address, bool bind)
    : onBarrier(0), onId(0), flaky(false) {
  dist = std::uniform_int_distribution<uint64_t>(0, UINT64_MAX);
  context = shared_ptr<zmq::context_t>(new zmq::context_t(4));
  socket = shared_ptr<zmq::socket_t>(
      new zmq::socket_t(*(context.get()), ZMQ_DEALER));
  socket->setsockopt(ZMQ_LINGER, 3000);
  if (bind) {
    LOG(INFO) << "Binding on address: " << address;
    socket->bind(address);
  } else {
    socket->connect(address);
  }
}

BiDirectionalRpc::~BiDirectionalRpc() {
  if (context.get() || socket.get()) {
    LOG(FATAL) << "Tried to destroy an RPC instance without calling shutdown";
  }
}

void BiDirectionalRpc::shutdown() {
  LOG(INFO) << "CLOSING SOCKET";
  socket->close();
  LOG(INFO) << "KILLING SOCKET";
  socket.reset();
  LOG(INFO) << "CLOSING CONTEXT";
  context->close();
  LOG(INFO) << "KILLING CONTEXT";
  context.reset();
  LOG(INFO) << "SHUTDOWN COMPLETE";
}

void BiDirectionalRpc::heartbeat() {
  if (!outgoingReplies.empty() &&
      (outgoingRequests.empty() || rand() % 2 == 0)) {
    // Re-send a random reply
    auto it = outgoingReplies.begin();
    std::advance(it, rand() % outgoingReplies.size());
    sendReply(*it);
  } else if (!outgoingRequests.empty()) {
    // Re-send a random request
    auto it = outgoingRequests.begin();
    std::advance(it, rand() % outgoingRequests.size());
    sendRequest(*it);
  } else {
    zmq::message_t message(1);
    message.data<char>()[0] = HEARTBEAT;
    FATAL_IF_FALSE_NOT_EAGAIN(socket->send(message, ZMQ_NOBLOCK));
  }
}

void BiDirectionalRpc::update() {
  zmq::message_t message;
  bool result = socket->recv(&message, ZMQ_NOBLOCK);
  FATAL_IF_FALSE_NOT_EAGAIN(result);
  if (!result) {
    // Nothing to recieve
    return;
  }

  RpcHeader header = (RpcHeader)(message.data<char>()[0]);
  if (flaky && rand() % 2 == 0) {
    // Pretend we never got the message
    return;
  }
  LOG(INFO) << "GOT PACKET WITH HEADER " << header;
  switch (header) {
    case HEARTBEAT: {
      // TODO: Update keepalive time
    } break;
    case REQUEST: {
      RpcId uid;
      memcpy(&uid, message.data<char>() + 1, sizeof(RpcId));
      LOG(INFO) << "GOT REQUEST: " << uid.str();

      bool skip = false;
      for (const IdPayload& it : incomingRequests) {
        if (it.id == uid) {
          // We already received this request.  Skip
          skip = true;
          break;
        }
      }
      if (!skip) {
        for (const IdPayload& it : outgoingReplies) {
          if (it.id == uid) {
            // We already processed this request.  Send the reply again
            skip = true;
            sendReply(it);
            break;
          }
        }
      }
      if (!skip) {
        string payload(message.size() - 1 - sizeof(RpcId), '\0');
        memcpy(&(payload[0]), message.data<char>() + 1 + sizeof(RpcId),
               payload.length());
        incomingRequests.push_back(IdPayload(uid, payload));
      }
    } break;
    case REPLY: {
      RpcId uid;
      memcpy(&uid, message.data<char>() + 1, sizeof(RpcId));

      bool skip = false;
      if (incomingReplies.find(uid) != incomingReplies.end()) {
        // We already received this reply.  Send acknowledge again and skip.
        sendAcknowledge(uid);
        skip = true;
      }
      if (!skip) {
        // Stop sending the request once you get the reply
        bool deletedRequest = false;
        for (auto it = outgoingRequests.begin(); it != outgoingRequests.end();
             it++) {
          if (it->id == uid) {
            outgoingRequests.erase(it);
            deletedRequest = true;
            tryToSendBarrier();
            break;
          }
        }
        if (deletedRequest) {
          string payload(message.size() - 1 - sizeof(RpcId), '\0');
          memcpy(&(payload[0]), message.data<char>() + 1 + sizeof(RpcId),
                 payload.length());
          incomingReplies.emplace(uid, payload);
          sendAcknowledge(uid);
        } else {
          // We must have processed both this request and reply.  Send the
          // acknowledge again.
          sendAcknowledge(uid);
        }
      }
    } break;
    case ACKNOWLEDGE: {
      RpcId uid;
      memcpy(&uid, message.data<char>() + 1, sizeof(RpcId));
      for (auto it = outgoingReplies.begin(); it != outgoingReplies.end();
           it++) {
        if (it->id == uid) {
          outgoingReplies.erase(it);
          break;
        }
      }
    } break;
  }
}

RpcId BiDirectionalRpc::request(const string& payload) {
  auto uuid = RpcId(onBarrier, onId++);
  auto idPayload = IdPayload(uuid, payload);
  if (outgoingRequests.empty() || outgoingRequests.front().id.barrier == onBarrier) {
    // We can send the request immediately
    outgoingRequests.push_back(idPayload);
    sendRequest(outgoingRequests.back());
  } else {
    // We have to wait for existing requests from an older barrier
    delayedRequests.push_back(idPayload);
  }
  return uuid;
}

void BiDirectionalRpc::reply(const RpcId& rpcId, const string& payload) {
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
    delayedRequests.pop_front();
    sendRequest(outgoingRequests.back());

    while (!delayedRequests.empty() && delayedRequests.front().id.barrier == outgoingRequests.front().id.barrier) {
      // Part of the same barrier, keep sending
      outgoingRequests.push_back(delayedRequests.front());
      delayedRequests.pop_front();
      sendRequest(outgoingRequests.back());
    }
  }
}

void BiDirectionalRpc::sendRequest(const IdPayload& idPayload) {
  LOG(INFO) << "SENDING REQUEST: " << idPayload.id.str() << " "
            << idPayload.payload;
  zmq::message_t message(1 + sizeof(RpcId) + idPayload.payload.length());
  message.data<char>()[0] = REQUEST;
  memcpy(message.data<char>() + 1, &(idPayload.id), sizeof(RpcId));
  memcpy(message.data<char>() + 1 + sizeof(RpcId), idPayload.payload.c_str(),
         idPayload.payload.length());
  FATAL_IF_FALSE_NOT_EAGAIN(socket->send(message, ZMQ_NOBLOCK));
}

void BiDirectionalRpc::sendReply(const IdPayload& idPayload) {
  zmq::message_t message(1 + sizeof(RpcId) + idPayload.payload.length());
  message.data<char>()[0] = REPLY;
  memcpy(message.data<char>() + 1, &(idPayload.id), sizeof(RpcId));
  memcpy(message.data<char>() + 1 + sizeof(RpcId), idPayload.payload.c_str(),
         idPayload.payload.length());
  FATAL_IF_FALSE_NOT_EAGAIN(socket->send(message, ZMQ_NOBLOCK));
}

void BiDirectionalRpc::sendAcknowledge(const RpcId& uid) {
  zmq::message_t message(1 + sizeof(RpcId));
  message.data<char>()[0] = ACKNOWLEDGE;
  memcpy(message.data<char>() + 1, &uid, sizeof(RpcId));
  FATAL_IF_FALSE_NOT_EAGAIN(socket->send(message, ZMQ_NOBLOCK));
}
}  // namespace codefs
