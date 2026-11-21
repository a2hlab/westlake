#include "fdsan_block.c"  /* extracted by run_fdsan_host_test.sh */
#include <assert.h>
int main(int argc, char **argv) {
    int on = argc > 1 && strcmp(argv[1], "on") == 0;
    uint64_t a = android_fdsan_create_owner_tag(3, 0x1234), b = android_fdsan_create_owner_tag(11, 0x5678);
    int fd = open("/dev/null", O_RDONLY);
    android_fdsan_exchange_owner_tag(fd, 0, a);
    if (!on) { assert(android_fdsan_get_owner_tag(fd) == 0); assert(android_fdsan_get_error_level() == 0);
               assert(android_fdsan_close_with_tag(fd, b) == 0); puts("OFF_PASS"); return 0; }
    assert(android_fdsan_get_owner_tag(fd) == a);
    assert(android_fdsan_get_error_level() == 2);
    android_fdsan_exchange_owner_tag(fd, b, b);          /* wrong expectation: reported, owner stays a */
    assert(android_fdsan_get_owner_tag(fd) == a);
    assert(android_fdsan_close_with_tag(fd, b) == 0);     /* wrong owner: reported, still closed */
    assert(fcntl(fd, F_GETFD) == -1);
    int fd2 = open("/dev/null", O_RDONLY);
    android_fdsan_exchange_owner_tag(fd2, 0, b);
    assert(android_fdsan_close_with_tag(fd2, b) == 0);    /* right owner: silent, cleared */
    assert(android_fdsan_get_owner_tag(fd2) == 0);
    assert(android_fdsan_set_error_level(0) == 2);        /* an app may turn reports off */
    int fd3 = open("/dev/null", O_RDONLY);
    android_fdsan_exchange_owner_tag(fd3, 0, a);
    assert(android_fdsan_close_with_tag(fd3, b) == 0);    /* not reported at level 0 */
    assert(android_fdsan_set_error_level(1) == 0);        /* warn once reports like warn always */
    assert(android_fdsan_get_error_level() == 2);
    assert(wl_fdsan_entry(70000) == NULL && wl_fdsan_entry(-1) == NULL);
    /* the real owner closing a number someone else already closed is reported */
    int fd4 = open("/dev/null", O_RDONLY);
    android_fdsan_set_error_level(2);
    android_fdsan_exchange_owner_tag(fd4, 0, a);
    android_fdsan_close_with_tag(fd4, b);
    int fd5 = open("/dev/null", O_RDONLY);
    assert(fd5 == fd4 && android_fdsan_get_owner_tag(fd5) == 0);
    android_fdsan_close_with_tag(fd5, a);
    puts("ON_PASS"); return 0;
}
