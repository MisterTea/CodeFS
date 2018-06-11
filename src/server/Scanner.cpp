#include "Scanner.hpp"

map<string, FileData> Scanner::scanRecursively(const string& path) {}

static FileData Scanner::scanFile(const string& path) {
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
  fd.set_stat(fStat);
  if (S_ISLNK(fileStat.st_mode)) {
    int bufsiz = sb.st_size + 1;

    /* Some magic symlinks under (for example) /proc and /sys
       report 'st_size' as zero. In that case, take PATH_MAX as
       a "good enough" estimate. */

    if (sb.st_size == 0) {
      bufsiz = PATH_MAX;
    }

    string s(bufsiz, '\0');
    nbytes = readlink(path.c_str(), &s[0], bufsiz);
    FATAL_FAIL(nbytes);
    s = s.substr(0, nbytes+1);
    fd.set_symlink_contents(s);
  }
  return fd;
}
