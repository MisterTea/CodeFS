#ifndef __CODE_FS_SCANNER_H__
#define __CODE_FS_SCANNER_H__

#include "Headers.hpp"

#include "FileSystem.hpp"

namespace codefs {
class Scanner {
 public:
  Scanner();
  void scanRecursively(const string& path,
                       unordered_map<string, FileData>* result);
  FileData scanFile(const string& path);

 protected:
  string xattrBuffer;
};
}  // namespace codefs

#endif  // __CODE_FS_SCANNER_H__