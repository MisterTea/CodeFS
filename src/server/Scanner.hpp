#include "Headers.hpp"

class Scanner {
    public:
    static map<string, FileData> scanRecursively(const string &path);
    static FileData scanFile(const string& path);
};
