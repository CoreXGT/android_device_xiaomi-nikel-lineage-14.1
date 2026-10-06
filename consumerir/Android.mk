# Copyright (C) 2013 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

$(warning MTK_IRTX_SUPPORT is $(MTK_IRTX_SUPPORT) and target is consumerir.$(TARGET_BOARD_PLATFORM))

ifeq ($(strip $(MTK_IRTX_SUPPORT)),yes)

$(warning consumerir.$(TARGET_BOARD_PLATFORM) is built)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := $(TOPDIR)/hardware/libhardware_legacy/include \
					$(TOPDIR)/hardware/libhardware/include

LOCAL_MODULE := consumerir.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := consumerir.c
LOCAL_SHARED_LIBRARIES := liblog libcutils
LOCAL_MODULE_TAGS := optional

# nikel is an arm64 target: system_server runs 64-bit and hw_get_module() in a
# 64-bit process only scans /system/lib64/hw. LOCAL_MULTILIB := both keeps the
# 32-bit variant built as well, so 32-bit clients can load the HAL too. (The
# 2026-10-05 boot-animation hang was NOT a multilib problem: the HAL was
# missing from the image entirely because PRODUCT_PACKAGES named the
# nonexistent module "consumerir." — see BUGFIXES.md #35.)
LOCAL_MULTILIB := both

include $(BUILD_SHARED_LIBRARY)

endif
