#include "Server.hpp"

#include "RawSocketUtils.hpp"
#include "ZmqBiDirectionalRpc.hpp"

namespace codefs {
Server::Server(const string &_address, shared_ptr<ServerFileSystem> _fileSystem)
    : address(_address), fileSystem(_fileSystem), clientFd(-1) {}

void Server::init() {
  lock_guard<std::recursive_mutex> lock(rpcMutex);
  rpc = shared_ptr<ZmqBiDirectionalRpc>(new ZmqBiDirectionalRpc(address, true));
}

int Server::update() {
  {
    lock_guard<std::recursive_mutex> lock(rpcMutex);
    rpc->update();
  }

  MessageWriter writer;
  MessageReader reader;
  while (true) {
    RpcId id;
    string payload;
    {
      lock_guard<std::recursive_mutex> lock(rpcMutex);
      if (!rpc->hasIncomingRequest()) {
        break;
      }
      auto idPayload = rpc->getFirstIncomingRequest();
      id = idPayload.id;
      payload = idPayload.payload;
    }
    reader.load(payload);
    unsigned char header = reader.readPrimitive<unsigned char>();
    LOG(INFO) << "CONSUMING REQUEST: " << id.str() << ": " << int(header) << " "
              << payload.size();
    switch (header) {
      case CLIENT_SERVER_CREATE_FILE: {
        string path = reader.readPrimitive<string>();
        int flags = reader.readPrimitive<int>();
        int mode = reader.readPrimitive<int>();
        int readWriteMode = (flags & O_ACCMODE);
        LOG(INFO) << "REQUESTING FILE: " << path << " FLAGS: " << flags << " "
                  << readWriteMode << " " << mode;
        optional<FileData> fileData = fileSystem->getNode(path);

        writer.start();

        bool access = true;
        if (readWriteMode == O_RDONLY) {
          if (!fileData) {
            writer.writePrimitive<int>(ENOENT);
            access = false;
          } else if (!fileData->can_read()) {
            writer.writePrimitive<int>(EACCES);
            access = false;
          }
        } else {
          if (!fileData) {
            LOG(INFO) << "FILE DOES NOT EXIST YET";

            // Get the parent path and make sure we can write there
            string parentPath =
                boost::filesystem::path(path).parent_path().string();
            LOG(INFO) << "PARENT PATH: " << parentPath;
            if (parentPath != string("/")) {
              optional<FileData> parentFileData =
                  fileSystem->getNode(parentPath);
              if (!parentFileData || !parentFileData->can_execute()) {
                writer.writePrimitive<int>(EACCES);
                access = false;
              }
            }

            if (access) {
              LOG(INFO) << "Creating empty file";
              fileSystem->writeFile(path, "");
              fileSystem->chmod(path, mode_t(mode));
            }
          } else if (!fileData->can_write()) {
            writer.writePrimitive<int>(EACCES);
            access = false;
          }
        }

        if (access) {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        fileSystem->rescanPathAndParent(fileSystem->relativeToAbsolute(path));

      } break;
      case CLIENT_SERVER_REQUEST_FILE: {
        string path = reader.readPrimitive<string>();
        int flags = reader.readPrimitive<int>();
        int readWriteMode = (flags & O_ACCMODE);
        LOG(INFO) << "REQUESTING FILE: " << path << " FLAGS: " << flags << " "
                  << readWriteMode;
        optional<FileData> fileData = fileSystem->getNode(path);

        writer.start();

        bool access = true;
        if (readWriteMode == O_RDONLY) {
          if (!fileData) {
            writer.writePrimitive<int>(ENOENT);
            writer.writePrimitive<string>("");
            access = false;
          } else if (!fileData->can_read()) {
            writer.writePrimitive<int>(EACCES);
            writer.writePrimitive<string>("");
            access = false;
          }
        } else {
          if (!fileData) {
            LOG(INFO) << "FILE DOES NOT EXIST YET";
            writer.writePrimitive<int>(ENOENT);
            writer.writePrimitive<string>("");
            access = false;
          } else if (!fileData->can_write()) {
            writer.writePrimitive<int>(EACCES);
            writer.writePrimitive<string>("");
            access = false;
          }
        }

        if (access) {
          bool skipLoadingFile = false;  // O_TRUNC never occurs in FUSE
          string fileContents = "";
          if (!skipLoadingFile) {
            fileContents = fileSystem->readFile(path);
            LOG(INFO) << "READ FILE: " << path << " " << fileContents.size();
          }

          writer.writePrimitive<int>(0);
          writer.writePrimitive<string>(compressString(fileContents));
        }
        reply(id, writer.finish());
        if (readWriteMode != O_RDONLY) {
          fileSystem->rescanPathAndParent(fileSystem->relativeToAbsolute(path));
        }
      } break;
      case CLIENT_SERVER_RETURN_FILE: {
        string path = reader.readPrimitive<string>();
        bool readOnly = reader.readPrimitive<bool>();
        int res = 0;
        if (readOnly) {
          LOG(INFO) << "RETURNED READ-ONLY FILE";
        } else {
          string fileContents =
              decompressString(reader.readPrimitive<string>());
          LOG(INFO) << "WRITING FILE " << path << " " << fileContents.size();

          res = fileSystem->writeFile(path, fileContents);
        }

        writer.start();
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        if (!readOnly) {
          fileSystem->rescanPathAndParent(fileSystem->relativeToAbsolute(path));
        }
      } break;
      case CLIENT_SERVER_FETCH_METADATA: {
        string path = reader.readPrimitive<string>();
        LOG(INFO) << "Fetching Metadata for " << path;
        auto s = fileSystem->serializeFileDataCompressed(path);
        writer.start();
        writer.writePrimitive<string>(path);
        writer.writePrimitive<string>(s);
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_MKDIR: {
        string path = reader.readPrimitive<string>();
        mode_t mode = reader.readPrimitive<int>();
        int res = fileSystem->mkdir(path, mode);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        fileSystem->rescanPathAndParent(fileSystem->relativeToAbsolute(path));
      } break;
      case CLIENT_SERVER_UNLINK: {
        string path = reader.readPrimitive<string>();
        LOG(INFO) << "UNLINKING: " << path << " "
                  << fileSystem->relativeToAbsolute(path);
        int res = fileSystem->unlink(path);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        fileSystem->rescanPathAndParent(fileSystem->relativeToAbsolute(path));
      } break;
      case CLIENT_SERVER_RMDIR: {
        string path = reader.readPrimitive<string>();
        int res = fileSystem->rmdir(path);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        fileSystem->rescanPathAndParent(fileSystem->relativeToAbsolute(path));
      } break;
      case CLIENT_SERVER_SYMLINK: {
        string from = reader.readPrimitive<string>();
        string to = reader.readPrimitive<string>();
        int res = fileSystem->symlink(from, to);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        fileSystem->rescanPathAndParent(fileSystem->relativeToAbsolute(from));
        fileSystem->rescanPathAndParent(fileSystem->relativeToAbsolute(to));
      } break;
      case CLIENT_SERVER_RENAME: {
        string from = reader.readPrimitive<string>();
        string to = reader.readPrimitive<string>();
        int res = fileSystem->rename(from, to);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        fileSystem->rescanPathAndParentAndChildren(
            fileSystem->relativeToAbsolute(from));
        fileSystem->rescanPathAndParentAndChildren(
            fileSystem->relativeToAbsolute(to));
      } break;
      case CLIENT_SERVER_LINK: {
        string from = reader.readPrimitive<string>();
        if (from[0] == '/') {
          from = fileSystem->relativeToAbsolute(from);
        }
        string to = reader.readPrimitive<string>();
        int res = fileSystem->link(from, to);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        fileSystem->rescanPathAndParent(fileSystem->relativeToAbsolute(from));
        fileSystem->rescanPathAndParent(fileSystem->relativeToAbsolute(to));
      } break;
      case CLIENT_SERVER_CHMOD: {
        string path = reader.readPrimitive<string>();
        int mode = reader.readPrimitive<int>();
        int res = fileSystem->chmod(path, mode);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        fileSystem->rescanPath(fileSystem->relativeToAbsolute(path));
      } break;
      case CLIENT_SERVER_LCHOWN: {
        string path = reader.readPrimitive<string>();
        int64_t uid = reader.readPrimitive<int64_t>();
        int64_t gid = reader.readPrimitive<int64_t>();
        int res = fileSystem->lchown(path, uid, gid);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        fileSystem->rescanPath(fileSystem->relativeToAbsolute(path));
      } break;
      case CLIENT_SERVER_TRUNCATE: {
        string path = reader.readPrimitive<string>();
        int64_t size = reader.readPrimitive<int64_t>();
        int res = fileSystem->truncate(path, size);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        fileSystem->rescanPath(fileSystem->relativeToAbsolute(path));
      } break;
      case CLIENT_SERVER_STATVFS: {
        struct statvfs stbuf;
        int res =
            ::statvfs(fileSystem->relativeToAbsolute("/").c_str(), &stbuf);
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
        reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_UTIMENSAT: {
        string path = reader.readPrimitive<string>();
        struct timespec ts[2];
        ts[0].tv_sec = reader.readPrimitive<int64_t>();
        ts[0].tv_nsec = reader.readPrimitive<int64_t>();
        ts[1].tv_sec = reader.readPrimitive<int64_t>();
        ts[1].tv_nsec = reader.readPrimitive<int64_t>();
        int res = fileSystem->utimensat(path, ts);
        writer.writePrimitive<int>(res);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        fileSystem->rescanPath(fileSystem->relativeToAbsolute(path));
      } break;
      case CLIENT_SERVER_LREMOVEXATTR: {
        string path = reader.readPrimitive<string>();
        string name = reader.readPrimitive<string>();
        int res = fileSystem->lremovexattr(path, name);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        fileSystem->rescanPath(fileSystem->relativeToAbsolute(path));
      } break;
      case CLIENT_SERVER_LSETXATTR: {
        string path = reader.readPrimitive<string>();
        string name = reader.readPrimitive<string>();
        string value = reader.readPrimitive<string>();
        int64_t size = reader.readPrimitive<int64_t>();
        int flags = reader.readPrimitive<int>();
        int res = fileSystem->lsetxattr(path, name, value, size, flags);
        if (res) {
          writer.writePrimitive<int>(errno);
        } else {
          writer.writePrimitive<int>(0);
        }
        reply(id, writer.finish());
        fileSystem->rescanPath(fileSystem->relativeToAbsolute(path));
      } break;
      default:
        LOG(FATAL) << "Invalid packet header: " << int(header);
    }
  }

  while (true) {
    RpcId id;
    string payload;
    {
      lock_guard<std::recursive_mutex> lock(rpcMutex);
      if (!rpc->hasIncomingReply()) {
        break;
      }
      auto idPayload = rpc->getFirstIncomingReply();
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
  MessageWriter writer;
  writer.start();
  writer.writePrimitive<unsigned char>(SERVER_CLIENT_METADATA_UPDATE);
  writer.writePrimitive<string>(path);
  writer.writeProto<FileData>(fileData);
  request(writer.finish());
}

}  // namespace codefs