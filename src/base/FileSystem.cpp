#include "FileSystem.hpp"

#include "MessageReader.hpp"
#include "MessageWriter.hpp"

namespace codefs {
string FileSystem::serializeFileDataCompressed(const string& path) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  MessageWriter writer;
  if (allFileData.find(path) == allFileData.end()) {
    writer.writePrimitive<int>(0);
  } else {
    const auto& fileData = allFileData.at(path);
    int numChildren = 0;
    for (auto& it : fileData.child_node()) {
      auto childPath = (boost::filesystem::path(path) / it).string();
      if (allFileData.find(childPath) == allFileData.end()) {
        numChildren++;
      }
    }
    writer.writePrimitive<int>(1 + numChildren);
    writer.writeProto(fileData);
    for (auto& it : fileData.child_node()) {
      auto childPath = (boost::filesystem::path(path) / it).string();
      if (allFileData.find(childPath) != allFileData.end()) {
        VLOG(1) << "SCANNING: " << path << " / " << it << " = " << childPath;
        const auto& childFileData = allFileData.at(childPath);
        writer.writeProto(childFileData);
      }
    }
  }
  return compressString(writer.finish());
}

void FileSystem::deserializeFileDataCompressed(const string& path,
                                               const string& s) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  MessageReader reader;
  reader.load(decompressString(s));
  int numFiles = reader.readPrimitive<int>();
  VLOG(1) << "DESERIALIZING " << numFiles << " FILES";
  for (int a = 0; a < numFiles; a++) {
    auto fileData = reader.readProto<FileData>();
    VLOG(1) << "GOT FILE: " << fileData.path();
    if (fileData.invalid()) {
      LOGFATAL << "Got an invalid file from the server!";
    }
    allFileData.erase(fileData.path());
    allFileData.insert(make_pair(fileData.path(), fileData));
  }
}
}  // namespace codefs