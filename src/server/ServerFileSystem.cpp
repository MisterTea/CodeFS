#include "ServerFileSystem.hpp"

#include "Scanner.hpp"

namespace codefs {
ServerFileSystem::ServerFileSystem(const string &_absoluteFuseRoot)
    : FileSystem(_absoluteFuseRoot) {}

  void ServerFileSystem::rescan(const string &absolutePath) {
    pathsToRescan.insert(absolutePath);

    // TODO: FUSE requires this to happen very fast, so hack it in for now, but we will buffer it later
    Scanner::scanNode(this, absolutePath, &allFileData);
  }
}  // namespace codefs
