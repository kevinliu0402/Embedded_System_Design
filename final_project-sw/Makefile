# =====================================================================
# Software Makefile - Maze Chase Game
#
# Builds (lab3 pattern):
#   game.ko    - kernel module that exposes /dev/game
#   game_app   - user-space binary that runs the game loop
# =====================================================================

ifneq (${KERNELRELEASE},)

# We are being included from the kernel build system.
        obj-m := game.o

else

# Standalone make: ARM cross-compile by default for DE1-SoC.
        KERNEL_SOURCE := /usr/src/linux-headers-$(shell uname -r)
        PWD := $(shell pwd)

CFLAGS := -O2 -Wall -Wextra
GAME_OBJS := game_app.o usbkeyboard.o
GAME_LIBS := -lusb-1.0 -lpthread

default: module game_app

module:
	${MAKE} -C ${KERNEL_SOURCE} SUBDIRS=${PWD} modules

game_app: $(GAME_OBJS)
	$(CC) $(CFLAGS) -o $@ $(GAME_OBJS) $(GAME_LIBS)

game_app.o: game_app.c game.h maze_data.h usbkeyboard.h
	$(CC) $(CFLAGS) -c -o $@ game_app.c

usbkeyboard.o: usbkeyboard.c usbkeyboard.h
	$(CC) $(CFLAGS) -c -o $@ usbkeyboard.c

clean:
	${MAKE} -C ${KERNEL_SOURCE} SUBDIRS=${PWD} clean
	${RM} game_app $(GAME_OBJS)

TARFILES = Makefile README game.h game.c game_app.c maze_data.h \
           usbkeyboard.h usbkeyboard.c
TARFILE  = final_project-sw.tar.gz
.PHONY: tar
tar: $(TARFILE)

$(TARFILE): $(TARFILES)
	tar zcfC $(TARFILE) .. $(TARFILES:%=final_project-sw/%)

endif
