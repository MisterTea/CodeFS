#include "ServerFileSystem.hpp"

#include "Scanner.hpp"

namespace codefs {
ServerFileSystem::ServerFileSystem(const string &_absoluteFuseRoot)
    : FileSystem(_absoluteFuseRoot), initialized(false) {}

void ServerFileSystem::init() {
  Scanner::scanRecursively(this, fuseToAbsolute("/"), &allFileData);
  initialized = true;
}

void ServerFileSystem::rescan(const string &absolutePath) {
  Scanner::scanNode(this, absolutePath, &allFileData);
}
}  // namespace codefs
