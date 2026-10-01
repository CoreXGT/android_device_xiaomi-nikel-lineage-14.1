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
# The module in device/xiaomi/nikel/consumerir is tagged `optional`, so it is
# skipped unless it is named here. Without it /system/lib/hw/consumerir.*.so is
# absent, hw_get_module(CONSUMERIR_HARDWARE_MODULE_ID) fails, and
# ConsumerIrManager.hasIrEmitter() returns false.
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