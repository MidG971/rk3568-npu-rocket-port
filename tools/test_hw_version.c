/* test_hw_version.c - Test RKNPU_GET_HW_VERSION ioctl */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <string.h>

/* From drm.h */
#define DRM_COMMAND_BASE 0x40
#define DRM_IOWR(nr, type) _IOWR('d', nr, type)

/* From rknpu_ioctl.h */
#define RKNPU_GET_HW_VERSION 0
#define RKNPU_ACTION 0x00

struct rknpu_action {
    uint32_t flags;
    uint32_t value;
};

#define DRM_IOCTL_RKNPU_ACTION DRM_IOWR(DRM_COMMAND_BASE + RKNPU_ACTION, struct rknpu_action)

int main() {
    setbuf(stdout, NULL);

    printf("Opening /dev/dri/card1...\n");
    int fd = open("/dev/dri/card1", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    printf("Opened fd=%d\n", fd);

    struct rknpu_action action;
    memset(&action, 0, sizeof(action));
    action.flags = RKNPU_GET_HW_VERSION;

    printf("Calling RKNPU_GET_HW_VERSION ioctl...\n");
    int ret = ioctl(fd, DRM_IOCTL_RKNPU_ACTION, &action);
    if (ret < 0) {
        perror("ioctl RKNPU_ACTION");
    } else {
        printf("HW Version: 0x%08x\n", action.value);
    }

    close(fd);
    printf("Done.\n");
    return 0;
}
