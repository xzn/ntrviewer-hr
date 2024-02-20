CC := gcc
CXX := g++
CPPFLAGS := -Iinclude
CFLAGS := -Og -g
# CFLAGS := -Ofast
EMBED_JPEG_TURBO := 0
USE_OGL_ES := 0
USE_ANGLE := 0
GL_DEBUG := 0

ifeq ($(OS),Windows_NT)
	LDLIBS := -Llib -static -lmingw32 -lSDL2main -lSDL2
	LDLIBS += -lm -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8
	LDLIBS += -lws2_32 -liphlpapi -lncnn -fopenmp
	LDLIBS += -lOSDependent -lglslang -lMachineIndependent -lGenericCodeGen -lglslang-default-resource-limits -lSPIRV -lSPIRV-Tools-opt -lSPIRV-Tools

	TARGET := ntrviewer.exe

	NASM := -DWIN64 -fwin64
else
	LDLIBS := -lSDL2 -lncnn

	TARGET := ntrviewer

	NASM := -DELF -felf64
endif
# LDFLAGS := -s

RM := rm

ifeq ($(EMBED_JPEG_TURBO),1)
JT_SRC := $(wildcard jpeg_turbo/*.c) jpeg_turbo/simd/x86_64/jsimd.c
JT_OBJ := $(JT_SRC:.c=.o)

JT_SRC_S := $(wildcard jpeg_turbo/simd/x86_64/*.asm)
JT_OBJ_S := $(JT_SRC_S:.asm=.o)

CPPFLAGS += -DEMBED_JPEG_TURBO
LDLIBS += -ljpeg
else
JT_OBJ :=
JT_OBJ_S :=

LDLIBS += -lturbojpeg
endif

ifeq ($(USE_OGL_ES),1)
CPPFLAGS += -DUSE_OGL_ES

ifeq ($(USE_ANGLE),1)
CPPFLAGS += -DUSE_ANGLE
endif

endif

ifeq ($(GL_DEBUG),1)
CPPFLAGS += -DGL_DEBUG
endif

$(TARGET): main.o realcugan.o realcugan_lib.o libNK.o libNKSDL.o libGLAD.o fsr/fsr_main.o fsr/image_utils.o $(JT_OBJ) $(JT_OBJ_S)
	$(CXX) $^ -o $@ $(CFLAGS) $(LDLIBS) $(LDFLAGS)

%.o: %.c
	$(CC) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS)

%.o: %.asm
	nasm $^ -o $@ $(NASM) -D__x86_64__ -Ijpeg_turbo/simd/nasm -Ijpeg_turbo/simd/x86_64

%.o: %.cpp
	$(CXX) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS)

realcugan_lib.o: realcugan-ncnn-vulkan/lib.cpp $(wildcard srmd-realcugan-vulkan/*.h)
	$(CXX) realcugan-ncnn-vulkan/lib.cpp -o $@ -c $(CFLAGS) $(CPPFLAGS) -Wno-attributes

realcugan.o: realcugan-ncnn-vulkan/realcugan.cpp $(wildcard realcugan-ncnn-vulkan/*.h)
	$(CXX) realcugan-ncnn-vulkan/realcugan.cpp -o $@ -c $(CFLAGS) $(CPPFLAGS) -Wno-attributes

main.o: main.c
	$(CC) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS) -Wall -Wextra

libNK.o: libNK.c
	$(CC) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS) -std=c89 -Wall -Wextra

clean:
	$(RM) $(TARGET) *.o jpeg_turbo/*.o jpeg_turbo/simd/x86_64/*.o fsr/*.o
