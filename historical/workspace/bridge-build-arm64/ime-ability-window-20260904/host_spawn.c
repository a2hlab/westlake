/* Transport a host-validated OH AppSpawn request; no hard-coded UID or token. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: host-spawn SOCKET REQUEST\n"); return 2; }
    FILE *in = fopen(argv[2], "rb");
    if (!in) { perror("request"); return 1; }
    unsigned char buffer[4096];
    size_t len = fread(buffer, 1, sizeof(buffer), in);
    int extra = fgetc(in); fclose(in);
    uint32_t magic = 0, message_len = 0;
    if (len >= sizeof(magic)) memcpy(&magic, buffer, sizeof(magic));
    if (len >= 12) memcpy(&message_len, buffer + 8, sizeof(message_len));
    if (len < 276 || len >= sizeof(buffer) || extra != EOF || magic != 0xef201234 || message_len != len) return 2;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(argv[1]) >= sizeof(address.sun_path)) return 2;
    strcpy(address.sun_path, argv[1]);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, (void *)&address, sizeof(address))) { perror("connect"); return 1; }
    struct timeval timeout = {30, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    for (size_t off = 0; off < len;) {
        ssize_t n = send(fd, buffer + off, len - off, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { perror("send"); close(fd); return 1; }
        off += (size_t)n;
    }
    unsigned char response[300];
    size_t got = 0;
    while (got < sizeof(response)) {
        ssize_t n = recv(fd, response + got, sizeof(response) - got, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { perror("recv"); close(fd); return 1; }
        got += (size_t)n;
    }
    close(fd);
    uint32_t response_magic, response_len;
    int32_t result, pid;
    memcpy(&response_magic, response, 4); memcpy(&response_len, response + 8, 4);
    memcpy(&result, response + 276, 4); memcpy(&pid, response + 280, 4);
    if (response_magic != magic || response_len != sizeof(response)) return 1;
    printf("HOST_SPAWN result=%d pid=%d\n", result, pid);
    return result == 0 && pid > 0 ? 0 : 1;
}
