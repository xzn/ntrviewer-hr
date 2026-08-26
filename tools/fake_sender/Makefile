# fake_sender: a/v test source for ntrviewer-hr
CC      := gcc
CFLAGS  := -O2 -g -Wall -Wextra -std=gnu11 -I../.. -I../../jpeg_turbo
LDLIBS  := -lturbojpeg -lws2_32 -lm
TARGET  := fake_sender.exe

$(TARGET): fake_sender.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: clean
