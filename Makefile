# Makefile for USB Streaming Reader (Raspberry Pi Pico + libusb)

CC = gcc
# CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags libusb-1.0 2>/dev/null || echo "-I/opt/homebrew/include/libusb-1.0 -I/usr/local/include/libusb-1.0")
# LIBS = $(shell pkg-config --libs libusb-1.0 2>/dev/null || echo "-L/opt/homebrew/lib -L/usr/local/lib -lusb-1.0")

CFLAGS = -Wall -Wextra -O2 -pthread $(shell pkg-config --cflags libusb-1.0 2>/dev/null)
LIBS = -pthread $(shell pkg-config --libs libusb-1.0 2>/dev/null)

TARGET = usb_reader
SRCS = usb_reader.c

.PHONY: all clean run setup

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) *.o data.csv

setup:
	@echo "Installing dependencies..."
	@if [ "$$(uname)" = "Darwin" ]; then \
		brew install libusb pkg-config; \
	elif [ "$$(uname)" = "Linux" ]; then \
		sudo apt-get update && sudo apt-get install -y libusb-1.0-0-dev pkg-config; \
	fi