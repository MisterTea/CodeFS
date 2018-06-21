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
