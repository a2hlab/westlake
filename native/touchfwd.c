/* touchfwd — WESTLAKE §421: make the PHYSICAL touchscreen drive noice.
 *
 * Real touches never reach the app: its windows are bridge-created sub-windows that OHOS's MMI
 * InputWindowsManager never registered, and subscribeMmi is disabled in the child (it SIGBUSes).
 * So OHOS delivers pointer events to SceneBoard/launcher instead, and the panel feels dead.
 *
 * This reads the touchscreen evdev node directly and re-emits each gesture into the bridge's
 * in-process injection channel (/data/local/tmp/noice_tap), which IS delivered. Taps become
 * "x y"; anything that moved far enough becomes a drag "x1 y1 x2 y2" so scrolling/sliders work.
 *
 * usage: touchfwd [/dev/input/event5] [&]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/input.h>

#define TAP_CHANNEL "/data/local/tmp/noice_tap"
#define DRAG_THRESHOLD 48      /* px before a gesture counts as a drag rather than a tap */

static void emit(const char* s) {
    int fd = open(TAP_CHANNEL, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return;
    write(fd, s, strlen(s));
    close(fd);
    fprintf(stderr, "[touchfwd] %s", s);
    fflush(stderr);
}

/* §563: stream DOWN/MOVE/UP instead of one synthetic gesture on lift. The bridge exposes
 * "d x y" / "m x y" / "u x y" which it forwards verbatim via dispatchSingleTouchViaViewRoot, so
 * the app sees a coherent moving pointer: press feedback appears immediately and RecyclerView /
 * ScrollView can actually scroll. Opt-in (WL_TOUCH_STREAM=1) so the proven lift-only tap path
 * stays the default. MOVEs are throttled -- the channel is one line per poll, so flooding it just
 * loses samples. */
static int g_stream = 0;
static int g_stream_click = 0;
static int g_x_min = 0, g_x_max = 0, g_y_min = 0, g_y_max = 0;
static int g_output_width = 1200, g_output_height = 1920;
static int g_coordinate_gain = 1;

static int read_axis_range(int fd, int primary, int fallback,
                           int* min_value, int* max_value) {
    struct input_absinfo info;
    if (ioctl(fd, EVIOCGABS(primary), &info) != 0 &&
        ioctl(fd, EVIOCGABS(fallback), &info) != 0) {
        return -1;
    }
    *min_value = info.minimum;
    *max_value = info.maximum;
    return info.maximum > info.minimum ? 0 : -1;
}

static int scale_axis(int value, int min_value, int max_value, int pixels) {
    if (max_value <= min_value || pixels <= 1) return value;
    long long normalized = (long long)(value - min_value) * g_coordinate_gain;
    if (normalized < 0) normalized = 0;
    if (normalized > max_value - min_value) normalized = max_value - min_value;
    return (int)((normalized * (pixels - 1) +
                 (max_value - min_value) / 2) /
                (max_value - min_value));
}

static void emit_stream(char c, int x, int y) {
    char b[64];
    snprintf(b, sizeof(b), "%c %d %d\n", c, x, y);
    emit(b);
}

/* The bridge consumes a single, truncate-on-read mailbox.  A fast finger can
 * otherwise replace DOWN with UP (or replace UP with the tap fallback) before
 * the 25 ms poll sees the earlier command.  Wait only at gesture boundaries;
 * MOVE coalescing is intentional and keeps scrolling current. */
static int wait_channel_consumed(int timeout_ms) {
    struct stat st;
    int waited_ms = 0;
    while (waited_ms < timeout_ms) {
        if (stat(TAP_CHANNEL, &st) != 0 || st.st_size == 0) return 1;
        usleep(2 * 1000);
        waited_ms += 2;
    }
    return 0;
}

static void emit_click(int x, int y) {
    char b[64];
    snprintf(b, sizeof(b), "c %d %d\n", x, y);
    emit(b);
}

int main(int argc, char** argv) {
    const char* dev = argc > 1 ? argv[1] : "/dev/input/event5";
    int fd = open(dev, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "touchfwd: open %s: %s\n", dev, strerror(errno)); return 1; }
    fprintf(stderr, "[touchfwd] forwarding %s -> %s\n", dev, TAP_CHANNEL);
    fflush(stderr);

    const char* width = getenv("WL_TOUCH_WIDTH");
    const char* height = getenv("WL_TOUCH_HEIGHT");
    const char* gain = getenv("WL_TOUCH_COORD_GAIN");
    if (width && atoi(width) > 0) g_output_width = atoi(width);
    if (height && atoi(height) > 0) g_output_height = atoi(height);
    if (gain && atoi(gain) > 0) g_coordinate_gain = atoi(gain);
    if (read_axis_range(fd, ABS_MT_POSITION_X, ABS_X, &g_x_min, &g_x_max) != 0 ||
        read_axis_range(fd, ABS_MT_POSITION_Y, ABS_Y, &g_y_min, &g_y_max) != 0) {
        fprintf(stderr,
                "[touchfwd] ABS range unavailable; forwarding coordinates unchanged\n");
        g_x_min = g_x_max = g_y_min = g_y_max = 0;
    } else {
        fprintf(stderr,
                "[touchfwd] scale raw X=%d..%d Y=%d..%d gain=%d -> %dx%d\n",
                g_x_min, g_x_max, g_y_min, g_y_max,
                g_coordinate_gain,
                g_output_width, g_output_height);
    }
    fflush(stderr);

    { const char* e = getenv("WL_TOUCH_STREAM"); g_stream = (e && *e == '1'); }
    { const char* e = getenv("WL_TOUCH_STREAM_CLICK");
      g_stream_click = (e && *e == '1'); }
    if (g_stream) { fprintf(stderr, "[touchfwd] STREAMING mode (d/m/u)\n"); fflush(stderr); }
    if (g_stream_click) {
        fprintf(stderr, "[touchfwd] short-tap main-queue click fallback enabled\n");
        fflush(stderr);
    }
    int x = -1, y = -1, downX = -1, downY = -1, touching = 0, sawDown = 0;
    int streamDown = 0, lastMx = -1, lastMy = -1;
    struct input_event ev;
    while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X)
                x = scale_axis(ev.value, g_x_min, g_x_max, g_output_width);
            else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y)
                y = scale_axis(ev.value, g_y_min, g_y_max, g_output_height);
            else if (ev.code == ABS_MT_TRACKING_ID) {
                if (ev.value == -1) touching = 0;
                else if (!touching) { touching = 1; sawDown = 1; downX = x; downY = y; }
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            if (ev.value) { touching = 1; sawDown = 1; downX = x; downY = y; }
            else touching = 0;
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            if (touching && sawDown && downX < 0) { downX = x; downY = y; }
            if (g_stream) {
                if (touching && !streamDown && x >= 0 && y >= 0) {
                    emit_stream('d', x, y);
                    (void)wait_channel_consumed(100);
                    streamDown = 1; lastMx = x; lastMy = y;
                } else if (touching && streamDown) {
                    int mdx = x - lastMx, mdy = y - lastMy;
                    if (mdx < 0) mdx = -mdx;
                    if (mdy < 0) mdy = -mdy;
                    if (mdx + mdy >= 8) { emit_stream('m', x, y); lastMx = x; lastMy = y; }
                } else if (!touching && streamDown) {
                    int dx = x - downX, dy = y - downY;
                    if (dx < 0) dx = -dx;
                    if (dy < 0) dy = -dy;
                    /* Do not let a pending coalesced MOVE overwrite the UP. */
                    (void)wait_channel_consumed(100);
                    emit_stream('u', x, y);
                    (void)wait_channel_consumed(100);
                    /* Toutiao's Handler-posted DOWN/UP reaches its view but the
                     * delayed performClick is stranded behind its main-queue
                     * barrier.  For a stationary physical gesture only, use
                     * the bridge's barrier-independent hit-tested click.  A
                     * drag never takes this path, so scrolling keeps the real
                     * MotionEvent stream and cannot turn into an accidental
                     * row activation. */
                    if (g_stream_click && downX >= 0 &&
                        dx <= DRAG_THRESHOLD && dy <= DRAG_THRESHOLD) {
                        emit_click(x, y);
                    }
                    streamDown = 0; sawDown = 0;
                    downX = downY = -1;
                }
                continue;   /* streaming replaces the lift-only tap below */
            }
            if (!touching && sawDown && x >= 0 && y >= 0) {
                char buf[64];
                int dx = x - downX, dy = y - downY;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (downX >= 0 && (dx > DRAG_THRESHOLD || dy > DRAG_THRESHOLD))
                    snprintf(buf, sizeof(buf), "%d %d %d %d\n", downX, downY, x, y);
                else
                    snprintf(buf, sizeof(buf), "%d %d\n", x, y);
                emit(buf);
                sawDown = 0; downX = downY = -1;
                /* §559: was 120 ms. That was sized for the bridge's old 300 ms poll; §558 cut
                 * the poll to 25 ms, so this is now pure dead time on every physical tap.
                 * ⚠️It cannot go to zero: the channel is a single file that the bridge
                 * truncates after reading, so a second gesture written before that read would
                 * overwrite the first and be lost. 25 ms == one poll period. */
                usleep(25 * 1000);
            }
        }
    }
    fprintf(stderr, "touchfwd: read ended: %s\n", strerror(errno));
    return 1;
}
