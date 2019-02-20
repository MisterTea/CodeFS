#include "Headers.hpp"

#include "BiDirectionalRpc.hpp"
#include "ClientFileSystem.hpp"
#include "MessageReader.hpp"
#include "MessageWriter.hpp"

namespace codefs {
class Client {
 public:
  Client(const string& _address, shared_ptr<ClientFileSystem> _fileSystem);
  int update();
  inline void heartbeat() { rpc->heartbeat(); }

  int open(const string& path, int flags, mode_t mode);
  int open(const string& path, int flags) { return open(path, flags, 0); }
  int close(const string& path);
  int pread(const string& path, char* buf, int size, int offset);
  int pwrite(const string& path, const char* buf, int size, int offset);

  int mkdir(const string& path, mode_t mode);
  int unlink(const string& path);
  int rmdir(const string& path);

  int symlink(const string& from, const string& to);
  int rename(const string& from, const string& to);
  int link(const string& from, const string& to);

  int chmod(const string& path, int mode);
  int lchown(const string& path, int64_t uid, int64_t gid);
  int truncate(const string& path, int64_t size);
  int statvfs(struct statvfs* stbuf);

  int utimensat(const string& path, const struct timespec ts[2]);
  int lremovexattr(const string& path, const string& name);
  int lsetxattr(const string& path, const string& name, const string& value,
                int64_t size, int flags);

 protected:
  string address;
  shared_ptr<BiDirectionalRpc> rpc;
  shared_ptr<ClientFileSystem> fileSystem;
  unordered_map<string, string> ownedFileContents;
  MessageReader reader;
  MessageWriter writer;
  recursive_mutex rpcMutex;
  int fdCounter;
  int twoPathsNoReturn(unsigned char header, const string& from,
                       const string& to);
  int singlePathNoReturn(unsigned char header, const string& path);
  string fileRpc(const string& payload);
};
}  // namespace codefs