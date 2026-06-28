/* test_open.c - Test DRM device open and version ioctl */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

int main() {
    setbuf(stdout, NULL);
    printf("Opening /dev/dri/card1 (RKNPU)...\n");

    int fd = open("/dev/dri/card1", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    printf("Opened successfully, fd=%d\n", fd);

    /* DRM_IOCTL_VERSION structure */
    struct {
        int version_major;
        int version_minor;
        int version_patchlevel;
        size_t name_len;
        char *name;
        size_t date_len;
        char *date;
        size_t desc_len;
        char *desc;
    } version = {0};

    char name[64] = {0}, date[64] = {0}, desc[256] = {0};
    version.name_len = sizeof(name);
    version.name = name;
    version.date_len = sizeof(date);
    version.date = date;
    version.desc_len = sizeof(desc);
    version.desc = desc;

    printf("Calling DRM_IOCTL_VERSION...\n");
    if (ioctl(fd, 0xc0406400, &version) < 0) {  /* DRM_IOCTL_VERSION */
        perror("ioctl VERSION");
    } else {
        printf("Driver: %s v%d.%d.%d\n", name, version.version_major,
               version.version_minor, version.version_patchlevel);
    }

    close(fd);
    printf("Done.\n");
    return 0;
}
