ifneq ($(OS),Windows_NT)
OS := $(shell uname -s)
ARCH := $(shell uname -m)
endif
ifeq ($(OS),Darwin)
CLANG := 1
# STATIC_MVK := 1
else
# STATIC_SDL := 1
endif
ifeq ($(CLANG),1)
CC := clang
CXX := clang++
else
CC := gcc
CXX := g++
endif

CPPFLAGS := -Iinclude -DPL_STATIC
ifeq ($(OS),Darwin)
CPPFLAGS += $(shell pkg-config sdl3 --cflags) $(shell pkg-config libplacebo --cflags)
endif
ifeq ($(DEBUG),1)
CFLAGS := -Og -g
else
CFLAGS := -flto=auto -O3 -ffast-math -fno-strict-aliasing
CPPFLAGS += -DNDEBUG
endif
CFLAGS += -Wall -Wextra -MMD
ifeq ($(CLANG),1)
CFLAGS += -Wno-c2x-extensions -Wno-c++11-extensions -Wno-unknown-warning-option -Wno-comment
else
CFLAGS += -flarge-source-files -Wno-unknown-pragmas
endif
CFLAGS += -Wno-missing-field-initializers
ifeq ($(STATIC_MVK),1)
CFLAGS += -DSTATIC_MVK
endif
EMBED_JPEG_TURBO := 1

ifeq ($(OS),Windows_NT)
LDLIBS := -Llib -static -lmingw32 -lSDL3 -lm
TARGET := ntrviewer.exe
NASM := -DWIN64 -fwin64 -D__x86_64__
else
ifeq ($(OS),Darwin)
LDLIBS := -Llib $(shell pkg-config sdl3 --libs)
else
LDLIBS := -Llib
ifneq ($(STATIC_SDL),1)
LDLIBS += $(shell pkg-config sdl3 --libs) $(shell pkg-config libplacebo --libs) $(shell pkg-config lcms2 --libs) $(shell pkg-config libunwind --libs) $(shell pkg-config liblzma --libs)
else
LDLIBS += -static-libgcc -static-libstdc++
LDLIBS += -Wl,-Bstatic -lSDL3
endif
endif
TARGET := ntrviewer
ifneq ($(ARCH),arm64)
NASM := -D__x86_64__
ifeq ($(OS),Darwin)
NASM += -fmacho64 -DDMACHO
else
NASM += -felf64 -DELF
endif
endif
endif

ifneq ($(LITE),1)
ifeq ($(OS),Darwin)
LDLIBS += $(shell pkg-config libplacebo --libs) $(shell pkg-config lcms2 --libs) $(shell pkg-config shaderc --libs)
ifeq ($(STATIC_MVK),1)
LDLIBS += -L${VULKAN_SDK}/lib/MoltenVK.xcframework/macos-arm64_x86_64 -lMoltenVK
LDLIBS += -Wl,-framework,IOSurface -Wl,-framework,Metal -Wl,-framework,CoreGraphics -Wl,-framework,IOKit -Wl,-framework,QuartzCore -Wl,-framework,Foundation -Wl,-framework,AppKit
endif
LDLIBS += -Wl,-framework,CoreFoundation -Wl,-framework,Network
else
LDLIBS += -lplacebo
endif
else
CPPFLAGS += -DUSE_SDL_RENDERER_ONLY
endif

ifneq ($(LITE),1)
GL_OBJ := placebo.o rashader.o
ifeq ($(OS),Darwin)
GL_OBJ += ui_renderer_metal.o
else
GL_OBJ += libGLAD.o libNK_SDL_GL3.o libNK_SDL_GLES2.o ui_renderer_ogl.o
endif
ifeq ($(OS),Windows_NT)
GL_OBJ += libGLAD_WGL.o libNK_D3D11.o ui_renderer_d3d11.o ui_compositor_csc.o
LDLIBS += -lshlwapi -lmincore
endif
GL_OBJ += libNK_SDL_Vulkan.o ui_renderer_vulkan.o vk_mem_alloc.o
ifneq ($(STATIC_MVK),1)
GL_OBJ += libvolk.o
endif
endif

ifeq ($(OS),Windows_NT)
LDLIBS += -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 -lws2_32 -liphlpapi
ifneq ($(LITE),1)
LDLIBS += -lshaderc_combined -lrashader -llcms2 -ld3dcompiler -ld3d11 -ldxgi -ldwmapi -lpathcch -lbcrypt -lruntimeobject -lntdll
ifeq ($(DEBUG),1)
LDLIBS += -lpropsys -luserenv -ldxcompiler
endif
endif
else
ifneq ($(LITE),1)
ifneq ($(OS),Darwin)
ifneq ($(STATIC_SDL),1)
LDLIBS += $(shell pkg-config shaderc --libs)
else
LDLIBS += -lshaderc_combined
# LDLIBS += -lglslang -lMachineIndependent -lOSDependent -lGenericCodeGen -lglslang-default-resource-limits -lSPIRV -lSPIRV-Tools-opt -lSPIRV-Tools
endif
endif
LDLIBS += -lrashader
endif
endif

GL_OBJ += libSTB_image.o libNK_SDL_renderer.o ui_common_sdl.o ui_renderer_sdl.o ui_main_nk.o ui_input_redirection.o ntr_common.o ntr_hb.o ntr_rp.o ntr_jpeg_delta.o ntr_stats_overlay.o
ifeq ($(OS),Windows_NT)
GL_OBJ += ntrviewer.res.o
endif

CLA_SRC := $(wildcard clarity/18px/*.png) $(wildcard clarity/24px/*.png) $(wildcard clarity/27px/*.png) $(wildcard clarity/36px/*.png)
CLA_INC := $(CLA_SRC:.png=.h)

ifneq ($(DEBUG),1)
LDFLAGS := -s
endif

RM := rm

ifeq ($(EMBED_JPEG_TURBO),1)
JT16_SRC := $(wildcard jpeg_turbo/jpeg16/*.c)
JT16_OBJ := $(JT16_SRC:.c=.o)

JT12_SRC := $(wildcard jpeg_turbo/jpeg12/*.c)
JT12_OBJ := $(JT12_SRC:.c=.o) $(subst jpeg16,jpeg12,$(JT16_OBJ))

JT8_SRC := $(wildcard jpeg_turbo/jpeg8/*.c)
JT8_OBJ := $(JT8_SRC:.c=.o) $(subst jpeg12,jpeg8,$(JT12_OBJ))

JT_SRC := $(wildcard jpeg_turbo/*.c)
ifeq ($(ARCH),arm64)
JT_SRC += $(wildcard jpeg_turbo/simd/arm/*.c) $(wildcard jpeg_turbo/simd/arm/aarch64/*.c)
else
JT_SRC += jpeg_turbo/simd/x86_64/jsimd.c
JT_SRC_S := $(wildcard jpeg_turbo/simd/x86_64/*.asm)
JT_OBJ_S := $(JT_SRC_S:.asm=.o)
endif
JT_OBJ := $(JT16_OBJ) $(JT12_OBJ) $(JT8_OBJ) $(JT_SRC:.c=.o)

CPPFLAGS += -DEMBED_JPEG_TURBO
else
JT_OBJ :=
JT_OBJ_S :=

LDLIBS += -lturbojpeg
endif

NK_SRC := $(wildcard nuklear/*.c)
NK_OBJ := $(NK_SRC:.c=.o)

FEC_SRC := $(wildcard fecal/*.cpp)
FEC_OBJ := $(FEC_SRC:.cpp=.o)
ifneq ($(ARCH),arm64)
FEC_OBJ += fecal/gf256_ssse3.o fecal/gf256_avx2.o fecal/gf256_ssse3_avx2.o
endif

TARGET_OBJ := main.o rp_syn.o ikcp.o $(GL_OBJ) $(JT_OBJ) $(JT_OBJ_S) $(FEC_OBJ) $(NK_OBJ)
TARGET_DEP := $(TARGET_OBJ:.o=.d)

$(TARGET): $(TARGET_OBJ)
	$(CXX) $^ -o $@ $(CFLAGS) $(LDLIBS) $(LDFLAGS) -w

CC_JT = $(CC) $< -o $@ -c $(CFLAGS) $(CPPFLAGS) -Ijpeg_turbo -Ijpeg_turbo/src -Ijpeg_turbo/include -Wno-stringop-overflow -Wno-unused-parameter -Wno-sign-compare

jpeg_turbo/jpeg8/%.o: jpeg_turbo/jpeg8/%.c
	$(CC_JT) -DBMP_SUPPORTED -DPPM_SUPPORTED

jpeg_turbo/jpeg8/%.o: jpeg_turbo/jpeg12/%.c
	$(CC_JT) -DBMP_SUPPORTED -DPPM_SUPPORTED

jpeg_turbo/jpeg8/%.o: jpeg_turbo/jpeg16/%.c
	$(CC_JT) -DBMP_SUPPORTED -DPPM_SUPPORTED

jpeg_turbo/jpeg12/%.o: jpeg_turbo/jpeg12/%.c
	$(CC_JT) -DBITS_IN_JSAMPLE=12 -DGIF_SUPPORTED -DPPM_SUPPORTED

jpeg_turbo/jpeg12/%.o: jpeg_turbo/jpeg16/%.c
	$(CC_JT) -DBITS_IN_JSAMPLE=12 -DGIF_SUPPORTED -DPPM_SUPPORTED

jpeg_turbo/jpeg16/%.o: jpeg_turbo/jpeg16/%.c
	$(CC_JT) -DBITS_IN_JSAMPLE=16 -DGIF_SUPPORTED -DPPM_SUPPORTED

jpeg_turbo/simd/arm/%.o: jpeg_turbo/simd/arm/%.c
	$(CC_JT) -DNEON_INTRINSICS -Ijpeg_turbo/simd -Ijpeg_turbo/simd/aarch64

jpeg_turbo/%.o: jpeg_turbo/%.c
	$(CC_JT) -DBMP_SUPPORTED -DGIF_SUPPORTED -DPPM_SUPPORTED -Wno-clobbered

%.o: %.asm
	nasm $< -o $@ $(NASM) -Ijpeg_turbo/simd/nasm -Ijpeg_turbo/simd

placebo.o: placebo.cpp
	$(CXX) $< -o $@ -c $(CFLAGS) $(CPPFLAGS) -std=c++17 -fno-fast-math

rashader.o: rashader.cpp
	$(CXX) $< -o $@ -c $(CFLAGS) $(CPPFLAGS) -std=c++17 -fno-fast-math

ntrviewer.res.o: win_manifest.rc win_manifest.xml
	windres --input $< --output $@ --output-format=coff

fecal/gf256_ssse3.o: fecal/gf256.cpp
	$(CXX) $< -o $@ -c $(CFLAGS) $(CPPFLAGS) -Wno-implicit-fallthrough -mssse3 -DGF_SUFFIX=_ssse3

fecal/gf256_avx2.o: fecal/gf256.cpp
	$(CXX) $< -o $@ -c $(CFLAGS) $(CPPFLAGS) -Wno-implicit-fallthrough -mavx2 -DGF_SUFFIX=_avx2

fecal/gf256_ssse3_avx2.o: fecal/gf256.cpp
	$(CXX) $< -o $@ -c $(CFLAGS) $(CPPFLAGS) -Wno-implicit-fallthrough -mssse3 -mavx2 -DGF_SUFFIX=_ssse3_avx2

fecal/%.o: fecal/%.cpp
	$(CXX) $< -o $@ -c $(CFLAGS) $(CPPFLAGS) -Wno-implicit-fallthrough -Wno-restrict -DGF256_TARGET_MOBILE

nuklear/%.o: nuklear/%.c
	$(CC) $< -o $@ -c $(CFLAGS) $(CPPFLAGS) -Wno-unused-function -std=c89

nuklear/nuklear_font.o: nuklear/nuklear_font.c
	$(CC) $< -o $@ -c $(CFLAGS) $(CPPFLAGS) -Wno-unused-function

nuklear/stb_%.o: nuklear/stb_%.c
	$(CC) $< -o $@ -c $(CFLAGS) $(CPPFLAGS)

libSTB_%.o: libSTB_%.c
	$(CC) $< -o $@ -c $(CFLAGS) $(CPPFLAGS) -Wno-unused-function

main.o: main.c
	$(CC) $< -o $@ -c $(CFLAGS) $(CPPFLAGS) -D_GNU_SOURCE

vk_mem_alloc.o: vk_mem_alloc.cpp
	$(CXX) $< -o $@ -c $(CFLAGS) $(CPPFLAGS) -std=c++20 -Wno-nullability-completeness -Wno-unused-parameter -Wno-unused-private-field -Wno-unused-variable

%.o: %.m
	$(CC) $< -o $@ -c $(CFLAGS) $(CPPFLAGS)

%.o: %.c $(CLA_INC)
	$(CC) $< -o $@ -c $(CFLAGS) $(CPPFLAGS) -D_GNU_SOURCE

clarity/%.h: clarity/%.png
	xxd -i $< > $@

-include $(TARGET_DEP)

clean:
	-$(RM) $(TARGET)
	-$(RM) *.o jpeg_turbo/jpeg8/*.o jpeg_turbo/jpeg12/*.o jpeg_turbo/jpeg16/*.o jpeg_turbo/*.o jpeg_turbo/simd/x86_64/*.o jpeg_turbo/simd/arm/*.o jpeg_turbo/simd/arm/aarch64/*.o fecal/*.o nuklear/*.o
	-$(RM) *.d jpeg_turbo/jpeg8/*.d jpeg_turbo/jpeg12/*.d jpeg_turbo/jpeg16/*.d jpeg_turbo/*.d jpeg_turbo/simd/x86_64/*.d jpeg_turbo/simd/arm/*.d jpeg_turbo/simd/arm/aarch64/*.d fecal/*.d nuklear/*.d
