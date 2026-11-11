LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    mtk_xlog.cpp \
    mtk_gui.cpp \
    mtk_ui.cpp \
    mtk_audio.cpp \
    mtk_omx.cpp

# icu55.c provides the ICU-55 flavoured names (ucnv_open_55 and friends) for
# MTK blobs that were built against ICU 55.
#
# It is only needed where the platform ICU is NEWER than 55 (cm-14.1 ships
# ICU 58).  cm-13.0 already ships ICU 55.1, which exports ucnv_open_55
# itself — and building the shim there is actively harmful: the shim is in
# the LD_PRELOAD library, so it shadows the platform's own ucnv_open_55, and
# its call to the unsuffixed ucnv_open resolves back to the shadowed symbol.
# The result is an infinite recursion: the zygote spins forever in
# "Preloading classes..." and the system never reaches the launcher.
ifeq ($(PLATFORM_VERSION),6.0.1)
    # cm-13.0: the platform ICU already provides the ICU-55 symbols.
else
    LOCAL_SRC_FILES += icu55.c
endif

# only for 64bit libraries
LOCAL_SRC_FILES_64 := mtk_parcel.cpp

LOCAL_SHARED_LIBRARIES := libbinder liblog libgui libui libicuuc
# libicui18n is deliberately NOT linked here: it is an 11 MB C++ library that
# every preloaded process would have to map, and the ICU shim below only
# needs the "uc" (common) API.  On cm-13.0 the platform's libicui18n also
# needs the platform's own libicuuc (it references the icu_55 C++ vtables),
# which is why the vendor's libicuuc must not overwrite the platform one —
# see patches/cm13/0008-vendor-icu-use-platform-icu.patch.
# Pulls in the 32-bit build of libmtk_libc_n (the LIBC_N symbol shim).
LOCAL_SHARED_LIBRARIES += libmtk_libc_n
LOCAL_MODULE := libmtk_symbols
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

# LIBC_N symbol shim.
#
# Some MTK vendor blobs (the OMX/video codec stack, librrc, libcurl-ss,
# libicuuc) were built against an Android 7.1 bionic and reference a few
# symbols with the version tag "LIBC_N" (e.g. __aeabi_memcpy@LIBC_N).
# The cm-13.0 platform libc only has "LIBC" and "LIBC_PRIVATE", so the
# linker cannot resolve them and every process that preloads
# libmtk_symbols.so dies with exit status 1 -> the device reboots into
# recovery.  This library re-exports those symbols under the LIBC_N
# version name.  See libc_n_shim.c.
include $(CLEAR_VARS)

LOCAL_SRC_FILES := libc_n_shim.c
# The 32-bit variant is pulled in as a dependency of libmtk_symbols (see
# above): the MTK blobs that need the LIBC_N symbols are 32-bit.
LOCAL_MODULE := libmtk_libc_n
LOCAL_MODULE_TAGS := optional
LOCAL_SHARED_LIBRARIES := libdl
LOCAL_LDFLAGS += -Wl,--version-script=$(LOCAL_PATH)/libc_n.version
LOCAL_LDFLAGS += -Wl,--no-undefined

include $(BUILD_SHARED_LIBRARY)
