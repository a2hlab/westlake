/**
 * appspawn-x spawn server.
 *
 * Listens on a Unix domain socket for spawn requests from foundation
 * (using the AppSpawnClient protocol). On each request, invokes the
 * registered SpawnHandler which typically fork()s a child process.
 */

#pragma once

#include "spawn_msg.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace appspawnx {

// Callback type for handling spawn requests
// Returns child PID on success, -1 on failure
using SpawnHandler = std::function<int(const SpawnMsg&)>;

class SpawnServer {
public:
    explicit SpawnServer(const std::string& socketName);
    ~SpawnServer();

    // Initialize OH security modules
    int initSecurity(const std::string& sandboxConfigPath);

    // Create and bind listening socket
    int createListenSocket();

    // Set the spawn request handler
    void setSpawnHandler(SpawnHandler handler);

    // Enter main event loop (blocks forever)
    // Listens for connections, reads spawn requests, calls handler
    void run();

    // ADR-0008 managed launches: route OH's spawn requests on this socket to each bundle's own spawner.
    void runRouter();

    // Close the listening socket (called in child after fork)
    void closeSocket();

private:
    std::string socketName_;
    std::string socketPath_;
    int listenFd_;
    SpawnHandler spawnHandler_;

    // The last OH binary request from the launcher that started a process, and its uid. A
    // request from that application's own processes gets these credentials, never its own.
    std::vector<uint8_t> launchRequest_;
    int32_t launchUid_ = -1;

    // ADR-0008 managed launch: the OH bundle whose processes OH's application manager asks this spawner
    // for (WESTLAKE_SPAWN_MANAGED_BUNDLE), or empty when only the launcher may ask.
    std::string managedBundle_;

    // The processes this spawner started and still runs (pid -> process name), and the app's main process: the
    // one started while none of the others ran. When it exits, the rest of the app's processes are ended, as
    // Android ends an app's processes with it; SIGTERM ends them all and the spawner. The signal handlers only
    // write the reaped pid (0 for SIGTERM) to signalPipe_, which the event loop reads.
    std::map<int, std::string> children_;
    int mainChild_ = -1;
    int signalPipe_[2] = {-1, -1};

    // Reads the pids the signal handlers wrote and acts on them.
    void onSignalsPending();
    // Ends every process this spawner started (SIGKILL), naming why.
    void endChildren(const std::string& why);

    // Handle a single client connection
    void handleConnection(int clientFd);

    // Decides whether the peer may make this request. The launcher (root) chooses credentials;
    // a process this spawner started may only name another process of its application, and its
    // request is rebuilt from the launch request. Sets fromLauncher; false refuses (logged).
    bool admit(int clientFd, std::vector<uint8_t>& buffer, bool isBinary, bool& fromLauncher,
               bool& fromAppMgr);

    // Parse raw message buffer into SpawnMsg
    // In real implementation, this would use appspawn protocol parser
    bool parseMessage(const uint8_t* data, size_t len, SpawnMsg& msg);

    // Send spawn result back to client
    void sendResult(int clientFd, const SpawnResult& result);
};

} // namespace appspawnx
