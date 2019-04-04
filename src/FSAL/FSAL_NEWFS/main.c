#include <stdlib.h>
#include <assert.h>
#include "fsal.h"
#include "fsal_types.h"
#include "FSAL/fsal_init.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_api.h"
#include "abstract_mem.h"
#include "nfs_exports.h"
#include "export_mgr.h"
#include "nfs_core.h"
#include "sal_functions.h"

#include "internal.h"

// 20KB
#define NEWFS_MAX_FILE_SIZE	(20 << 20)

/*
 *  The name of this module.
 */
static const char* module_name = "newfs";

/**
 * Newfs global module object
 */
struct newfs_fsal_module NewFS = {
	.fsal = {
		.fs_info = {
			.maxfilesize = NEWFS_MAX_FILE_SIZE,
			.maxread = NEWFS_MAX_FILE_SIZE,
			.maxwrite = NEWFS_MAX_FILE_SIZE,
			.acl_support = 0,
			.lock_support = true,
			.lock_support_async_block = false,
		}
	}
};

/**
 * @brief Initialize and register the FSAL
 *
 */
MODULE_INIT void init(void)
{
	struct fsal_module *myself = &NewFS.fsal;

	LogDebug(COMPONENT_FSAL,
		 "NewFs module registering.");

	if (register_fsal(myself, module_name, FSAL_MAJOR_VERSION,
			  FSAL_MINOR_VERSION, FSAL_ID_NEWFS) != 0) {
		LogCrit(COMPONENT_FSAL,
			"NewFs module failed to register.");
	}

	/* override default module operations */
	// TODO
	/* Initialize the fsal_obj_handle ops for FSAL NewFS */
}

/**
 * @brief Release FSAL resources
 */
MODULE_FINI void fini(void)
{
	LogDebug(COMPONENT_FSAL, "NewFS module finishing.");

	if (unregister_fsal(&NewFS.fsal) != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to unload NewFS FSAL.");
		abort();
	}
}
