PREFIX = /usr

ifneq ($(DEB_HOST_MULTIARCH),)
	CROSS_COMPILE ?= $(DEB_HOST_MULTIARCH)-
endif

ifeq ($(origin CC),default)
	CC := $(CROSS_COMPILE)gcc
endif
ifeq ($(origin CXX),default)
	CXX := $(CROSS_COMPILE)g++
endif

ifeq ($(DEBUG),)
	BUILD_DIR ?= build/release
else
	BUILD_DIR ?= build/debug
endif

# extract Git revision and version number from debian/changelog
GIT_REVISION:=$(shell git rev-parse HEAD)
DEB_VERSION:=$(shell head -1 debian/changelog | awk '{ print $$2 }' | sed 's/[\(\)]//g')

TARGET = wb-mqtt-iec104
SRC_DIR = src

COMMON_SRCS := $(shell find $(SRC_DIR) -name "*.cpp" -and -not -name main.cpp)
COMMON_OBJS := $(COMMON_SRCS:%=$(BUILD_DIR)/%.o)

LDFLAGS = -lwbmqtt1 -lpthread -llib60870
CXXFLAGS = -std=c++20 -Wall -Werror -I$(SRC_DIR) -DWBMQTT_COMMIT="$(GIT_REVISION)" -DWBMQTT_VERSION="$(DEB_VERSION)"

ifeq ($(DEBUG),)
	CXXFLAGS += -O3 -DNDEBUG
else
	CXXFLAGS += -g -O0 --coverage -ggdb
	LDFLAGS += --coverage
endif

TEST_DIR = test
TEST_SRCS := $(shell find $(TEST_DIR) -name "*.cpp")
TEST_OBJS := $(TEST_SRCS:%=$(BUILD_DIR)/%.o)
TEST_TARGET = test-app
TEST_LDFLAGS = -lgtest -lwbmqtt_test_utils

VALGRIND_FLAGS = --error-exitcode=180 -q

COV_REPORT ?= $(BUILD_DIR)/cov
GCOVR_FLAGS := -s --html $(COV_REPORT).html -x $(COV_REPORT).xml
ifneq ($(COV_FAIL_UNDER),)
	GCOVR_FLAGS += --fail-under-line $(COV_FAIL_UNDER)
endif

all: $(TARGET)

$(TARGET): $(COMMON_OBJS) $(BUILD_DIR)/$(SRC_DIR)/main.cpp.o
	$(CXX) -o $(BUILD_DIR)/$@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) -c $(CXXFLAGS) -o $@ $^

test: $(TEST_DIR)/$(TEST_TARGET)
	rm -f $(TEST_DIR)/*.dat.out
	if [ "$(shell arch)" != "armv7l" ] && [ "$(CROSS_COMPILE)" = "" ] || [ "$(CROSS_COMPILE)" = "x86_64-linux-gnu-" ]; then \
		valgrind $(VALGRIND_FLAGS) $(TEST_DIR)/$(TEST_TARGET) $(TEST_ARGS) || \
		if [ $$? = 180 ]; then \
			echo "*** VALGRIND DETECTED ERRORS ***" 1>& 2; \
			exit 1; \
		else $(TEST_DIR)/abt.sh show; exit 1; fi; \
	else \
		$(TEST_DIR)/$(TEST_TARGET) $(TEST_ARGS) || { $(TEST_DIR)/abt.sh show; exit 1; } \
	fi
ifneq ($(DEBUG),)
	gcovr $(GCOVR_FLAGS) $(BUILD_DIR)/$(SRC_DIR) $(BUILD_DIR)/$(TEST_DIR)
endif

$(TEST_DIR)/$(TEST_TARGET): $(TEST_OBJS) $(COMMON_OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS) $(TEST_LDFLAGS) -fno-lto

distclean: clean

clean:
	rm -rf build $(TEST_DIR)/$(TEST_TARGET)

install:
	install -Dm0755 $(BUILD_DIR)/$(TARGET) -t $(DESTDIR)$(PREFIX)/bin
	install -Dm0644 wb-mqtt-iec104.schema.json -t $(DESTDIR)$(PREFIX)/share/wb-mqtt-confed/schemas
	install -Dm0644 wb-mqtt-iec104.sample.conf -t $(DESTDIR)$(PREFIX)/share/wb-mqtt-iec104
	install -Dm0644 wb-mqtt-iec104.wbconfigs $(DESTDIR)/etc/wb-configs.d/17wb-mqtt-iec104

.PHONY: all test clean
