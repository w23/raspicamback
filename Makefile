.SUFFIXES:
MAKEOPTS+=-r

CFLAGS += -Wall -Wextra -D_GNU_SOURCE -I3p/atto -fPIE
BUILDDIR ?= build

ifeq ($(DEBUG), 1)
	CONFIG = dbg
	CFLAGS += -O0 -ggdb3
else
	CONFIG = rel
	CFLAGS += -O3
endif

ifeq ($(NOWERROR), 1)
	CONFIG := $(CONFIG)nowerror
else
	CFLAGS += -Werror
endif

PLATFORM = pi
RPI_ROOT ?= /opt/raspberry-pi

ifeq ($(CROSS), 1)
	RPI_TOOLCHAIN ?= gcc-linaro-arm-linux-gnueabihf-raspbian-x64
	RPI_TOOLCHAINDIR ?= $(RPI_ROOT)/raspberry-tools/arm-bcm2708/$(RPI_TOOLCHAIN)
	RPI_VCDIR ?= $(RPI_ROOT)/raspberry-firmware/hardfp/opt/vc
	CC = $(RPI_TOOLCHAINDIR)/bin/arm-linux-gnueabihf-gcc
	COMPILER = gcc
else
	RPI_VCDIR ?= /opt/vc
	CC ?= cc
endif

CFLAGS += -I$(RPI_VCDIR)/include -I$(RPI_VCDIR)/include/interface/vcos/pthreads
CFLAGS += -I$(RPI_VCDIR)/include/interface/vmcs_host/linux -DATTO_PLATFORM_RPI
LIBS += -lbrcmGLESv2 -lbrcmEGL -lbcm_host -lvcos -lvchiq_arm -L$(RPI_VCDIR)/lib -lrt -lm
LIBS += -lmmal -lmmal_core -lmmal_util

SOURCES += \
	3p/atto/src/app_linux.c \
	3p/atto/src/app_rpi.c

COMPILER ?= $(CC)
OBJDIR ?= $(BUILDDIR)/$(PLATFORM)-$(CONFIG)-$(COMPILER)

DEPFLAGS = -MMD -MP
COMPILE.c = $(CC) -std=gnu99 $(CFLAGS) $(DEPFLAGS) -MT $@ -MF $@.d

$(OBJDIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(COMPILE.c) -c $< -o $@

EXE = $(OBJDIR)/raspicamback
all: $(EXE)
SOURCES += \
	   atto_gl_impl.c \
	   rpicamtex.c \
	   main.c

OBJECTS = $(SOURCES:%=$(OBJDIR)/%.o)
DEPS = $(OBJECTS:%=%.d)

-include $(DEPS)

$(EXE): $(OBJECTS)
	$(CC) $^ $(LIBS) -o $@

clean:
	rm -f $(OBJECTS) $(DEPS) $(EXE)

run: $(EXE)
	$(EXE) $(ARGS)

debug: $(EXE)
	gdb --args $(EXE) $(ARGS)

.PHONY: all clean
