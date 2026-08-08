#include <stdio.h>

#include "launch.h"

bool launch_can_launch_homebrew(void)
{
    /* False when we were not started by hbloader, e.g. installed as a title. */
    return envHasNextLoad();
}

/*
 * appletRequestLaunchApplication is restricted to AppletType_*Application, or
 * to AppletType_LibraryApplet on [5.0.0+]. In practice that covers both normal
 * ways of starting homebrew (title takeover and album takeover) on any modern
 * firmware, but it has to be checked rather than assumed.
 */
bool launch_can_launch_title(void)
{
    switch (appletGetAppletType()) {
        case AppletType_Application:
        case AppletType_SystemApplication:
            return true;
        case AppletType_LibraryApplet:
            return hosversionAtLeast(5, 0, 0);
        default:
            return false;
    }
}

bool launch_is_application_mode(void)
{
    AppletType type = appletGetAppletType();
    return type == AppletType_Application || type == AppletType_SystemApplication;
}

static Result launch_homebrew(const Entry *entry)
{
    if (!launch_can_launch_homebrew())
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    /* hbloader expects argv[0] to be the NRO path, quoted. */
    char argv[ENTRY_PATH_LEN + 4];
    snprintf(argv, sizeof(argv), "\"%s\"", entry->path);

    return envSetNextLoad(entry->path, argv);
}

/*
 * Hands the application id to the OS and lets it do the work. Nothing here
 * touches title content, keys, or signatures — the system performs its own
 * verification exactly as it does when launching from the HOME menu.
 */
static Result launch_title(const Entry *entry)
{
    if (!launch_can_launch_title())
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    /* Catches "this title needs an update first" before the launch request. */
    Result rc = nsInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = nsCheckApplicationLaunchVersion(entry->application_id);
        nsExit();
        if (R_FAILED(rc))
            return rc;
    }

    return appletRequestLaunchApplication(entry->application_id, NULL);
}

/*
 * Cores are separate NRO programs, launched the same way as any other homebrew.
 * Keeping them out-of-process is what lets each core keep its own license
 * instead of being linked into this one.
 */
static Result launch_game(const Entry *entry, const SystemList *systems)
{
    if (!systems || entry->system_index < 0 ||
        (size_t)entry->system_index >= systems->count)
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    const System *system = &systems->items[entry->system_index];
    if (system->core[0] == '\0')
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    if (!launch_can_launch_homebrew())
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    char argv[SYSTEM_ARGS_LEN + 2 * ENTRY_PATH_LEN];
    if (!system_expand_args(system, entry->path, argv, sizeof(argv)))
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    return envSetNextLoad(system->core, argv);
}

Result launch_entry(const Entry *entry, const SystemList *systems)
{
    switch (entry->kind) {
        case EntryKind_Homebrew: return launch_homebrew(entry);
        case EntryKind_Game:     return launch_game(entry, systems);
        default:                 return launch_title(entry);
    }
}
