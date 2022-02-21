CC := gcc
CPPFLAGS :=
CFLAGS := -Ofast -g -s
LDLIBS := -lmingw32 -lSDL2main -lSDL2 -Wl,-Bstatic -lws2_32
LDFLAGS :=

RM := rm

TARGET := ntrviewer-hr.exe

$(TARGET): main.o lib.o ikcp.o
	$(CC) $^ -o $@ $(CFLAGS) $(LDLIBS) $(LDFLAGS)

%.o: %/c
	$(CC) $^ -o $@ -c $(CFLAGS) $(CPPFLAGS)

clean:
	$(RM) $(TARGET) *.o
