/* Minimal stand-in for libnx's switch.h, used only to syntax-check the sources
   on a host without devkitPro. Mirrors the real signatures. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t  s32;
typedef u32      Result;

#define R_FAILED(res)    ((res) != 0)
#define R_SUCCEEDED(res) ((res) == 0)
#define MAKERESULT(mod, desc) ((Result)((((mod)&0x1FF)) | ((desc)&0x1FFF) << 9))

#define Module_Libnx           345
#define LibnxError_NotFound    5
#define LibnxError_OutOfMemory 4
#define LibnxError_BadInput    9
#define LibnxError_IoError     10

typedef struct { char name[0x200]; char author[0x100]; } NacpLanguageEntry;
typedef struct { NacpLanguageEntry lang[16]; u8 rest[0x1000]; } NacpStruct;

typedef struct { NacpStruct nacp; u8 icon[0x20000]; } NsApplicationControlData;

typedef struct {
    u64 application_id;
    u8  last_event;
    u8  attributes;
    u8  reserved[6];
    u64 last_updated;
} NsApplicationRecord;

typedef enum {
    NsApplicationControlSource_CacheOnly   = 0,
    NsApplicationControlSource_Storage     = 1,
    NsApplicationControlSource_StorageOnly = 2,
} NsApplicationControlSource;

Result nsInitialize(void);
void   nsExit(void);
Result nsListApplicationRecord(NsApplicationRecord *records, s32 count, s32 entry_offset, s32 *out_entrycount);
Result nsGetApplicationControlData(NsApplicationControlSource source, u64 application_id,
                                   NsApplicationControlData *buffer, size_t size, u64 *actual_size);
Result nsGetApplicationDesiredLanguage(NacpStruct *nacp, NacpLanguageEntry **langentry);
Result nsCheckApplicationLaunchVersion(u64 application_id);

typedef enum {
    AppletType_None = -2,
    AppletType_Default = -1,
    AppletType_Application = 0,
    AppletType_SystemApplet = 1,
    AppletType_LibraryApplet = 2,
    AppletType_OverlayApplet = 3,
    AppletType_SystemApplication = 4,
} AppletType;

typedef struct AppletStorage AppletStorage;

AppletType appletGetAppletType(void);
Result     appletRequestLaunchApplication(u64 application_id, AppletStorage *s);
bool       appletMainLoop(void);

bool   envHasNextLoad(void);
Result envSetNextLoad(const char *path, const char *argv);
bool   hosversionAtLeast(u8 major, u8 minor, u8 micro);

typedef struct PrintConsole PrintConsole;
PrintConsole *consoleInit(PrintConsole *console);
void          consoleUpdate(PrintConsole *console);
void          consoleExit(PrintConsole *console);

typedef struct { u64 buttons_cur; u64 buttons_down; } PadState;

#define HidNpadStyleSet_NpadStandard 0xFU

#define HidNpadButton_A        (1UL << 0)
#define HidNpadButton_X        (1UL << 2)
#define HidNpadButton_Y        (1UL << 3)
#define HidNpadButton_Plus     (1UL << 10)
#define HidNpadButton_AnyUp    (1UL << 24)
#define HidNpadButton_AnyDown  (1UL << 25)
#define HidNpadButton_AnyLeft  (1UL << 26)
#define HidNpadButton_AnyRight (1UL << 27)

void padConfigureInput(u32 max_players, u32 style_set);
void padInitializeDefault(PadState *pad);
void padUpdate(PadState *pad);
u64  padGetButtonsDown(const PadState *pad);
