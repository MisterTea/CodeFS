#include "Client.hpp"

#include "FileUtils.hpp"
#include "RawSocketUtils.hpp"

namespace codefs {
Client::Client(const string& _address, shared_ptr<ClientFileSystem> _fileSystem)
    : address(_address), fileSystem(_fileSystem), fdCounter(1) {
  rpc =
      shared_ptr<ZmqBiDirectionalRpc>(new ZmqBiDirectionalRpc(address, false));
  writer.start();
  writer.writePrimitive<unsigned char>(CLIENT_SERVER_INIT);
  RpcId initId = rpc->request(writer.finish());

  while (true) {
    LOG(INFO) << "Waiting for init...";
    rpc->update();
    rpc->heartbeat();
    if (rpc->hasIncomingReplyWithId(initId)) {
      string payload = rpc->consumeIncomingReplyWithId(initId);
      reader.load(payload);
      int numFileData = reader.readPrimitive<int>();
      vector<FileData> allFileData;
      allFileData.reserve(numFileData);
      for (int a = 0; a < numFileData; a++) {
        allFileData.push_back(reader.readProto<FileData>());
      }
      fileSystem->init(allFileData);
      break;
    }
    sleep(1);
  }
}

int Client::update() {
  lock_guard<std::recursive_mutex> lock(rpcMutex);
  rpc->update();

  while (rpc->hasIncomingRequest()) {
    auto idPayload = rpc->getFirstIncomingRequest();
    auto id = idPayload.id;
    string payload = idPayload.payload;
    reader.load(payload);
    unsigned char header = reader.readPrimitive<unsigned char>();
    switch (header) {
      case SERVER_CLIENT_METADATA_UPDATE: {
        string path = reader.readPrimitive<string>();
        LOG(INFO) << "UPDATING PATH: " << path;
        FileData fileData = reader.readProto<FileData>();
        fileSystem->setNode(fileData);
        writer.start();
        writer.writePrimitive(header);
        rpc->reply(id, writer.finish());
      } break;
      default:
        LOG(FATAL) << "Invalid packet header: " << int(header);
    }
  }

  return 0;
}

int Client::open(const string& path, int flags, mode_t mode) {
  fileSystem->invalidatePathAndParent(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_REQUEST_FILE);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<int>(flags);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    reader.load(result);
    int rpcErrno = reader.readPrimitive<int>();
    if (rpcErrno) {
      errno = rpcErrno;
      return -1;
    }
    ownedFileContents.erase(path);
    string fileContents = reader.readPrimitive<string>();
    LOG(INFO) << "READ FILE: " << path << " WITH CONTENTS " << fileContents;
    ownedFileContents.emplace(path, fileContents);
    return fdCounter++;
  }
}
int Client::close(const string& path) {
  fileSystem->invalidatePath(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_RETURN_FILE);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<string>(ownedFileContents.at(path));
    LOG(INFO) << "RETURNED FILE " << path << " TO SERVER WITH "
              << ownedFileContents[path].size() << " BYTES";
    ownedFileContents.erase(path);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    reader.load(result);
    int res = reader.readPrimitive<int>();
    int rpcErrno = reader.readPrimitive<int>();
    if (res) {
      errno = rpcErrno;
      return -1;
    }
    return 0;
  }
}
int Client::pread(const string& path, char* buf, int size, int offset) {
  auto it = ownedFileContents.find(path);
  if (it == ownedFileContents.end()) {
    LOG(FATAL) << "TRIED TO READ AN INVALID PATH";
  }
  if (offset >= int(it->second.size())) {
    return 0;
  }
  auto start = it->second.c_str() + offset;
  int actualSize = min(int(it->second.size()), offset + size) - offset;
  LOG(INFO) << it->second.size() << " " << size << " " << offset << " "
            << actualSize << endl;
  memcpy(buf, start, actualSize);
  return actualSize;
}
int Client::pwrite(const string& path, const char* buf, int size, int offset) {
  auto it = ownedFileContents.find(path);
  if (it == ownedFileContents.end()) {
    LOG(FATAL) << "TRIED TO READ AN INVALID PATH: " << path;
  }
  LOG(INFO) << "WRITING " << size << " TO " << path;
  if (int(it->second.size()) < offset + size) {
    it->second.resize(offset + size, '\0');
  }
  memcpy(&(it->second[offset]), buf, size);
  return size;
}

int Client::mkdir(const string& path, mode_t mode) {
  fileSystem->invalidatePathAndParent(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_MKDIR);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<int>(mode);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    reader.load(result);
    int res = reader.readPrimitive<int>();
    int rpcErrno = reader.readPrimitive<int>();
    if (res) {
      errno = rpcErrno;
    }
    return res;
  }
}

int Client::unlink(const string& path) {
  fileSystem->invalidatePathAndParent(path);
  return singlePathNoReturn(CLIENT_SERVER_UNLINK, path);
}

int Client::rmdir(const string& path) {
  fileSystem->invalidatePathAndParent(path);
  return singlePathNoReturn(CLIENT_SERVER_RMDIR, path);
}

int Client::symlink(const string& from, const string& to) {
  auto relativeFrom = from;
  if (relativeFrom[0] == '/') {
    relativeFrom = fileSystem->absoluteToRelative(relativeFrom);
  }
  fileSystem->invalidatePathAndParent(to);
  return twoPathsNoReturn(CLIENT_SERVER_SYMLINK, relativeFrom, to);
}

int Client::rename(const string& from, const string& to) {
  fileSystem->invalidatePathAndParent(from);
  fileSystem->invalidatePathAndParent(to);
  return twoPathsNoReturn(CLIENT_SERVER_RENAME, from, to);
}

int Client::link(const string& from, const string& to) {
  auto relativeFrom = from;
  if (relativeFrom[0] == '/') {
    relativeFrom = fileSystem->absoluteToRelative(relativeFrom);
  }
  fileSystem->invalidatePathAndParent(to);
  return twoPathsNoReturn(CLIENT_SERVER_LINK, relativeFrom, to);
}

int Client::chmod(const string& path, int mode) {
  fileSystem->invalidatePath(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_CHMOD);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<int>(mode);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    reader.load(result);
    int res = reader.readPrimitive<int>();
    int rpcErrno = reader.readPrimitive<int>();
    if (res) {
      errno = rpcErrno;
    }
    return res;
  }
}
int Client::lchown(const string& path, int64_t uid, int64_t gid) {
  fileSystem->invalidatePath(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_LCHOWN);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<int64_t>(uid);
    writer.writePrimitive<int64_t>(gid);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    reader.load(result);
    int res = reader.readPrimitive<int>();
    int rpcErrno = reader.readPrimitive<int>();
    if (res) {
      errno = rpcErrno;
    }
    return res;
  }
}
int Client::truncate(const string& path, int64_t size) {
  fileSystem->invalidatePath(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_TRUNCATE);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<int64_t>(size);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    reader.load(result);
    int res = reader.readPrimitive<int>();
    int rpcErrno = reader.readPrimitive<int>();
    if (res) {
      errno = rpcErrno;
    }
    return res;
  }
}
int Client::statvfs(struct statvfs* stbuf) {
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_STATVFS);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  LOG(INFO) << "GOT RESULT " << result;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    reader.load(result);
    LOG(INFO) << "READING RES";
    int res = reader.readPrimitive<int>();
    LOG(INFO) << "READING ERRNO";
    int rpcErrno = reader.readPrimitive<int>();
    StatVfsData statVfsProto = reader.readProto<StatVfsData>();
    if (res) {
      errno = rpcErrno;
      return res;
    }
    stbuf->f_bsize = statVfsProto.bsize();
    stbuf->f_frsize = statVfsProto.frsize();
    stbuf->f_blocks = statVfsProto.blocks();
    stbuf->f_bfree = statVfsProto.bfree();
    stbuf->f_bavail = statVfsProto.bavail();
    stbuf->f_files = statVfsProto.files();
    stbuf->f_ffree = statVfsProto.ffree();
    stbuf->f_favail = statVfsProto.favail();
    stbuf->f_fsid = statVfsProto.fsid();
    stbuf->f_flag = statVfsProto.flag();
    stbuf->f_namemax = statVfsProto.namemax();
    return 0;
  }
}

int Client::utimensat(const string& path, const struct timespec ts[2]) {
  fileSystem->invalidatePath(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_UTIMENSAT);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<int64_t>(ts[0].tv_sec);
    writer.writePrimitive<int64_t>(ts[0].tv_nsec);
    writer.writePrimitive<int64_t>(ts[1].tv_sec);
    writer.writePrimitive<int64_t>(ts[1].tv_nsec);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    reader.load(result);
    int res = reader.readPrimitive<int>();
    int rpcErrno = reader.readPrimitive<int>();
    if (res) {
      errno = rpcErrno;
    }
    return res;
  }
}
int Client::lremovexattr(const string& path, const string& name) {
  fileSystem->invalidatePath(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_LREMOVEXATTR);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<string>(name);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    reader.load(result);
    int res = reader.readPrimitive<int>();
    int rpcErrno = reader.readPrimitive<int>();
    if (res) {
      errno = rpcErrno;
    }
    return res;
  }
}
int Client::lsetxattr(const string& path, const string& name,
                      const string& value, int64_t size, int flags) {
  fileSystem->invalidatePath(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_LSETXATTR);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<string>(name);
    writer.writePrimitive<string>(value);
    writer.writePrimitive<int64_t>(size);
    writer.writePrimitive<int>(flags);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    reader.load(result);
    int res = reader.readPrimitive<int>();
    int rpcErrno = reader.readPrimitive<int>();
    if (res) {
      errno = rpcErrno;
    }
    return res;
  }
}

int Client::twoPathsNoReturn(unsigned char header, const string& from,
                             const string& to) {
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(header);
    writer.writePrimitive<string>(from);
    writer.writePrimitive<string>(to);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    reader.load(result);
    int res = reader.readPrimitive<int>();
    int rpcErrno = reader.readPrimitive<int>();
    if (res) {
      errno = rpcErrno;
    }
    return res;
  }
}

int Client::singlePathNoReturn(unsigned char header, const string& path) {
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(header);
    writer.writePrimitive<string>(path);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    reader.load(result);
    int res = reader.readPrimitive<int>();
    int rpcErrno = reader.readPrimitive<int>();
    if (res) {
      errno = rpcErrno;
    }
    return res;
  }
}

string Client::fileRpc(const string& payload) {
  RpcId id;
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    id = rpc->request(payload);
  }
  while (true) {
    usleep(100);
    {
      lock_guard<std::recursive_mutex> lock(rpcMutex);
      if (rpc->hasIncomingReplyWithId(id)) {
        return rpc->consumeIncomingReplyWithId(id);
      }
    }
  }
}

}  // namespace codefs