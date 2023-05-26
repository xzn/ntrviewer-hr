CC := gcc
CPPFLAGS := -Iinclude
CFLAGS := -Og -g
LDLIBS := -Llib -lmingw32 -lSDL2main -lSDL2 -lvfw32 -Wl,-Bstatic -lws2_32
LDFLAGS :=

RM := rm

TARGET := ntrviewer-hr.exe

$(TARGET): main.o lib.o ikcp.o ffmpeg_opt/libavcodec/jpegls.o ffmpeg_opt/libavcodec/jpeglsdec.o ffmpeg_opt/libavcodec/mathtables.o ffmpeg_opt/libavutil/intmath.o ffmpeg_opt/libavcodec/decode.o
	$(CC) $^ -o $@ $(CFLAGS) $(LDLIBS) $(LDFLAGS)

%.o: %.c
	$(CC) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS) -Iffmpeg_opt

clean:
	$(RM) $(TARGET) *.o ffmpeg_opt/libavcodec/*.o ffmpeg_opt/libavutil/*.o
