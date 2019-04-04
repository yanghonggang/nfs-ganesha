#ifndef _FSAL_NEWFS_INTERNAL_H
#define _FSAL_NEWFS_INTERNAL_H

#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "fsal_convert.h"
#include <stdbool.h>
#include <uuid/uuid.h>
#include "FSAL/fsal_commonlib.h"

struct newfs_fsal_module {
	struct fsal_module fsal;
	struct fsal_obj_ops handle_ops;
	char* ceph_conf_path;
	char* fdb_conf_path;
};
extern struct newfs_fsal_module NewFS;

#endif
