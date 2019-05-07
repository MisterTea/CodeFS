#ifndef __RPC_ID_H__
#define __RPC_ID_H__

#include "Headers.hpp"

namespace codefs {
class RpcId {
 public:
  RpcId() : barrier(0), id(0) {}
  RpcId(int64_t _barrier, uint64_t _id) : barrier(_barrier), id(_id) {}
  bool operator==(const RpcId& other) const {
    return barrier == other.barrier && id == other.id;
  }
  bool operator<(const RpcId& other) const {
    return barrier < other.barrier ||
           (barrier == other.barrier && id < other.id);
  }
  string str() const { return to_string(barrier) + "/" + to_string(id); }
  bool empty() { return barrier == 0 && id == 0; }

  int64_t barrier;
  uint64_t id;
};

}  // namespace codefs

#endif