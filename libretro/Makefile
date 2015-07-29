DEBUG := 0
NAME  := blastem_libretro
SOEXT := .so
FLTO  :=
FPIC  :=
WITH_Z80      := 1
WITH_DYNAREC  :=
OPTIMIZE_FLAG :=

ifeq ($(DEBUG), 1)
   OPTIMIZE_FLAG := -g
else
   OPTIMIZE_FLAG := -O2
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

ifeq ($(platform),unix)
   FPIC := -fPIC
else ifeq ($(platform),win32)
   SOEXT := .dll
endif

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
   ../debug.o\
   ../gdb_remote.o\
   ../vdp.o\
   ../io.o\
   ../romdb.o\
   ../gst.o\

ifeq ($(WITH_DYNAREC),x86)
   CFLAGS  += -DX86_32 -m32
   LDFLAGS += -m32
   OBJECTS += ../m68k_core_x86.o ../gen_x86.o ../backend_x86.o
else ifeq ($(WITH_DYNAREC),x86_64)
   CFLAGS  += -DX86_64 -m64
   LDFLAGS += -m64
   OBJECTS += ../m68k_core_x86.o ../gen_x86.o ../backend_x86.o
endif

ifeq ($(WITH_Z80),1)
   OBJECTS += ../z80inst.o ../z80_to_x86.o
else
   CFLAGS += -DNO_Z80
endif

# blastem-libretro objects
OBJECTS += mem.o render_libretro.o libco/libco.o terminal.o

all : $(TARGET)

$(TARGET): $(OBJECTS) Makefile
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJECTS) $(LIBS)

%.o : %.S Makefile
	$(CC) -c -o $@ $<

%.o : %.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

clean :
	rm -rf $(OBJECTS) $(TARGET)

