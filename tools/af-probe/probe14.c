/* probe14: which i2c address does each supported VCM driver use?
 *
 * The kernel contains two AF lens drivers: DW9714AF and DW9718AF.
 * nikel's DW9714AF VCM lives at 7-bit 0x72, but every write the driver made
 * during MOVETO went to 0x0e and NAKed.
 *
 * Hypothesis: 0x0e belongs to the OTHER lens driver's entry, and SETDRVNAME
 * selects which one is used. If SETDRVNAME("DW9718AF") is accepted and then
 * MOVETO uses a different address, the mechanism is identified and the fix
 * becomes "make the DW9714AF entry use 0x72".
 *
 * Run under ftrace on i2c bus 2 and compare the addresses.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define CMD_SETDRVNAME 0x4020410au
#define CMD_MOVETO     0x40044101u

int main(int argc, char **argv) {
    const char *name = argc > 1 ? argv[1] : "DW9714AF";
    const char *dev = argc > 2 ? argv[2] : "/dev/MAINAF";
    int fd = open(dev, O_RDWR);
    printf("open = %d %s\n", fd, fd < 0 ? strerror(errno) : "");
    fflush(stdout);
    if (fd < 0) return 1;

    unsigned char nb[20];
    memset(nb, 0, sizeof nb);
    strncpy((char *)nb, name, sizeof nb - 1);
    int i;
    for (i = 0; i < 3; i++) {
        errno = 0;
        int r = ioctl(fd, CMD_SETDRVNAME, nb);
        printf("SETDRVNAME(\"%s\") #%d ret=%d %s\n", name, i, r,
               r < 0 ? strerror(errno) : "OK");
        fflush(stdout);
    }
    if (i == 3) {
        errno = 0;
        int r = ioctl(fd, CMD_MOVETO, 512u);
        printf("MOVETO(512) ret=%d %s\n", r, r < 0 ? strerror(errno) : "OK");
    }
    sleep(1);
    close(fd);
    return 0;
}