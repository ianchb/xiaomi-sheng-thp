CXX ?= g++
CPPFLAGS += -Isrc
CXXFLAGS ?= -O3 -flto -std=c++20
CXXFLAGS += -Wall -Wextra -Werror

HAPTICS ?= 1
ifeq ($(HAPTICS),1)
HAPTICS_SOURCE := src/bluez_pen_haptics.cpp
CXXFLAGS += -pthread
LDLIBS += -lsystemd -pthread
else
HAPTICS_SOURCE := src/focus_pen_haptics_stub.cpp
endif

BUILD := build
TARGET := $(BUILD)/xiaomi-sheng-thp
HAPTICS_STAMP := $(BUILD)/.haptics-$(HAPTICS)
SOURCES := \
	$(HAPTICS_SOURCE) \
	src/nvt_touch_core.cpp \
	src/nvt_finger_filter.cpp \
	src/nvt_stylus.cpp \
	src/xiaomi_sheng_thp.cpp
HEADERS := \
	src/focus_pen_haptics.hpp \
	src/nvt_touch_core.hpp \
	src/nvt_finger_filter.hpp \
	src/nvt_stylus.hpp \
	src/nvt_focus_pen_pressure.hpp

PREFIX ?= /usr
LIBEXECDIR ?= $(PREFIX)/libexec/xiaomi-sheng-thp
SYSTEMD_UNIT_DIR ?= $(PREFIX)/lib/systemd/system
DOCDIR ?= $(PREFIX)/share/doc/xiaomi-sheng-thp

.PHONY: all clean install

all: $(TARGET)

$(BUILD):
	mkdir -p $@

$(HAPTICS_STAMP): | $(BUILD)
	rm -f $(BUILD)/.haptics-0 $(BUILD)/.haptics-1
	touch $@

$(TARGET): $(SOURCES) $(HEADERS) $(HAPTICS_STAMP) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDLIBS) -o $@

install: $(TARGET)
	install -Dm755 -s $(TARGET) \
		$(DESTDIR)$(LIBEXECDIR)/xiaomi-sheng-thp
	install -Dm644 systemd/xiaomi-sheng-thp.service \
		$(DESTDIR)$(SYSTEMD_UNIT_DIR)/xiaomi-sheng-thp.service
	install -Dm644 README.md \
		$(DESTDIR)$(DOCDIR)/README.md
	install -Dm644 LICENSE \
		$(DESTDIR)$(DOCDIR)/copyright

clean:
	rm -rf $(BUILD)
