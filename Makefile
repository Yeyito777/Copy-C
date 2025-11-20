CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lX11 -lgit2

TARGET = copy

all: $(TARGET)

$(TARGET): copy.c
	$(CC) $(CFLAGS) -o $(TARGET) copy.c $(LDFLAGS)

install: $(TARGET)
	mkdir -p /home/yeyito/.local/bin
	cp $(TARGET) /home/yeyito/.local/bin

clean:
	rm -f $(TARGET) a.out

debug:
	$(CC) -DDEBUG $(CFLAGS) -o $(TARGET) copy.c $(LDFLAGS)
