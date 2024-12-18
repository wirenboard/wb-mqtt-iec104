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

TARGET = wb-mqtt-iec104
OBJS = main.o
SRC_DIR = src
LIB60870_DIR = thirdparty/lib60870/lib60870-C/src

COMMON_OBJS = log.o config_parser.o gateway.o IEC104Server.o iec104_exception.o

DEBUG_CXXFLAGS = -O0 --coverage
DEBUG_LDFLAGS = --coverage

NORMAL_CXXFLAGS = -O2
NORMAL_LDFLAGS =

TEST_DIR = test
TEST_OBJS = main.o config.test.o gateway.test.o
TEST_TARGET = test-app
TEST_LDFLAGS = -lgtest -lwbmqtt_test_utils

VALGRIND_FLAGS = --error-exitcode=180 -q

COV_REPORT ?= cov
GCOVR_FLAGS := -s --html $(COV_REPORT).html -x $(COV_REPORT).xml
ifneq ($(COV_FAIL_UNDER),)
	GCOVR_FLAGS += --fail-under-line $(COV_FAIL_UNDER)
endif

TEST_OBJS := $(patsubst %, $(TEST_DIR)/%, $(TEST_OBJS))
COMMON_OBJS := $(patsubst %, $(SRC_DIR)/%, $(COMMON_OBJS))
OBJS := $(patsubst %, $(SRC_DIR)/%, $(OBJS))

LIB60870_OBJS = \
	$(LIB60870_DIR)/common/linked_list.o                       \
	$(LIB60870_DIR)/hal/memory/lib_memory.o                    \
	$(LIB60870_DIR)/hal/socket/linux/socket_linux.o            \
	$(LIB60870_DIR)/hal/thread/linux/thread_linux.o            \
	$(LIB60870_DIR)/hal/time/unix/time.o                       \
	$(LIB60870_DIR)/iec60870/frame.o                           \
	$(LIB60870_DIR)/iec60870/apl/cpXXtime2a.o                  \
	$(LIB60870_DIR)/iec60870/cs101/cs101_asdu.o                \
	$(LIB60870_DIR)/iec60870/cs101/cs101_bcr.o                 \
	$(LIB60870_DIR)/iec60870/cs101/cs101_information_objects.o \
	$(LIB60870_DIR)/iec60870/cs101/cs101_master_connection.o   \
	$(LIB60870_DIR)/iec60870/cs101/cs101_queue.o               \
	$(LIB60870_DIR)/iec60870/cs104/cs104_connection.o          \
	$(LIB60870_DIR)/iec60870/cs104/cs104_frame.o               \
	$(LIB60870_DIR)/iec60870/cs104/cs104_slave.o               \
	$(LIB60870_DIR)/iec60870/link_layer/buffer_frame.o         \
	$(SRC_DIR)/lib60870_common.o                               \

COMMON_OBJS += $(LIB60870_OBJS)

LIB60870_INCLUDES = -I$(LIB60870_DIR)/inc/api -I$(LIB60870_DIR)/inc/internal -I$(LIB60870_DIR)/common/inc -I$(LIB60870_DIR)/hal/inc

LDFLAGS = -lwbmqtt1 -lpthread
CXXFLAGS = -std=c++17 -Wall -Werror $(LIB60870_INCLUDES) -I$(SRC_DIR)
CFLAGS = -Wall $(LIB60870_INCLUDES) -I$(SRC_DIR)

CXXFLAGS += $(if $(or $(DEBUG)),$(DEBUG_CXXFLAGS),$(NORMAL_CXXFLAGS))
LDFLAGS += $(if $(or $(DEBUG)),$(DEBUG_LDFLAGS),$(NORMAL_LDFLAGS))

# extract Git revision and version number from debian/changelog
GIT_REVISION:=$(shell git rev-parse HEAD)
DEB_VERSION:=$(shell head -1 debian/changelog | awk '{ print $$2 }' | sed 's/[\(\)]//g')

CXXFLAGS += -DWBMQTT_COMMIT="$(GIT_REVISION)" -DWBMQTT_VERSION="$(DEB_VERSION)"

all: $(TARGET)

$(TARGET): $(OBJS) $(COMMON_OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

src/%.o: src/%.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $^

test/%.o: test/%.cpp
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
	gcovr $(GCOVR_FLAGS) $(SRC_DIR) $(TEST_DIR)
endif

$(TEST_DIR)/$(TEST_TARGET): $(TEST_OBJS) $(COMMON_OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS) $(TEST_LDFLAGS) -fno-lto

distclean: clean

clean:
	rm -rf $(SRC_DIR)/*.o $(TARGET) $(TEST_DIR)/*.o $(TEST_DIR)/$(TEST_TARGET) $(LIB60870_OBJS)
	rm -rf $(SRC_DIR)/*.gcda $(SRC_DIR)/*.gcno $(TEST_DIR)/*.gcda $(TEST_DIR)/*.gcno

install:
	install -Dm0644 wb-mqtt-iec104.schema.json -t $(DESTDIR)$(PREFIX)/share/wb-mqtt-confed/schemas
	install -Dm0644 wb-mqtt-iec104.sample.conf -t $(DESTDIR)$(PREFIX)/share/wb-mqtt-iec104
	install -Dm0755 $(TARGET) -t $(DESTDIR)$(PREFIX)/bin
	install -Dm0644 wb-mqtt-iec104.wbconfigs $(DESTDIR)/etc/wb-configs.d/17wb-mqtt-iec104

.PHONY: all test clean
