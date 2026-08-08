#include <string.h>

#include <switch.h>

#include "diag.h"
#include "keyboard.h"

bool keyboard_prompt(const char *header, const char *guide, const char *initial,
                     char *out, size_t out_size)
{
    if (!out || out_size < 2)
        return false;

    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) {
        diag_logf("swkbdCreate failed: 0x%x", rc);
        return false;
    }

    swkbdConfigMakePresetDefault(&kbd);

    if (header)
        swkbdConfigSetHeaderText(&kbd, header);
    if (guide)
        swkbdConfigSetGuideText(&kbd, guide);
    if (initial)
        swkbdConfigSetInitialText(&kbd, initial);

    swkbdConfigSetStringLenMax(&kbd, (u32)(out_size - 1));

    char buffer[512] = { 0 };
    size_t limit = out_size < sizeof(buffer) ? out_size : sizeof(buffer);

    rc = swkbdShow(&kbd, buffer, limit);
    swkbdClose(&kbd);

    /* Cancelling is a normal outcome, not a failure worth reporting loudly. */
    if (R_FAILED(rc)) {
        diag_logf("keyboard dismissed: 0x%x", rc);
        return false;
    }

    snprintf(out, out_size, "%s", buffer);
    return true;
}
