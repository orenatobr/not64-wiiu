#ifndef WIIU_SAVE_H
#define WIIU_SAVE_H

#include "../../../fileBrowser/fileBrowser.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Makes the core read the ROM from `data` (not copied, must outlive its use). */
void wiiu_rom_source_set(const void *data, unsigned int size, fileBrowser_file *out);
void wiiu_rom_source_clear(void);

/* Points the core's saveFile_* callbacks at stdio. */
void wiiu_save_backend_install(void);

#ifdef __cplusplus
}
#endif

#endif
