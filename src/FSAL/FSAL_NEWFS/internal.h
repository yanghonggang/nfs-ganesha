#ifndef _FSAL_NEWFS_INTERNAL_H
#define _FSAL_NEWFS_INTERNAL_H

#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "fsal_convert.h"
#include <stdbool.h>
#include <uuid/uuid.h>
#include "FSAL/fsal_commonlib.h"

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

struct newfs_handle {
	struct fsal_obj_handle handle;		/*< The public handle */
	const struct fsal_up_vector* up_ops;	/*< FIXME */
	struct fsal_share share;		/*< FIXME */

	struct newfs_export* export;		/*< The first export this handle
						 *< belongs to */
};

/**
 * NewFS private export object
 */
struct newfs_export {
	struct fsal_export export; /*< The public export object */
	struct newfs_handle* root; /*< The root handle */

	// FIXME: fdb related members
	char* user_id; 		   /* cephx user_id for this mount */
	char* secret_key;	   /* keyring path of ceph user */
	char* cephf_conf;	   /* config file of the backend ceph cluster */
};


void deconstruct_handle(struct newfs_handle* obj);

#endif
