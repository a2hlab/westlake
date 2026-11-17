#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include "bionic_apk_path.h"

static unsigned checks;
static void check(int value)
{
    checks++;
    if (!value) { fprintf(stderr,"BIONIC_APK_PATH_FAILED %u\n",checks); abort(); }
}

/* Exercise real descriptor links, closure/reuse and native-library separation.
 * These checks cover ABI path classification, not APK signature verification.
 */
int main(void)
{
    check(in_apk("/code/base.apk"));
    check(in_apk("/code/base.apk!/lib/arm64-v8a/libtest.so"));
    check(in_apk("/code/dir.apk/libtest.so"));
    check(!in_apk("/code/base.apk.backup"));
    check(!in_apk("/code/libtest.so"));
    check(!in_apk(NULL));
    char directory[] = "/tmp/wl-apk-path-XXXXXX";
    check(mkdtemp(directory) != NULL);
    char apk[256],native[256],label[128],member[256],invalid[256];
    snprintf(apk,sizeof(apk),"%s/base.apk",directory);
    snprintf(native,sizeof(native),"%s/native.so",directory);
    int a=open(apk,O_CREAT|O_EXCL|O_RDWR,0600);check(a>=0);
    int b=open(native,O_CREAT|O_EXCL|O_RDWR,0600);check(b>=0);
    snprintf(label,sizeof(label),"/proc/self/fd/%d",a);
    snprintf(member,sizeof(member),"%s!/lib/arm64-v8a/libtest.so",label);
    check(in_apk(label));check(in_apk(member));
    snprintf(invalid,sizeof(invalid),"%s/other.apk",label);check(!in_apk(invalid));
    snprintf(invalid,sizeof(invalid),"%s!native.so",label);check(!in_apk(invalid));
    snprintf(invalid,sizeof(invalid),"/proc/self/fd/%d",b);check(!in_apk(invalid));
    for (const char **p=(const char *[]){"/proc/self/fd/", "/proc/self/fd/-1", "/proc/self/fd/not-an-fd.apk",
            "/proc/self/fd/2147483648", "/proc/self/fd/999999999999999999999", NULL};*p;p++) check(!in_apk(*p));
    errno=E2BIG;check(in_apk(label));check(errno==E2BIG);
    check(close(a)==0);errno=E2BIG;check(!in_apk(label));check(errno==E2BIG);
    check(dup2(b,a)==a);check(!in_apk(label));check(!in_apk(member));
    close(a);close(b);unlink(apk);unlink(native);rmdir(directory);
    printf("BIONIC_APK_PATH_PASS %u\n",checks);
    return 0;
}
