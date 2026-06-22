# Build with: mingw32-make  (or: make, from an MSYS2 MinGW-w64 shell)
# Produces wm.exe in this directory.
#
# MSVC users: see the build command in the header comment of main.c instead
# (cl /O2 /DUNICODE /D_UNICODE *.c ... /link /SUBSYSTEM:WINDOWS).

CC      = gcc
RC      = windres
CFLAGS  = -O2 -municode -mwindows -Wall -Wextra
LIBS    = -luser32 -lgdi32 -ldwmapi -ladvapi32 -lshell32 -lole32

SRCS = main.c window_state.c util.c focus.c desktop.c hotkeys.c tray.c
OBJS = $(SRCS:.c=.o)
RES  = wm.res

wm.exe: $(OBJS) $(RES)
	$(CC) $(CFLAGS) $(OBJS) $(RES) -o $@ $(LIBS)

%.o: %.c wm_common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(RES): wm.rc wm.ico
	$(RC) wm.rc -O coff -o $(RES)

clean:
	rm -f $(OBJS) $(RES) wm.exe

.PHONY: clean
