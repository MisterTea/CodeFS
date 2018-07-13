#ifndef __MESSAGE_WRITER_H__
#define __MESSAGE_WRITER_H__

#include "Headers.hpp"

namespace codefs {
class MessageWriter {
 public:
  MessageWriter() : packHandler(buffer) {}

  inline void start() {
    buffer.clear();
  }

  template <typename T>
  inline void writePrimitive(const T& t) {
    packHandler.pack(t);
  }

  template <typename T>
  inline void writeProto(const T& t) {
    string s;
    t.SerializeToString(&s);
    writePrimitive<string>(s);
  }

  inline string finish() {
    string s(buffer.data(), buffer.size());
    start();
    return s;
  }

 protected:
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> packHandler;
};
}  // namespace codefs

#endif  // __MESSAGE_WRITER_H__