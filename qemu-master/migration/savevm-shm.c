/*
 * Zezhou: potentially a customized runtime state format.
 * 
 * 
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "net/net.h"
#include "migration.h"
#include "migration/snapshot.h"
#include "migration-stats.h"
#include "migration/vmstate.h"
#include "migration/misc.h"
#include "migration/register.h"
#include "migration/global_state.h"
#include "migration/channel-block.h"
#include "ram.h"
#include "qemu-file.h"
#include "savevm.h"
#include "postcopy-ram.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-builtin-visit.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "sysemu/cpus.h"
#include "exec/memory.h"
#include "exec/target_page.h"
#include "trace.h"
#include "qemu/iov.h"
#include "qemu/job.h"
#include "qemu/main-loop.h"
#include "block/snapshot.h"
#include "qemu/cutils.h"
#include "io/channel-buffer.h"
#include "io/channel-file.h"
#include "sysemu/replay.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "sysemu/xen.h"
#include "migration/colo.h"
#include "qemu/bitmap.h"
#include "net/announce.h"
#include "qemu/yank.h"
#include "yank_functions.h"
#include "sysemu/qtest.h"
#include "options.h"


/* Send VM state header to the dest.
 * In my experiment, it's "QEVM pc-i440fx-9.0".
 * You can add qemu_fflush(f) to send the data instantly.
 */
void qemu_savevm_state_header_shm(QEMUFile *f)
{
    puts("qemu_savevm_state_header_shm");

    // MigrationState *s = migrate_get_current();

    // s->vmdesc = json_writer_new(false);

    // trace_savevm_state_header();
    // qemu_put_be32(f, QEMU_VM_FILE_MAGIC);
    // qemu_put_be32(f, QEMU_VM_FILE_VERSION);

    // if (s->send_configuration) {
    //     qemu_put_byte(f, QEMU_VM_CONFIGURATION);

    //     /*
    //      * This starts the main json object and is paired with the
    //      * json_writer_end_object in
    //      * qemu_savevm_state_complete_precopy_non_iterable
    //      */
    //     json_writer_start_object(s->vmdesc, NULL);

    //     json_writer_start_object(s->vmdesc, "configuration");
    //     vmstate_save_state(f, &vmstate_configuration, &savevm_state, s->vmdesc);
    //     json_writer_end_object(s->vmdesc);
    // }
}