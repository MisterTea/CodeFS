#include "Scanner.hpp"

namespace codefs {
const int MAX_XATTR_SIZE = 64 * 1024;

Scanner::Scanner() { xattrBuffer = string(MAX_XATTR_SIZE, '\0'); }

void Scanner::scanRecursively(const string& path_string,
                              unordered_map<string, FileData>* result) {
  boost::filesystem::path pt(path_string);
  try {
    if (exists(pt)) {
      // path exists
      for (auto& p : boost::filesystem::directory_iterator(pt)) {
        string p_str = p.path().string();
        if (is_regular_file(p)) {
          FileData p_filedata = scanFile(p_str);
          result->emplace(p_str, p_filedata);
        } else if (is_directory(p)) {
          scanRecursively(p_str, result);
        } else {
          LOG(ERROR)
              << p << " exists, but is neither a regular file nor a directory";
        }
      }
    } else {
      LOG(FATAL) << "path " << path_string << "doesn't exist!";
    }
  } catch (const boost::filesystem::filesystem_error& ex) {
    LOG(FATAL) << ex.what();
  }
  return;
}

FileData Scanner::scanFile(const string& path) {
  FileData fd;
  fd.set_access(access(path.c_str(), R_OK | W_OK | X_OK));
  struct stat fileStat;
  FATAL_FAIL(lstat(path.c_str(), &fileStat));
  StatData fStat;
  fStat.set_dev(fileStat.st_dev);
  fStat.set_ino(fileStat.st_ino);
  fStat.set_mode(fileStat.st_mode);
  fStat.set_nlink(fileStat.st_nlink);
  fStat.set_uid(fileStat.st_uid);
  fStat.set_gid(fileStat.st_gid);
  fStat.set_rdev(fileStat.st_rdev);
  fStat.set_size(fileStat.st_size);
  fStat.set_blksize(fileStat.st_blksize);
  fStat.set_blocks(fileStat.st_blocks);
  fStat.set_atime(fileStat.st_atime);
  fStat.set_mtime(fileStat.st_mtime);
  fStat.set_ctime(fileStat.st_ctime);
  *(fd.mutable_stat_data()) = fStat;
  if (S_ISLNK(fileStat.st_mode)) {
    int bufsiz = fileStat.st_size + 1;

    /* Some magic symlinks under (for example) /proc and /sys
       report 'st_size' as zero. In that case, take PATH_MAX as
       a "good enough" estimate. */

    if (fileStat.st_size == 0) {
      bufsiz = PATH_MAX;
    }

    string s(bufsiz, '\0');
    int nbytes = readlink(path.c_str(), &s[0], bufsiz);
    FATAL_FAIL(nbytes);
    s = s.substr(0, nbytes + 1);
    fd.set_symlink_contents(s);
  }

  // Load extended attributes
  {
    memset(&xattrBuffer[0], '\0', MAX_XATTR_SIZE);
    auto listSize = llistxattr(path.c_str(), &xattrBuffer[0], MAX_XATTR_SIZE);
    FATAL_FAIL(listSize);
    string s(&xattrBuffer[0], listSize);
    vector<string> keys = split(s, '\0');
    for (string key : keys) {
      auto xattrSize =
          lgetxattr(path.c_str(), key.c_str(), &xattrBuffer[0], MAX_XATTR_SIZE);
      FATAL_FAIL(xattrSize);
      string value(&xattrBuffer[0], xattrSize);
      fd.add_xattr_key(key);
      fd.add_xattr_value(value);
    }
  }
  return fd;
}
}  // namespace codefs
