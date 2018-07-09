#include "BiDirectionalRpc.hpp"

namespace codefs {
BiDirectionalRpc::BiDirectionalRpc(Handler* _handler, const string& address,
                                   bool bind)
    : handler(_handler) {
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
    sendReply(it->first);
  } else if (!outgoingRequests.empty()) {
    // Re-send a random request
    auto it = outgoingRequests.begin();
    std::advance(it, rand() % outgoingRequests.size());
    sendRequest(it->first);
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
  LOG(INFO) << "GOT PACKET WITH HEADER " << header;
  switch (header) {
    case HEARTBEAT: {
      // TODO: Update keepalive time
    } break;
    case REQUEST: {
      sole::uuid uid;
      memcpy(&uid, message.data<char>() + 1, sizeof(sole::uuid));
      LOG(INFO) << "GOT REQUEST: " << uid.str();

      if (outgoingReplies.find(uid) != outgoingReplies.end()) {
        // We already processed this request.  Send the reply again
        sendReply(uid);
      } else {
        string payload(message.size() - 1 - sizeof(sole::uuid), '\0');
        memcpy(&(payload[0]), message.data<char>() + 1 + sizeof(sole::uuid),
               payload.length());
        auto result = handler->request(payload);
        if (result.first) {
          outgoingReplies.emplace(uid, result.second);
          sendReply(uid);
        } else {
          // Drop it on the floor, the request will come back again later
        }
      }
    } break;
    case REPLY: {
      sole::uuid uid;
      memcpy(&uid, message.data<char>() + 1, sizeof(sole::uuid));

      if (outgoingRequests.find(uid) == outgoingRequests.end()) {
        // We already processed this reply.  Just send the acknowledge again
        sendAcknowledge(uid);
      } else {
        string payload(message.size() - 1 - sizeof(sole::uuid), '\0');
        memcpy(&(payload[0]), message.data<char>() + 1 + sizeof(sole::uuid),
               payload.length());
        incomingReplies.emplace(uid, payload);
        outgoingRequests.erase(outgoingRequests.find(uid));
        sendAcknowledge(uid);
      }
    } break;
    case ACKNOWLEDGE: {
      sole::uuid uid;
      memcpy(&uid, message.data<char>() + 1, sizeof(sole::uuid));
      auto it = outgoingReplies.find(uid);
      if (it == outgoingReplies.end()) {
        // We already processed this acknowledge.
      } else {
        // This RPC is finished
        outgoingReplies.erase(it);
      }
    } break;
  }
}

sole::uuid BiDirectionalRpc::request(const string& payload) {
  auto uuid = sole::uuid4();
  outgoingRequests.emplace(uuid, payload);
  sendRequest(uuid);
  return uuid;
}

void BiDirectionalRpc::sendRequest(const sole::uuid& uid) {
  auto it = outgoingRequests.find(uid);
  if (it == outgoingRequests.end()) {
    LOG(FATAL) << "Tried to send a request that didn't exist!";
  }
  LOG(INFO) << "SENDING REQUEST: " << uid.str() << " " << it->second;
  zmq::message_t message(1 + sizeof(sole::uuid) + it->second.length());
  message.data<char>()[0] = REQUEST;
  memcpy(message.data<char>() + 1, &uid, sizeof(sole::uuid));
  memcpy(message.data<char>() + 1 + sizeof(sole::uuid), it->second.c_str(),
         it->second.length());
  FATAL_IF_FALSE_NOT_EAGAIN(socket->send(message, ZMQ_NOBLOCK));
}

void BiDirectionalRpc::sendReply(const sole::uuid& uid) {
  auto it = outgoingReplies.find(uid);
  if (it == outgoingReplies.end()) {
    LOG(FATAL) << "Tried to send a reply that didn't exist!";
  }
  zmq::message_t message(1 + sizeof(sole::uuid) + it->second.length());
  message.data<char>()[0] = REPLY;
  memcpy(message.data<char>() + 1, &uid, sizeof(sole::uuid));
  memcpy(message.data<char>() + 1 + sizeof(sole::uuid), it->second.c_str(),
         it->second.length());
  FATAL_IF_FALSE_NOT_EAGAIN(socket->send(message, ZMQ_NOBLOCK));
}

void BiDirectionalRpc::sendAcknowledge(const sole::uuid& uid) {
  zmq::message_t message(1 + sizeof(sole::uuid));
  message.data<char>()[0] = ACKNOWLEDGE;
  memcpy(message.data<char>() + 1, &uid, sizeof(sole::uuid));
  FATAL_IF_FALSE_NOT_EAGAIN(socket->send(message, ZMQ_NOBLOCK));
}
}  // namespace codefs
