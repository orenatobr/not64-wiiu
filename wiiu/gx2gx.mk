#---------------------------------------------------------------------------------
# GX2 renderer for glN64_GX (see docs/GX2_RENDERER.md). Included from Makefile.wiiu:
#
#   include wiiu/gx2gx.mk
#   CPPFILES += $(GX2GX_CPPFILES) $(GLN64_CPPFILES)
#   CFLAGS   += $(GX2GX_CFLAGS)
#   INCLUDES += $(GX2GX_INCLUDES)
#
# The compat directory must come FIRST on the include path so that <gccore.h> and <ogc/*.h>
# resolve to the shim instead of (a not present) libogc.
#---------------------------------------------------------------------------------
GX2GX_DIR	:=	wiiu/source/gx2gx

# the renderer proper (pure GX API layer + GX2 backend); Gx2ShaderData.h is generated and committed
GX2GX_CPPFILES	:=	$(GX2GX_DIR)/Gx2Core.cpp $(GX2GX_DIR)/Gx2Tev.cpp $(GX2GX_DIR)/Gx2Tex.cpp \
			$(GX2GX_DIR)/Gx2Math.cpp $(GX2GX_DIR)/Gx2Wiiu.cpp

# glN64_GX translation units (same list as Makefile.GLN64_dev_wii)
GLN64_CPPFILES	:=	$(addprefix glN64_GX/,glN64.cpp Config_linux.cpp OpenGL.cpp N64.cpp RSP.cpp VI.cpp \
			Textures.cpp FrameBuffer.cpp Combiner.cpp gDP.cpp gSP.cpp GBI.cpp DepthBuffer.cpp CRC.cpp \
			2xSAI.cpp TEV_combiner.cpp RDP.cpp F3D.cpp F3DEX.cpp F3DEX2.cpp L3D.cpp L3DEX.cpp L3DEX2.cpp \
			S2DEX.cpp S2DEX2.cpp F3DPD.cpp F3DDKR.cpp F3DWRUS.cpp F3DCBFD.cpp)

GX2GX_INCLUDES	:=	$(GX2GX_DIR)/compat $(GX2GX_DIR)

# glN64_GX is compiled as it is on Wii (GLN64_GX + __GX__ + __LINUX__ selects the GX code paths);
# __WIIU__ selects the hooks described in the doc. HW_RVL must NOT be defined (no MEM2 texture cache).
GX2GX_CFLAGS	:=	-DGLN64_GX -D__GX__ -D__LINUX__ -D__WIIU__

# glN64_GX is old code: keep the noise of its pre-existing warnings out of the build log
GLN64_WARN	:=	-Wno-unused -Wno-sign-compare -Wno-reorder -Wno-write-strings -Wno-narrowing

# per-object flags live in Makefile.wiiu (they have to come after CXXFLAGS is defined, and the
# renderer's compat headers must replace the core's, not follow them)
