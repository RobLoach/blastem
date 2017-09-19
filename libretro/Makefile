DEBUG := 0
NAME  := blastem_libretro
SOEXT := .so
FLTO  :=
WITH_Z80      := 1
WITH_DYNAREC  :=
OPTIMIZE_FLAG :=

ifeq ($(DEBUG), 1)
   OPTIMIZE_FLAG := -g
else
   OPTIMIZE_FLAG := -O2
endif

ifeq (,$(ARCH))
   ARCH = $(shell uname -m)
endif

WITH_DYNAREC = $(ARCH)

PIC = 1
ifeq ($(ARCH), $(filter $(ARCH), i386 i686))
   WITH_DYNAREC = x86
   PIC = 0
endif

ifneq ($(SANITIZER),)
   CFLAGS  := -fsanitize=$(SANITIZER) $(CFLAGS)
   LDFLAGS := -fsanitize=$(SANITIZER) $(LDLAGS)
endif

CFLAGS  := $(OPTIMIZE_FLAG) -std=gnu99 -Wall -D__LIBRETRO__ -I. -I.. -Ilibco $(SANITIZER)
LDFLAGS := $(SANITIZER) -shared -Wl,--no-undefined
LIBS    := -lm

ifeq ($(platform),)
   platform := unix
endif

ifeq ($(platform),win32)
   SOEXT := .dll
endif

ifeq ($(PIC), 1)
   FPIC = -fPIC
else
   FPIC = -fno-PIC
endif

prefix := /usr
libdir := $(prefix)/lib

LIBRETRO_INSTALL_DIR := libretro

TARGET  := $(NAME)$(SOEXT)
CFLAGS  += $(CFLAGS) $(FPIC) $(FLTO)
LDFLAGS += $(LDFLAGS) $(FPIC) $(FLTO)

# BlastEm objects
OBJECTS += \
   ../gen.o\
   ../backend.o\
   ../68kinst.o\
   ../m68k_core.o\
   ../ym2612.o\
   ../psg.o\
   ../wave.o\
   ../config.o\
   ../tern.o\
   ../util.o\
   ../blastem.o\
   ../vdp.o\
   ../io.o\
   ../romdb.o\
   ../gst.o

ifeq ($(WITH_DYNAREC),x86)
   CFLAGS  += -DX86_32 -m32
   LDFLAGS += -m32
   OBJECTS += ../m68k_core_x86.o ../gen_x86.o ../backend_x86.o
else ifeq ($(WITH_DYNAREC),x86_64)
   CFLAGS  += -DX86_64 -m64
   LDFLAGS += -m64
   OBJECTS += ../m68k_core_x86.o ../gen_x86.o ../backend_x86.o
else
   $(error Invalid or missing WITH_DYNAREC value)
endif

ifeq ($(WITH_Z80),1)
   OBJECTS += ../z80inst.o ../z80_to_x86.o
else
   CFLAGS += -DNO_Z80
endif

# blastem-libretro objects
OBJECTS += mem.o render_libretro.o libco/libco.o

all : $(TARGET)

$(TARGET): $(OBJECTS) Makefile
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJECTS) $(LIBS)

%.o : %.S Makefile
	$(CC) -c -o $@ $<

%.o : %.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

clean :
	rm -rf $(OBJECTS) $(TARGET)

install:
	install -D -m 755 $(TARGET) $(DESTDIR)$(libdir)/$(LIBRETRO_INSTALL_DIR)/$(TARGET)

uninstall:
	rm $(DESTDIR)$(libdir)/$(LIBRETRO_INSTALL_DIR)/$(TARGET)

.PHONY: clean install uninstall
