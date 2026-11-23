/* A graphics plugin that draws nothing.
 *
 * Used while the GX2 renderer is not linked in, so the rest of the system
 * (audio, input, saves, the CPU core) can run and be checked without video.
 * It is replaced by the glN64 renderer in the real build.
 */
#include "../../../main/winlnxdefs.h"
#include "../../../glN64_GX/GFXPlugin.h"
#include "../../../main/Gfx_#1.3.h"

/* The core also calls these function pointers, which a real plugin may set. */
void (*fBRead)(DWORD addr) = 0;
void (*fBWrite)(DWORD addr, DWORD size) = 0;
void (*fBGetFrameBufferInfo)(void *p) = 0;
void (*readScreen)(void **dest, long *width, long *height) = 0;

EXPORT void CALL CaptureScreen(char *Directory) { (void)Directory; }
EXPORT void CALL ChangeWindow(void) {}
EXPORT void CALL CloseDLL(void) {}
EXPORT void CALL DrawScreen(void) {}
EXPORT void CALL DllAbout(HWND hParent) { (void)hParent; }
EXPORT void CALL DllConfig(HWND hParent) { (void)hParent; }
EXPORT void CALL DllTest(HWND hParent) { (void)hParent; }
EXPORT void CALL GetDllInfo(PLUGIN_INFO *PluginInfo) { (void)PluginInfo; }
EXPORT BOOL CALL InitiateGFX(GFX_INFO Gfx_Info) { (void)Gfx_Info; return TRUE; }
EXPORT void CALL MoveScreen(int xpos, int ypos) { (void)xpos; (void)ypos; }
EXPORT void CALL ProcessDList(void) {}
EXPORT void CALL ProcessRDPList(void) {}
EXPORT void CALL RomClosed(void) {}
EXPORT void CALL RomOpen(void) {}
EXPORT void CALL ShowCFB(void) {}
EXPORT void CALL UpdateScreen(void) {}
EXPORT void CALL ViStatusChanged(void) {}
EXPORT void CALL ViWidthChanged(void) {}
