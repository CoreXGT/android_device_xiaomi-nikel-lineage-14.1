LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

ALL_PREBUILT += $(INSTALLED_KERNEL_TARGET)

# Consumerir (IR remote) — DEAD FILE, never read by the build.
# build/target/board/Android.mk does
#   -include $(TARGET_DEVICE_DIR)/AndroidBoard.mk
# but cm-14.1 never defines TARGET_DEVICE_DIR, so the include silently expands
# to /AndroidBoard.mk and nothing in this file takes effect. The module gate
# (MTK_IRTX_SUPPORT) is set in board.mk (included via BoardConfig.mk) and in
# common.mk, and the PRODUCT_PACKAGES name must be the literal
# "consumerir.mt6797" — see BUGFIXES.md #35.
MTK_IRTX_SUPPORT := yes

# include the non-open-source counterpart to this file
