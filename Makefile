CC := gcc
CXX := g++
CPPFLAGS := -Iinclude
CFLAGS := -Og -g
LDLIBS := -Llib -lmingw32 -lSDL2main -lSDL2 -lvfw32 -Wl,-Bstatic -lws2_32 -lncnn -lole32 -fopenmp
LDLIBS += -lOSDependent -lglslang -lMachineIndependent -lGenericCodeGen -lglslang-default-resource-limits -lSPIRV -lSPIRV-Tools-opt -lSPIRV-Tools -lSPIRV-Tools-link
LDFLAGS :=

RM := rm

TARGET := ntrviewer-hr.exe

JT_SRC := $(wildcard jpeg_turbo/*.c) jpeg_turbo/simd/x86_64/jsimd.c
JT_OBJ := $(JT_SRC:.c=.o)

JT_SRC_S := $(wildcard jpeg_turbo/simd/x86_64/*.asm)
JT_OBJ_S := $(JT_SRC_S:.asm=.o)

ZSTD_SRC := $(wildcard zstd/common/*.c) $(wildcard zstd/decompress/*.c)
ZSTD_OBJ := $(ZSTD_SRC:.c=.o)

$(TARGET): main.o realcugan.o realcugan_lib.o srmd.o srmd_lib.o realsr.o realsr_lib.o huffmandec.o rledec.o lz4.o libNK.o libNKSDL.o libGLAD.o ikcp.o ffmpeg_opt/libavcodec/jpegls.o ffmpeg_opt/libavcodec/jpeglsdec.o ffmpeg_opt/libavcodec/mathtables.o ffmpeg_opt/libavutil/intmath.o ffmpeg_opt/libavcodec/decode.o imagezero/iz_dec.o imagezero/table.o $(JT_OBJ) $(JT_OBJ_S) $(ZSTD_OBJ)
	$(CXX) $^ -o $@ $(CFLAGS) $(LDLIBS) $(LDFLAGS)

%.o: %.c
	$(CC) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS) -Iffmpeg_opt

%.o: %.asm
	nasm $^ -o $@ -DWIN64 -D__x86_64__ -fwin64 -Ijpeg_turbo/simd/nasm -Ijpeg_turbo/simd/x86_64

%.o: %.cpp
	$(CXX) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS) -Iimagezero

realsr_lib.o: realsr-ncnn-vulkan/lib.cpp $(wildcard realsr-ncnn-vulkan/*.h)
	$(CXX) realsr-ncnn-vulkan/lib.cpp -o $@ -c $(CFLAGS) $(CPPFLAGS) -I../ncnn-src/src -I../ncnn-build/src -Wno-attributes

realsr.o: realsr-ncnn-vulkan/realsr.cpp $(wildcard realsr-ncnn-vulkan/*.h)
	$(CXX) realsr-ncnn-vulkan/realsr.cpp -o $@ -c $(CFLAGS) $(CPPFLAGS) -I../ncnn-src/src -I../ncnn-build/src -Wno-attributes

srmd_lib.o: srmd-ncnn-vulkan/lib.cpp $(wildcard srmd-ncnn-vulkan/*.h)
	$(CXX) srmd-ncnn-vulkan/lib.cpp -o $@ -c $(CFLAGS) $(CPPFLAGS) -I../ncnn-src/src -I../ncnn-build/src -Wno-attributes

srmd.o: srmd-ncnn-vulkan/srmd.cpp $(wildcard srmd-ncnn-vulkan/*.h)
	$(CXX) srmd-ncnn-vulkan/srmd.cpp -o $@ -c $(CFLAGS) $(CPPFLAGS) -I../ncnn-src/src -I../ncnn-build/src -Wno-attributes

realcugan_lib.o: realcugan-ncnn-vulkan/lib.cpp $(wildcard srmd-realcugan-vulkan/*.h)
	$(CXX) realcugan-ncnn-vulkan/lib.cpp -o $@ -c $(CFLAGS) $(CPPFLAGS) -I../ncnn-src/src -I../ncnn-build/src -Wno-attributes

realcugan.o: realcugan-ncnn-vulkan/realcugan.cpp $(wildcard realcugan-ncnn-vulkan/*.h)
	$(CXX) realcugan-ncnn-vulkan/realcugan.cpp -o $@ -c $(CFLAGS) $(CPPFLAGS) -I../ncnn-src/src -I../ncnn-build/src -Wno-attributes

main.o: main.c
	$(CC) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS) -Iffmpeg_opt -Wall -Wextra

libNK.o: libNK.c
	$(CC) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS) -std=c89 -Wall -Wextra

clean:
	$(RM) $(TARGET) *.o ffmpeg_opt/libavcodec/*.o ffmpeg_opt/libavutil/*.o imagezero/*.o jpeg_turbo/*.o jpeg_turbo/simd/x86_64/*.o zstd/common/*.o zstd/decompress/*.o
