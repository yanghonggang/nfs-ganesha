#ifndef _FSAL_NEWFS_INTERNAL_H
#define _FSAL_NEWFS_INTERNAL_H

#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "fsal_convert.h"
#include <stdbool.h>
#include <uuid/uuid.h>
#include "FSAL/fsal_commonlib.h"

#include "newfs/newfs_c.h"

/* Max length of a ceph user_id string */
#define	MAXUIDLEN	(64)

/* Max length of a secret key for the ceph user */
#define MAXSECRETLEN	(88)

/* Max file size in newfs */
#define NEWFS_MAX_FILE_SIZE     (20 << 20)

struct newfs_fsal_module {
	struct fsal_module fsal;
	struct fsal_obj_ops handle_ops;
	char* ceph_conf_path;
	char* fdb_conf_path;		/*< foudationdb conf path */
	/* TODO: FDBDatabase* db */
};
extern struct newfs_fsal_module NewFS;

struct newfs_fd {
  /* The open and share mode etc. */
  fsal_openflags_t openflags;
  /* rw lock to protect the file descriptor */
  pthread_rwlock_t fdlock;
  /* The newfs file descriptor. */
  Fh *fd;
};

struct newfs_state_fd {
  struct state_t state;
  struct newfs_fd newfs_fd;
};

struct newfs_handle {
	struct fsal_obj_handle handle;		/*< The public handle */
        struct newfs_fd fd;
        struct newfs_item *item;		/*< newfs-internal file/dir
                                                    item*/
	const struct fsal_up_vector* up_ops;	/*< FIXME */
	struct fsal_share share;		/*< ref: newfs_fsal_merge */

	struct newfs_export* export;		/*< The first export this handle
						 *< belongs to */
};

/**
 * NewFS private export object
 */
struct newfs_export {
	struct fsal_export export; /*< The public export object */
	struct newfs_info *newfs_info; /*< The info used to access all newfs
                                         methods on this export */
	struct newfs_handle* root; /*< The root handle */

	// FIXME: fdb related members
	char* user_id; 		   /* cephx user_id for this mount */
	char* secret_key;	   /* keyring path of ceph user */
	char* cephf_conf;	   /* config file of the backend ceph cluster */
};


fsal_status_t newfs2fsal_error(const int newfs_errorcode);
int construct_handle(struct newfs_export *export, struct newfs_item *item,
                     struct stat *st, struct newfs_handle **obj);
void deconstruct_handle(struct newfs_handle* obj);

void handle_ops_init(struct fsal_obj_ops *ops);

#endif
