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
  lock_guard<mutex> lock(rpcMutex);
  rpc->update();

  while (rpc->hasIncomingRequest()) {
    auto idPayload = rpc->consumeIncomingRequest();
    auto id = idPayload.id;
    string payload = idPayload.payload;
    reader.load(payload);
    unsigned char header = reader.readPrimitive<unsigned char>();
    switch (header) {
      case CLIENT_SERVER_REQUEST_FILE: {
        string path = reader.readPrimitive<string>();
        int flags = reader.readPrimitive<int>();
        int readWriteMode = (flags & O_ACCMODE);
        const FileData *fileData = fileSystem->getNode(path);

        writer.start();
        writer.writePrimitive(header);

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
            // Get the parent path and make sure we can write there
            string parentPath =
                boost::filesystem::path(path).parent_path().string();
            const FileData *parentFileData = fileSystem->getNode(parentPath);
            if (parentFileData == NULL || !parentFileData->can_execute()) {
              writer.writePrimitive<int>(EACCES);
              writer.writePrimitive<string>("");
              access = false;
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
        rpc->reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_RETURN_FILE: {
        string path = reader.readPrimitive<string>();
        string fileContents = reader.readPrimitive<string>();

        fileSystem->writeFile(path, fileContents);

        writer.start();
        writer.writePrimitive(header);
        rpc->reply(id, writer.finish());
      } break;
      case CLIENT_SERVER_INIT: {
        writer.start();
        writer.writePrimitive<int>(fileSystem->allFileData.size());
        for (auto &it : fileSystem->allFileData) {
          writer.writeProto(it.second);
        }
        rpc->reply(id, writer.finish());
      } break;
      default:
        LOG(FATAL) << "Invalid packet header: " << int(header);
    }
  }

  while (rpc->hasIncomingReply()) {
    auto idPayload = rpc->consumeIncomingReply();
    // auto id = idPayload.id;
    string payload = idPayload.payload;
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
  lock_guard<mutex> lock(rpcMutex);
  writer.start();
  writer.writePrimitive<unsigned char>(SERVER_CLIENT_METADATA_UPDATE);
  writer.writePrimitive<string>(path);
  writer.writeProto<FileData>(fileData);
  rpc->request(writer.finish());
}

}  // namespace codefs