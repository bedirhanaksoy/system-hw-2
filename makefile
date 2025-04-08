# compiler and flags
CC = gcc
CFLAGS = -Wall -g

# target executable name
TARGET = syshw2

# default build
all: $(TARGET)

# compile main.c to main
$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c

# run the program with example arguments
run: $(TARGET)
	./$(TARGET) 5 739

# clean up build files
clean:
	rm -f $(TARGET)
