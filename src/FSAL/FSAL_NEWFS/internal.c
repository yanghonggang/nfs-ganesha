#include <sys/stat.h>
#include "fsal_types.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "internal.h"

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
