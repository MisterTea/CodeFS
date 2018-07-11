#ifndef __MESSAGE_READER_H__
#define __MESSAGE_READER_H__

#include "Headers.hpp"

namespace codefs {
class MessageReader {
 public:
  MessageReader() {}

  inline void load(const string& s) {
    unpackHandler.reserve_buffer(s.size());
    memcpy(unpackHandler.buffer(), s.c_str(), s.length());
    unpackHandler.buffer_consumed(s.length());
  }

  template <typename T>
  inline T readPrimitive() {
    msgpack::object_handle oh;
    FATAL_IF_FALSE(unpackHandler.next(oh));
    T t = oh.get().convert();
    return t;
  }

  template <typename T>
  inline T readProto() {
    T t;
    string s = readPrimitive<string>();
    if (!t.ParseFromString(s)) {
      throw std::runtime_error("Invalid proto");
    }
    return t;
  }

 protected:
  msgpack::unpacker unpackHandler;
};
}  // namespace codefs

#endif  // __MESSAGE_READER_H__