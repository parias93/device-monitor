OUTPUT = main
INCLUDES = -I../systemd/src/libudev
CFLAGS = -Wall -g -O0 $(INCLUDES)
LIBS = $(SYSTEMD_SRC)/.libs

.PHONY: clean
all: main.c
ifndef SYSTEMD_SRC
	$(error "Variable SYSTEMD_SRC not defined. Aborting.")
endif
	gcc $(CFLAGS) -o $(OUTPUT) $^ -L$(LIBS) -ludev -Wl,-rpath=$(LIBS)

clean:
	rm $(OUTPUT)

fuse: fuse.c
	gcc -Wall fuse.c `pkg-config fuse3 --cflags --libs` -o fuse
