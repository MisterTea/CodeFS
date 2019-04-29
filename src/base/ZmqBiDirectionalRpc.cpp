#include "ZmqBiDirectionalRpc.hpp"

namespace codefs {
ZmqBiDirectionalRpc::ZmqBiDirectionalRpc(const string& _address, bool _bind)
    : BiDirectionalRpc(true), address(_address), bind(_bind) {
  context = shared_ptr<zmq::context_t>(new zmq::context_t(4, 16));
  socket =
      shared_ptr<zmq::socket_t>(new zmq::socket_t(*(context.get()), ZMQ_PAIR));
  socket->setsockopt(ZMQ_LINGER, 3000);
  if (bind) {
    LOG(INFO) << "Binding on address: " << address;
    socket->bind(address);
  } else {
    LOG(INFO) << "Connecting to address: " << address;
    socket->connect(address);
  }
  LOG(INFO) << "Done";
}

ZmqBiDirectionalRpc::~ZmqBiDirectionalRpc() {
  if (context.get() || socket.get()) {
    LOG(FATAL) << "Tried to destroy an RPC instance without calling shutdown";
  }
}

void ZmqBiDirectionalRpc::shutdown() {
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

void ZmqBiDirectionalRpc::update() {
  while (true) {
    zmq::message_t message;
    bool result = socket->recv(&message, ZMQ_DONTWAIT);
    FATAL_IF_FALSE_NOT_EAGAIN(result);
    if (message.more()) {
      LOG(FATAL) << "DID NOT GET ALL";
    }
    if (!result) {
      // Nothing to recieve
      return;
    }

    VLOG(1) << "Got message with size " << message.size() << endl;
    string s(message.data<char>(), message.size());
    BiDirectionalRpc::receive(s);
  }
}

void ZmqBiDirectionalRpc::reconnect() {
  // Skip reconnect
  return;

  shutdown();

  context = shared_ptr<zmq::context_t>(new zmq::context_t(4));
  socket.reset(new zmq::socket_t(*(context.get()), ZMQ_PAIR));
  socket->setsockopt(ZMQ_LINGER, 3000);
  if (bind) {
    LOG(INFO) << "Binding on address: " << address;
    socket->bind(address);
  } else {
    socket->connect(address);
  }
}

void ZmqBiDirectionalRpc::send(const string& message) {
  VLOG(1) << "SENDING " << message.length();
  if (message.length() == 0) {
    LOG(FATAL) << "Invalid message size";
  }
  zmq::message_t zmqMessage(message.c_str(), message.length());
  auto startSendTime = time(NULL);
  while (true) {
    bool retval = socket->send(zmqMessage);  //, ZMQ_DONTWAIT);
    if (retval) {
      break;
    }
    FATAL_IF_FALSE_NOT_EAGAIN(retval);
    usleep(100 * 1000);
    if (startSendTime + 5 < time(NULL)) {
      reconnect();
      startSendTime = time(NULL);
    }
  }
}
}  // namespace codefs
