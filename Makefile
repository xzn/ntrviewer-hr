CC := gcc
CXX := g++
CPPFLAGS := -Iinclude
CFLAGS := -Og -g
LDLIBS := -Llib -lmingw32 -lSDL2main -lSDL2 -lvfw32 -Wl,-Bstatic -lws2_32
LDFLAGS :=

RM := rm

TARGET := ntrviewer-hr.exe

JT_SRC := $(wildcard jpeg_turbo/*.c) jpeg_turbo/simd/x86_64/jsimd.c
JT_OBJ := $(JT_SRC:.c=.o)

JT_SRC_S := $(wildcard jpeg_turbo/simd/x86_64/*.asm)
JT_OBJ_S := $(JT_SRC_S:.asm=.o)

ZSTD_SRC := $(wildcard zstd/common/*.c) $(wildcard zstd/decompress/*.c)
ZSTD_OBJ := $(ZSTD_SRC:.c=.o)

$(TARGET): main.o libNK.o libNKSDL.o libGLAD.o ikcp.o ffmpeg_opt/libavcodec/jpegls.o ffmpeg_opt/libavcodec/jpeglsdec.o ffmpeg_opt/libavcodec/mathtables.o ffmpeg_opt/libavutil/intmath.o ffmpeg_opt/libavcodec/decode.o imagezero/iz_dec.o imagezero/table.o $(JT_OBJ) $(JT_OBJ_S) $(ZSTD_OBJ)
	$(CC) $^ -o $@ $(CFLAGS) $(LDLIBS) $(LDFLAGS)

%.o: %.c
	$(CC) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS) -Iffmpeg_opt

%.o: %.asm
	nasm $^ -o $@ -DWIN64 -D__x86_64__ -fwin64 -Ijpeg_turbo/simd/nasm -Ijpeg_turbo/simd/x86_64

%.o: %.cpp
	$(CXX) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS) -Iimagezero

main.o: main.c
	$(CC) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS) -Iffmpeg_opt -Wall -Wextra

libNK.o: libNK.c
	$(CC) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS) -std=c89 -Wall -Wextra

clean:
	$(RM) $(TARGET) *.o ffmpeg_opt/libavcodec/*.o ffmpeg_opt/libavutil/*.o jpeg_turbo/*.o jpeg_turbo/simd/x86_64/*.o zstd/common/*.o zstd/decompress/*.c
