#include "Server.hpp"

#include "RawSocketUtils.hpp"

namespace codefs {
Server::Server(const string &_address, shared_ptr<ServerFileSystem> _fileSystem)
    : address(_address), fileSystem(_fileSystem), clientFd(-1) {}

void Server::init() {
  lock_guard<mutex> lock(rpcMutex);
  rpc = shared_ptr<BiDirectionalRpc>(new BiDirectionalRpc(address, true));
}

int Server::update() {
  {
    lock_guard<mutex> lock(rpcMutex);
    rpc->update();
  }

  while (true) {
    RpcId id;
    string payload;
    {
      lock_guard<mutex> lock(rpcMutex);
      if (!rpc->hasIncomingRequest()) {
        break;
      }
      auto idPayload = rpc->consumeIncomingRequest();
      id = idPayload.id;
      payload = idPayload.payload;
    }
    reader.load(payload);
    unsigned char header = reader.readPrimitive<unsigned char>();
    LOG(INFO) << "CONSUMING REQUEST: " << id.str() << ": " << header << " "
              << payload;
    switch (header) {
      case CLIENT_SERVER_REQUEST_FILE: {
        string path = reader.readPrimitive<string>();
        int flags = reader.readPrimitive<int>();
        int readWriteMode = (flags & O_ACCMODE);
        LOG(INFO) << "REQUESTING FILE: " << path << " FLAGS: " << flags << " "
                  << readWriteMode;
        const FileData *fileData = fileSystem->getNode(path);
        LOG(INFO) << "FILEDATA: " << long(fileData);

        writer.start();

        bool access = true;
        if (readWriteMode == O_RDONLY) {
          if (fileData == NULL) {
            writer.writePrimitive<int>(ENOENT);
            writer.writePrimitive<string>("");
            access = false;
          } else if (!fileData->can_read()) {
            writer.writePrimitive<int>(EACCES);
            writer.writePrimitive<string>("");
            access = false;
          }
        } else {
          if (fileData == NULL) {
            LOG(INFO) << "FILE DOES NOT EXIST YET";
            // Get the parent path and make sure we can write there
            string parentPath =
                boost::filesystem::path(path).parent_path().string();
            LOG(INFO) << "PARENT PATH: " << parentPath;
            if (parentPath != string("/")) {
              const FileData *parentFileData = fileSystem->getNode(parentPath);
              if (parentFileData == NULL || !parentFileData->can_execute()) {
                writer.writePrimitive<int>(EACCES);
                writer.writePrimitive<string>("");
                access = false;
              }
            }

            if (access) {
              fileSystem->writeFile(path, "");
            }
          } else if (!fileData->can_write()) {
            writer.writePrimitive<int>(EACCES);
            writer.writePrimitive<string>("");
            access = false;
          }
        }

        if (access) {
          bool skipLoadingFile =
              (readWriteMode == O_WRONLY && !(flags & O_APPEND));
          string fileContents = "";
          if (!skipLoadingFile) {
            fileContents = fileSystem->readFile(path);
          }

          clientLockedPaths.insert(path);
          writer.writePrimitive<int>(0);
          writer.writePrimitive<string>(fileContents);
        }
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_RETURN_FILE: {
        string path = reader.readPrimitive<string>();
        string fileContents = reader.readPrimitive<string>();
        LOG(INFO) << "WRITING FILE " << path << " " << fileContents;

        int res = fileSystem->writeFile(path, fileContents);

        writer.start();
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_INIT: {
        LOG(INFO) << "INITIALIZING";
        writer.start();
        writer.writePrimitive<int>(fileSystem->allFileData.size());
        for (auto &it : fileSystem->allFileData) {
          writer.writeProto(it.second);
        }
        reply(id, writer.finish());
        LOG(INFO) << "REPLY SENT";
      } break;
      case CLIENT_SERVER_MKDIR: {
        string path = reader.readPrimitive<string>();
        int res = mkdir(fileSystem->fuseToMount(path).c_str(), 0);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_UNLINK: {
        string path = reader.readPrimitive<string>();
        LOG(INFO) << "UNLINKING: " << path << " " << fileSystem->fuseToMount(path);
        int res = unlink(fileSystem->fuseToMount(path).c_str());
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_RMDIR: {
        string path = reader.readPrimitive<string>();
        int res = rmdir(fileSystem->fuseToMount(path).c_str());
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_SYMLINK: {
        string from = reader.readPrimitive<string>();
        string to = reader.readPrimitive<string>();
        int res = symlink(fileSystem->fuseToMount(from).c_str(),
                          fileSystem->fuseToMount(to).c_str());
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_RENAME: {
        string from = reader.readPrimitive<string>();
        string to = reader.readPrimitive<string>();
        int res = rename(fileSystem->fuseToMount(from).c_str(),
                         fileSystem->fuseToMount(to).c_str());
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_LINK: {
        string from = reader.readPrimitive<string>();
        string to = reader.readPrimitive<string>();
        int res = link(fileSystem->fuseToMount(from).c_str(),
                       fileSystem->fuseToMount(to).c_str());
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_CHMOD: {
        string path = reader.readPrimitive<string>();
        int mode = reader.readPrimitive<int>();
        int res = chmod(fileSystem->fuseToMount(path).c_str(), mode);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_LCHOWN: {
        string path = reader.readPrimitive<string>();
        int64_t uid = reader.readPrimitive<int64_t>();
        int64_t gid = reader.readPrimitive<int64_t>();
        int res = lchown(fileSystem->fuseToMount(path).c_str(), uid, gid);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_TRUNCATE: {
        string path = reader.readPrimitive<string>();
        int64_t size = reader.readPrimitive<int64_t>();
        int res = truncate(fileSystem->fuseToMount(path).c_str(), size);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_STATVFS: {
        LOG(INFO) << "STARTING STATVFS";
        struct statvfs stbuf;
        int res = statvfs(fileSystem->fuseToMount("/").c_str(), &stbuf);
        StatVfsData statVfsProto;
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);

          statVfsProto.set_bsize(stbuf.f_bsize);
          statVfsProto.set_frsize(stbuf.f_frsize);
          statVfsProto.set_blocks(stbuf.f_blocks);
          statVfsProto.set_bfree(stbuf.f_bfree);
          statVfsProto.set_bavail(stbuf.f_bavail);
          statVfsProto.set_files(stbuf.f_files);
          statVfsProto.set_ffree(stbuf.f_ffree);
          statVfsProto.set_favail(stbuf.f_favail);
          statVfsProto.set_fsid(stbuf.f_fsid);
          statVfsProto.set_flag(stbuf.f_flag);
          statVfsProto.set_namemax(stbuf.f_namemax);
        }
        writer.writeProto<StatVfsData>(statVfsProto);
        LOG(INFO) << "STARTING STATVFS REPLY";
        reply(id, writer.finish());
        LOG(INFO) << "ENDING STATVFS";
      } break;
      case CLIENT_SERVER_UTIMENSAT: {
        string path = reader.readPrimitive<string>();
        struct timespec ts[2];
        ts[0].tv_sec = reader.readPrimitive<int64_t>();
        ts[0].tv_nsec = reader.readPrimitive<int64_t>();
        ts[1].tv_sec = reader.readPrimitive<int64_t>();
        ts[1].tv_nsec = reader.readPrimitive<int64_t>();
        int res = utimensat(0, fileSystem->fuseToMount(path).c_str(), ts,
                            AT_SYMLINK_NOFOLLOW);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_LREMOVEXATTR: {
        string path = reader.readPrimitive<string>();
        string name = reader.readPrimitive<string>();
        int res = lremovexattr(fileSystem->fuseToMount(path).c_str(),
                               name.c_str());
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_LSETXATTR: {
        string path = reader.readPrimitive<string>();
        string name = reader.readPrimitive<string>();
        string value = reader.readPrimitive<string>();
        int64_t size = reader.readPrimitive<int64_t>();
        int flags = reader.readPrimitive<int>();
        string absolutePath = fileSystem->fuseToMount(path);
        int res = lsetxattr(absolutePath.c_str(), name.c_str(), value.c_str(),
                            size, flags);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
      } break;
      default:
        LOG(FATAL) << "Invalid packet header: " << int(header);
    }
  }

  while (true) {
    RpcId id;
    string payload;
    {
      lock_guard<mutex> lock(rpcMutex);
      if (!rpc->hasIncomingReply()) {
        break;
      }
      auto idPayload = rpc->consumeIncomingReply();
      id = idPayload.id;
      payload = idPayload.payload;
    }
    reader.load(payload);
    unsigned char header = reader.readPrimitive<unsigned char>();

    switch (header) {
      case SERVER_CLIENT_METADATA_UPDATE: {
      } break;

      default:
        LOG(FATAL) << "Invalid packet header: " << int(header);
    }
  }

  return 0;
}

void Server::metadataUpdated(const string &path, const FileData &fileData) {
  writer.start();
  writer.writePrimitive<unsigned char>(SERVER_CLIENT_METADATA_UPDATE);
  writer.writePrimitive<string>(path);
  writer.writeProto<FileData>(fileData);
  request(writer.finish());
}

}  // namespace codefs