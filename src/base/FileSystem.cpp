#include "FileSystem.hpp"

#include "MessageReader.hpp"
#include "MessageWriter.hpp"

namespace codefs {
string FileSystem::serializeAllFileDataCompressed() {
  std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
  MessageWriter writer;
  writer.writePrimitive<int>(allFileData.size());
  for (auto& it : allFileData) {
    writer.writeProto(it.second);
  }
  return compressString(writer.finish());
}

void FileSystem::deserializeAllFileDataCompressed(const string& s) {
  std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
  MessageReader reader;
  reader.load(decompressString(s));
  int numFiles = reader.readPrimitive<int>();
  for (int a = 0; a < numFiles; a++) {
    auto fileData = reader.readProto<FileData>();
    allFileData.insert(make_pair(fileData.path(), fileData));
  }
}
}