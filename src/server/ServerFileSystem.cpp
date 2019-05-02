#include "ServerFileSystem.hpp"

namespace codefs {
ServerFileSystem::ServerFileSystem(
    const string& _rootPath, const set<boost::filesystem::path>& _excludes)
    : FileSystem(_rootPath),
      initialized(false),
      handler(NULL),
      excludes(_excludes) {}

void ServerFileSystem::init() {
  scanRecursively(rootPath);
  initialized = true;
}

void ServerFileSystem::rescanPath(const string& absolutePath) {
  std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
  scanNode(absolutePath);
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
  rescanPath(relativeToAbsolute(path));
  return 0;
}

const int MAX_XATTR_SIZE = 64 * 1024;

void ServerFileSystem::scanRecursively(
    const string& path_string, shared_ptr<ctpl::thread_pool> scanThreadPool) {
  bool waitUntilFinished = false;
  if (scanThreadPool.get() == NULL) {
    // scanThreadPool.reset(new ctpl::thread_pool(8));
    waitUntilFinished = true;
  }

  boost::filesystem::path pt(path_string);
  if (!exists(pt)) {
    return;
  }
  auto path =
      boost::filesystem::canonical(boost::filesystem::path(path_string));
  if (excludes.find(path) != excludes.end()) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
  VLOG(1) << "SCANNING DIRECTORY " << path_string;
  scanNode(path_string);

  if (boost::filesystem::is_directory(pt)) {
    // path exists
    for (auto& p : boost::make_iterator_range(
             boost::filesystem::directory_iterator(pt), {})) {
      string p_str = p.path().string();
      if (boost::filesystem::is_symlink(p.path()) ||
          boost::filesystem::is_regular_file(p.path())) {
        // LOG(INFO) << "SCANNING FILE " << p_str;
        this->scanNode(p_str);
      } else if (boost::filesystem::is_directory(p.path())) {
        scanRecursively(p_str, scanThreadPool);
      } else {
        LOG(INFO) << p
                  << " exists, but is neither a regular file nor a directory";
      }
    }
  } else {
    LOG(INFO) << "path " << path_string << "isn't a directory!";
  }

  VLOG(1) << "RECURSIVE SCAN FINISHED";
  if (waitUntilFinished) {
    scanThreadPool.reset();
  }
}

void ServerFileSystem::scanNode(const string& path) {
  auto pathObj = boost::filesystem::path(path);
  try {
    if (exists(pathObj)) {
      pathObj = boost::filesystem::canonical(pathObj);
    }
  } catch (const boost::filesystem::filesystem_error& ex) {
    // Can happen for self-referencing symbolic links
    LOG(ERROR) << "Error resolving file: " << ex.what();
  }
  if (excludes.find(pathObj) != excludes.end()) {
    return;
  }

  FileData fd;
  {
    std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
    VLOG(1) << "SCANNING NODE : " << path;

    fd.set_path(absoluteToRelative(path));
    fd.set_deleted(false);
    fd.set_invalid(false);

#if __APPLE__
    // faccessat doesn't have AT_SYMLINK_NOFOLLOW
    bool symlinkToDeadFile = false;
    if (::faccessat(0, path.c_str(), F_OK, 0) != 0) {
      // The file is gone, but this could be a symlink and the symlink could
      // still be alive.

      if (boost::filesystem::symbolic_link_exists(path)) {
        symlinkToDeadFile = true;
      } else {
        VLOG(1) << "FILE IS GONE: " << path << " " << errno;
        {
          std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
          allFileData.erase(absoluteToRelative(path));
        }
        fd.set_deleted(true);
        if (handler != NULL) {
          VLOG(1) << "UPDATING METADATA: " << path;
          handler->metadataUpdated(absoluteToRelative(path), fd);
        }

        return;
      }
    }

    if (symlinkToDeadFile) {
      // TODO: Re-implement access().  Until then, clients will think they can
      // edit symlinks when they cant.
      fd.set_can_read(true);
      fd.set_can_write(true);
      fd.set_can_execute(true);
    } else {
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
    }
#else
    if (::faccessat(0, path.c_str(), F_OK, AT_SYMLINK_NOFOLLOW) != 0) {
      VLOG(1) << "FILE IS GONE: " << path << " " << errno;
      // The file is gone
      {
        std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
        allFileData.erase(absoluteToRelative(path));
      }
      fd.set_deleted(true);
      if (handler != NULL) {
        VLOG(1) << "UPDATING METADATA: " << path;
        handler->metadataUpdated(absoluteToRelative(path), fd);
      }

      return;
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
        if (s.find(rootPath) == 0) {
          s = absoluteToRelative(s);
        } else {
          // This symlink goes outside the root directory.
        }
      }
      fd.set_symlink_contents(s);
    }

    if (S_ISDIR(fileStat.st_mode)) {
      // Populate children
      for (auto& it : boost::make_iterator_range(
               boost::filesystem::directory_iterator(path), {})) {
        if (boost::filesystem::is_symlink(it.path()) ||
            boost::filesystem::is_regular_file(it.path()) ||
            boost::filesystem::is_directory(it.path())) {
          fd.add_child_node(it.path().filename().string());
        }
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
        auto xattrSize = lgetxattr(path.c_str(), key.c_str(), &xattrBuffer[0],
                                   MAX_XATTR_SIZE);
        FATAL_FAIL(xattrSize);
        string value(&xattrBuffer[0], xattrSize);
        fd.add_xattr_key(key);
        fd.add_xattr_value(value);
      }
    }

    VLOG(1) << "SETTING: " << absoluteToRelative(path);
    {
      std::lock_guard<std::recursive_mutex> lock(fileDataMutex);
      allFileData[absoluteToRelative(path)] = fd;
    }
  }

  if (handler != NULL) {
    VLOG(1) << "UPDATING METADATA: " << path;
    handler->metadataUpdated(absoluteToRelative(path), fd);
  }
}

}  // namespace codefs
