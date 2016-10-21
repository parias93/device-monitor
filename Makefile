OUTPUT = main
INCLUDES = -I../systemd/src/libudev
CFLAGS = -Wall -g -O0 $(INCLUDES)
LIBS = /home/strunk/code/systemd/.libs

.PHONY: clean
all: main.c
	gcc $(CFLAGS) -o $(OUTPUT) $^ -L$(LIBS) -ludev -Wl,-rpath=$(LIBS)

clean:
	rm $(OUTPUT)
