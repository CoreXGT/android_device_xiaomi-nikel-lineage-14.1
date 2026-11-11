#include <ui/GraphicBufferMapper.h>
#include <ui/PixelFormat.h>
#include <ui/Rect.h>
#include <log/log.h>
#include <dlfcn.h>
// std::string is used by the GraphicBuffer ctor shim below. cm-14.1 pulled
// <string> in transitively, cm-13.0 does not.
#include <string>

/*
#include <cutils/process_name.h>
#include <cutils/properties.h>
#include <utils/String8.h>
#include <utils/KeyedVector.h>
#include <utils/Singleton.h>
*/

#define LOG_TAG "DECKER_SHIM"

extern "C" {
    void _ZN7android19GraphicBufferMapper9lockYCbCrEPK13native_handlejRKNS_4RectEP13android_ycbcr(buffer_handle_t, uint32_t, const android::Rect&, android_ycbcr*);

    void _ZN7android19GraphicBufferMapper9lockYCbCrEPK13native_handleiRKNS_4RectEP13android_ycbcr(buffer_handle_t handle, int usage, const android::Rect& bounds, android_ycbcr *ycbcr) {
        _ZN7android19GraphicBufferMapper9lockYCbCrEPK13native_handlejRKNS_4RectEP13android_ycbcr(handle, static_cast<uint32_t>(usage), bounds, ycbcr);
    }

    void _ZN7android19GraphicBufferMapper4lockEPK13native_handlejRKNS_4RectEPPv(buffer_handle_t, uint32_t, const android::Rect&, void**);

    void _ZN7android5Fence4waitEi(int);

    void _ZN7android5Fence4waitEj(unsigned int timeout) {
        _ZN7android5Fence4waitEi(static_cast<int>(timeout));
    }
    /*
     * GraphicBuffer constructor shim.
     *
     * The MTK blobs use two different ABIs:
     *   - the MIUI 6.0 era blobs call the 4-argument constructor
     *     GraphicBuffer(w, h, format, usage)
     *   - the 7.1 era blobs (libMtkOmxVenc) call the one that also carries a
     *     requestorName
     *     GraphicBuffer(w, h, format, usage, const std::string&)
     *
     * cm-14.1's libgui exports the 4-argument one, cm-13.0's libui exports
     * the 4-argument one and NOT the requestorName variant.  So both
     * constructors are provided here and each one forwards to whichever
     * flavour the platform actually has:
     *
     *   requestorName variant  -> real requestorName variant (cm-14.1)
     *                           -> 4-argument one (cm-13.0)
     *   4-argument variant    -> real 4-argument one (cm-13.0, via RTLD_NEXT
     *                           so the preload copy does not call itself)
     *                           -> requestorName variant (cm-14.1)
     *
     * Without this the 4-argument constructor was shadowed by this library
     * and then called a symbol that does not exist on cm-13.0, so the
     * linker resolved it to NULL and surfaceflinger died with SIGSEGV
     * (pc = 0x0) while allocating its first buffer.
     */
    typedef void (*GraphicBufferCtor4)(void*, uint32_t, uint32_t, android::PixelFormat, uint32_t);
    typedef void (*GraphicBufferCtor5)(void*, uint32_t, uint32_t, android::PixelFormat, uint32_t,
                                        const std::string&);

    static GraphicBufferCtor4 real_ctor4() {
        static GraphicBufferCtor4 func = NULL;
        if (func == NULL) {
            func = (GraphicBufferCtor4)dlsym(RTLD_NEXT, "_ZN7android13GraphicBufferC1Ejjij");
        }
        return func;
    }

    static GraphicBufferCtor5 real_ctor5() {
        static GraphicBufferCtor5 func = NULL;
        if (func == NULL) {
            func = (GraphicBufferCtor5)dlsym(RTLD_NEXT,
                    "_ZN7android13GraphicBufferC1EjjijNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE");
        }
        return func;
    }

    void _ZN7android13GraphicBufferC1Ejjij(void *instance, uint32_t inWidth, uint32_t inHeight,
            android::PixelFormat inFormat, uint32_t inUsage) {
        GraphicBufferCtor4 ctor4 = real_ctor4();
        if (ctor4 != NULL) {
            ctor4(instance, inWidth, inHeight, inFormat, inUsage);
            return;
        }
        GraphicBufferCtor5 ctor5 = real_ctor5();
        if (ctor5 != NULL) {
            ctor5(instance, inWidth, inHeight, inFormat, inUsage, std::string("<Unknown>"));
            return;
        }
        ALOGE("nikel: no GraphicBuffer constructor found in the platform libs");
    }

    void _ZN7android13GraphicBufferC1EjjijNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
            void *instance, uint32_t inWidth, uint32_t inHeight, android::PixelFormat inFormat,
            uint32_t inUsage, const std::string& inRequestorName) {
        GraphicBufferCtor5 ctor5 = real_ctor5();
        if (ctor5 != NULL) {
            ctor5(instance, inWidth, inHeight, inFormat, inUsage, inRequestorName);
            return;
        }
        GraphicBufferCtor4 ctor4 = real_ctor4();
        if (ctor4 != NULL) {
            ctor4(instance, inWidth, inHeight, inFormat, inUsage);
            return;
        }
        ALOGE("nikel: no GraphicBuffer constructor found in the platform libs");
    }

}