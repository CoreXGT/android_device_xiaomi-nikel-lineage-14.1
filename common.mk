# Common overlays
DEVICE_PACKAGE_OVERLAYS += device/xiaomi/nikel/overlay

# Display
PRODUCT_PACKAGES += \
    libion

# GPS
PRODUCT_COPY_FILES += \
    device/xiaomi/nikel/configs/etc/agps_profiles_conf2.xml:system/etc/agps_profiles_conf2.xml

# Gello
# PRODUCT_PACKAGES += \
#     Gello

# Snap
PRODUCT_PACKAGES += \
    Snap

# FMRadio
PRODUCT_PACKAGES += \
    libfmjni \
    FMRadio

# Consumerir (IR remote)
# The module in device/xiaomi/nikel/consumerir gates its whole definition on
#   ifeq ($(strip $(MTK_IRTX_SUPPORT)),yes)
# and that value has to be set here, because this is the only file involved the
# build demonstrably reads. Both other candidates are dead ends in cm-14.1:
#   board.mk         never included — build/core/config.mk never references it
#   AndroidBoard.mk  never included either — build/target/board/Android.mk does
#                    `-include $(TARGET_DEVICE_DIR)/AndroidBoard.mk`, but
#                    TARGET_DEVICE_DIR is an Android 10+ variable; in cm-14.1 the
#                    leading `-` makes that empty include vanish silently
# Getting this wrong is not a no-op: the feature file still installs while the
# HAL does not, and ConsumerIrService throws at construction on the mismatch,
# which crash-loops system_server.
MTK_IRTX_SUPPORT := yes

# The module is also tagged `optional`, so it is skipped unless named here.
# Without it /system/lib/hw/consumerir.*.so is absent, hw_get_module() fails and
# ConsumerIrManager.hasIrEmitter() returns false, which hides the IR UI in apps.
PRODUCT_PACKAGES += \
    consumerir.$(TARGET_BOARD_PLATFORM)

# Filesystem management tools
PRODUCT_PACKAGES += \
    e2fsck \
    fsck.f2fs \
    mkfs.f2fs \
    make_ext4fs

# exFAT
PRODUCT_PACKAGES += \
    mount.exfat \
    fsck.exfat \
    mkfs.exfat

# NTFS
PRODUCT_PACKAGES += \
    fsck.ntfs \
    mkfs.ntfs \
    mount.ntfs

# USB
PRODUCT_PACKAGES += \
    com.android.future.usb.accessory

# WallpaperPicker
PRODUCT_PACKAGES += \
    WallpaperPicker

# Sensor Calibration
PRODUCT_PACKAGES += \
    libem_sensor_jni

# Date
PRODUCT_BUILD_PROP_OVERRIDES += BUILD_UTC_DATE=0

# include other configs
include device/xiaomi/nikel/permissions.mk
include device/xiaomi/nikel/media.mk
include device/xiaomi/nikel/wifi.mk
include device/xiaomi/nikel/telephony.mk