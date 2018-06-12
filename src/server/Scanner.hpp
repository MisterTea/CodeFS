#include "Headers.hpp"

class Scanner {
    public:
    static unordered_map<string, FileData> scanRecursively(const string &path);
    static FileData scanFile(const string& path);
};
