#include "Scanner.hpp"

#include "FileSystem.hpp"

namespace codefs {
const int MAX_XATTR_SIZE = 64 * 1024;

void Scanner::scanRecursively(FileSystem* fileSystem, const string& path_string,
                              unordered_map<string, FileData>* result) {
  LOG(INFO) << "SCANNING DIRECTORY " << path_string;
  FileData p_filedata = scanNode(fileSystem, path_string, result);

  boost::filesystem::path pt(path_string);
  try {
    if (exists(pt)) {
      // path exists
      for (auto& p : boost::filesystem::directory_iterator(pt)) {
        string p_str = p.path().string();
        if (boost::filesystem::is_regular_file(p.path())) {
          LOG(INFO) << "SCANNING FILE " << p_str;
          FileData p_filedata = scanNode(fileSystem, p_str, result);
        } else if (boost::filesystem::is_directory(p.path())) {
          scanRecursively(fileSystem, p_str, result);
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

FileData Scanner::scanNode(FileSystem* fileSystem, const string& path,
                           unordered_map<string, FileData>* result) {
  LOG(INFO) << "SCANNING NODE : " << path;

  FileData fd;
  fd.set_path(fileSystem->absoluteToRelative(path));
  fd.set_deleted(false);
  fd.set_invalid(false);

  if (access(path.c_str(), F_OK) != 0) {
    // The file is gone
    result->erase(fileSystem->absoluteToRelative(path));
    fd.set_deleted(true);
    return fd;
  }

  if (access(path.c_str(), R_OK) == 0) {
    fd.set_can_read(true);
  } else {
    fd.set_can_read(false);
  }
  if (access(path.c_str(), W_OK) == 0) {
    fd.set_can_write(true);
  } else {
    fd.set_can_write(false);
  }
  if (access(path.c_str(), X_OK) == 0) {
    fd.set_can_execute(true);
  } else {
    fd.set_can_execute(false);
  }
  struct stat fileStat;
  FATAL_FAIL(lstat(path.c_str(), &fileStat));
  StatData fStat;
  FileSystem::statToProto(fileStat, &fStat);
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

  if (S_ISDIR(fileStat.st_mode)) {
    // Populate children
    for (auto& it : boost::filesystem::directory_iterator(path)) {
      LOG(INFO) << "FOUND CHILD: " << it.path().filename().string();
      fd.add_child_node(it.path().filename().string());
    }
  }

  // Load extended attributes
  {
    string xattrBuffer = string(MAX_XATTR_SIZE, '\0');
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

  (*result)[fileSystem->absoluteToRelative(path)] = fd;
  return fd;
}
}  // namespace codefs
