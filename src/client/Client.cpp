#include "Client.hpp"

#include "FileUtils.hpp"

namespace codefs {
Client::Client(const string& _address, shared_ptr<ClientFileSystem> _fileSystem)
    : address(_address), fileSystem(_fileSystem) {
  MessageReader reader;
  MessageWriter writer;
  rpc =
      shared_ptr<ZmqBiDirectionalRpc>(new ZmqBiDirectionalRpc(address, false));
  writer.start();
  writer.writePrimitive<unsigned char>(CLIENT_SERVER_FETCH_METADATA);
  writer.writePrimitive<int>(1);
  writer.writePrimitive<string>(string("/"));
  RpcId initId = rpc->request(writer.finish());

  while (true) {
    LOG(INFO) << "Waiting for init...";
    {
      lock_guard<std::recursive_mutex> lock(mutex);
      rpc->update();
      rpc->heartbeat();
      if (rpc->hasIncomingReplyWithId(initId)) {
        string payload = rpc->consumeIncomingReplyWithId(initId);
        reader.load(payload);
        auto path = reader.readPrimitive<string>();
        auto data = reader.readPrimitive<string>();
        fileSystem->deserializeFileDataCompressed(path, data);
        break;
      }
    }
    sleep(1);
  }
}

int Client::update() {
  MessageReader reader;
  MessageWriter writer;
  lock_guard<std::recursive_mutex> lock(mutex);
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
        fileSystem->invalidateVfsCache();
        FileData fileData = reader.readProto<FileData>();
        if (fileData.invalid()) {
          LOGFATAL << "Got filedata with invalid set";
        }
        if (path != fileData.path()) {
          LOGFATAL << "PATH MISMATCH: " << path << " != " << fileData.path();
        }
        vector<string> pathsToDownload = fileSystem->getPathsToDownload(path);
        if (pathsToDownload.empty()) {
          // Only update this node if we already have it in cache
          fileSystem->setNode(fileData);
        }
        writer.start();
        writer.writePrimitive(header);
        rpc->reply(id, writer.finish());
      } break;
      default:
        LOGFATAL << "Invalid packet header: " << int(header);
    }
  }

  return 0;
}

vector<optional<FileData>> Client::getNodes(const vector<string>& paths) {
  vector<RpcId> rpcIds;
  string payload;
  vector<string> metadataToFetch;
  for (auto path : paths) {
    vector<string> pathsToDownload = fileSystem->getPathsToDownload(path);
    VLOG(1) << "Number of paths: " << pathsToDownload.size();
    for (auto it : pathsToDownload) {
      LOG(INFO) << "GETTING SCAN FOR PATH: " << it;
      metadataToFetch.push_back(it);
    }
  }

  if (!metadataToFetch.empty()) {
    RpcId id;
    {
      lock_guard<std::recursive_mutex> lock(mutex);
      MessageWriter writer;
      writer.start();
      writer.writePrimitive<unsigned char>(CLIENT_SERVER_FETCH_METADATA);
      writer.writePrimitive<int>(metadataToFetch.size());
      for (auto s : metadataToFetch) {
        writer.writePrimitive<string>(s);
      }
      payload = writer.finish();
      id = rpc->request(payload);
    }
    rpcIds.push_back(id);
    string result;
    while (true) {
      usleep(1);
      {
        lock_guard<std::recursive_mutex> lock(mutex);
        if (rpc->hasIncomingReplyWithId(id)) {
          result = rpc->consumeIncomingReplyWithId(id);
          break;
        }
      }
    }
    {
      lock_guard<std::recursive_mutex> lock(mutex);
      MessageReader reader;
      reader.load(result);
      while (reader.sizeRemaining()) {
        auto path = reader.readPrimitive<string>();
        auto data = reader.readPrimitive<string>();
        fileSystem->deserializeFileDataCompressed(path, data);
      }
    }
  }

  vector<optional<FileData>> retval;
  for (const auto& path : paths) {
    for (int waitTicks = 0;; waitTicks++) {
      auto node = fileSystem->getNode(path);
      if (node) {
        bool invalid = node->invalid();
        if (!invalid) {
          retval.push_back(node);
          break;
        } else {
          LOG(INFO) << path
                    << " is invalid, waiting for new version: " << waitTicks;
          if (((waitTicks + 1) % 10) == 0) {
            LOG(ERROR) << path
                       << " is invalid for too long, demanding new version "
                          "from server";
            string payload;
            {
              lock_guard<std::recursive_mutex> lock(mutex);
              MessageWriter writer;
              writer.start();
              writer.writePrimitive<unsigned char>(
                  CLIENT_SERVER_FETCH_METADATA);
              writer.writePrimitive<int>(1);
              writer.writePrimitive<string>(path);
              payload = writer.finish();
            }
            string result = fileRpc(payload);
            {
              lock_guard<std::recursive_mutex> lock(mutex);
              MessageReader reader;
              reader.load(result);
              auto path = reader.readPrimitive<string>();
              auto data = reader.readPrimitive<string>();
              fileSystem->deserializeFileDataCompressed(path, data);
            }
          } else {
            usleep(100 * 1000);
          }
        }
      } else {
        retval.push_back(node);
        break;
      }
    }
  }
  return retval;
}

optional<FileData> Client::getNodeAndChildren(const string& path,
                                              vector<FileData>* children) {
  auto parentNode = getNode(path);
  vector<string> childrenPaths;
  if (parentNode) {
    // Check the children
    VLOG(1) << "NUM CHILDREN: " << parentNode->child_node_size();
    children->clear();
    for (int a = 0; a < parentNode->child_node_size(); a++) {
      string fileName = parentNode->child_node(a);
      string childPath = (boost::filesystem::path(parentNode->path()) /
                          boost::filesystem::path(fileName))
                             .string();
      childrenPaths.push_back(childPath);
    }

    if (!childrenPaths.empty()) {
      auto childNodes = getNodes(childrenPaths);
      for (auto childNode : childNodes) {
        if (childNode) {
          children->push_back(*childNode);
        }
      }
    }
  }
  return parentNode;
}

int Client::open(const string& path, int flags) {
  MessageReader reader;
  MessageWriter writer;
  int readWriteMode = (flags & O_ACCMODE);
  bool readOnly = (readWriteMode == O_RDONLY);
  LOG(INFO) << "Reading file " << path << " (readonly? " << readOnly << ")";
  int fd = fileSystem->getNewFd();

  if (!fileSystem->ownsPathContents(path)) {
    if (!readOnly) {
      fileSystem->invalidatePathAndParent(path);
    }

    auto cachedData = fileSystem->getCachedFile(path);
    if (readOnly && cachedData) {
      LOG(INFO) << "FETCHING FROM FILE CACHE: " << cachedData->size();
      fileSystem->addOwnedFileContents(path, fd, *cachedData, readOnly);
    } else {
      string payload;
      {
        lock_guard<std::recursive_mutex> lock(mutex);
        fileSystem->invalidateVfsCache();
        writer.start();
        writer.writePrimitive<unsigned char>(CLIENT_SERVER_REQUEST_FILE);
        writer.writePrimitive<string>(path);
        writer.writePrimitive<int>(flags);
        payload = writer.finish();
      }
      string result = fileRpc(payload);
      {
        lock_guard<std::recursive_mutex> lock(mutex);
        reader.load(result);
        int rpcErrno = reader.readPrimitive<int>();
        if (rpcErrno) {
          errno = rpcErrno;
          return -1;
        }
        string fileContents = decompressString(reader.readPrimitive<string>());
        LOG(INFO) << "READ FILE: " << path << " WITH CONTENTS SIZE "
                  << fileContents.size();
        fileSystem->addOwnedFileContents(path, fd, fileContents, readOnly);
      }
    }
  } else {
    LOG(INFO) << "FILE IS ALREADY IN LOCAL CACHE, SKIPPING READ";
    fileSystem->addHandleToOwnedFile(path, fd, readOnly);
  }
  return fd;
}

int Client::create(const string& path, int flags, mode_t mode) {
  MessageReader reader;
  MessageWriter writer;
  int readWriteMode = (flags & O_ACCMODE);
  bool readOnly = (readWriteMode == O_RDONLY);
  LOG(INFO) << "Creating file " << path << " (readonly? " << readOnly << ")";
  int fd = fileSystem->getNewFd();

  if (!fileSystem->ownsPathContents(path)) {
    if (!readOnly) {
      fileSystem->invalidatePathAndParent(path);
    }

    auto cachedData = fileSystem->getCachedFile(path);
    if (cachedData) {
      LOGFATAL << "TRIED TO CREATE A FILE THAT IS CACHED";
    } else {
      string payload;
      {
        lock_guard<std::recursive_mutex> lock(mutex);
        fileSystem->invalidateVfsCache();
        writer.start();
        writer.writePrimitive<unsigned char>(CLIENT_SERVER_CREATE_FILE);
        writer.writePrimitive<string>(path);
        writer.writePrimitive<int>(flags);
        writer.writePrimitive<int>(mode);
        payload = writer.finish();
      }
      // Create an invalid node until we get the real one
      fileSystem->createStub(path);
      string result = fileRpc(payload);
      {
        lock_guard<std::recursive_mutex> lock(mutex);
        reader.load(result);
        int rpcErrno = reader.readPrimitive<int>();
        if (rpcErrno) {
          errno = rpcErrno;
          fileSystem->deleteNode(path);
          return -1;
        }
        LOG(INFO) << "CREATED FILE: " << path;
        fileSystem->addOwnedFileContents(path, fd, "", readOnly);
      }
    }
  } else {
    LOGFATAL << "Tried to create a file that is already owned!";
  }

  return fd;
}

int Client::close(const string& path, int fd) {
  MessageReader reader;
  MessageWriter writer;

  bool readOnly;
  string content;
  fileSystem->closeOwnedFile(path, fd, &readOnly, &content);

  string payload;
  {
    lock_guard<std::recursive_mutex> lock(mutex);
    fileSystem->invalidateVfsCache();
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_RETURN_FILE);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<bool>(readOnly);
    fileSystem->setCachedFile(path, content);
    if (readOnly) {
      LOG(INFO) << "RETURNED FILE " << path << " TO SERVER READ-ONLY";
    } else {
      writer.writePrimitive<string>(compressString(content));
      LOG(INFO) << "RETURNED FILE " << path << " TO SERVER WITH "
                << content.size() << " BYTES";
    }
    payload = writer.finish();
  }

  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(mutex);
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

int Client::pread(const string& path, char* buf, int64_t size, int64_t offset) {
  return fileSystem->readOwnedFile(path, buf, size, offset);
}

int Client::pwrite(const string& path, const char* buf, int64_t size,
                   int64_t offset) {
  return fileSystem->writeOwnedFile(path, buf, size, offset);
}

int Client::mkdir(const string& path, mode_t mode) {
  MessageReader reader;
  MessageWriter writer;
  fileSystem->invalidatePathAndParent(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(mutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_MKDIR);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<int>(mode);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(mutex);
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
  fileSystem->invalidatePathAndParent(from);
  fileSystem->invalidatePathAndParent(to);
  return twoPathsNoReturn(CLIENT_SERVER_SYMLINK, from, to);
}

int Client::rename(const string& from, const string& to) {
  fileSystem->invalidateVfsCache();
  LOG(INFO) << "RENAMING FROM " << from << " TO " << to;
  fileSystem->renameOwnedFileIfItExists(from, to);
  fileSystem->invalidatePathAndParentAndChildren(from);
  fileSystem->invalidatePathAndParentAndChildren(to);
  return twoPathsNoReturn(CLIENT_SERVER_RENAME, from, to);
}

int Client::link(const string& from, const string& to) {
  fileSystem->invalidatePathAndParent(from);
  fileSystem->invalidatePathAndParent(to);
  return twoPathsNoReturn(CLIENT_SERVER_LINK, from, to);
}

int Client::chmod(const string& path, int mode) {
  MessageReader reader;
  MessageWriter writer;
  fileSystem->invalidatePath(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(mutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_CHMOD);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<int>(mode);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(mutex);
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
  MessageReader reader;
  MessageWriter writer;
  fileSystem->invalidatePath(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(mutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_LCHOWN);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<int64_t>(uid);
    writer.writePrimitive<int64_t>(gid);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(mutex);
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
  MessageReader reader;
  MessageWriter writer;
  fileSystem->invalidateVfsCache();
  if (fileSystem->truncateOwnedFileIfExists(path, size)) {
    return 0;
  }

  fileSystem->invalidatePath(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(mutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_TRUNCATE);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<int64_t>(size);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(mutex);
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
  StatVfsData statVfsProto;
  MessageReader reader;
  MessageWriter writer;

  auto cachedVfs = fileSystem->getVfsCache();

  if (cachedVfs) {
    statVfsProto = *cachedVfs;
  } else {
    string payload;
    {
      lock_guard<std::recursive_mutex> lock(mutex);
      writer.start();
      writer.writePrimitive<unsigned char>(CLIENT_SERVER_STATVFS);
      payload = writer.finish();
    }
    string result = fileRpc(payload);
    {
      lock_guard<std::recursive_mutex> lock(mutex);
      reader.load(result);
      int res = reader.readPrimitive<int>();
      int rpcErrno = reader.readPrimitive<int>();
      statVfsProto = reader.readProto<StatVfsData>();
      if (res) {
        errno = rpcErrno;
        return res;
      }
      fileSystem->setVfsCache(statVfsProto);
    }
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

int Client::utimensat(const string& path, const struct timespec ts[2]) {
  MessageReader reader;
  MessageWriter writer;
  fileSystem->invalidatePath(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(mutex);
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
    lock_guard<std::recursive_mutex> lock(mutex);
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
  MessageReader reader;
  MessageWriter writer;
  fileSystem->invalidatePath(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(mutex);
    writer.start();
    writer.writePrimitive<unsigned char>(CLIENT_SERVER_LREMOVEXATTR);
    writer.writePrimitive<string>(path);
    writer.writePrimitive<string>(name);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(mutex);
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
  MessageReader reader;
  MessageWriter writer;
  fileSystem->invalidatePath(path);
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(mutex);
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
    lock_guard<std::recursive_mutex> lock(mutex);
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
  MessageReader reader;
  MessageWriter writer;
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(mutex);
    writer.start();
    writer.writePrimitive<unsigned char>(header);
    writer.writePrimitive<string>(from);
    writer.writePrimitive<string>(to);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(mutex);
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
  MessageReader reader;
  MessageWriter writer;
  string payload;
  {
    lock_guard<std::recursive_mutex> lock(mutex);
    writer.start();
    writer.writePrimitive<unsigned char>(header);
    writer.writePrimitive<string>(path);
    payload = writer.finish();
  }
  string result = fileRpc(payload);
  {
    lock_guard<std::recursive_mutex> lock(mutex);
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
    lock_guard<std::recursive_mutex> lock(mutex);
    id = rpc->request(payload);
  }
  while (true) {
    usleep(1);
    {
      lock_guard<std::recursive_mutex> lock(mutex);
      if (rpc->hasIncomingReplyWithId(id)) {
        return rpc->consumeIncomingReplyWithId(id);
      }
    }
  }
}

}  // namespace codefs