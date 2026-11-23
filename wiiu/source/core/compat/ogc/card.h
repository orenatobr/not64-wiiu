/* GameCube memory cards do not exist on the Wii U. The core includes this
 * header for CARD types it only uses in Wii/GameCube code paths, which are
 * compiled out under __WIIU__. Saves go through the saveFile_* callbacks. */
#ifndef WIIU_COMPAT_CARD_H
#define WIIU_COMPAT_CARD_H
#include "../gctypes.h"
#endif
