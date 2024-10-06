#
# KallistiGL test program in C++
# (c)2001 Megan Potter
#   

BUILD_DIR = build

COMPONENTS = dcdriver dc-tests-ta pvr test

TARGET = $(BUILD_DIR)/dc-pvr-tests.elf
OBJS = $(patsubst %,$(BUILD_DIR)/%.o,$(COMPONENTS))
OBJS_SOURCES = $(patsubst %.o,%.cpp,$(COMPONENTS))

KOS_ROMDISK_DIR = romdisk
KOS_CPPFLAGS += -std=c++23

all: rm-elf $(TARGET)
	mkdir -p data

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS)

rm-elf:
	-rm -f $(TARGET) romdisk.*

$(TARGET): $(OBJS)
	-mkdir -p $(dir $@)
	kos-c++ -o $(TARGET) $(OBJS) -lGL -lkosutils

build/%.o: src/%.cpp
	-mkdir -p $(dir $@)
	kos-c++ -c $< -o $@

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS) romdisk.img
	$(KOS_STRIP) $(TARGET)

