# mt6797 platform boardconfig
LOCAL_PATH := device/xiaomi/nikel

include device/xiaomi/nikel/board.mk

# Platform
ARCH_ARM_HAVE_TLS_REGISTER := true
TARGET_BOARD_PLATFORM := mt6797
TARGET_NO_BOOTLOADER := true
TARGET_NO_FACTORYIMAGE := true

# CPU
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_ABI2 :=
TARGET_CPU_VARIANT := cortex-a53
TARGET_CPU_SMP := true

TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv7-a-neon
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := cortex-a53
TARGET_BOARD_SUFFIX := _64
TARGET_USES_64_BIT_BINDER := true

TARGET_CPU_CORTEX_A53 := true

TARGET_BOOTLOADER_BOARD_NAME := mt6797

# Ashmem
DISABLE_ASHMEM_TRACKING := true
BOARD_NO_SECURE_DISCARD := true

# Kernel
TARGET_USES_64_BIT_BINDER := true
TARGET_IS_64_BIT := true
BOARD_KERNEL_CMDLINE := bootopt=64S3,32N2,64N2 androidboot.selinux=permissive
BOARD_KERNEL_BASE := 0x40078000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_MKBOOTIMG_ARGS := \
	--base 0x40078000 \
	--pagesize 2048 \
	--kernel_offset 0x00008000 \
	--ramdisk_offset 0x04f88000 \
	--second_offset 0x00000000 \
	--tags_offset 0x03f88000 \
	--board nikel

# Hack for building without kernel sources
# Guarded on $(OUT): OUT is only exported by lunch (build/envsetup.sh
# setpaths), but breakfast/brunch run three config-only make passes
# (check_product x2 + dump-var cache) BEFORE lunching. Unguarded, $(OUT) was
# empty there and every `breakfast nikel` printed three
#   mkdir: cannot create directory '/obj': Permission denied
# lines (real builds always have OUT set, so the dir is still created then).
ifneq (,$(strip $(OUT)))
ifeq ($(TARGET_DEVICE),nikel)
$(shell mkdir -p $(OUT)/obj/KERNEL_OBJ/usr)
endif
endif

# Partitions
BOARD_SYSTEMIMAGE_PARTITION_SIZE:=3221225472
BOARD_CACHEIMAGE_PARTITION_SIZE:=452984832
BOARD_USERDATAIMAGE_PARTITION_SIZE:=3221225472
BOARD_RECOVERYIMAGE_PARTITION_SIZE:=16777216
BOARD_BOOTIMAGE_PARTITION_SIZE:=16777216
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_FLASH_BLOCK_SIZE := 131072

BOARD_HAS_LARGE_FILESYSTEM := true
TARGET_USERIMAGES_USE_EXT4 := true
BOARD_SUPPRESS_SECURE_ERASE := true

# Display
TARGET_SCREEN_HEIGHT := 1920
TARGET_SCREEN_WIDTH := 1080

# Camera
TARGET_HAS_LEGACY_CAMERA_HAL1 := true

# Bluetooth
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := $(LOCAL_PATH)/bluetooth

# LightHAL
TARGET_PROVIDES_LIBLIGHT := true

# Recovery
BOARD_HAS_NO_SELECT_BUTTON := true
TARGET_RECOVERY_FSTAB := $(LOCAL_PATH)/rootdir/recovery.fstab
# TWRP-specific
ifeq ($(RECOVERY_VARIANT), twrp)
DEVICE_RESOLUTION := 1080x1920
DEVICE_SCREEN_WIDTH := 1080
DEVICE_SCREEN_HEIGHT := 1920
RECOVERY_SDCARD_ON_DATA := true
TW_INTERNAL_STORAGE_PATH := "/data/media"
TW_INTERNAL_STORAGE_MOUNT_POINT := "data"
TW_EXTERNAL_STORAGE_PATH := "/external_sd"
TW_EXTERNAL_STORAGE_MOUNT_POINT := "external_sd"
TW_DEFAULT_EXTERNAL_STORAGE := true
endif

TARGET_SYSTEM_PROP := $(LOCAL_PATH)/system.prop

# Seccomp filter
BOARD_SECCOMP_POLICY := $(LOCAL_PATH)/seccomp

# MTK RIL class (MediaTekRIL + MtkEccList).
#
# The subclass is written against the cm-14.1 telephony API. cm-13.0's RIL
# base class differs in three places:
#   * processUnsolicited()/processSolicited() take no slot argument there
#   * setupDataCall() takes radioTechnology/profile/authType as Strings
# so a separate copy of the two files lives in ril-cm13/ for that platform.
# (PLATFORM_VERSION comes from build/core/version_defaults.mk, which is
# included before the device makefiles.)
ifeq ($(PLATFORM_VERSION),6.0.1)
BOARD_RIL_CLASS := ../../../device/xiaomi/nikel/ril-cm13
else
BOARD_RIL_CLASS := ../../../device/xiaomi/nikel/ril
endif

