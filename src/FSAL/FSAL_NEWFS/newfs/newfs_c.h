#ifndef _NEWFS_C_H
#define _NEWFS_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* basic structures */
// This is the newfs internal cpp struct
//struct newfs_item;
//typedef struct newfs_item newfs_item;
struct newfs_item {
  uint64_t ino;

  uint64_t dev;
  uint64_t size;
  uint32_t nlink;
  uint32_t uid;
  uint32_t gid; 
  uint32_t mode;

  struct timespec ctime;
  struct timespec mtime;
  struct timespec atime;
};

struct newfs_info {
};

/* apis*/
//int newfs_init(const char *cluster_file, struct newfs_info **fs_info);
//int newfs_fini(struct newfs_info *fs_info);
//
//int newfs_mkdir(struct newfs_info *fs_info, struct newfs_item *parent,
//                const char *name, struct newfs_item **out);
//int newfs_rmdir(struct newfs_info *fs, struct newfs_item *parent,
//                const char *name);
//int newfs_readdir(struct newfs_info *fs, struct newfs_item *parent,
//                  struct newfs_item *out);
//
int newfs_lookup(struct newfs_info *fs_info, struct newfs_item *parent,
                 const char *name, struct newfs_item **out, struct stat *st);
int newfs_mkdir(struct newfs_info *fs_info, struct newfs_item *parent,
                const char *name, struct stat *st, struct newfs_item **out);

// FIXME: start cross transaction maybe invalid
// ret:
//   < 0 ~ error
//   = 1 ~ read one entry success
//   = 0 ~ eof
int newfs_readdir(struct newfs_info *fs_info, struct newfs_item *parent,
                  uint64_t start, struct newfs_item **out, struct stat *st); 
#ifdef __cplusplus
}
#endif
#endif
