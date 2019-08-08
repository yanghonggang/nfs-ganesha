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

static struct config_item export_params[] = {
  CONF_ITEM_NOOP("name"),
  CONF_ITEM_STR("user_id", 0, MAXUIDLEN, NULL, newfs_export, user_id),
  CONF_ITEM_STR("secret_access_key", 0, MAXSECRETLEN, NULL, newfs_export,
  		secret_key),
  CONFIG_EOL
};

static struct config_block export_param_block = {
  .dbus_interface_name = "org.ganesha.nfsd.config.fsal.newfs-export%d",
  .blk_desc.name = "FSAL",
  .blk_desc.type = CONFIG_BLOCK,
  .blk_desc.u.blk.init = noop_conf_init,
  .blk_desc.u.blk.params = export_params,
  .blk_desc.u.blk.commit = noop_conf_commit
}; 

static struct config_item newfs_items[] = {
  CONF_ITEM_PATH("ceph_conf", 1, MAXPATHLEN, NULL,
                 newfs_fsal_module, ceph_conf_path),
  CONF_ITEM_PATH("fdb_conf_path", 1, MAXPATHLEN, NULL,
                 newfs_fsal_module, fdb_conf_path),
  CONF_ITEM_MODE("umask", 0,
                 newfs_fsal_module, fsal.fs_info.umask),
  CONFIG_EOL
};

static struct config_block newfs_block = {
  .dbus_interface_name = "org.ganesha.nfsd.config.fsal.newfs",
  .blk_desc.name = "NEWFS",
  .blk_desc.type = CONFIG_BLOCK,
  .blk_desc.u.blk.init = noop_conf_init,
  .blk_desc.u.blk.params = newfs_items,
  .blk_desc.u.blk.commit = noop_conf_commit
};

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */
static fsal_status_t init_config(struct fsal_module *module_in,
                                 config_file_t config_struct,
                                 struct config_error_type *err_type)
{
  struct newfs_fsal_module *myself = container_of(module_in,
                                       struct newfs_fsal_module, fsal);

  LogDebug(COMPONENT_FSAL,
           "NEWFS module setup.");

  (void) load_config_from_parse(config_struct,
                                &newfs_block,
                                myself,
                                true,
                                err_type);
  if (!config_error_is_harmless(err_type))
    return fsalstat(ERR_FSAL_INVAL, 0);

  display_fsinfo(&myself->fsal);
  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a new export under this FSAL
 *
 * This function create a new export object for the newfs FSAL.
 *
 * @param[in]	module_in	The supplied module handle
 *
 * @return FSAL status.
 */
static fsal_status_t create_export(struct fsal_module* module_in,
                                   void* parse_node,
                                   struct config_error_type* err_type,
                                   const struct fsal_up_vector* up_ops)
{
  fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

  /* The internal export object */
  struct newfs_export* export = gsh_calloc(1, sizeof(struct newfs_export));
  /* The 'private' root handle */
  struct newfs_handle* handle = NULL;
  /* Return code */
  int rc = 0;
  struct stat st;
  newfs_item *item;
 
  /* TODO: librados related init */

  fsal_export_init(&export->export);
  export_ops_init(&export->export.exp_ops);

  /* get params for this export, if any */
  if (parse_node) {
    rc = load_config_from_node(parse_node,
                               &export_param_block,
                               export,
                               true,
                               err_type);
    if (rc != 0) {
     gsh_free(export);
     return fsalstat(ERR_FSAL_INVAL, 0);
    }
  }

  /* newfs related init */
  rc = newfs_init(NewFS.fdb_conf_path, &export->newfs_info,
                  op_ctx->ctx_export->fullpath);
  if (rc != 0) {
    status.major = ERR_FSAL_SERVERFAULT;
    LogCrit(COMPONENT_FSAL,
            "Unable to mount NEWFS cluster for %s",
            op_ctx->ctx_export->fullpath);
    goto error;
  }

  if (fsal_attach_export(module_in, &export->export.exports) != 0) {
    status.major = ERR_FSAL_SERVERFAULT;
    LogCrit(COMPONENT_FSAL,
            "Unable to attach export for %s.",
            op_ctx->ctx_export->fullpath);
    goto error;
  }

  export->export.fsal = module_in;
  export->export.up_ops = up_ops;

  LogDebug(COMPONENT_FSAL, "NEWFS module export %s.",
           op_ctx->ctx_export->fullpath);

  rc = newfs_walk(export->newfs_info, "/", &item, &st);
  if (rc < 0) {
    status = newfs2fsal_error(rc);
    goto error;
  }

  construct_handle(export, item, &st, &handle);

  export->root = handle;
  op_ctx->fsal_export = &export->export;

  return status;

error:
  if (item)
    newfs_put(export->newfs_info, item);

  if (export) {
    if (export->newfs_info)
      newfs_fini(export->newfs_info);

    gsh_free(export);
  }
 
  return status;
}

/**
 * @brief Initialize and register the FSAL
 *
 */
MODULE_INIT void init(void)
{
  struct fsal_module *myself = &NewFS.fsal;
  
  LogDebug(COMPONENT_FSAL, "NewFs module registering.");
  
  if (register_fsal(myself, module_name, FSAL_MAJOR_VERSION,
                    FSAL_MINOR_VERSION, FSAL_ID_NEWFS) != 0) {
    LogCrit(COMPONENT_FSAL, "NewFs module failed to register.");
  }
  
  /* override default module operations */
  myself->m_ops.create_export = create_export;
  myself->m_ops.init_config = init_config;

  /* Initialize the fsal_obj_handle ops for FSAL NewFS */
  handle_ops_init(&NewFS.handle_ops);
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
