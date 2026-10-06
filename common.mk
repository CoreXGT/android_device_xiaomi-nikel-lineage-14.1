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
# and that value has to be set here so it is defined by the time Android.mk
# files are parsed. board.mk also sets it — board.mk IS read, via
# `include device/xiaomi/nikel/board.mk` in BoardConfig.mk (board-config phase).
# AndroidBoard.mk is the real dead end: build/target/board/Android.mk does
#   -include $(TARGET_DEVICE_DIR)/AndroidBoard.mk
# and cm-14.1 never defines TARGET_DEVICE_DIR, so that include silently expands
# to /AndroidBoard.mk and AndroidBoard.mk is never read at all.
# Getting this wrong is not a no-op: the feature file still installs while the
# HAL does not, and ConsumerIrService throws at construction on the mismatch,
# which crash-loops system_server.
MTK_IRTX_SUPPORT := yes

# The module is also tagged `optional`, so it is skipped unless named here.
# The name MUST be the literal "consumerir.mt6797": $(TARGET_BOARD_PLATFORM) is
# NOT available in product config (it is defined in BoardConfig.mk, which is
# parsed after product_config.mk — build/core/envsetup.mk:131 vs config.mk), so
# `consumerir.$(TARGET_BOARD_PLATFORM)` expanded to the nonexistent module
# "consumerir." — the kati stamp proves the lookups of
# ALL_MODULES.consumerir..INSTALLED / ._32.CLASS. The only check that would
# have warned about it runs for sdk builds only (main.mk, ifdef is_sdk_build),
# so full builds stayed silent. The module was still *defined* and
# `make consumerir.mt6797` still copied it into the staging tree, which is why
# boot-tested images only contained the HAL when that manual step had been run
# and no installclean had wiped the staging tree since. The 2026-10-05 OTA
# (8f92c9a068) hung at the boot animation exactly this way — BUGFIXES.md #35.
PRODUCT_PACKAGES += \
    consumerir.mt6797

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