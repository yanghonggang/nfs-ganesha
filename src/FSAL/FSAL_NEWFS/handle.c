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
                       fsal_cookie_t *whence, void *dir_state,
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
  //        we need a newfs_opendir() or we cannot make sure dir's entries
  //        stay the same as the first time newfs_readir()
  while (!(*eof)) {
    struct newfs_item *item = NULL;
    struct stat st;
    struct dirent de;
    rc = newfs_readdir(export->newfs_info, dir->item, &de, start, &item, &st);
    if (rc < 0) {
      fsal_status = newfs2fsal_error(rc);
      break;
    } else if (rc == 1) {
      struct newfs_handle *obj = NULL;
      struct attrlist attrs;
      enum fsal_dir_result cb_rc;

      // FIXME: how to handle . and ..
      rc = construct_handle(export, item, &st, &obj);
      if (rc < 0) {
        fsal_status = newfs2fsal_error(rc);
        break;
      }

      fsal_prepare_attrs(&attrs, attrmask);
      posix2fsal_attributes_all(&st, &attrs);
      // TODO: security labels support
      // rc = newfs_fsal_get_sec_label(obj, &attrs);
      // if (rc < 0) {
      //   fsal_status = newfs2fsal_error(rc);
      //   break;
      // }
      cb_rc = cb(de.d_name, &obj->handle, &attrs, dir_state, de.d_off);
      fsal_release_attrs(&attrs);
      if (cb_rc >= DIR_READAHEAD) {
        /* Read ahead not supported by this FSAL. */
        break;
      }
      start ++; /* next entry */
    } else if (rc == 0) {
      *eof = true;
    } else {
      /* Can't happend */
      abort();
    }
  }
 
  return fsal_status;
}

/**
 * @brief Freshen and return attributes
 *
 * This function freshens and returns the attributes of the given file.
 *
 * @param[in] obj_hdl Object to interrogate
 *
 * @return FSAL status
 *
 */
static fsal_status_t newfs_fsal_getattrs(struct fsal_obj_handle *obj_hdl,
                                         struct attrlist *attrs)
{
  int rc = -1;
  struct stat st;

  struct newfs_export *export = container_of(op_ctx->fsal_export,
                                             struct newfs_export, export);
  struct newfs_handle *handle = container_of(obj_hdl, struct newfs_handle,
                                             handle);

  LogFullDebug(COMPONENT_FSAL, "%s enter obj_hdl %p", __func__, obj_hdl);

  rc = newfs_getattr(export->newfs_info, handle->item, &st);
  if (rc < 0) {
    if (attrs->request_mask & ATTR_RDATTR_ERR) {
      /* Caller asked for error to be visible */
      attrs->valid_mask = ATTR_RDATTR_ERR;
    }
    return newfs2fsal_error(rc);
  }

  posix2fsal_attributes_all(&st, attrs);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Rename a file
 *
 * This function renames a file, possible moving it into another directory.
 * We assume most checks are done by the caller.
 *
 * @param[in]	olddir_hdl	Source directory
 * @param[in]	old_name	Original name
 * @param[in]	newdir_hdl	Destination directory
 * @param[in]	new_name	New name
 *
 * @return FSAL status.
 */
static fsal_status_t newfs_fsal_rename(struct fsal_obj_handle *obj_hdl,
                                       struct fsal_obj_handle *olddir_hdl,
                                       const char *old_name,
                                       struct fsal_obj_handle *newdir_hdl,
                                       const char *new_name)
{
  int rc = -1;
  struct newfs_export *export = container_of(op_ctx->fsal_export,
                                             struct newfs_export, export);
  struct newfs_handle *olddir = container_of(olddir_hdl, struct newfs_handle,
                                             handle);
  struct newfs_handle *newdir = container_of(newdir_hdl, struct newfs_handle,
                                             handle);

  LogFullDebug(COMPONENT_FSAL, "%s enter obj_hdl %p olddir_hdl %p oname %s"
               " newdir_hdl %p nname %s", __func__, obj_hdl, olddir_hdl,
               new_name, newdir_hdl, new_name);

  rc = newfs_rename(export->newfs_info, olddir->item, old_name, newdir->item,
                    new_name);
  if (rc < 0) {
    /*
     * RFC5661, section 18.26.3 - renaming on top of a non-empty direcotry
     * should return NFS4ERR_EXIST. pg474
     */
    if (rc == -ENOTEMPTY)
      rc = -EEXIST;
    return newfs2fsal_error(rc);
  }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Remove a name
 *
 * This function removes a name from the filesytem and possibly deletes the
 * associated file. Directories must be empty to be removed.
 *
 * @param[in]	dir_hdl	The directory from which to remove the name
 * @param[in]	obj_hdl	The object being removed
 * @param[in]	name	The name to remove
 *
 * @return FSAL status.
 */

static fsal_status_t newfs_fsal_unlink(struct fsal_obj_handle *dir_hdl,
                                       struct fsal_obj_handle *obj_hdl,
                                       const char *name)
{
  int rc = -1;
  struct newfs_export *export = container_of(op_ctx->fsal_export,
                                             struct newfs_export, export);
  struct newfs_handle *dir = container_of(dir_hdl, struct newfs_handle, handle);

  LogFullDebug(COMPONENT_FSAL, "Unlink %s, type %s", name,
               object_file_type_to_str(obj_hdl->type));

  if (obj_hdl->type != DIRECTORY) {
    rc = newfs_unlink(export->newfs_info, dir->item, name);
  } else {
    rc = newfs_rmdir(export->newfs_info, dir->item, name);
  }
  if (rc < 0) {
    LogDebug(COMPONENT_FSAL, "Unlink %s returned %s (%d)", name, strerror(-rc),
             -rc);
    return newfs2fsal_error(rc);
  }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Open a newfs_fd
 *
 * @param[in] myself		The newfs internal object handle
 * @param[in] openflags		Mode for open
 * @param[in] posix_flags	POSIX open flags for open
 *
 * @return FSAL status.
 */

static fsal_status_t newfs_open_my_fd(struct newfs_handle *myself,
                                      fsal_openflags_t openflags,
                                      int posix_flags, struct newfs_fd *my_fd)
{
  int rc = -1;
  struct newfs_export *export = container_of(op_ctx->fsal_export,
                                             struct newfs_export, export);

  LogFullDebug(COMPONENT_FSAL,
               "my_fd = %p my_fd->fd = %p openflags = %x, posix_flags = %x",
               my_fd, my_fd->fd, openflags, posix_flags);

  assert(my_fd->fd == NULL
         && my_fd->openflags == FSAL_O_CLOSED && openflags != 0);

  LogFullDebug(COMPONENT_FSAL,
               "openflags = %x, posix_flags = %x",
               openflags, posix_flags);
  rc = newfs_open(export->newfs_info, myself->item, posix_flags,
                  &my_fd->fd);
  if (rc < 0) {
    my_fd->fd = NULL;
    LogFullDebug(COMPONENT_FSAL,
                 "open failed with %s",
                 strerror(-rc));
    return newfs2fsal_error(rc);
  }

  /* Save the file descriptor, make sure we only save the
   * open modes that actually represent the open file.
   */
  LogFullDebug(COMPONENT_FSAL,
               "fd = %p, new openflags = %x",
               my_fd->fd, openflags);

  my_fd->openflags = openflags;

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t newfs_close_my_fd(struct newfs_handle *handle,
                                       struct newfs_fd *my_fd)
{
  int rc = -1;
  fsal_status_t status = fsalstat(ERR_FSAL_NO_ERROR, 0);

  if (my_fd->fd != NULL && my_fd->openflags != FSAL_O_CLOSED) {
    rc = newfs_close(handle->export->newfs_info, my_fd->fd);
    if (rc < 0)
      status = newfs2fsal_error(rc);
    my_fd->fd = NULL;
    my_fd->openflags = FSAL_O_CLOSED;
  }
  return status;
}

/*
 * @brief Close a file
 *
 * This function closes a file, freeing resources used for read/write
 * access and releasing capabilities.
 *
 * @param[in] obj_hdl File to close
 *
 * @return FSAL status.
 */

static fsal_status_t newfs_fsal_close(struct fsal_obj_handle *obj_hdl)
{
  fsal_status_t status;

  struct newfs_handle *handle = container_of(obj_hdl, struct newfs_handle,
                                             handle);

  if (handle->fd.openflags == FSAL_O_CLOSED)
    return fsalstat(ERR_FSAL_NOT_OPENED, 0);

  /* Take write lock on object to protect file descriptor.
   * This can block over an I/O operation.
   */
  PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

  status = newfs_close_my_fd(handle, &handle->fd);

  PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

  return status;
}

/**
 * @brief Open a file descriptor for read or write and possibly create
 *
 * This function opens a file for read or write, possibly creating it.
 * If the caller is passing a state, it must hold the state_lock 
 * execlusive.
 *
 * stat can be NULL which indicates a stateless open (such as via the
 * NFS v3 CREATE operation), in which case the FSAL must assure protection
 * of any resources. If the file being created, such protection is
 * simple since no one else will have access to the object yet, however,
 * in the case of an exclusive create, the common resources may still need
 * protection.
 *
 * If Name is NULL, obj_hdl is the file itself, otherwise obj_hdl is the 
 * parent directory.
 *
 * On an exclusive create, the upper layer may know the object handle
 * already, so it MAY call with name == NULL. In this case, the caller
 * expectes just to check the verifier.
 *
 * On a call with an existing object handle for an UNCHECKED create,
 * we can set the size to 0.
 *
 * If attributes are not set on create, the FSAL will set some minimal
 * attributes (for example, mode might be set to 0600).
 *
 * If an open by name success and did not result in Ganesha creating a file,
 * the caller will need to do subsequent permission check to confirm the
 * open. This is becuase the permission attributes were not available
 * beforehand.
 *
 * @param[in] obj_hdl	 		File to open or parent directory
 * @param[in,out] state	 		state_t to use for this operation
 * @param[in] openflags	 		Mode for open
 * @param[in] ceatemode	 		Mode for create
 * @param[in] name	 		Name for file if being created or opened
 * @param[in] attrib_set 		Attributes to set on created file
 * @param[in] verifier	 		Verifier to use for exclusive create
 * @param[in,out] new_obj		Newly created object
 * @param[in,out] caller_perm_check     The caller must do a permission check
 *
 * @return FSAL status.
 */
fsal_status_t newfs_fsal_open2(struct fsal_obj_handle *obj_hdl,
                               struct state_t *state,
                               fsal_openflags_t openflags,
                               enum fsal_create_mode createmode,
                               const char *name,
                               struct attrlist *attrib_set,
                               fsal_verifier_t verifier,
                               struct fsal_obj_handle **new_obj,
                               struct attrlist *attrs_out,
                               bool *caller_perm_check)
{
  fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
  int rc = -1;
  struct newfs_fd *my_fd = NULL;
  struct stat st;
  bool truncated = false;
  bool created = false;
  bool setattrs = attrib_set != NULL;
  mode_t unix_mode = 0;
  int posix_flags = 0;

  struct newfs_export *export = container_of(op_ctx->fsal_export,
                                  struct newfs_export, export);
  Fh *fd = NULL;
  struct newfs_item *item = NULL;
  struct newfs_handle *obj = NULL;

  struct newfs_handle *myself = container_of(obj_hdl, struct newfs_handle,
                                             handle);

  LogFullDebug(COMPONENT_FSAL, "%s enter obj_hdl %p", __func__, obj_hdl);

  if (state != NULL) {
    my_fd = &container_of(state, struct newfs_state_fd, state)->newfs_fd;
  }

  if (setattrs) {
    LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG, "attrs ", attrib_set, false);
  }

  fsal2posix_openflags(openflags, &posix_flags);
  truncated = (posix_flags & O_TRUNC) != 0;

  if (createmode >= FSAL_EXCLUSIVE) {
    /* Now fixup attrs for verifier if exclusive create */
    set_common_verifier(attrib_set, verifier); 
  }

  /* obj_hdl is the file itself */
  if (name == NULL) {
    /* This is an open by handle */
    if (state != NULL) {
      /* Prepare to take the share reservation, but only if we
       * are called with a valid state (if state is NULL the
       * caller is a stateless create such as NFS v3 CREATE).
       */

      /* This can block over an I/O operation. */
      PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);
      /* Check share reservation conflicts. */
      status = check_share_conflict(&myself->share,
                                    openflags, false);

      if (FSAL_IS_ERROR(status)) {
        PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
        return status;
      }

      /* Take the share reservation now by updating the
       * counters.
       */
      update_share_counters(&myself->share, FSAL_O_CLOSED,
                            openflags);

      PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
    } else {
      /* We need to use the global fd to continue, and take  
       * the lock to protect it.
       */
      my_fd = &myself->fd;
      PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);
    }

    if (my_fd->openflags != FSAL_O_CLOSED) {
      newfs_close_my_fd(myself, my_fd);
    }
    status = newfs_open_my_fd(myself, openflags, posix_flags, my_fd);

    if (FSAL_IS_ERROR(status)) {
      if (state == NULL) {
        /* Release the lock taken above, and return
         * since there is nothing to undo
         */
        PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
        return status;
      } else {
        /* Error - need to release the share */
        goto undo_share;
      }
    }

    if (createmode >= FSAL_EXCLUSIVE || truncated) {
      /* Refresh the attributes */
      rc = newfs_getattr(export->newfs_info, myself->item, &st);
      if (rc == 0) {
        LogFullDebug(COMPONENT_FSAL,
                     "New size = %"PRIx64,
                     st.st_size);
      } else {
        status = newfs2fsal_error(rc);
      }

      /* Now check verifier for exclusive, but not for
       * FSAL_EXCLUSIVE_9P.
       */
      if (!FSAL_IS_ERROR(status) &&
          createmode >= FSAL_EXCLUSIVE &&
          createmode != FSAL_EXCLUSIVE_9P &&
          !obj_hdl->obj_ops->check_verifier(
            obj_hdl, verifier)) {
        /* Verifier didn't match */
        status = fsalstat(posix2fsal_error(EEXIST), EEXIST);
      }

      if (attrs_out) {
        /* Save out new attributes */
        posix2fsal_attributes_all(&st, attrs_out);
      }
    } else if (attrs_out && attrs_out->request_mask &
               ATTR_RDATTR_ERR) {
      attrs_out->valid_mask = ATTR_RDATTR_ERR;
    }

    if (state == NULL) {
      /* If no state, release the lock taken above and return
       * status. If success, we haven't done any permission
       * check so ask the caller to do so.
       */
      PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
      *caller_perm_check = !FSAL_IS_ERROR(status);
      return status;
    }

    if (!FSAL_IS_ERROR(status)) {
      /* Return success. We haven't done any permission
       * check so ask the caller to do so.
       */
      *caller_perm_check = true;
      return status;
    }

    /* Close on error */
    (void) newfs_close_my_fd(myself, my_fd);

undo_share:
    /* Can only get here with status not NULL and an error */

    /* On error we need to release our share reservation
     * and undo the update of the share counters.
     * This can block an I/O operation
     */
    PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

    update_share_counters(&myself->share, openflags, FSAL_O_CLOSED);

    PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

    return status;
  } /* !name */

  /*
   * In this path where we are opening by name, we can't check share
   * reservation yet since we don't hava an object_handle yet. If we
   * indeed create the object handle (there is no race with another
   * open by name), then there CAN NOT be a share conflict, otherwise
   * the share conflict will be resolved when the object handles are
   * mergeed.
   */
  if (createmode == FSAL_NO_CREATE) {
    /* Non creation case, newfs doesn't have open by name so we
     * have to do a lookup and then handle as an open by handle.
     * FIXME: newfs add a open by name interface???
     */
    struct fsal_obj_handle *temp = NULL;

    /* We don't have open by name... */
    status = obj_hdl->obj_ops->lookup(obj_hdl, name, &temp, NULL);

    if (FSAL_IS_ERROR(status)) {
      LogFullDebug(COMPONENT_FSAL, "lookup returned %s", fsal_err_txt(status));
      return status;
    }

    /* Now call ourselves without name and attributes to open */
    status = obj_hdl->obj_ops->open2(temp, state, openflags,
                                     FSAL_NO_CREATE, NULL, NULL,
                                     verifier, new_obj,
                                     attrs_out,
                                     caller_perm_check);

    if (FSAL_IS_ERROR(status)) {
      /* Release the object we found by lookup. */
      temp->obj_ops->release(temp);
      LogFullDebug(COMPONENT_FSAL, "open returned %s", fsal_err_txt(status));
    }

    return status;
  } /* == FSAL_NO_CREATE */

  /*
   * Now add in O_CREAT and O_EXCL.
   * Even with FSAL_UNGUARDED we try exclusive create first so
   * we can safely set attributes.
   */
  if (createmode != FSAL_NO_CREATE) {
    /* Now add in O_CREAT and O_EXCL. */
    posix_flags |= O_CREAT;

    /* And if we are at least FSAL_GUARDED, do an O_EXCL create. */
    if (createmode >= FSAL_GUARDED)
      posix_flags |= O_EXCL;

    /* Fetch the mode attribute to use in the openat system call. */
    unix_mode = fsal2unix_mode(attrib_set->mode) &
                  ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

    /* Don't set the mode if we later set the attributes */
    FSAL_UNSET_MASK(attrib_set->valid_mask, ATTR_MODE);
  } /* != FSAL_NO_CREATE*/

  /*
   * If we have FSAL_UNCHECKED and want to set more attributes
   * than the mode, we attempt an O_EXCL create first, if that
   * success, then we will be allowed to set the additional
   * attributes, otherwise, we don't know we created the file
   * and this can NOT set the attributes. 
   */ 
  if (createmode == FSAL_UNCHECKED && (attrib_set->valid_mask != 0)) {
    posix_flags |= O_EXCL;
  }

  memset(&st, 0, sizeof(struct stat));

  st.st_uid = op_ctx->creds->caller_uid;
  st.st_gid = op_ctx->creds->caller_gid;
  st.st_mode = unix_mode;

  /* myself->item => parent item */
  rc = newfs_create(export->newfs_info, myself->item, name, &st, &fd, &item,
                    posix_flags);
  if (rc < 0) {
    LogFullDebug(COMPONENT_FSAL, "Create %s failed with %s", name,
                 strerror(-rc));
  }

  if (rc == -EEXIST && createmode == FSAL_UNCHECKED) {
    /* We tried to create O_EXCL to set attributes and failed.
     * Remove O_EXCL and retry, also remember not to set attributes.
     * We still try O_CREATE again just in case file disappears out
     * from under us.
     *
     * Note that because we have dropped O_EXCL, later on we will
     * not assume we created the file, and thus will not set
     * additional attributes. We don't need to separately track
     * the condition of not wanting to set attributes.
     */
     posix_flags &= ~O_EXCL;
     rc = newfs_create(export->newfs_info, myself->item, name, &st, &fd, &item,
                       posix_flags);
     if (rc < 0) {
       LogFullDebug(COMPONENT_FSAL, "Non-exclusive Create %s failed with %s",
                    name, strerror(-rc));
     }
  }
  if (rc < 0) {
    return newfs2fsal_error(rc);
  }

  /* Remember if we were responsible for creating the file.
   * Note that in an UNCHECKED retry we MIGHT have re-created the
   * file and won't remember that. Oh well, so in that rare case we
   * leak a partially created file if we have a subsequent error in here. 
   */
  created = (posix_flags & O_EXCL) != 0;
  *caller_perm_check = false;

  construct_handle(export, item, &st, &obj);

  /* If we didn't have a state above, use the global fd. At this point,
   * since we just created the global fd, no one else can have a
   * reference to it, and thus we can manipulate unlocked which is
   * handy since we can then call setattr2 which WILL take the lock
   * without a double locking deadlock. 
   */
  if (my_fd == NULL)
    my_fd = &obj->fd;

  my_fd->fd = fd;
  my_fd->openflags = openflags;

  *new_obj = &obj->handle;

  if (created && setattrs && attrib_set->valid_mask != 0) {
    /*
     * Set attributes using our newly opened file descriptor as the
     * share_fd if there are any left to set(mode and truncate
     * have already been handled).
     *
     * Note that we only set the attributes if we were responsible
     * for creating the file and we have attributes to set.
     */
    status = (*new_obj)->obj_ops->setattr2(*new_obj,
                                           false,
                                           state,
                                           attrib_set);
    if (FSAL_IS_ERROR(status))
      goto fileerr;

    if (attrs_out != NULL) {
      status = (*new_obj)->obj_ops->getattrs(*new_obj,
                                             attrs_out);
      if (FSAL_IS_ERROR(status) &&
          (attrs_out->request_mask & ATTR_RDATTR_ERR) == 0) {
        /* Get attributes failed and caller expected
         * to get the attributes. Otherwise continue
         * with attrs_Out indicating ATTR_RDATTR_ERR.
         */
        goto fileerr;
      }
    }
  } else if (attrs_out != NULL) {
    /* Since we haven't set any attributes other than what was set
     * on create (if we event created), just use the stat results
     * we used to create the fsal_obj_handle.
     */
    posix2fsal_attributes_all(&st, attrs_out);
  }

  if (state != NULL) {
    /* Prepare to take the share reservation, but only if we are
     * called with a valid state (if state is NULL
     */

    /* This can block over an I/O operation. */
    PTHREAD_RWLOCK_wrlock(&(*new_obj)->obj_lock);

    /* Take the share reservation now by updating the counters. */
    update_share_counters(&obj->share, FSAL_O_CLOSED, openflags);

    PTHREAD_RWLOCK_unlock(&(*new_obj)->obj_lock);
  }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

fileerr:

  /* Close the file we just opened. */
  (void) newfs_close_my_fd(container_of(*new_obj,
                             struct newfs_handle, handle), my_fd);

  /* Release the handle we just allocated. */
  (*new_obj)->obj_ops->release(*new_obj);
  *new_obj = NULL;

  if (created) {
    /* Remove the file we just created. */
    newfs_unlink(export->newfs_info, myself->item, name);
  }

  return status;
}

/**
 * @Brief Re-open a file that may be already opened
 *
 * This function supports changing the access mode of a share reservation and
 * thus should only be called with a share state. This state_lock must be held.
 *
 * This MAY be used to open a file the first time if there is no need for
 * open by name or create semantics. One example would be 9P lopen
 *
 * @param[in] obj_hdl	File on which to operate
 * @param[in] state	state_t to use for this operation
 * @param[in] openflags	Mode for re-open
 *
 * @return FSAL status.
 */

static fsal_status_t newfs_fsal_reopen2(struct fsal_obj_handle *obj_hdl,
                                        struct state_t *state,
                                        fsal_openflags_t openflags)
{
  fsal_status_t status = {0, 0};
  int posix_flags = 0;
  fsal_openflags_t old_openflags;

  struct newfs_fd *my_share_fd = NULL;
  struct newfs_fd temp_fd = {FSAL_O_CLOSED, PTHREAD_RWLOCK_INITIALIZER, NULL};
  struct newfs_fd *my_fd = &temp_fd;

  struct newfs_handle *myself = container_of(obj_hdl, struct newfs_handle,
                                             handle);
  my_share_fd = &container_of(state, struct newfs_state_fd,
                              state)->newfs_fd;

  LogFullDebug(COMPONENT_FSAL, "%s enter obj_hdl %p", __func__, obj_hdl);

  fsal2posix_openflags(openflags, &posix_flags);

  /* This can block over an I/O operation. */
  PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

  old_openflags = my_share_fd->openflags;

  /* We can conflict with old share, so go ahead and check now. */
  status = check_share_conflict(&myself->share, openflags, false);

  if (FSAL_IS_ERROR(status)) {
    PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

    return status;
  }

  /* Set up the new share so we can drop the lock and not have a
   * conflicting share be asserted, updating the share conters.
   */
  update_share_counters(&myself->share, old_openflags, openflags);

  PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

  status = newfs_open_my_fd(myself, openflags, posix_flags, my_fd);

  if (FSAL_IS_ERROR(status)) {
    /* Close the existing file descriptor and copy the new
     * one over. Make sure on one is using the fd that we are
     * about to close!
     */
    PTHREAD_RWLOCK_wrlock(&my_share_fd->fdlock);

    newfs_close_my_fd(myself, my_share_fd);
    my_share_fd->fd = my_fd->fd;
    my_share_fd->openflags = my_fd->openflags;

    PTHREAD_RWLOCK_unlock(&my_share_fd->fdlock);
  } else {
    /* We had a failure on open - we need to revert the share.
     * This can block over an I/O operation.
     */
    PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

    update_share_counters(&myself->share, openflags, old_openflags);

    PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
  }

  return status;
}

/**
 * @Brief Manage closing a file when a state is no longer needed.
 *
 * When the upper layers are ready to dispense with a state, this method is
 * called to allow the FSAL to close any file descriptors or release any other
 * resources associated with the state. A call to free_state should be assumed
 * to follow soon.
 *
 * @param[in] obj_hdl	File on which to operate
 * @param[in] state	state_t to use for this operation
 *
 * @reurn FSAL status.
 */
static fsal_status_t newfs_fsal_close2(struct fsal_obj_handle *obj_hdl,
                                      struct state_t *state)
{
  fsal_status_t status = {0, 0};
  struct newfs_handle *myself = container_of(obj_hdl, struct newfs_handle,
                                             handle);
  struct newfs_fd *my_fd = &container_of(state, struct newfs_state_fd,
                                             state)->newfs_fd;

  if (state) {
    if (state->state_type == STATE_TYPE_SHARE ||
        state->state_type == STATE_TYPE_NLM_SHARE ||
        state->state_type == STATE_TYPE_9P_FID) {
      /* Tihs is a share state, we must update the share conters */

      /* This can block over an I/O operation */
      PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

      update_share_counters(&myself->share,
                            my_fd->openflags,
                            FSAL_O_CLOSED);

      PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
    }
  } else if (my_fd->openflags == FSAL_O_CLOSED) {
    return fsalstat(ERR_FSAL_NOT_OPENED, 0);
  }

  /* Acquire state's fdlock to make sure no other thread
   * is operating on the fd while we close it.
   */
  PTHREAD_RWLOCK_wrlock(&my_fd->fdlock);
  status = newfs_close_my_fd(myself, my_fd);
  PTHREAD_RWLOCK_unlock(&my_fd->fdlock);

  return status;
}

/**
 * @brief Return open status of a state.
 *
 * This function returns open flags representing the current open
 * status for a state. This state_lock must be held.
 *
 * @param[in] obj_hdl	File on which to operate
 * @param[in] state	File state to interrogate
 *
 * @retval Flags representing current open status
 */

static fsal_openflags_t newfs_fsal_status2(struct fsal_obj_handle *obj_hdl,
                                           struct state_t *state)
{
  struct newfs_fd *my_fd = &container_of(state, struct newfs_state_fd,
                                             state)->newfs_fd;

  return my_fd->openflags;
}

/**
 * @brief Function to open an fsal_obj_handle's global file descriptor.
 *
 * @param[in]	obj_hdl		File on which to operate
 * @param[in]	openflags	Mode for open
 * @param[out]	fd		File descriptor that is to be used
 *
 * return FSAL status.
 */
static fsal_status_t newfs_open_func(struct fsal_obj_handle *obj_hdl,
                                     fsal_openflags_t openflags,
                                     struct fsal_fd *fd)
{
  int posix_flags = 0;
  struct newfs_handle *myself = container_of(obj_hdl, struct newfs_handle,
                                             handle);


  fsal2posix_openflags(openflags, &posix_flags);

  return newfs_open_my_fd(myself, openflags, posix_flags,
                          (struct newfs_fd *)fd);
}

/**
 * @brief Function to close an fsal_obj_handle's global file descriptor.
 *
 * @param[in]	obj_hdl	File on which to operate
 * @param[in]	fd	File handle to close
 *
 * @return FSAL status.
 */
static fsal_status_t newfs_close_func(struct fsal_obj_handle *obj_hdl,
                                      struct fsal_fd *fd)
{
  struct newfs_handle *myself = container_of(obj_hdl, struct newfs_handle,
                                             handle);

  return newfs_close_my_fd(myself, (struct newfs_fd *)fd);
}

/**
 * @brief Find a file descriptor for a read or write operation.
 *
 * @param[out]	fd		File descriptor	 
 * @param[in]	obj_hdl		File on which to operate
 * @param[in]	bypass		If state doesn't indicate a share reservation,
 *                              bypass any deny read
 * @param[in]	state		state_t to use for this operation
 * @param[in]	openflags	Mode for open
 * @param[in]	open_func	Function to open a file descriptor
 * @param[out]	has_lock	Indicates that obj_hdl->obj_lock is held read
 * @param[out]	closefd		Indicates that file descriptor must be closed
 * @param[in]	open_for_locks	Indicates file is open for locks
 *
 * We do not need file descriptors for non-regular files, so this never has to
 * handle them.
 */
static fsal_status_t newfs_find_fd(Fh **fd,
                                   struct fsal_obj_handle *obj_hdl,
                                   bool bypass,
                                   struct state_t *state,
                                   fsal_openflags_t openflags,
                                   bool *has_lock,
                                   bool *closedfd,
                                   bool open_for_locks)
{
  struct newfs_handle *myself = container_of(obj_hdl, struct newfs_handle,
                                             handle);
  struct newfs_fd temp_fd = {FSAL_O_CLOSED, PTHREAD_RWLOCK_INITIALIZER, NULL};
  struct newfs_fd *out_fd = &temp_fd;
  fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
  bool reusing_open_state_fd = false;

  status = fsal_find_fd((struct fsal_fd **)&out_fd, obj_hdl,
                        (struct fsal_fd *)&myself->fd, &myself->share,
                        bypass, state, openflags,
                        newfs_open_func, newfs_close_func,
                        has_lock, closedfd, open_for_locks,
                        &reusing_open_state_fd);

  LogFullDebug(COMPONENT_FSAL, "fd = %p", out_fd->fd);

  *fd = out_fd->fd;
  return status;
}

/**
 * @brief Read data from a file
 *
 * This function reads data from the given file. The FSAL must be able to
 * perform the read whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations. This is
 * an (optionally) aysnchronous call. When the I/O is complete, the done
 * callback is called with the results.
 *
 * @param[in]	obj_hdl		File on which to operate
 * @param[in]	bypass		If state doesn't indicate a share reservation,
 *                              bypass any deny read
 * @param[in,out] done_cb	Callback to call when I/O is done
 * @param[in,out] read_arg	Info about read, passed back in callback
 * @param[in,out] caller_arg	Opaque arg from the caller for callback
 *
 * @return Nothing; results are in callback
 */
static void newfs_fsal_read2(struct fsal_obj_handle *obj_hdl,
                             bool bypass,
                             fsal_async_cb done_cb,
                             struct fsal_io_arg *read_arg,
                             void *caller_arg)
{
  fsal_status_t status = {0, 0};
  Fh *my_fd = NULL;
  bool has_lock = false;
  bool closefd = false;
  ssize_t nb_read = 0;
  struct newfs_handle *myself = container_of(obj_hdl, struct newfs_handle,
                                             handle);
  struct newfs_export *export = container_of(op_ctx->fsal_export,
                                  struct newfs_export, export);
  uint64_t offset = read_arg->offset;
  struct newfs_fd *newfs_fd = NULL;
  int i = 0;

  if (read_arg->info != NULL) {
    /* Currently we don't support READ_PLUS */
    done_cb(obj_hdl, fsalstat(ERR_FSAL_NOTSUPP, 0), read_arg,
            caller_arg);
    return;
  }

  /* Acquire state's fdlock to prevent OPEN upgrade closing the
   * file descriptor while we use it.
   */
  if (read_arg->state) {
    newfs_fd = &container_of(read_arg->state, struct newfs_state_fd,
                             state)->newfs_fd;

    PTHREAD_RWLOCK_rdlock(&newfs_fd->fdlock);
  }

  /* Get a usable file descriptor */
  status = newfs_find_fd(&my_fd, obj_hdl, bypass, read_arg->state,
                         FSAL_O_READ, &has_lock, &closefd, false);

  if (FSAL_IS_ERROR(status))
    goto out;

  read_arg->io_amount = 0;

  for (i = 0; i < read_arg->iov_count; i++) {
    nb_read = newfs_read(export->newfs_info, my_fd, offset,
                         read_arg->iov[i].iov_len,
                         read_arg->iov[i].iov_base);

    if (nb_read == 0) {
      read_arg->end_of_file = true;
      break;
    } else if (nb_read < 0) {
      status = newfs2fsal_error(nb_read);
      goto out;
    }

    read_arg->io_amount += nb_read;
    offset += nb_read;
  }

out:
  if (newfs_fd)
    PTHREAD_RWLOCK_unlock(&newfs_fd->fdlock);

  if (closefd)
    (void) newfs_close(myself->export->newfs_info, my_fd);

  if (has_lock)
    PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

  done_cb(obj_hdl, status, read_arg, caller_arg);
}

/**
 * @brief Write data to a file
 *
 * This function writes data to a file. The FSAL must be able to
 * perform the write whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations. Even
 * with bypass == true, it will enforce a mandatory (NFSv4) deny_write if
 * an appropriate state is not passed).
 *
 * The FSAL is expected to enforce sync if necessary.
 *
 * @param[in]	obj_hdl		File on which to operate
 * @param[in]	bypass		If state doesn't indicate a share reservation,
 *                              bypass any non-mandatory deny write
 * @param[in,out] done_cb	Callback to call when I/O is done
 * @param[in,out] write_arg	Info about write, passed back in callback
 * @param[in,out] caller_arg	Opaque arg from the caller for callback
 */

static void newfs_fsal_write2(struct fsal_obj_handle *obj_hdl, bool bypass,
                              fsal_async_cb done_cb,
                              struct fsal_io_arg *write_arg, void *caller_arg)
{

  struct newfs_fd *newfs_fd = NULL;
  fsal_status_t status = {0, 0};
  Fh *my_fd = NULL;
  bool has_lock = false;
  bool closefd = false;
  fsal_openflags_t openflags = FSAL_O_WRITE;
  int i = 0;
  int rc = -1;
  ssize_t nb_written = 0;
  uint64_t offset = write_arg->offset;

  struct newfs_export *export = container_of(op_ctx->fsal_export,
                                             struct newfs_export, export);
  struct newfs_handle *myself = container_of(obj_hdl, struct newfs_handle,
                                             handle);

  /* Acquire state's fdlock to prevent OPEN upgrade closing the
   * file descriptor while we use it.
   */
  if (write_arg->state) {
    newfs_fd = &container_of(write_arg->state, struct newfs_state_fd,
                             state)->newfs_fd;

    PTHREAD_RWLOCK_rdlock(&newfs_fd->fdlock);
  }

  /* Get a usable file descriptor */
  status = newfs_find_fd(&my_fd, obj_hdl, bypass, write_arg->state,
                         openflags, &has_lock, &closefd, false);
  if (FSAL_IS_ERROR(status)) {
    LogDebug(COMPONENT_FSAL,
             "newfs_find_fd failed %s", msg_fsal_err(status.major));
    goto out;
  }

  for (i = 0; i < write_arg->iov_count; i++) {
    nb_written = newfs_write(export->newfs_info, my_fd, offset,
                             write_arg->iov[i].iov_len,
                             write_arg->iov[i].iov_base);
    if (nb_written == 0) {
      break;
    } else if (nb_written < 0) {
      status = newfs2fsal_error(nb_written);
      goto out;
    }

    write_arg->io_amount += nb_written;
    offset += nb_written;
  }

  if (write_arg->fsal_stable) {
    rc = newfs_fsync(export->newfs_info, my_fd, false);

    if (rc < 0) {
      status = newfs2fsal_error(rc);
      write_arg->fsal_stable = false;
    }
  }

out:
  if (newfs_fd)
    PTHREAD_RWLOCK_unlock(&newfs_fd->fdlock);

  if (closefd)
    (void) newfs_close(myself->export->newfs_info, my_fd);

  if (has_lock)
    PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

  done_cb(obj_hdl, status, write_arg, caller_arg);
}

/**
 * @brief Commit written data
 *
 * This function flushes possibly buffered data to a file. This method
 * differs from commit due to the need to interact with share reservations
 * and the fact that the FSAL manages the state of "file descriptors". The
 * FSAL must be able to perform this operation without being passed a specific
 * state.
 *
 * @param[in] obj_hdl		File on which to operate
 * @param[in] offset		Start of range to commit
 * @param[in] len 		Length of range to commit
 *
 * @return FSAL status.
 */
static fsal_status_t newfs_fsal_commit2(struct fsal_obj_handle *obj_hdl,
                                        off_t offset,
                                        size_t len)
{
  int rc = -1;
  struct newfs_handle *myself = container_of(obj_hdl, struct newfs_handle,
                                             handle);
  struct newfs_export *export = container_of(op_ctx->fsal_export,
                                             struct newfs_export, export);

  /* we can avoid opening altogether */ 
  rc = newfs_sync_item(export->newfs_info, myself->item, 0);

  return newfs2fsal_error(rc);
}

/**
 * @brief Set attributes on an object
 *
 * This function sets attributes on an object. Which attributes are
 * set is determined by attrib_set->valid_mask. The FSAL must manage bypass
 * or not of share reservations, and a state may be passed.
 *
 * @param[in] obj_hdl		File on which to operate
 * @param[in] state		state_t to use for this operation
 * @param[in] attrib_set 	Attributes to set
 */
static fsal_status_t newfs_fsal_setattr2(struct fsal_obj_handle *obj_hdl,
                                         bool bypass, struct state_t *state,
                                         struct attrlist *attrib_set)
{
  fsal_status_t status = {0, 0};
  int rc = -1;
  struct newfs_handle *myself = container_of(obj_hdl, struct newfs_handle,
                                             handle);
  bool has_lock = false;
  bool closefd = false;
  /* Stat buffer */
  struct stat st;
  /* Mask of attributes to set */
  uint32_t mask = 0;

  struct newfs_export *export = container_of(op_ctx->fsal_export,
                                             struct newfs_export, export);
  bool reusing_open_state_fd = false;

  if (attrib_set->valid_mask & ~NEWFS_SETTABLE_ATTRIBUTES) {
    LogDebug(COMPONENT_FSAL,
             "bad mask %"PRIx64" not settable %"PRIx64,
             attrib_set->valid_mask,
             attrib_set->valid_mask & ~NEWFS_SETTABLE_ATTRIBUTES);
    return fsalstat(ERR_FSAL_INVAL, 0);
  }

  LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG,
              "attrs ", attrib_set, false);

  /* apply umask, if mode attribute is to be changed */
  if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MODE))
    attrib_set->mode &=
      ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

  /* Test if size is being set, make sure file is regular and if so,
   * require a read/write file descriptor.
   */
  if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_SIZE)) {
    if (obj_hdl->type != REGULAR_FILE) {
      LogFullDebug(COMPONENT_FSAL,
                   "Setting size on non-regular file");
      return fsalstat(ERR_FSAL_INVAL, EINVAL);
    }

    /* We don't actually need an open fd, we are just doing the
     * share reservation checking, thus the NULL parameters.
     */
    status = fsal_find_fd(NULL, obj_hdl, NULL, &myself->share,
                          bypass, state, FSAL_O_RDWR, NULL, NULL,
                          &has_lock, &closefd, false,
                          &reusing_open_state_fd);

    if (FSAL_IS_ERROR(status)) {
      LogFullDebug(COMPONENT_FSAL,
                   "fsal_find_fd status=%s",
                   fsal_err_txt(status));
      goto out;
    }
  }

  memset(&st, 0, sizeof(struct stat));

  // FIXME: newfs_truncate???
  if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_SIZE)) {
    mask |= NEWFS_SETATTR_SIZE;
    st.st_size = attrib_set->filesize;
    LogDebug(COMPONENT_FSAL,
             "setting size to %lu", st.st_size);
  }

  if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MODE)) {
    mask |= NEWFS_SETATTR_MODE;
    st.st_mode = fsal2unix_mode(attrib_set->mode);
  }

  if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_OWNER)) {
    mask |= NEWFS_SETATTR_UID;
    st.st_uid = attrib_set->owner;
  }

  if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_GROUP)) {
    mask |= NEWFS_SETATTR_GID;
    st.st_gid = attrib_set->group;
  }

  if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_ATIME)) {
    mask |= NEWFS_SETATTR_ATIME;
    st.st_atim = attrib_set->atime;
  }

  if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_ATIME_SERVER)) {
    mask |= NEWFS_SETATTR_ATIME;
    struct timespec timestamp;

    rc = clock_gettime(CLOCK_REALTIME, &timestamp);
    if (rc != 0) {
      LogDebug(COMPONENT_FSAL,
               "clock_gettime returned %s (%d)",
               strerror(errno), errno);
      status = fsalstat(posix2fsal_error(errno), errno);
      st.st_atim = timestamp;
    }
  }

  if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MTIME)) {
    mask |= NEWFS_SETATTR_MTIME;
    st.st_mtim = attrib_set->mtime;
  }

  if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MTIME_SERVER)) {
    mask |= NEWFS_SETATTR_MTIME;
    struct timespec timestamp;

    rc = clock_gettime(CLOCK_REALTIME, &timestamp);
    if (rc != 0) {
      LogDebug(COMPONENT_FSAL,
               "clock_gettime returned %s (%d)",
               strerror(-rc), -rc);
      status = newfs2fsal_error(rc);
      goto out;
    }
    st.st_mtim = timestamp;
  }

  if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_CTIME)) {
    mask |= NEWFS_SETATTR_CTIME;
    st.st_ctim = attrib_set->ctime;
  }

  // FIXME: set btime???

  rc = newfs_setattr(export->newfs_info, myself->item, &st, mask);
  if (rc < 0) {
    LogDebug(COMPONENT_FSAL,
             "setattr returned %s (%d)",
             strerror(-rc), -rc);
    goto out;
  } else {
    /* Success */
    status = fsalstat(ERR_FSAL_NO_ERROR, 0);
  }
out:
  if (has_lock)
    PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

  return status;
}

/**
 * @brief Write wire handle
 *
 * This function writes a 'wire' handle to be sent to clients and
 * received from the.
 *
 * @param[in]	obj_hdl		Handle to digest
 * @param[in]	output_type	Type of digest requested
 * @param[in,out] fh_desc	Location/size of buffer for 
 *                              digest/length modified to digest length
 *
 * @return FSAL status.
 */

static fsal_status_t newfs_fsal_handle_to_wire(
                       const struct fsal_obj_handle *obj_hdl,
                       uint32_t output_type,
                       struct gsh_buffdesc *fh_desc)
{
  /* The private 'full' object handle */
  const struct newfs_handle *handle = container_of(obj_hdl, struct newfs_handle,
                                                   handle);

  switch (output_type) {
  case FSAL_DIGEST_NFSV3:
  case FSAL_DIGEST_NFSV4:
    if (fh_desc->len < sizeof(struct newfs_handle_key)) {
      LogMajor(COMPONENT_FSAL,
               "digest_handle: space to small for handle. Need %zu, have %zu",
               sizeof(handle->key), fh_desc->len);
      return fsalstat(ERR_FSAL_TOOSMALL, 0);
    } else {
      memcpy(fh_desc->addr, &handle->key, sizeof(struct newfs_handle_key));
    }
    break;
  default:
    return fsalstat(ERR_FSAL_SERVERFAULT, 0);
  }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Give a hash key for file handle
 *
 * This function locates a unique hash key for a given file.
 *
 * @param[in]	obj_hdl	The file whose key is to be found
 * @param[out]	fh_desc	Address and length of key
 */
static void newfs_fsal_handle_to_key(struct fsal_obj_handle *obj_hdl,
                                     struct gsh_buffdesc *fh_desc)
{
  /* The private 'full' object handle */
  struct newfs_handle *handle = container_of(obj_hdl, struct newfs_handle,
                                             handle);

  fh_desc->addr = &handle->key;
  fh_desc->len = sizeof(handle->key);
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
  ops->getattrs = newfs_fsal_getattrs;
  ops->rename = newfs_fsal_rename;
  ops->unlink = newfs_fsal_unlink;
  ops->close = newfs_fsal_close;
  ops->open2 = newfs_fsal_open2;
  ops->reopen2 = newfs_fsal_reopen2;
  ops->close2 = newfs_fsal_close2;
  ops->status2 = newfs_fsal_status2;
  ops->read2 = newfs_fsal_read2;
  ops->write2 = newfs_fsal_write2;
  ops->commit2 = newfs_fsal_commit2;
  ops->setattr2 = newfs_fsal_setattr2;
  ops->handle_to_wire = newfs_fsal_handle_to_wire;
  ops->handle_to_key = newfs_fsal_handle_to_key;
}
