/*
 * guppy-tap: tap the printer's touchscreen at screen coordinates, for
 * scripted UI scenarios (RAM measurements). Runs on the printer.
 *
 *   guppy-tap X Y [HOLD_MS]
 *
 * Writes input events into the touchscreen's event device; the kernel
 * passes them to every reader of the device (here: guppyscreen via tslib)
 * as if they came from the panel. Screen coordinates are converted to raw
 * touch coordinates with the inverse of tslib's linear calibration
 * (/etc/pointercal in the zMod chroot).
 *
 * Build with the FF5M toolchain wrappers (32-bit time_t, so struct
 * input_event is 16 bytes like the kernel's):
 *   $CROSS_COMPILE"gcc" -O2 -o guppy-tap guppy-tap.c
 */
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define DEVICE "/dev/input/event0"
#define POINTERCAL "/data/.mod/.zmod/etc/pointercal"

static int fd;

static void emit(int type, int code, int value) {
  struct input_event ev;
  memset(&ev, 0, sizeof(ev));
  gettimeofday(&ev.time, NULL);
  ev.type = type;
  ev.code = code;
  ev.value = value;
  if (write(fd, &ev, sizeof(ev)) != sizeof(ev)) {
    perror("write");
    exit(1);
  }
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s X Y [HOLD_MS]\n", argv[0]);
    return 2;
  }
  int xs = atoi(argv[1]), ys = atoi(argv[2]);
  int hold_ms = argc > 3 ? atoi(argv[3]) : 150;
  if (xs < 0 || xs >= 800 || ys < 0 || ys >= 480) {
    fprintf(stderr, "coordinates out of the 800x480 screen\n");
    return 2;
  }

  /* tslib linear: xs = (a2 + a0*x + a1*y) / a6, ys = (a5 + a3*x + a4*y) / a6 */
  long long a[7];
  FILE *f = fopen(POINTERCAL, "r");
  if (!f || fscanf(f, "%lld %lld %lld %lld %lld %lld %lld", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6]) != 7) {
    fprintf(stderr, "cannot read %s\n", POINTERCAL);
    return 1;
  }
  fclose(f);
  double bx = (double)xs * a[6] - a[2];
  double by = (double)ys * a[6] - a[5];
  double det = (double)a[0] * a[4] - (double)a[1] * a[3];
  int x = (int)((bx * a[4] - by * a[1]) / det + 0.5);
  int y = (int)((by * a[0] - bx * a[3]) / det + 0.5);

  fd = open(DEVICE, O_WRONLY);
  if (fd < 0) {
    perror(DEVICE);
    return 1;
  }
  /* Several samples while pressed: tslib's dejitter filter averages them. */
  int samples = hold_ms / 10 > 3 ? hold_ms / 10 : 3;
  for (int i = 0; i < samples; i++) {
    emit(EV_ABS, ABS_X, x);
    emit(EV_ABS, ABS_Y, y);
    if (i == 0) emit(EV_KEY, BTN_TOUCH, 1);
    emit(EV_SYN, SYN_REPORT, 0);
    usleep(10000);
  }
  emit(EV_KEY, BTN_TOUCH, 0);
  emit(EV_SYN, SYN_REPORT, 0);
  close(fd);
  printf("tap %d,%d (raw %d,%d)\n", xs, ys, x, y);
  return 0;
}
