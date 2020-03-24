SDK_PATH:=$(shell pwd)/../../../../ARDroneLib
PC_TARGET=yes
USE_LINUX=yes

ifdef MYKONOS
   include $(ARDRONE_CUSTOM_CONFIG)
   include $(ARDRONE_BUILD_CONFIG)
else
   include $(SDK_PATH)/Soft/Build/custom.makefile
   include $(SDK_PATH)/Soft/Build/config.makefile
endif

ifeq "$(RELEASE_BUILD)" "yes"
   ARDRONE_TARGET_DIR=$(shell pwd)/../../Build/Release
else
   ARDRONE_TARGET_DIR=$(shell pwd)/../../Build/Debug
endif

TARGET=ardrone_video_demo

SRC_DIR:=$(shell pwd)/../Sources

# Define application source files
GENERIC_BINARIES_SOURCE_DIR:=$(SRC_DIR)

GENERIC_BINARIES_COMMON_SOURCE_FILES+=\
Video/pre_stage.c\
Video/post_stage.c\
Video/display_stage.c

GENERIC_INCLUDES+=					\
	$(SRC_DIR) \
	$(LIB_DIR) \
	$(SDK_PATH)/Soft/Common \
	$(SDK_PATH)/Soft/Lib

GENERIC_TARGET_BINARIES_PREFIX=

GENERIC_TARGET_BINARIES_DIR=$(ARDRONE_TARGET_DIR)

GENERIC_BINARIES_SOURCE_ENTRYPOINTS+=			\
   ardrone_testing_tool.c

GENERIC_INCLUDES:=$(addprefix -I,$(GENERIC_INCLUDES))

GENERIC_LIB_PATHS=-L$(GENERIC_TARGET_BINARIES_DIR)
GENERIC_LIBS=-lm -ldl -lpc_ardrone -lrt -lgtk-x11-2.0 -lgdk-x11-2.0 -lpangocairo-1.0 -latk-1.0 -lcairo -lgdk_pixbuf-2.0 -lgio-2.0 -lpangoft2-1.0 -lpango-1.0 -lfontconfig -lfreetype -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0

SDK_FLAGS+="USE_APP=yes"
SDK_FLAGS+="APP_ID=linux_video_demo"
GENERIC_CFLAGS+= -pthread -I/usr/include/gstreamer-1.0 -I/usr/include/glib-2.0 -I/usr/lib/aarch64-linux-gnu/glib-2.0/include

export GENERIC_CFLAGS
export GENERIC_LIBS
export GENERIC_LIB_PATHS
export GENERIC_INCLUDES
export GENERIC_BINARIES_SOURCE_DIR
export GENERIC_BINARIES_COMMON_SOURCE_FILES
export GENERIC_TARGET_BINARIES_PREFIX
export GENERIC_TARGET_BINARIES_DIR
export GENERIC_BINARIES_SOURCE_ENTRYPOINTS

# Bug fix ...
export GENERIC_LIBRARY_SOURCE_DIR=$(GENERIC_BINARIES_SOURCE_DIR)


.PHONY: $(TARGET) build_libs

all: build_libs $(TARGET)

$(TARGET):
	@$(MAKE) -C $(SDK_PATH)/VP_SDK/Build $(TMP_SDK_FLAGS) $(SDK_FLAGS) $(MAKECMDGOALS) USE_LINUX=yes
	mv $(ARDRONE_TARGET_DIR)/ardrone_testing_tool $(TARGET)
	mv $(TARGET) $(ARDRONE_TARGET_DIR)/

$(MAKECMDGOALS): build_libs
	@$(MAKE) -C $(SDK_PATH)/VP_SDK/Build $(TMP_SDK_FLAGS) $(SDK_FLAGS) $(MAKECMDGOALS) USE_LINUX=yes

build_libs:
	@$(MAKE) -C $(SDK_PATH)/Soft/Build $(TMP_SDK_FLAGS) $(SDK_FLAGS) $(MAKECMDGOALS) USE_LINUX=yes

