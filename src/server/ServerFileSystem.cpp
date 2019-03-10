#include "ServerFileSystem.hpp"

#include "Scanner.hpp"

namespace codefs {
ServerFileSystem::ServerFileSystem(const string& _rootPath)
    : FileSystem(_rootPath), initialized(false), handler(NULL) {}

void ServerFileSystem::init() {
  Scanner::scanRecursively(this, rootPath, &allFileData);
  initialized = true;
}

void ServerFileSystem::rescanPath(const string& absolutePath) {
  std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
  FileData fileData = Scanner::scanNode(this, absolutePath, &allFileData);
  if (handler == NULL) {
    LOG(FATAL) << "TRIED TO RESCAN WITH NO HANDLER";
  }
  LOG(INFO) << "UPDATING METADATA: " << absolutePath;
  handler->metadataUpdated(absoluteToRelative(absolutePath), fileData);
}

string ServerFileSystem::readFile(const string& path) {
  return fileToStr(relativeToAbsolute(path));
}

int ServerFileSystem::writeFile(const string& path,
                                const string& fileContents) {
  FILE* fp = ::fopen(relativeToAbsolute(path).c_str(), "wb");
  if (fp == NULL) {
    return -1;
  }
  size_t bytesWritten = 0;
  while (bytesWritten < fileContents.length()) {
    size_t written = ::fwrite(fileContents.c_str() + bytesWritten, 1,
                              fileContents.length() - bytesWritten, fp);
    bytesWritten += written;
  }
  ::fclose(fp);
  return 0;
}
}  // namespace codefs
