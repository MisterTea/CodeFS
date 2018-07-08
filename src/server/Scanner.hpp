#ifndef __CODE_FS_SCANNER_H__
#define __CODE_FS_SCANNER_H__

#include "Headers.hpp"

#include "FileSystem.hpp"

namespace codefs {
class Scanner {
 public:
  static void scanRecursively(FileSystem* fileSystem,
                              const string& path,
                              unordered_map<string, FileData>* result);
  static FileData scanNode(FileSystem* fileSystem,
                           const string& path,
                           unordered_map<string, FileData>* result);
};
}  // namespace codefs

#endif  // __CODE_FS_SCANNER_H__