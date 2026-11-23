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
#ifndef RECOMPILER_USES_CODEGEN_AREA
#define RECOMPILER_USES_CODEGEN_AREA 0
#endif

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

/* The part of the codegen area the recompiler's code cache lives in, or 0 when there is none.
 *
 * TEST ONLY (make DYNAREC_TEST=1). This hands the recompiler the area as plain writable memory, which is
 * what Cemu allows. A console keeps the area execute-only: it needs OSSwitchSecCodeGenMode / OSCodegenCopy
 * around every write, and only the core OSGetCodegenCore() names may use it. None of that is done yet, and
 * the recompiler still stalls in real games (see docs/DYNAREC.md), so it stays off. */
void *wiiu_jit_cache(unsigned size)
{
	WiiuJitInfo info;

	if (!wiiu_jit_available())
		return 0;

	wiiu_jit_query(&info);
	return info.size >= size ? info.area : 0;
}
