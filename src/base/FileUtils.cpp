#include "FileUtils.hpp"

void FileUtils::touch(const string& path) {
    FILE *fp = ::fopen(path.c_str(), "ab+");
    ::fclose(fp);
}