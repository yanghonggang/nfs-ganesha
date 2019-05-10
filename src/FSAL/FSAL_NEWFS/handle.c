#include <fcntl.h>
#include <fsal.h>
#include "fsal_types.h"
#include "fsal_convert.h"
#include "fsal_api.h"
#include "internal.h"
#include "nfs_exports.h"
#include "FSAL/fsal_commonlib.h"

/**
 * @brief Release an object
 *
 * This function destroys the object referred to by the given handle
 *
 * @param[in] obj_hdl The object to release
 *
 * @return FSAL status codes.
 */

static void newfs_fsal_release(struct fsal_obj_handle *obj_hdl)
{
  struct newfs_handle *obj = container_of(obj_hdl, struct newfs_handle,
                                          handle);
  if (obj != obj->export->root) {
    // FIXME: also release newfs releated stub?
    // API: 
    //
    deconstruct_handle(obj);
  }
}

/**
 * @brief Look up an object by name
 *
 * This function looks up an object by name in a directory.
 *
 * @param[in]	dir_pub	The directory in which to look up the object.
 * @param[in]	path	The name to look up
 * @param[in]	obj_pub	The looked up object.
 *
 * @return FSAL status codes.
 */

static fsal_status_t newfs_fsal_lookup(struct fsal_obj_handle *dir_hdl,
                                       const char *path,
                                       struct fsal_obj_handle **obj_hdl,
                                       struct attrlist *attrs_out)
{
  int rc;
  struct stat st;
  struct newfs_item *item;
  struct newfs_handle *obj;

  struct newfs_export *export = container_of(op_ctx->fsal_export,
                                  struct newfs_export, export);
  struct newfs_handle *dir = container_of(dir_hdl, struct newfs_handle, handle);

  LogFullDebug(COMPONENT_FSAL, "%s enter dir_hdl %p path %s", __func__,
               dir_hdl, path);

  rc = newfs_lookup(export->newfs_info, dir->item, path, &item, &st);
  if (rc < 0) {
    return newfs2fsal_error(rc);
  }

  rc = construct_handle(export, item, &st, &obj);
  if (rc < 0) {
    return newfs2fsal_error(rc);
  }

  *obj_hdl = &obj->handle;

  if (attrs_out != NULL) {
    posix2fsal_attributes_all(&st, attrs_out);
  }

  return fsalstat(0, 0);
}

/**
 * @brief Merge a duplicate handle with an original handle
 *
 * This function is used if an upper layer detects that a duplicate
 * object handle has been created. It allows the FSAL to merge anything
 * from the duplicate back into the original.
 *
 * The caller must release the object (the caller may have to close
 * files if the merge is unsuccessful).
 *
 * @param[in]	orig_hdl Original handle
 * @param[in]	Handle to merge into original
 *
 * @return	FSAL status.
 *
 */

static fsal_status_t newfs_fsal_merge(struct fsal_obj_handle *orig_hdl,
                                      struct fsal_obj_handle *dupe_hdl)
{
  fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

  if (orig_hdl->type == REGULAR_FILE &&
      dupe_hdl->type == REGULAR_FILE) {
    /* We need to merge the share reservations on this file.
     * This could result in ERR_FSAL_SHARE_DENIED.
     */
    struct newfs_handle *orig, *dupe;

    orig = container_of(orig_hdl, struct newfs_handle, handle);
    dupe = container_of(dupe_hdl, struct newfs_handle, handle);

    /* This can block over an I/O operation. */
    PTHREAD_RWLOCK_wrlock(&orig_hdl->obj_lock);

    status = merge_share(&orig->share, &dupe->share);

    PTHREAD_RWLOCK_unlock(&orig_hdl->obj_lock);
  }

  return status;
}

/**
 * @brief Create a directory
 *
 * This function creates a new directory.
 *
 * For support_ex, this method will handle attribute setting. The caller
 * MUST include the mode attribute and SHOULD NOT include the owner or
 * group attributes if they are the same as the op_ctx->cred.
 *
 * @param[in]	dir_hdl	Directory in which to create the directory
 * @param[in]	name	Name of directory to create
 * @param[in]	attrib	Attributes to set on newly created object
 * @param[out]	new_obj	Newly created object
 * @param[out]	attrs_out FIXME: ???final attrs??
 *
 * @note On success, @a new_object has beed ref'd
 *
 * @return FSAL status.
 */
static fsal_status_t newfs_fsal_mkdir(struct fsal_obj_handle *dir_hdl,
                       const char *name, struct attrlist *attrib,
                       struct fsal_obj_handle **new_obj,
                       struct attrlist *attrs_out)
{
  int rc = -1;
  struct newfs_item *item = NULL;
  struct newfs_handle *obj = NULL;
  struct stat st;

  struct newfs_export *export = container_of(op_ctx->fsal_export,
                                  struct newfs_export, export);
  struct newfs_handle *dir = container_of(dir_hdl, struct newfs_handle, handle);

  LogFullDebug(COMPONENT_FSAL, "%s enter dir_hdl %p name %s", __func__,
               dir_hdl, name);

  memset(&st, 0, sizeof(struct stat));

  st.st_uid = op_ctx->creds->caller_uid;
  st.st_gid = op_ctx->creds->caller_gid;
  st.st_mode = fsal2unix_mode(attrib->mode)
               &  ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

  rc = newfs_mkdir(export->newfs_info, dir->item, name, &st, &item);
  if (rc < 0) {
    return newfs2fsal_error(rc);
  }

  rc = construct_handle(export, item, &st, &obj);
  if (rc < 0) {
    return newfs2fsal_error(rc);
  }

  *new_obj = &obj->handle;

  if (attrs_out != NULL) {
    posix2fsal_attributes_all(&st, attrs_out);
  }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Read a directory
 *
 * This function reads the contents of a directory (excluding . and ..,
 * which is ironic since the newfs not store anything for them) and passes
 * dirent information to the supplied callback.
 *
 * @param[in]	dir_hdl		The directory to read
 * @param[in]	whence		The cookie indicating resumption, NULL to start
 * @param[in]	dir_state	Opaque, passed to cb
 * @param[in]	cb		Callback that receives directrory entries
 * @param[out]	eof		True if there are no more entries
 *
 * @return FSAL status.
 */
static fsal_status_t newfs_fsal_readdir(struct fsal_obj_handle *dir_hdl,
                       fsal_cookie_t *whence, void *cb_arg,
                       fsal_readdir_cb cb, attrmask_t attrmask, bool *eof)
{
  int rc = -1;
  fsal_status_t fsal_status = {ERR_FSAL_NO_ERROR, 0};
  uint64_t start = 0;

  struct newfs_export *export = container_of(op_ctx->fsal_export,
                                  struct newfs_export, export);
  struct newfs_handle *dir = container_of(dir_hdl, struct newfs_handle, handle);

  LogFullDebug(COMPONENT_FSAL, "%s enter dir_hdl %p", __func__, dir_hdl);

  if (whence != NULL) {
    start = *whence;
  }
  // FIXME: newfs_opendir() ????

  while (!(*eof)) {
  }
 
  return fsal_status;
}

/**
 * @brief Override functions in ops vector
 *
 * This function overrides implemented functions in the ops vector
 * with version for this FSAL.
 *
 * @param[in] ops Handle operations vector
 */

void handle_ops_init(struct fsal_obj_ops *ops)
{
  fsal_default_obj_ops_init(ops);

  ops->release = newfs_fsal_release;
  ops->lookup = newfs_fsal_lookup;
  ops->merge = newfs_fsal_merge;
  ops->mkdir = newfs_fsal_mkdir;
  ops->readdir = newfs_fsal_readdir;
}
