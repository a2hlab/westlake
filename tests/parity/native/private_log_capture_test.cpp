#include "PrivateLogCapture.h"
#include <atomic>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

static int checks;
static void require(bool condition, const char* message) {
    if (!condition) { fprintf(stderr, "FAIL %s\n", message); exit(1); }
    ++checks;
}

int main() {
    char directory[] = "/tmp/westlake-private-log-XXXXXX";
    require(mkdtemp(directory) != nullptr, "private fixture");
    std::string file = std::string(directory) + "/android.log";
    auto capture = [&] { return westlake::capturePrivateAndroidLog(directory, 4, "test", "synthetic-message"); };
    require(!westlake::capturePrivateAndroidLog(nullptr, 4, "test", "message"), "disabled sink");
    require(!westlake::capturePrivateAndroidLog("relative", 4, "test", "message"), "reject relative directory");
    chmod(directory, 0755);
    require(!capture() && access(file.c_str(), F_OK) != 0, "reject shared directory");
    chmod(directory, 0700);
    require(capture(), "private append");
    struct stat metadata{};
    require(stat(file.c_str(), &metadata) == 0 && (metadata.st_mode & 07777) == 0600, "private file mode");
    chmod(file.c_str(), 0644);
    auto before = metadata.st_size;
    require(!capture() && stat(file.c_str(), &metadata) == 0 && metadata.st_size == before,
            "reject shared existing file");
    chmod(file.c_str(), 0600);
    std::string alternate = std::string(directory) + "/alternate";
    require(link(file.c_str(), alternate.c_str()) == 0 && !capture(), "reject hard-linked file");
    unlink(alternate.c_str());
    rename(file.c_str(), alternate.c_str());
    require(symlink(alternate.c_str(), file.c_str()) == 0 && !capture(), "reject symlink file");
    unlink(file.c_str());
    rename(alternate.c_str(), file.c_str());
    std::string directoryLink = std::string(directory) + "-link";
    require(symlink(directory, directoryLink.c_str()) == 0
            && !westlake::capturePrivateAndroidLog(directoryLink.c_str(), 4, "test", "message"),
            "reject symlink directory");
    unlink(directoryLink.c_str());
    unlink(file.c_str());
    require(mkfifo(file.c_str(), 0600) == 0 && !capture(), "reject FIFO without blocking");
    unlink(file.c_str());
    require(capture(), "recreate private file");
    constexpr off_t limit = 8 * 1024 * 1024;
    require(truncate(file.c_str(), limit) == 0 && !capture(), "stop at size cap");
    truncate(file.c_str(), limit - 800);
    std::vector<std::thread> writers;
    std::atomic<int> accepted{0};
    for (int i = 0; i < 16; ++i) writers.emplace_back([&] { for (int n = 0; n < 100; ++n) if (capture()) ++accepted; });
    for (auto& writer : writers) writer.join();
    require(accepted > 0 && stat(file.c_str(), &metadata) == 0 && metadata.st_size <= limit,
            "concurrent writers respect cap");
    unlink(file.c_str());
    rmdir(directory);
    printf("PASS %d private log capture checks\n", checks);
}
