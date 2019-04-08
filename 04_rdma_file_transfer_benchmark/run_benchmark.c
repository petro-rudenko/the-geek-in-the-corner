#include "common.h"
#include "verbs_common.h"


int main(int argc, char **argv)
{
    parse_cmd(argc, argv);

    if (ctx.server_address == NULL) {
        if (ctx.protocol == VERBS_READ) {
            run_verbs_read_sender();
        } else {
            run_verbs_write_sender();
        }
    } else {
        if (ctx.protocol == VERBS_READ) {
            run_verbs_read_receiver();
        } else {
            run_verbs_write_receiver();
        }
    }

    close(ctx.fd);
    return 0;
}
