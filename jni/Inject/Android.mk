LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := inject 
LOCAL_SRC_FILES := inject.cpp

#shellcode.s

LOCAL_LDLIBS += -L$(SYSROOT)/usr/lib -llog

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
					$(LOCAL_PATH)/include \
					$(LOCAL_PATH)/include/Utils \

#LOCAL_FORCE_STATIC_EXECUTABLE := true

include $(BUILD_EXECUTABLE)