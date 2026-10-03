/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "ConsumerIrHal"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/times.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware/hardware.h>
#include <hardware/consumerir.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define IRTX_IOC_SET_CARRIER_FREQ _IOW('R', 0, unsigned int)

#define IRTX_IOC_SET_IRTX_LED_EN    _IOW('R', 10, unsigned int)


static const consumerir_freq_range_t consumerir_freqs[] = {
    {.min = 30000, .max = 30000},
    {.min = 33000, .max = 33000},
    {.min = 36000, .max = 36000},
    {.min = 38000, .max = 38000},
    {.min = 40000, .max = 40000},
    {.min = 56000, .max = 56000},
};

static int data_debug[] = {9000, 4500, 580, 564, 580, 564, 580, 1692, 580, 564, 
                    580, 564, 580, 564, 580, 564, 580, 564, 580, 1692, 
                    580, 1692, 580, 564, 580, 1692, 580, 1692, 580, 1692, 
                    580, 1692, 580, 1692, 580, 564, 580, 564, 580, 564, 
                    580, 1692, 580, 564, 580, 564, 580, 564, 580, 564, 
                    580, 1692, 580, 1692, 580, 1692, 580, 564, 580, 1692, 
                    580, 1692, 580, 1692, 580, 1692, 580, 39344, 9000, 2252, 
                    580, 96176, 9000, 2252, 580, 48088};

#define LG_TEST_FREQ    (38008)
#define PROP_LG_PW_TEST                             "irtx.hal.pw_test.enable"
#define PROP_LG_PW_TEST_VAL_EN              "1"  // enable

#define PROP_IRTX_LOG_ENABLE                     "irtx.hal.log.enable"
#define PROP_IRTX_LOG_ENABLE_VAL_EN      "1"  // enable

static int check_prop_match(const char *prop, const char *dest_value)
{
    char value[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = 0;

    ALOGD("%s prop:%s, dest_value:%s\n", __FUNCTION__, prop, dest_value);

    ret = property_get(prop, value, NULL);
    ALOGD("%s property_get %s, ret:%d\n", __FUNCTION__, prop, ret);
    if (ret > 0)
    {
        ret = strcmp(dest_value, PROP_LG_PW_TEST_VAL_EN);
        if (!ret) {
            ret = 1;
        } else {
            ALOGD("%s property_get %s, ret:%d, value:%s\n", __FUNCTION__, prop, ret, value);
        }
    }

    ALOGD("%s ret:%d\n", __FUNCTION__, ret);
    ret = ((ret == 1) ? ret : 0);
    return ret;
}

static int irtx_hal_power_test_enabled()
{
    return check_prop_match(PROP_LG_PW_TEST, PROP_LG_PW_TEST_VAL_EN);
}

static int irtx_hal_log_enabled()
{
    return check_prop_match(PROP_IRTX_LOG_ENABLE, PROP_IRTX_LOG_ENABLE_VAL_EN);
}

static int lg_tv_power_test()
{
    int total_time = 0; // micro-seconds
    long i;
    int fd, buffer_len;
    int ret = 0;
    unsigned int *wave_buffer = NULL;
    int carrier_freq = LG_TEST_FREQ;

    int p_len = sizeof(data_debug) / sizeof(data_debug[0]);

    carrier_freq = LG_TEST_FREQ;
    for (i = 0; i < p_len; i++)
        total_time += data_debug[i];

    /* simulate the time spent transmitting by sleeping */
    ALOGD("transmit for %d uS at %d Hz\n", total_time, carrier_freq);

    ALOGD("data_len:%d, data_debug[]:\n", p_len);

    // out put log for debug: start
    if (irtx_hal_log_enabled()) {
        for (i=0; i < (p_len >> 3); i++) {
            int ii = (i << 3);
            ALOGD("&data_debug[%d]:0x%p, %d, %d, %d, %d, %d, %d, %d, %d\n", ii, (data_debug + ii)
                , data_debug[ii + 0], data_debug[ii + 1], data_debug[ii + 2], data_debug[ii + 3]
                , data_debug[ii + 4], data_debug[ii + 5], data_debug[ii + 6], data_debug[ii + 7]);
        }
        i = (i << 3);
        for (; i < (p_len ); i++) {
            ALOGD("0x%x, ", data_debug[i]);
        }
        ALOGD("\n");
    }
    // out put log for debug: end

    fd = open("/dev/irtx", O_RDWR);
    if(fd < 0) {
        ALOGE("file open fail, errno=%d\n", errno);
        return -1;
    }

    ret = ioctl(fd, IRTX_IOC_SET_CARRIER_FREQ, &carrier_freq);
    if(ret) {
        ALOGE("file ioctl(0x%x) fail, errno=%d\n", IRTX_IOC_SET_CARRIER_FREQ, errno);
        goto exit;
    }
    
    buffer_len = p_len;   // one u32 per pattern element (duration in us)
    wave_buffer = malloc(buffer_len * 4);
    if (!wave_buffer) {
        ALOGE("malloc(%d) fail\n", buffer_len * 4);
        return -1;
    }
    ALOGD("U32 number=%d\n", buffer_len);

    memset(wave_buffer, 0, buffer_len * 4);\
    for (i = 0; i < p_len; i++) {
        /* Same duration-per-u32 encoding as consumerir_transmit - the driver
         * does not understand a 1-bit-per-microsecond bitmask. */
        wave_buffer[i] = (unsigned int)data_debug[i];
    }
    ret = write(fd, (char *)wave_buffer, buffer_len * 4);

    ALOGD("converted len:%d, data:\n", buffer_len);

    // out put log for debug: start
    if (irtx_hal_log_enabled()) {
        for (i=0; i < (buffer_len >> 3); i++) {
            int ii = (i << 3);
            ALOGD("&wave_buffer[%d]%p, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n", ii, &(wave_buffer[ii])
                , wave_buffer[ii + 0], wave_buffer[ii + 1], wave_buffer[ii + 2], wave_buffer[ii + 3]
                , wave_buffer[ii + 4], wave_buffer[ii + 5], wave_buffer[ii + 6], wave_buffer[ii + 7]);
        }
        i = (i << 3);
        for (; i < buffer_len; i++) {
            ALOGD("0x%x, ", wave_buffer[i]);
        }
        ALOGD("==\n");
    }
    // out put log for debug: end

    /* Require the full byte count; the driver reports a rejected buffer by
     * returning 0 rather than a negative errno. */
    if (ret != buffer_len * 4) {
        ALOGE("file write short: ret=%d expected=%d errno=%d\n",
              ret, buffer_len * 4, errno);
        ret = -1;
        goto exit;
    } else {
        ALOGD("data is delivered to kernel, sleep now\n");
        //usleep(total_time); // sleep in kernel
    }

exit:
    ALOGD("done, turn off IrTx\n");
    close(fd);
    free(wave_buffer);

    return ret;
}


static int consumerir_transmit(struct consumerir_device *dev __unused,
   int carrier_freq, const int pattern[], int pattern_len)
{
    int total_time = 0; // micro-seconds
    long i;
    int fd, buffer_len;
    int ret = 0;
    unsigned int *wave_buffer = NULL;

    if (!irtx_hal_power_test_enabled()) {
        for (i = 0; i < pattern_len; i++)
            total_time += pattern[i];

        /* simulate the time spent transmitting by sleeping */
        ALOGD("transmit for %d uS at %d Hz\n", total_time, carrier_freq);

        ALOGD("pattern_len:%d, pattern[]:\n", pattern_len);

        // out put log for debug: start
        if (irtx_hal_log_enabled()) {
            for (i=0; i < (pattern_len >> 3); i++) {
                int ii = (i << 3);
                ALOGD("&pattern[%d]:0x%p - %d, %d, %d, %d, %d, %d, %d, %d\n", ii, &(pattern[ii]), pattern[ii + 0], pattern[ii + 1]
                    , pattern[ii + 2], pattern[ii + 3], pattern[ii + 4], pattern[ii + 5], pattern[ii+ 6], pattern[ii + 7]);
            }
            i = (i << 3);
            for (; i < (pattern_len ); i++) {
                ALOGD("0x%x, ", pattern[i]);
            }
            ALOGD("\n");
        }
        // out put log for debug: end

        fd = open("/dev/irtx", O_RDWR);
        if(fd < 0) {
            ALOGE("file open fail, errno=%d\n", errno);
            return -1;
        }

        ret = ioctl(fd, IRTX_IOC_SET_CARRIER_FREQ, &carrier_freq);
        if(ret) {
            ALOGE("file ioctl(0x%x) fail, errno=%d\n", IRTX_IOC_SET_CARRIER_FREQ, errno);
            goto exit;
        }
        
        /* The mt_irtx driver in this kernel (3.18.22, kern_bd54) does NOT read a
         * 1-bit-per-microsecond bitmask. It reads one duration per u32 element,
         * alternating mark/space, and synthesises the carrier itself.
         *
         * Verified with a native prober against /dev/irtx on nikel:
         *   bitmask, 1 bit/us   -> write() returns 0   (driver rejects buffer)
         *   duration per u32    -> write() returns count (driver transmits,
         *                          and blocks for the waveform duration)
         *
         * The old bitmask path made the driver compute garbage
         * (zly4 total_time=-809311486) and log
         * (zly5_error rel_buffer_num>=num is error), so nothing was emitted.
         *
         * See BUGFIXES.md section 30 for the full investigation. */
        buffer_len = pattern_len;            // one u32 per pattern element
        wave_buffer = malloc(buffer_len * 4);
        if (!wave_buffer) {
            ALOGE("malloc(%d) fail\n", buffer_len * 4);
            goto exit;
        }
        ALOGD("U32 number=%d\n", buffer_len);

        for (i = 0; i < pattern_len; i++)
            wave_buffer[i] = (unsigned int)pattern[i];

        ret = write(fd, (char *)wave_buffer, buffer_len * 4);
        ALOGD("converted len:%d, data:\n", buffer_len);

        // out put log for debug: start
        if (irtx_hal_log_enabled()) {
            for (i=0; i < (buffer_len >> 3); i++) {
                int ii = (i << 3);
                ALOGD("&wave_buffer[%d]:%p - 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n", ii, &(wave_buffer[ii])
                    , wave_buffer[ii + 0], wave_buffer[ii + 1], wave_buffer[ii + 2], wave_buffer[ii + 3]
                    , wave_buffer[ii + 4], wave_buffer[ii+ 5], wave_buffer[ii + 6], wave_buffer[ii + 7]);
            }
            i = (i << 3);
            for (; i < buffer_len; i++) {
                ALOGD("0x%x, ", wave_buffer[i]);
            }
            ALOGD("==\n");
        }
        // out put log for debug: end

        /* A short write means the driver rejected the buffer. The driver
         * returns 0 (not a negative errno) when it cannot parse the
         * waveform, so testing only `ret < 0` reported success while
         * nothing was emitted. Require the full byte count instead. */
        if (ret != buffer_len * 4) {
            ALOGE("file write short: ret=%d expected=%d errno=%d\n",
                  ret, buffer_len * 4, errno);
            ret = -1;
            goto exit;
        } else {
            ALOGD("data is delivered to kernel, sleep now\n");
            //usleep(total_time); // sleep in kernel
        }

    exit:
        ALOGD("done, turn off IrTx\n");
        close(fd);
    } else {   // irtx_hal_power_test_enabled
        // only do LG TV power test
        lg_tv_power_test();
    }
    
    if (wave_buffer) {
        free(wave_buffer);
    }
    return ret;
}

static int consumerir_get_num_carrier_freqs(struct consumerir_device *dev __unused)
{
    return ARRAY_SIZE(consumerir_freqs);
}

static int consumerir_get_carrier_freqs(struct consumerir_device *dev __unused,
    size_t len, consumerir_freq_range_t *ranges)
{
    size_t to_copy = ARRAY_SIZE(consumerir_freqs);

    to_copy = len < to_copy ? len : to_copy;
    memcpy(ranges, consumerir_freqs, to_copy * sizeof(consumerir_freq_range_t));
    return to_copy;
}

static int consumerir_close(hw_device_t *dev)
{
    free(dev);
    return 0;
}

/*
 * Generic device handling
 */
static int consumerir_open(const hw_module_t* module, const char* name,
        hw_device_t** device)
{
    if (strcmp(name, CONSUMERIR_TRANSMITTER) != 0) {
        return -EINVAL;
    }
    if (device == NULL) {
        ALOGE("NULL device on open");
        return -EINVAL;
    }

    consumerir_device_t *dev = malloc(sizeof(consumerir_device_t));
    memset(dev, 0, sizeof(consumerir_device_t));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*) module;
    dev->common.close = consumerir_close;

    dev->transmit = consumerir_transmit;
    dev->get_num_carrier_freqs = consumerir_get_num_carrier_freqs;
    dev->get_carrier_freqs = consumerir_get_carrier_freqs;

    *device = (hw_device_t*) dev;
    return 0;
}

static struct hw_module_methods_t consumerir_module_methods = {
    .open = consumerir_open,
};

consumerir_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag                = HARDWARE_MODULE_TAG,
        .module_api_version = CONSUMERIR_MODULE_API_VERSION_1_0,
        .hal_api_version    = HARDWARE_HAL_API_VERSION,
        .id                 = CONSUMERIR_HARDWARE_MODULE_ID,
        .name               = "MTK IR HAL",
        .author             = "WCD/OSS3/SS5",
        .methods            = &consumerir_module_methods,
    },
};
