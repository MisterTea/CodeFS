#include "Headers.hpp"

#include "FileSystem.hpp"

namespace codefs {
class Scanner {
 public:
  Scanner();
  unordered_map<string, FileData> scanRecursively(const string& path);
  FileData scanFile(const string& path);

 protected:
  string xattrBuffer;
};
}  // namespace codefs