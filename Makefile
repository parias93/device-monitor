OUTPUT = main
INCLUDES = -I../systemd/src/libudev
CFLAGS = -Wall -g -O0 $(INCLUDES)
LIBS = $(SYSTEMD_SRC)/.libs

.PHONY: clean clean-fsroot
all: main.c
ifndef SYSTEMD_SRC
	$(error "Variable SYSTEMD_SRC not defined. Aborting.")
endif
	gcc $(CFLAGS) -o $(OUTPUT) $^ -L$(LIBS) -ludev -Wl,-rpath=$(LIBS)

clean:
	rm $(OUTPUT)

fuse: fuse.c
	gcc -Wall -g -O0 fuse.c fsroot.c `pkg-config fuse3 --cflags --libs` -Wl,-rpath=/usr/local/lib -o fuse

fsroot: fsroot.c
	# Compile only: this target generates object code for fsroot.c
	gcc -c -std=gnu99 -Wall -g -O0 fsroot.c hash.c mm.c
main: fsroot
	gcc -std=gnu99 -Wall -g -O0 fsroot-main.c fsroot.o hash.o mm.o -o fsroot-main -pthread
clean-fsroot:
	rm fsroot-main
