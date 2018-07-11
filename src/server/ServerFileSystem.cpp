#include "ServerFileSystem.hpp"

#include "Scanner.hpp"

namespace codefs {
ServerFileSystem::ServerFileSystem(const string& _absoluteFuseRoot)
    : FileSystem(_absoluteFuseRoot), initialized(false) {}

void ServerFileSystem::init() {
  Scanner::scanRecursively(this, fuseToAbsolute("/"), &allFileData);
  initialized = true;
}

void ServerFileSystem::rescan(const string& absolutePath) {
  Scanner::scanNode(this, absolutePath, &allFileData);
}

string ServerFileSystem::readFile(const string& path) {
  return fileToStr((boost::filesystem::path(absoluteFuseRoot) / path).string());
}

void ServerFileSystem::writeFile(const string& path, const string& fileContents) {
  std::ofstream out(
      (boost::filesystem::path(absoluteFuseRoot) / path).string());
  out << fileContents;
  out.close();
}
}  // namespace codefs
