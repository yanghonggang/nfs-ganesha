#ifndef _NEWFS_C_H
#define _NEWFS_C_H

#include <stdint.h>
#include <sys/statvfs.h> /* struct statvfs */
#include <sys/file.h> /* struct flock */

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

/* XXX (get|set)attr mask bits */
#define NEWFS_SETATTR_MODE	(1 << 0)
/* owner */
#define NEWFS_SETATTR_UID   	(1 << 1)
/* group */
#define NEWFS_SETATTR_GID   	(1 << 2)
#define NEWFS_SETATTR_MTIME 	(1 << 3)
#define NEWFS_SETATTR_ATIME 	(1 << 4)
#define NEWFS_SETATTR_SIZE  	(1 << 5)
#define NEWFS_SETATTR_CTIME 	(1 << 6)

/* Commands for manipulating delegation state */
#ifndef NEWFS_DELEGATION_NONE
#define NEWFS_DELEGATION_NONE	(0)
#define NEWFS_DELEGATION_RD	(1)
#define NEWFS_DELEGATION_WR	(2)
#endif

/* apis*/
int newfs_init(const char *cluster_file, struct newfs_info **fs_info,
               const char *root);
int newfs_fini(struct newfs_info *fs_info);

int newfs_lookup(struct newfs_info *fs_info, struct newfs_item *parent,
                 const char *name, struct newfs_item **out, struct stat *st);

/**
 * @brief Lookup newfs item by key
 *
 * This function lookup a newfs item by its key/ino.
 *
 * @param[in]	fs_info		The info used to access all newfs methods
 * @param[in]	ino		newfs item's ino
 * @param[out]	out		returned newfs item 
 *
 * @return return 0 on success
 *         return -1 otherwise.
 */
int newfs_lookup_item(struct newfs_info *fs_info, uint64_t ino,
                      struct newfs_item **out);

/**
 * @brief Lookup newfs item by key/ino in local item cache
 *
 * This function try to find item related to the specified key in
 * local item cache.
 *
 * @param[in]	fs_info		The info used to access all newfs methods
 * @param[in]	ino		newfs item's digest(ino)
 *
 * @returns Point to newfs_item on success,
 *          NULL otherwise.
 */
struct newfs_item *newfs_get_item(struct newfs_info *fs_info, uint64_t ino);

/**
 * @brief Find newfs item of the specified path
 *
 * This function recursivelly parse the path and return related newfs_item
 * and stat.
 *
 * @param[in]	fs_info		The info used to access all newfs methods
 * @param[in]	path		Full path of a newfs dir/file
 * @param[out]	out		returned newfs item
 * @param[out]	st		returned stat info
 *
 * @return return 0 on success
 *         return -1 otherwise.
 */
int newfs_walk(struct newfs_info *fs_info, const char *path,
               struct newfs_item **out, struct stat *st);
/**
 * @brief Decrease newfs_item's reference count by 1
 *
 * @param[in]	fs_info		The info used to access all newfs methods
 * @param[in]	i		The newfs_item whose refer counter to decrease
 *
 * @return return 0 on success
 *         return -1 otherwise
 */
int newfs_put(struct newfs_info *fs_info, struct newfs_item *i);
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
int newfs_setattr(struct newfs_info *fs_info, struct newfs_item *item,
                  struct stat *st, uint32_t mask);

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

/**
 * @brief Read data from a file
 *
 * This function read data from the given file.
 *
 * @param[in]	fs_info		The info used to access all newfs methods
 * @param[in]	fh		File descriptor of the file to be read
 * @param[in]	offset		offset from the start of the file to be read
 * @param[out]	buf		store data into this buffer
 *
 * @return If success, the number of bytes actually read is returned.
 *         Upon reading end-of-file, zero is returned.
 *         Otherwrise, a -1 is returned.
 */
int newfs_read(struct newfs_info *fs_info, Fh *fh, uint64_t offset,
               uint64_t len, char *buf);

/**
 * @brief Write data into a file
 *
 * This function write data into the given file.
 *
 * @param[in]	fs_info		The info used to access all newfs methods
 * @param[in]	fh		File descriptor of the file to be written
 * @param[in]	offset		offset from the start of the file to be written
 * @param[in]	buf		data is from this buffer
 *
 * @return If success, the number of bytes actually were written is returned.
 *         Zero indicates nothing was written.
 *         Otherwrise, a -1 is returned.
 */
int newfs_write(struct newfs_info *fs_info, Fh *fh, uint64_t offset,
                uint64_t len, char *buf);

/**
 * @brief Commit buffered modifcation to disk
 *
 * This funciton commit all buffered modifications of file metadata and data to disk.
 *
 * @param[in]	fs_info		The info used to access all newfs methods
 * @param[in]	fh		File descriptor of the file to sync
 * @param[in]	syncdataonly	only sync data, not include metadata
 *
 * @return return 0 on success
 *         return -1 otherwise.
 */
int newfs_fsync(struct newfs_info *fs_info, Fh *fh, int syncdataonly);

/**
 * @brief Commit buffered modifcation of an item to disk
 *
 * This funciton commit all buffered modifications to metadata and data of a
 * newfs item to disk.
 *
 * @param[in]	fs_info		The info used to access all newfs methods
 * @param[in]	item		File descriptor of the item to sync
 * @param[in]	syncdataonly	only sync data, not include metadata
 *
 * @return return 0 on success
 *         return -1 otherwise.
 */
int newfs_sync_item(struct newfs_info *fs_info, struct newfs_item *item,
                    int syncdataonly);

/**
 * @brief Synchronize newfs's metadata and data to disk
 *
 * This function commit all buffered modification to the whole newfs to disk.
 *
 * @param[in]	fs_info		The info used to access all newfs methods
 *
 * @return return 0 on success
 *         return -1 otherwise.
 */
int newfs_sync_fs(struct newfs_info *fs_info);

int newfs_statfs(struct newfs_info *fs_info, struct newfs_item *item,
                 struct statvfs *statvfs);
/**
 * @brief Remove/Request a delegation on an open Fh
 *
 * This function commit all buffered modification to the whole newfs to disk.
 *
 * @param[in]	fs_info		The info used to access all newfs methods
 * @param[in]	fh		Handle of the open file
 * @param[in]	cmd		delegation commands
 *
 * @return return 0 on success
 *         return -1 otherwise.
 */
int newfs_delegation(struct newfs_info *fs_info, Fh *fh, unsigned int cmd);

int newfs_getlk(struct newfs_info *fs_info, Fh *fh, struct flock *lock_args,
                uint64_t owner);
int newfs_setlk(struct newfs_info *fs_info, Fh *fh, struct flock *lock_args,
                uint64_t owner, bool sleep);
#ifdef __cplusplus
}
#endif
#endif
