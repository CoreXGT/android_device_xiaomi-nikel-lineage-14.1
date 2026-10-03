LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

ALL_PREBUILT += $(INSTALLED_KERNEL_TARGET)

# Consumerir (IR remote) — must be "yes", NOT "true".
# device/xiaomi/nikel/consumerir/Android.mk gates the whole module definition on
#   ifeq ($(strip $(MTK_IRTX_SUPPORT)),yes)
# and that file is read by kati, so this variable has to be set here. It was
# previously set in board.mk, which this build system never includes (cm-14.1
# reads AndroidBoard.mk), so the module was silently never defined while
# android.hardware.consumerir was still installed — and ConsumerIrService throws
# at construction when the feature and halOpen() disagree, which keeps
# system_server in a crash loop.
MTK_IRTX_SUPPORT := yes

# include the non-open-source counterpart to this file
