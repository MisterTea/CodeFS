#include "ServerFileSystem.hpp"

#include "Scanner.hpp"

namespace codefs {
ServerFileSystem::ServerFileSystem(const string& _absoluteFuseRoot,
                                   const string& _fuseMountPoint)
    : FileSystem(_absoluteFuseRoot),
      fuseMountPoint(_fuseMountPoint),
      initialized(false),
      handler(NULL) {}

void ServerFileSystem::init() {
  Scanner::scanRecursively(this, fuseToAbsolute("/"), &allFileData);
  initialized = true;
}

void ServerFileSystem::rescan(const string& absolutePath) {
  FileData fileData = Scanner::scanNode(this, absolutePath, &allFileData);
  if (handler == NULL) {
    LOG(FATAL) << "TRIED TO RESCAN WITH NO HANDLER";
  }
  LOG(INFO) << "UPDATING METADATA: " << absolutePath;
  handler->metadataUpdated(absolutePath, fileData);
}

string ServerFileSystem::readFile(const string& path) {
  return fileToStr((boost::filesystem::path(fuseMountPoint) / path).string());
}

int ServerFileSystem::writeFile(const string& path,
                                const string& fileContents) {
  FILE* fp = fopen(
      (boost::filesystem::path(fuseMountPoint) / path).string().c_str(), "ab");
  if (fp == NULL) {
    return -1;
  }
  auto written = fwrite(fileContents.c_str(), fileContents.length(), 1, fp);
  if (written != fileContents.length()) {
    return -1;
  }
  fclose(fp);
  return 0;
}
}  // namespace codefs
