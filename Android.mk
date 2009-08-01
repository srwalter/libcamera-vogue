LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libcamera

LOCAL_SRC_FILES += \
    VogueCamera.cpp \
    CameraHardwareStub.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libui

include $(BUILD_SHARED_LIBRARY)
