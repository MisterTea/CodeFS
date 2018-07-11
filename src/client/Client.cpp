#include "Client.hpp"

#include "FileUtils.hpp"
#include "RawSocketUtils.hpp"

namespace codefs {
Client::Client(const string& _address, shared_ptr<ClientFileSystem> _fileSystem)
    : address(_address), fileSystem(_fileSystem) {
  rpc = shared_ptr<BiDirectionalRpc>(new BiDirectionalRpc(address, false));
  writer.start();
  writer.writePrimitive<unsigned char>(CLIENT_SERVER_INIT);
  RpcId initId = rpc->request(writer.finish());

  while (true) {
    LOG(INFO) << "Waiting for init...";
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
    }
    sleep(1);
  }
}

int Client::update() {
  lock_guard<mutex> lock(rpcMutex);
  rpc->update();

  while (rpc->hasIncomingRequest()) {
    auto idPayload = rpc->consumeIncomingRequest();
    auto id = idPayload.id;
    string payload = idPayload.payload;
    reader.load(payload);
    unsigned char header = reader.readPrimitive<unsigned char>();
    switch (header) {
      case SERVER_CLIENT_METADATA_UPDATE: {
        string path = reader.readPrimitive<string>();
        FileData fileData = reader.readProto<FileData>();
        writer.start();
        writer.writePrimitive(header);
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

int Client::mkdir(const string& path) {
  return singlePathNoReturn(CLIENT_SERVER_MKDIR, path);
}

int Client::unlink(const string& path) {
  return singlePathNoReturn(CLIENT_SERVER_UNLINK, path);
}

int Client::rmdir(const string& path) {
  return singlePathNoReturn(CLIENT_SERVER_RMDIR, path);
}

int Client::symlink(const string& from, const string& to) {
  return twoPathsNoReturn(CLIENT_SERVER_SYMLINK, from, to);
}
int Client::rename(const string& from, const string& to) {
  return twoPathsNoReturn(CLIENT_SERVER_RENAME, from, to);
}
int Client::link(const string& from, const string& to) {
  return twoPathsNoReturn(CLIENT_SERVER_LINK, from, to);
}

int Client::twoPathsNoReturn(unsigned char header, const string& from, const string& to) {
  string payload;
  {
    lock_guard<mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(header);
    writer.writePrimitive<string>(from);
    writer.writePrimitive<string>(to);
  }
  string result = fileRpc(payload);
  {
    lock_guard<mutex> lock(rpcMutex);
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
    lock_guard<mutex> lock(rpcMutex);
    writer.start();
    writer.writePrimitive<unsigned char>(header);
    writer.writePrimitive<string>(path);
  }
  string result = fileRpc(payload);
  {
    lock_guard<mutex> lock(rpcMutex);
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
  auto id = rpc->request(payload);
  while (true) {
    sleep(1);
    lock_guard<mutex> lock(rpcMutex);
    if (rpc->hasIncomingReplyWithId(id)) {
      return rpc->consumeIncomingReplyWithId(id);
    }
  }
}

}  // namespace codefs