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

  int mkdir(const string& path);
  int unlink(const string& path);
  int rmdir(const string& path);

  int symlink(const string& from, const string& to);
  int rename(const string& from, const string& to);
  int link(const string& from, const string& to);

 protected:
  string address;
  shared_ptr<BiDirectionalRpc> rpc;
  shared_ptr<ClientFileSystem> fileSystem;
  unordered_map<string, FileData> fsData;
  MessageReader reader;
  MessageWriter writer;
  mutex rpcMutex;
  int twoPathsNoReturn(unsigned char header, const string& from,
                       const string& to);
  int singlePathNoReturn(unsigned char header, const string& path);
  string fileRpc(const string& payload);
};
}  // namespace codefs