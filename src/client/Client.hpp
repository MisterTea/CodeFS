#include "Headers.hpp"

#include "ClientFileSystem.hpp"
#include "MessageReader.hpp"
#include "MessageWriter.hpp"
#include "ZmqBiDirectionalRpc.hpp"

namespace codefs {
struct OwnedFileInfo {
  unordered_set<int> fds;
  string content;
  bool readOnly;

  OwnedFileInfo(int fd, string _content, bool _readOnly)
      : content(_content), readOnly(_readOnly) {
    fds.insert(fd);
  }
};

class Client {
 public:
  Client(const string& _address, shared_ptr<ClientFileSystem> _fileSystem);
  int update();
  inline void heartbeat() {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    rpc->heartbeat();
  }

  optional<FileData> getNode(const string& path) { return getNodes({path})[0]; }
  vector<optional<FileData>> getNodes(const vector<string>& paths);
  optional<FileData> getNodeAndChildren(const string& path,
                                        vector<FileData>* children);
  inline bool hasDirectory(const string& path) {
    auto fileData = getNode(path);
    return fileData && S_ISDIR(fileData->stat_data().mode());
  }

  int open(const string& path, int flags);
  int create(const string& path, int flags, mode_t mode);
  int close(const string& path, int fd);
  int pread(const string& path, char* buf, int64_t size, int64_t offset);
  int pwrite(const string& path, const char* buf, int64_t size, int64_t offset);

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

  optional<int64_t> getSizeOverride(const string& path) {
    auto it = ownedFileContents.find(path);
    if (it == ownedFileContents.end()) {
      return optional<int64_t>();
    }
    return int64_t(it->second.content.size());
  }

 protected:
  string address;
  shared_ptr<ZmqBiDirectionalRpc> rpc;
  shared_ptr<ClientFileSystem> fileSystem;
  unordered_map<string, OwnedFileInfo> ownedFileContents;
  recursive_mutex rpcMutex;
  optional<StatVfsData> cachedStatVfsProto;
  int fdCounter;
  int twoPathsNoReturn(unsigned char header, const string& from,
                       const string& to);
  int singlePathNoReturn(unsigned char header, const string& path);
  string fileRpc(const string& payload);
};
}  // namespace codefs