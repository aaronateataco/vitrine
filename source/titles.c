#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "entry.h"

#define RECORD_BATCH 32

static void copy_nacp_field(char *dst, size_t dst_size, const char *src, size_t src_size)
{
    size_t len = strnlen(src, src_size);
    if (len >= dst_size)
        len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/*
 * Enumerates every application installed on the console.
 *
 * This only ever asks ns for records and their public control data (name,
 * author, icon) — the same metadata the HOME menu draws. No content is opened,
 * decrypted, or inspected; launching later goes through the OS as well.
 */
Result titles_scan(EntryList *list)
{
    Result rc = nsInitialize();
    if (R_FAILED(rc))
        return rc;

    /* ~0x24000 bytes — far too large for the stack. */
    NsApplicationControlData *control = malloc(sizeof(*control));
    if (!control) {
        nsExit();
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }

    NsApplicationRecord records[RECORD_BATCH];
    s32 offset = 0;

    for (;;) {
        s32 count = 0;
        rc = nsListApplicationRecord(records, RECORD_BATCH, offset, &count);
        if (R_FAILED(rc) || count <= 0)
            break;

        for (s32 i = 0; i < count; i++) {
            u64 actual = 0;

            /*
             * Archived titles keep a record but have no control data. Failing
             * here is the cheapest way to filter them out — they would not
             * launch anyway.
             */
            if (R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                                     records[i].application_id,
                                                     control, sizeof(*control), &actual)))
                continue;

            if (actual < sizeof(control->nacp))
                continue;

            NacpLanguageEntry *language = NULL;
            if (R_FAILED(nsGetApplicationDesiredLanguage(&control->nacp, &language)) || !language)
                continue;

            Entry *entry = entry_list_add(list);
            if (!entry)
                break;

            entry->kind = EntryKind_Title;
            entry->application_id = records[i].application_id;
            copy_nacp_field(entry->name, sizeof(entry->name),
                            language->name, sizeof(language->name));
            copy_nacp_field(entry->author, sizeof(entry->author),
                            language->author, sizeof(language->author));
        }

        offset += count;
        if (count < RECORD_BATCH)
            break;
    }

    free(control);
    nsExit();
    return 0;
}
