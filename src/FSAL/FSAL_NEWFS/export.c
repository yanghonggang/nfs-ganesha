#include <limits.h>
#include <stdint.h>
#include <sys/statvfs.h>
#include "abstract_mem.h"
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "internal.h"
#include "sal_functions.h"

/**
 * @brief Clean up an export
 *
 * This function cleans up an export after the last reference is released.
 *
 * @param[in,out] export_pub The export to be released
 *
 * @retval ERR_FSAL_NO_ERROR on success.
 * @retval ERR_FSAL_BUSY if the export is in use.
 */
static void release(struct fsal_export* export_pub)
{
	struct newfs_export* export = container_of(export_pub,
					struct newfs_export, export);
	deconstruct_handle(export->root);
	export->root = 0;

	fsal_detach_export(export->export.fsal, &export->export.exports);
	free_export_ops(&export->export);

	// FIXME: fdb cleanup
	gsh_free(export);
	export = NULL;
}

/**
 * @brief Return a handle corresponding to a path
 *
 * This function looks up the given path and supplies an FSAL object handle.
 *
 * @param[in] export_pub The export in which to look up the file
 * @param[in] path	 The path to look up
 * @param[in] pub_handle The created public FSAL handle
 *
 * @return FSAL status.
 */
static fsal_status_t lookup_path(struct fsal_export* export_pub,
				 const char* path,
				 struct fsal_obj_handle** pub_handle,
				 struct attrlist* attrs_out)
{
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	return status;
}

/**
 * @brief Set operations for exports
 *
 * This function overrides operations that we've implemented, leaving
 * the rest for the default.
 *
 * @param[in,out] ops Operations vector
 */
void export_ops_init(struct export_ops* ops)
{
	ops->release = release;
}
