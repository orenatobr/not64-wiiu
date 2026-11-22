/* Executable memory for the PowerPC recompiler.
 *
 * Wii U applications cannot make heap memory executable. The only place that can
 * hold generated code is the "codegen" area the OS lends to titles whose cos.xml
 * allows it, and it can only be used from one specific CPU core.
 *
 * Whether a homebrew title launched through Aroma gets that area depends on
 * the console setup, so the recompiler is strictly opt-in: it is only offered
 * when the OS reports a codegen area that is large enough. Otherwise the core
 * falls back to the interpreters, which need no executable memory.
 */
#include <stdint.h>

#include <coreinit/codegen.h>

#include "wiiu_jit.h"

/* The recompiler's code cache (RECOMP_CACHE_SIZE) plus some headroom. */
#define MIN_CODEGEN_BYTES (12u * 1024u * 1024u)

/* Set to 1 once the recompiler allocates from the codegen area (see
 * docs/DYNAREC.md). Until then it must not be offered, even if the area exists. */
#define RECOMPILER_USES_CODEGEN_AREA 0

void wiiu_jit_query(WiiuJitInfo *info)
{
	void *addr = 0;
	uint32_t size = 0;

	if (!info)
		return;

	OSGetCodegenVirtAddrRange(&addr, &size);

	info->area = addr;
	info->size = size;
	info->core = (int)OSGetCodegenCore();
	info->mode = (int)OSGetCodegenMode();
}

int wiiu_jit_available(void)
{
	WiiuJitInfo info;

	if (!RECOMPILER_USES_CODEGEN_AREA)
		return 0;

	wiiu_jit_query(&info);
	return info.area != 0 && info.size >= MIN_CODEGEN_BYTES;
}
