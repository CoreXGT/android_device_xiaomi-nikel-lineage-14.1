/* probe12: map the mcuAF driver surface.
 *
 * The one thing that decides whether the fix is kernel or HAL:
 *   does this kernel's mcuAF driver implement SETPARA at all?
 *
 * "setMCUParam(cmdId, param)" -> ioctl(SETPARA 0x40084105, &pair{could be cmdId,param}).
 * The HAL calls setMCUParam(1, debug.af_ois.disable) at AF-manager start and we
 * know that exact call returns EPERM. If EVERY cmdId returns EPERM, the driver has
 * no SETPARA case in its switch at all -> it is a partial/stub driver, and the
 * VCM is likely never taken out of standby, which would explain:
 *     MOVETO returns success, curpos never changes, the lens never physically moves.
 *
 * If only cmdId 1 is EPERM while neighbours succeed, then the case exists and we
 * have found the one specific missing parameter -> a much narrower bug.
 *
 * Also dumps the full GET struct and probes SETINFPOS/SETMACROPOS so the field
 * layout can be identified (which word is curpos, which is MAX).
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define CMD_SETPARA     0x40084105u
#define CMD_SETINFPOS   0x40044102u
#define CMD_SETMACROPOS 0x40044103u
#define CMD_MOVETO      0x40044101u
#define CMD_GET         0x80104100u
#define CMD_SETDRVNAME  0x4020410au

static int fd;

static void dump(const char *tag) {
    unsigned int b[16];
    memset(b, 0, sizeof b);
    errno = 0;
    int r = ioctl(fd, CMD_GET, b);
    printf("  GET[%-12s] ret=%-3d", tag, r);
    if (r < 0) { printf(" errno=%d(%s)\n", errno, strerror(errno)); return; }
    int i;
    printf(" ");
    for (i = 0; i < 16; i++) printf("%s%u", i ? " " : "", b[i]);
    printf("\n");
    fflush(stdout);
}

/* setMCUParam passes the cmd id and the param; the kernel copy is 2 x unsigned int.
 * try every plausible packing so we do not mistake a bad argument for a missing case */
static int setpara(unsigned a, unsigned b) {
    unsigned pair[2] = { a, b };
    unsigned one = a;
    errno = 0;
    int r = ioctl(fd, CMD_SETPARA, pair);
    if (r < 0) {
        errno = 0;
        int r2 = ioctl(fd, CMD_SETPARA, &one);
        printf("    SETPARA(cmd=%-2u val=%-6u) pair_ret=%-3d (%s)  scalar_ret=%d (%s)\n",
               a, b, r, strerror(errno), r2, r2 < 0 ? strerror(errno) : "OK");
    } else {
        printf("    SETPARA(cmd=%-2u val=%-6u) pair_ret=%-3d  OK\n", a, b, r);
    }
    fflush(stdout);
    return r;
}

int main(int argc, char **argv) {
    const char *dev = argc > 1 ? argv[1] : "/dev/MAINAF";
    fd = open(dev, O_RDWR);
    printf("open = %d %s\n", fd, fd < 0 ? strerror(errno) : "");
    fflush(stdout);
    if (fd < 0) return 1;

    unsigned char name[20];
    memset(name, 0, sizeof name);
    strcpy((char *)name, "DW9714AF");
    int i;
    printf("\n-- bind (HAL does SETDRVNAME x3) --\n");
    for (i = 0; i < 3; i++)
        printf("  SETDRVNAME #%d ret=%d\n", i, ioctl(fd, CMD_SETDRVNAME, name));

    printf("\n-- full GET struct --\n");
    dump("after-bind");

    printf("\n-- SETPARA sweep: is the case implemented at all? --\n");
    for (i = 0; i < 8; i++) setpara(i, 0);

    printf("\n-- SETINFPOS / SETMACROPOS sweep (which fields do they touch?) --\n");
    for (i = 0; i <= 1023; i += 1023) {
        errno = 0;
        int r = ioctl(fd, CMD_SETINFPOS, (unsigned)i);
        printf("  SETINFPOS(%u) ret=%d %s\n", i, r, r < 0 ? strerror(errno) : "");
        dump("after-inf");
        r = ioctl(fd, CMD_SETMACROPOS, (unsigned)i);
        printf("  SETMACROPOS(%u) ret=%d %s\n", i, r, r < 0 ? strerror(errno) : "");
        dump("after-macro");
    }

    printf("\n-- MOVETO sweep (does curpos ever change?) --\n");
    const int t[5] = { 0, 256, 512, 768, 1023 };
    for (i = 0; i < 5; i++) {
        errno = 0;
        int r = ioctl(fd, CMD_MOVETO, (unsigned)t[i]);
        printf("  MOVETO(%-4d) ret=%-3d %s\n", t[i], r, r < 0 ? strerror(errno) : "OK");
        fflush(stdout);
        usleep(400000);
        char tag[32];
        snprintf(tag, sizeof tag, "move-%d", t[i]);
        dump(tag);
    }

    close(fd);
    return 0;
}