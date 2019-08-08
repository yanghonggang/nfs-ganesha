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

  int rc = newfs_fini(export->newfs_info);
  assert(rc == 0);
  
  deconstruct_handle(export->root);
  export->newfs_info = NULL;
  export->root = NULL;
  
  fsal_detach_export(export->export.fsal, &export->export.exports);
  free_export_ops(&export->export);
  
  gsh_free(export);
  export = NULL;
}

/**
 * @brief Return a handle corresponding to a path
 *
 * This function looks up the given path and supplies an FSAL object
 * handle.
 *
 * @param[in]	export_pub	The export in which to look up the file
 * @param[in]	path		The path to look up
 * @param[out]	pub_handle	The created public FSAL handle
 *
 * @return FSAL status.
 */

static fsal_status_t lookup_path(struct fsal_export *export_pub,
                                 const char *path,
                                 struct fsal_obj_handle **pub_handle,
                                 struct attrlist *attrs_out)
{
  fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

  /* The 'private' full export handle */
  struct newfs_export *export = container_of(export_pub, struct newfs_export,
                                             export);

  /* The 'private' full object handle */
  struct newfs_handle *handle = NULL;
  /* Find the actual path in the supplied path */
  const char *realpath = NULL;
  newfs_item *item = NULL;
  struct stat st;
  int rc = -1;

  LogFullDebug(COMPONENT_FSAL,
               "path: %s, fullpath: %s",
               path, op_ctx->ctx_export->fullpath);

  if (*path != '/') {
    /* XXX newfs only support '/' path now */
    status.major = ERR_FSAL_INVAL;
    return status;
  } else {
    realpath = path;
  }

  *pub_handle = NULL;

  /* special case the root */
  if (strcmp(realpath, "/") == 0) {
    assert(export->root);
    *pub_handle = &export->root->handle;
    return status;
  }

  rc = newfs_walk(export->newfs_info, realpath, &item, &st);
  if (rc < 0)
    return newfs2fsal_error(rc);

  construct_handle(export, item, &st, &handle);

  if (attrs_out != NULL)
    posix2fsal_attributes_all(&st, attrs_out);

  *pub_handle = &handle->handle;

  return status;
}

void prepare_unexport(struct fsal_export *export_pub)
{
  struct newfs_export *export = container_of(export_pub,
                                  struct newfs_export, export);

  /* Flush all buffers */
  newfs_sync_fs(export->newfs_info);
}

/**
 * @brief Decode a digested handle
 *
 * This function decodes a previously digested handle.
 *
 * @param
 */
static fsal_status_t wire_to_host(struct fsal_export *exp_hdl,
                                  fsal_digesttype_t in_type,
                                  struct gsh_buffdesc *fh_desc,
                                  int flags)
{
  switch (in_type) {
  /* Digested Handles */
  case FSAL_DIGEST_NFSV3:
  case FSAL_DIGEST_NFSV4:
    /* wire handles */
    fh_desc->len = sizeof(struct newfs_handle_key);
    break;
  default:
    return fsalstat(ERR_FSAL_SERVERFAULT, 0);
  }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a handle object from a wire handle
 *
 * The wire handle is given in a buffer outlined by desc, which it
 * looks like we shouldn't modify.
 *
 * @param[in]	export_pub	Public export
 * @param[in]	desc		Handle buffer descriptor
 * @param[out]	pub_handle	The created handle
 *
 * @return FSAL status.
 */
static fsal_status_t create_handle(struct fsal_export *export_pub,
                                   struct gsh_buffdesc *desc,
                                   struct fsal_obj_handle **pub_handle,
                                   struct attrlist *attrs_out)
{
  /* Full 'private' export structure */
  struct newfs_export *export = container_of(export_pub, struct newfs_export,
                                             export);
  /* FSAL status to return */
  fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
  /* The FSAL specific portion of the handle received by the client */
  struct newfs_handle_key *key = desc->addr;
  int rc = -1;
  /* Stat buffer */
  struct stat st;
  /* Handle to be created */
  struct newfs_handle *handle = NULL;
  /* newfs item pointer */
  newfs_item *item = NULL;

  *pub_handle = NULL;

  if (desc->len != sizeof(struct newfs_handle_key)) {
    status.major = ERR_FSAL_INVAL;
    return status;
  }

  /* Check our local cache first */
  item = newfs_get_item(export->newfs_info, key->ino);
  if (!item) {
    /* Try the slow way, may not be in cache now. */
    rc = newfs_lookup_item(export->newfs_info, key->ino, &item);
    if (rc < 0)
      return newfs2fsal_error(rc);
  }

  rc = newfs_getattr(export->newfs_info, item, &st);
  if (rc < 0)
    return newfs2fsal_error(rc);

  construct_handle(export, item, &st, &handle);

  if (attrs_out != NULL)
    posix2fsal_attributes_all(&st, attrs_out);

  return status;
}

/**
 * @brief Get dynamic filesystem info
 *
 * This function returns dynamic filesytem information for the given
 * export.
 *
 * @param[in]	export_pub	The public export handle
 * @param[out]	info		The dynamic FS information
 *
 * @return FSAL status
 */
static fsal_status_t get_fs_dynamic_info(struct fsal_export *export_pub,
                                         struct fsal_obj_handle *obj_hdl,
                                         fsal_dynamicfsinfo_t *info)
{
  /* Full 'private' export */
  struct newfs_export *export = container_of(export_pub, struct newfs_export,
                                             export);
  /* Return value from newfs calls */
  int rc = -1;
  /* Filesystem stat */
  struct statvfs vfs_st;

  rc = newfs_statfs(export->newfs_info, export->root->item, &vfs_st);
  if (rc < 0) {
    return newfs2fsal_error(rc);
  }

  memset(info, 0, sizeof(fsal_dynamicfsinfo_t));
  info->total_bytes = vfs_st.f_frsize * vfs_st.f_blocks;
  info->free_bytes = vfs_st.f_frsize * vfs_st.f_bfree;
  info->avail_bytes = vfs_st.f_frsize * vfs_st.f_bavail;
  info->total_files = vfs_st.f_files;
  info->free_files = vfs_st.f_ffree;
  info->avail_files = vfs_st.f_favail;
  info->time_delta.tv_sec = 1;
  info->time_delta.tv_nsec = 0;

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
  ops->prepare_unexport = prepare_unexport;
  ops->release = release;
  ops->lookup_path = lookup_path;
  ops->wire_to_host = wire_to_host;
  ops->create_handle = create_handle;
  ops->get_fs_dynamic_info = get_fs_dynamic_info;
  ops->alloc_state = newfs_alloc_state;
  ops->free_state = newfs_free_state;
}
