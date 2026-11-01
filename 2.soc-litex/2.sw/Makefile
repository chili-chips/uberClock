BUILD_DIR?=../build/

include $(BUILD_DIR)/software/include/generated/variables.mak
include $(SOC_DIRECTORY)/software/common.mak

UBERCLOCK_OBJECTS = uberclock.o uberclock_hw.o uberclock_channels.o uberclock_fifo.o \
                     uberclock_fft.o uberclock_track.o uberclock_siggen.o \
                     uberclock_capture.o uberclock_dma.o uberclock_parse.o \
                     uberclock_commands.o

OBJECTS   = donut.o helloc.o crt0.o main.o console.o cmd_list.o $(UBERCLOCK_OBJECTS) ubddr3.o kiss_fft.o
CFLAGS   += -I$(CURDIR)/kissfft
# -iquote (not -I) for this project's own headers: it is searched for
# #include "..." before -I dirs, so src/uberclock/*.c resolves our
# inc/uberclock/*.h and root-level console.h/cmd_list.h/ubddr3.h to THIS
# project's copies rather than e.g. LiteX libbase's own console.h, which
# -I's directory-order would otherwise shadow them with once the including
# file is no longer directly in this directory (main.c/cmd_list.c relied on
# the "including file's own directory is searched first" quote-include rule
# instead, which only works for files that live directly in $(CURDIR)).
CFLAGS   += -iquote $(CURDIR)/inc
CFLAGS   += -iquote $(CURDIR)
CFLAGS   += -DFIXED_POINT=16
ifdef WITH_CXX
	OBJECTS += hellocpp.o
	CFLAGS += -DWITH_CXX
endif


all: demo.bin


%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
ifneq ($(OS),Windows_NT)
	chmod -x $@
endif

vpath %.a $(PACKAGES:%=../%)

demo.elf: $(OBJECTS)
	$(CC) $(LDFLAGS) -T linker.ld -N -o $@ \
		$(OBJECTS) \
		$(PACKAGES:%=-L$(BUILD_DIR)/software/%) \
		-Wl,--whole-archive \
		-Wl,--gc-sections \
		-Wl,-Map,$@.map \
		$(LIBS:lib%=-l%)

ifneq ($(OS),Windows_NT)
	chmod -x $@
endif

# pull in dependency info for *existing* .o files
-include $(OBJECTS:.o=.d)

donut.o: CFLAGS   += -w

VPATH = $(BIOS_DIRECTORY):$(BIOS_DIRECTORY)/cmds:$(CPU_DIRECTORY):$(CURDIR)/src/uberclock


%.o: %.cpp
	$(compilexx)

%.o: %.c
	$(compile)

%.o: %.S
	$(assemble)

clean:
	$(RM) $(OBJECTS) $(OBJECTS:.o=.d) demo.elf demo.bin demo.elf.map .*~ *~

.PHONY: all clean
