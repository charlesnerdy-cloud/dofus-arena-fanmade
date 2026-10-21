LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := main
LOCAL_SRC_FILES := game.c
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../../SDL/include \
    $(LOCAL_PATH)/../../SDL_image/include \
    $(LOCAL_PATH)/../../SDL_ttf/include \
    $(LOCAL_PATH)/../../SDL_mixer/include
LOCAL_SHARED_LIBRARIES := SDL2 SDL2_image SDL2_ttf SDL2_mixer
LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid -lm
LOCAL_CFLAGS := -O2 -std=c99

include $(BUILD_SHARED_LIBRARY)
