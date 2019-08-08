#include <sys/stat.h>
#include "fsal_types.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "internal.h"

/**
 * @brief FSAL status from NEWFS error
 *
 * This function returns a fsal_status_t with the FSAL error as th major,
 * and the posix error as minor.
 *
 * @param[in] newfs_errorcode NEWFS error
 *
 * @return FSAL status.
 */
fsal_status_t newfs2fsal_error(const int newfs_errorcode)
{
  fsal_status_t status = {0, 0};
  // FIXME
  return status;
}

/**
 * @brief Construct a new filehandle
 *
 * This function constructs a new NEWFS FSAL object handle and attaches
 * it to the export. After this call the attributes have been filled
 * in and the handle is up-to-date and usable.
 *
 * @param[in]	export 
 *
 */
int construct_handle(struct newfs_export *export, newfs_item *item,
                     struct stat *st, struct newfs_handle **obj)
{
  /* Pointer to the handle under construction */
  struct newfs_handle *constructing = NULL;

  assert(item);

  constructing = gsh_calloc(1, sizeof(struct newfs_handle));
  constructing->item = item;
  constructing->up_ops = export->export.up_ops;

  fsal_obj_handle_init(&constructing->handle, &export->export,
                       posix2fsal_type(st->st_mode));

  constructing->handle.obj_ops = &NewFS.handle_ops;
  constructing->handle.fsid = posix2fsal_fsid(st->st_dev);
  constructing->handle.fileid = st->st_ino;

  constructing->export = export;

  *obj = constructing;

  return 0;
}

/**
 * @brief Release all resources for a handle
 *
 * @param[in] obj Handle to release
 */
void deconstruct_handle(struct newfs_handle* obj)
{
        // FIXME: fdb
        fsal_obj_handle_fini(&obj->handle);
        gsh_free(obj);
}
