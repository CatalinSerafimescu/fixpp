CC      = gcc
CFLAGS  = -O2 -fPIC -Wall -Wextra -std=c11
LDFLAGS = -shared -ldl

TARGET  = libmallocnesia.so

.PHONY: all clean install

all: $(TARGET)

$(TARGET): mallocnesia.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/lib/
	ldconfig

clean:
	rm -f $(TARGET)
