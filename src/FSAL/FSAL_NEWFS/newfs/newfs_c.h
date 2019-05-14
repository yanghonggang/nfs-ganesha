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

// FIXME
//#include <boost/intrusive_ptr.hpp>
//typedef boost::intrusive_ptr<newfs_item> ItemRef;
struct Fh {
  struct newfs_item *item; // FIXME
  int _ref;
  int mode;
  loff_t pos;
};
typedef struct Fh Fh;

/* apis*/
//int newfs_init(const char *cluster_file, struct newfs_info **fs_info);
//int newfs_fini(struct newfs_info *fs_info);

int newfs_lookup(struct newfs_info *fs_info, struct newfs_item *parent,
                 const char *name, struct newfs_item **out, struct stat *st);
int newfs_mkdir(struct newfs_info *fs_info, struct newfs_item *parent,
                const char *name, struct stat *st, struct newfs_item **out);
int newfs_rmdir(struct newfs_info *fs_info, struct newfs_item *parent,
                const char *name);

// FIXME: start cross transaction maybe invalid
// ret:
//   < 0 ~ error
//   = 1 ~ read one entry success
//   = 0 ~ eof
//struct dirent {
//  ino_t          d_ino;       /* inode number */
//  off_t          d_off;       /* offset to the next dirent */
//  unsigned short d_reclen;    /* length of this record */
//  unsigned char  d_type;      /* type of file */
//  char           d_name[256]; /* filename */
//};
int newfs_readdir(struct newfs_info *fs_info, struct newfs_item *parent,
                  struct dirent *de, uint64_t start, struct newfs_item **out,
                  struct stat *st);
int newfs_getattr(struct newfs_info *fs_info, struct newfs_item *item,
                  struct stat *st);
// NOTE: http://idocs.umcloud.com:8090/pages/viewpage.action?pageId=8847494
int newfs_rename(struct newfs_info *fs_info, struct newfs_item *from,
                 const char* old_name, struct newfs_item *to,
                 const char* new_name);
int newfs_unlink(struct newfs_info *fs_info, struct newfs_item *parent,
                 const char *name);
int newfs_create(struct newfs_info *fs_info, struct newfs_item *parent,
                 const char *name, struct stat *st, Fh **fh,
                 struct newfs_item **out, int oflags);
// open by handle?
int newfs_open(struct newfs_info *fs_info, struct newfs_item *item,
               int flags, Fh **fh);
int newfs_close(struct newfs_info *fs_info, Fh *fh);

#ifdef __cplusplus
}
#endif
#endif
