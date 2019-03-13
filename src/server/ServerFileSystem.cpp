#include "ServerFileSystem.hpp"

namespace codefs {
ServerFileSystem::ServerFileSystem(const string& _rootPath)
    : FileSystem(_rootPath), initialized(false), handler(NULL) {}

void ServerFileSystem::init() {
  scanRecursively(rootPath, &allFileData);
  initialized = true;
}

void ServerFileSystem::rescanPath(const string& absolutePath) {
  std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
  FileData fileData = scanNode(absolutePath, &allFileData);
}

string ServerFileSystem::readFile(const string& path) {
  std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
  return fileToStr(relativeToAbsolute(path));
}

int ServerFileSystem::writeFile(const string& path,
                                const string& fileContents) {
  std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
  FILE* fp = ::fopen(relativeToAbsolute(path).c_str(), "wb");
  if (fp == NULL) {
    return -1;
  }
  size_t bytesWritten = 0;
  while (bytesWritten < fileContents.length()) {
    size_t written = ::fwrite(fileContents.c_str() + bytesWritten, 1,
                              fileContents.length() - bytesWritten, fp);
    bytesWritten += written;
  }
  ::fclose(fp);
  return 0;
}

const int MAX_XATTR_SIZE = 64 * 1024;

void ServerFileSystem::scanRecursively(
    const string& path_string, unordered_map<string, FileData>* result) {
  std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
  LOG(INFO) << "SCANNING DIRECTORY " << path_string;
  FileData p_filedata = scanNode(path_string, result);

  boost::filesystem::path pt(path_string);
  try {
    if (exists(pt) && boost::filesystem::is_directory(pt)) {
      // path exists
      for (auto& p : boost::make_iterator_range(
               boost::filesystem::directory_iterator(pt), {})) {
        string p_str = p.path().string();
        if (boost::filesystem::is_regular_file(p.path()) ||
            boost::filesystem::is_symlink(p.path())) {
          LOG(INFO) << "SCANNING FILE " << p_str;
          scanNode(p_str, result);
        } else if (boost::filesystem::is_directory(p.path())) {
          scanRecursively(p_str, result);
        } else {
          LOG(ERROR)
              << p << " exists, but is neither a regular file nor a directory";
        }
      }
    } else {
      LOG(ERROR) << "path " << path_string
                 << "doesn't exist or isn't a directory!";
    }
  } catch (const boost::filesystem::filesystem_error& ex) {
    LOG(FATAL) << ex.what();
  }

  LOG(INFO) << "RECURSIVE SCAN FINISHED";
  return;
}

FileData ServerFileSystem::scanNode(const string& path,
                                    unordered_map<string, FileData>* result) {
  std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
  LOG(INFO) << "SCANNING NODE : " << path;

  FileData fd;
  fd.set_path(absoluteToRelative(path));
  fd.set_deleted(false);
  fd.set_invalid(false);

#if __APPLE__
  // faccessat doesn't have AT_SYMLINK_NOFOLLOW
  if (::faccessat(0, path.c_str(), F_OK, 0) != 0) {
    LOG(INFO) << "FILE IS GONE: " << path << " " << errno;
    // The file is gone
    result->erase(absoluteToRelative(path));
    fd.set_deleted(true);
    if (handler != NULL) {
      LOG(INFO) << "UPDATING METADATA: " << path;
      handler->metadataUpdated(absoluteToRelative(path), fd);
    }

    return fd;
  }

  if (::faccessat(0, path.c_str(), R_OK, 0) == 0) {
    fd.set_can_read(true);
  } else {
    fd.set_can_read(false);
  }
  if (::faccessat(0, path.c_str(), W_OK, 0) == 0) {
    fd.set_can_write(true);
  } else {
    fd.set_can_write(false);
  }
  if (::faccessat(0, path.c_str(), X_OK, 0) == 0) {
    fd.set_can_execute(true);
  } else {
    fd.set_can_execute(false);
  }
#else
  if (::faccessat(0, path.c_str(), F_OK, AT_SYMLINK_NOFOLLOW) != 0) {
    LOG(INFO) << "FILE IS GONE: " << path << " " << errno;
    // The file is gone
    result->erase(absoluteToRelative(path));
    fd.set_deleted(true);
    if (handler != NULL) {
      LOG(INFO) << "UPDATING METADATA: " << path;
      handler->metadataUpdated(absoluteToRelative(path), fd);
    }

    return fd;
  }

  if (::faccessat(0, path.c_str(), R_OK, AT_SYMLINK_NOFOLLOW) == 0) {
    fd.set_can_read(true);
  } else {
    fd.set_can_read(false);
  }
  if (::faccessat(0, path.c_str(), W_OK, AT_SYMLINK_NOFOLLOW) == 0) {
    fd.set_can_write(true);
  } else {
    fd.set_can_write(false);
  }
  if (::faccessat(0, path.c_str(), X_OK, AT_SYMLINK_NOFOLLOW) == 0) {
    fd.set_can_execute(true);
  } else {
    fd.set_can_execute(false);
  }
#endif

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
    if (s[0] == '/') {
      s = absoluteToRelative(s);
    }
    fd.set_symlink_contents(s);
  }

  if (S_ISDIR(fileStat.st_mode)) {
    // Populate children
    for (auto& it : boost::make_iterator_range(
             boost::filesystem::directory_iterator(path), {})) {
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

  LOG(INFO) << "SETTING: " << absoluteToRelative(path);
  (*result)[absoluteToRelative(path)] = fd;

  if (handler != NULL) {
    LOG(INFO) << "UPDATING METADATA: " << path;
    handler->metadataUpdated(absoluteToRelative(path), fd);
  }

  return fd;
}

}  // namespace codefs
