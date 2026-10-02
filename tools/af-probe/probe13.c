/* probe13: clean MOVETO sweep with long holds, for human observation.
 *
 * Ground truth still missing: probe10/11/12 could not tell whether MOVETO moves
 * the VCM, because /dev/MAINAF never reports a position (GET's curpos word stays
 * 0 no matter what). So the one observer that can settle it is the user watching
 * the lens barrel from the BACK of the phone.
 *
 * Deliberately does NOT touch SETINFPOS / SETMACROPOS: probe12 showed those two
 * define the accepted MOVETO range (setting both to 1023 collapsed it to
 * [1023,1023] and made every other target fail with EINVAL). Here we leave the
 * factory range alone so 0..1023 are all valid.
 *
 * Order: bind exactly like the HAL, then three long alternating holds so any
 * real motor motion is unmistakable.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define CMD_SETDRVNAME 0x4020410au
#define CMD_MOVETO     0x40044101u
#define CMD_GET        0x80104100u

static int fd;

static void show(const char *tag) {
    unsigned int b[16];
    memset(b, 0, sizeof b);
    ioctl(fd, CMD_GET, b);
    printf("        %-10s curpos=%-6u macro=%-6u inf=%-6u\n", tag, b[0], b[1], b[2]);
    fflush(stdout);
}

static void move(int pos) {
    printf("\n>>>>>>>>>>  MOVETO(%d)  <<<<<<<<<<  watch the lens on the BACK of the phone\n", pos);
    fflush(stdout);
    errno = 0;
    int r = ioctl(fd, CMD_MOVETO, (unsigned)pos);
    printf("        ret=%d errno=%d (%s)\n", r, errno, r < 0 ? strerror(errno) : "OK");
    fflush(stdout);
    sleep(3);
    show("after");
}

int main(int argc, char **argv) {
    const char *dev = argc > 1 ? argv[1] : "/dev/MAINAF";
    fd = open(dev, O_RDWR);
    printf("open = %d %s\n", fd, fd < 0 ? strerror(errno) : "");
    fflush(stdout);
    if (fd < 0) { printf("camera must be CLOSED\n"); return 1; }

    unsigned char name[20];
    memset(name, 0, sizeof name);
    strcpy((char *)name, "DW9714AF");
    int i;
    for (i = 0; i < 3; i++)
        printf("SETDRVNAME #%d ret=%d\n", i, ioctl(fd, CMD_SETDRVNAME, name));
    show("bound");

    move(0);       /* one end  */
    move(1023);    /* far end  */
    move(0);       /* back     */

    printf("\nTell me: did the lens barrel shift / was there a whir or click?\n");
    close(fd);
    return 0;
}