#ifndef __FILESYSTEM_H__
#define __FILESYSTEM_H__

#include "Headers.hpp"

namespace codefs {
class FileSystem {
 public:
  virtual ~FileSystem() {}
  virtual void write(const string &path, const string &data) = 0;
  virtual string read(const string &path) = 0;
};
}  // namespace codefs

#ifdef __APPLE__
// Add missing xattr functions for OS/X

#define G_PREFIX "org"
#define G_KAUTH_FILESEC_XATTR G_PREFIX ".apple.system.Security"
inline ssize_t llistxattr(const char *path, char *list, size_t size) {
  ssize_t res = listxattr(path, list, size, XATTR_NOFOLLOW);
  if (res > 0) {
    if (list) {
      size_t len = 0;
      char *curr = list;
      do {
        size_t thislen = strlen(curr) + 1;
        if (strcmp(curr, G_KAUTH_FILESEC_XATTR) == 0) {
          memmove(curr, curr + thislen, res - len - thislen);
          res -= thislen;
          break;
        }
        curr += thislen;
        len += thislen;
      } while (len < res);
    }
  }

  return res;
}

#define A_PREFIX "com"
#define A_KAUTH_FILESEC_XATTR A_PREFIX ".apple.system.Security"
#define XATTR_APPLE_PREFIX "com.apple."
inline ssize_t lgetxattr(const char *path, const char *name, void *value,
                         size_t size) {
  int res;

  if (strcmp(name, A_KAUTH_FILESEC_XATTR) == 0) {
    char new_name[MAXPATHLEN];

    memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
    memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);

    res = getxattr(path, new_name, value, size, 0, XATTR_NOFOLLOW);

  } else {
    res = getxattr(path, name, value, size, 0, XATTR_NOFOLLOW);
  }

  return res;
}

inline int lsetxattr(const char *path, const char *name, const void *value,
                     size_t size, int flags) {
  int res;

  if (!strncmp(name, XATTR_APPLE_PREFIX, sizeof(XATTR_APPLE_PREFIX) - 1)) {
    flags &= ~(XATTR_NOSECURITY);
  }

  if (!strcmp(name, A_KAUTH_FILESEC_XATTR)) {
    char new_name[MAXPATHLEN];

    memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
    memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);

    res = setxattr(path, new_name, value, size, 0, XATTR_NOFOLLOW);

  } else {
    res = setxattr(path, name, value, size, 0, XATTR_NOFOLLOW);
  }

  if (res == -1) {
    return -errno;
  }

  return 0;
}

inline int lremovexattr(const char *path, const char *name) {
  int res;

  if (strcmp(name, A_KAUTH_FILESEC_XATTR) == 0) {
    char new_name[MAXPATHLEN];

    memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
    memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);

    res = removexattr(path, new_name, XATTR_NOFOLLOW);

  } else {
    res = removexattr(path, name, XATTR_NOFOLLOW);
  }

  return res;
}

#endif

#endif  // __FILESYSTEM_H__
