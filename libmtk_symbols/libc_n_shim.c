/*
 * LIBC_N symbol shim for nikel.
 *
 * Several MTK blobs in vendor/xiaomi/nikel (the OMX/video codec stack,
 * librrc, libcurl-ss and libicuuc) were built against an Android 7.1
 * bionic and therefore reference a handful of symbols with the version
 * tag "LIBC_N" (for example __aeabi_memcpy@LIBC_N).
 *
 * The cm-13.0 platform libc only knows the versions "LIBC" and
 * "LIBC_PRIVATE", so the dynamic linker cannot resolve those references:
 *
 *     CANNOT LINK EXECUTABLE: cannot locate symbol "__aeabi_memcpy"
 *     referenced by "/system/lib/libicuuc.so"
 *
 * Because init.mt6797.rc exports LD_PRELOAD="libmtk_symbols.so", that
 * failure hits *every* process init starts (they all die with exit
 * status 1), init then tears the services down and reboots into
 * recovery after lmkd, a critical service, has died four times.
 *
 * This library re-exports the missing symbols under the LIBC_N version
 * name (see libc_n.version) so the linker can satisfy them.  The
 * implementations forward to the real libc entry points, so the
 * semantics are identical to bionic's own __aeabi_* helpers:
 *
 *     __aeabi_memcpy4/8   copy exactly n bytes (the EABI caller guarantees
 *                         that n is already a multiple of 4/8)
 *     __aeabi_memmove4    move exactly n bytes
 *     __aeabi_memset4/8   set exactly n bytes
 *     __aeabi_memclr4/8   clear exactly n bytes
 *
 * Only the vendor blobs ask for the LIBC_N flavour; the platform's own
 * libraries keep using the @LIBC_PRIVATE definitions from libc.so.
 *
 * NOTE: the libc prototypes are declared by hand on purpose.  bionic's
 * <string.h>/<stdio.h>/<unistd.h> define __fwrite_chk/__write_chk and
 * friends themselves when the fortify wrappers are enabled, and a second
 * definition would not link.
 */

#include <stddef.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Plain libc entry points (no fortify wrappers in this translation unit). */
extern void* memcpy(void* dst, const void* src, size_t n);
extern void* memmove(void* dst, const void* src, size_t n);
extern void* memset(void* s, int c, size_t n);
extern size_t fread(void* ptr, size_t size, size_t nmemb, void* stream);
extern size_t fwrite(const void* ptr, size_t size, size_t nmemb, void* stream);
extern void abort(void);

/* write(2) is called through the raw syscall: bionic's <unistd.h> redirects
 * write() to __write_chk() when the fortify wrappers are on, and that would
 * recurse into the very function implemented below. */
static void nikon_fatal(const char* msg) {
    syscall(__NR_write, 2, msg, sizeof(msg) - 1);
    abort();
}

/* ---------------------------------------------------------------- */
/* ARM EABI memory helpers                                          */
/* ---------------------------------------------------------------- */

void __aeabi_memcpy(void* dst, const void* src, size_t n) {
    memcpy(dst, src, n);
}

void __aeabi_memcpy4(void* dst, const void* src, size_t n) {
    memcpy(dst, src, n);
}

void __aeabi_memcpy8(void* dst, const void* src, size_t n) {
    memcpy(dst, src, n);
}

void __aeabi_memmove(void* dst, const void* src, size_t n) {
    memmove(dst, src, n);
}

void __aeabi_memmove4(void* dst, const void* src, size_t n) {
    memmove(dst, src, n);
}

void __aeabi_memset(void* s, int c, size_t n) {
    memset(s, c, n);
}

void __aeabi_memset4(void* s, int c, size_t n) {
    memset(s, c, n);
}

void __aeabi_memset8(void* s, int c, size_t n) {
    memset(s, c, n);
}

void __aeabi_memclr(void* s, size_t n) {
    memset(s, 0, n);
}

void __aeabi_memclr4(void* s, size_t n) {
    memset(s, 0, n);
}

void __aeabi_memclr8(void* s, size_t n) {
    memset(s, 0, n);
}

/* ---------------------------------------------------------------- */
/* FORTIFY helpers the vendor blobs were built with               */
/* ---------------------------------------------------------------- */

size_t __fread_chk(void* ptr, size_t size, size_t nmemb, void* stream, size_t objsize) {
    if (size != 0 && nmemb > objsize / size) {
        nikon_fatal("*** buffer overflow detected ***\n");
    }
    return fread(ptr, size, nmemb, stream);
}

size_t __fwrite_chk(const void* ptr, size_t size, size_t nmemb, void* stream) {
    return fwrite(ptr, size, nmemb, stream);
}

long __write_chk(int fd, const void* buf, size_t nbytes, size_t buflen) {
    if (nbytes > buflen) {
        nikon_fatal("*** buffer overflow detected ***\n");
    }
    return (long)syscall(__NR_write, fd, buf, nbytes);
}

/* ---------------------------------------------------------------- */
/* Unwinder helper                                                  */
/* ---------------------------------------------------------------- */

/*
 * bionic exports __gnu_Unwind_Find_exidx as @LIBC_PRIVATE, so it cannot
 * be referenced from another shared library.  dlsym() ignores the
 * version tag, so the real implementation can still be reached that
 * way.  The return type is an enum (int) in the unwinder API.
 */
int __gnu_Unwind_Find_exidx(void* pc, void* exception) {
    static int (*real_func)(void*, void*) = NULL;
    if (real_func == NULL) {
        real_func = (int (*)(void*, void*))dlsym(RTLD_NEXT, "__gnu_Unwind_Find_exidx");
        if (real_func == NULL) {
            nikon_fatal("nikel: __gnu_Unwind_Find_exidx not found in libc\n");
        }
    }
    return real_func(pc, exception);
}

/* ---------------------------------------------------------------- */
/* __pthread_gettid                                                  */
/* ---------------------------------------------------------------- */

/*
 * Two of the MTK blobs (libvcodecdrv.so, libMtkOmxVdecEx.so) were built
 * against the Android 5/6 era bionic and ask for __pthread_gettid with the
 * LIBC_PRIVATE version.  cm-13.0's libc exports the function as a *public*
 * @LIBC symbol, so the versioned lookup fails and the library cannot be
 * loaded at all:
 *
 *   E/libEGL: load_driver(/system/lib/egl/libGLES_mali.so): dlopen failed:
 *       cannot locate symbol "__pthread_gettid" referenced by
 *       "/system/lib/libvcodecdrv.so"
 *   F/libEGL: couldn't find an OpenGL ES implementation
 *   F/libc: Fatal signal 6 (SIGABRT)
 *
 * libGLES_mali.so pulls in libvcodecdrv.so, so without this the device has
 * no OpenGL ES at all and the UI cannot render.
 */
int __pthread_gettid(void) {
    return (int)syscall(__NR_gettid);
}

/*
 * The LIBC_PRIVATE version is attached by the version script
 * (libc_n.version), not by a .symver directive.
 */
