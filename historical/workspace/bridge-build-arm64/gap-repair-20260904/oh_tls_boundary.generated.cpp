#include <jni.h>
#include <dlfcn.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <time.h>
namespace {

struct WlSslApi {
    void* libssl = nullptr;
    void* libcrypto = nullptr;
    const void* (*TLS_client_method)();
    void* (*SSL_CTX_new)(const void*);
    int   (*SSL_CTX_load_verify_locations)(void*, const char*, const char*);
    void  (*SSL_CTX_set_verify)(void*, int, void*);
    void* (*SSL_new)(void*);
    int   (*SSL_set_fd)(void*, int);
    long  (*SSL_ctrl)(void*, int, long, void*);
    int   (*SSL_set1_host)(void*, const char*);
    int   (*SSL_connect)(void*);
    int   (*SSL_read)(void*, void*, int);
    int   (*SSL_write)(void*, const void*, int);
    int   (*SSL_get_error)(const void*, int);
    long  (*SSL_get_verify_result)(const void*);
    void* (*SSL_get1_peer_certificate)(const void*);
    void* (*SSL_get_peer_cert_chain)(const void*);
    int (*OPENSSL_sk_num)(const void*);
    void* (*OPENSSL_sk_value)(const void*, int);
    const char* (*SSL_get_version)(const void*);
    const void* (*SSL_get_current_cipher)(const void*);
    const char* (*SSL_CIPHER_get_name)(const void*);
    int   (*SSL_shutdown)(void*);
    void  (*SSL_free)(void*);
    int   (*i2d_X509)(void*, unsigned char**);
    void  (*X509_free)(void*);
    void* ctx = nullptr;
    bool  ready = false;
    bool  tried = false;
};
static WlSslApi g_tls;
static pthread_mutex_t g_tls_lock = PTHREAD_MUTEX_INITIALIZER;

static const char* kCaFile = "/etc/ssl/certs/cacert.pem";

static void* wl_tls_dlopen(const char* const* names) {
    for (int i = 0; names[i] != nullptr; i++) {
        void* h = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
        if (h != nullptr) {
            fprintf(stderr, "[WESTLAKE-441] dlopen %s OK\n", names[i]);
            return h;
        }
    }
    return nullptr;
}

#define WL_TLS_SYM(handle, field)                                                   \
    do {                                                                            \
        g_tls.field = reinterpret_cast<decltype(g_tls.field)>(dlsym(handle, #field)); \
        if (g_tls.field == nullptr) {                                               \
            fprintf(stderr, "[WESTLAKE-441] MISSING symbol %s\n", #field);          \
            missing++;                                                              \
        }                                                                           \
    } while (0)

static bool wl_tls_init() {
    pthread_mutex_lock(&g_tls_lock);
    if (g_tls.tried) { pthread_mutex_unlock(&g_tls_lock); return g_tls.ready; }
    g_tls.tried = true;

    static const char* kSslNames[] = {
        "libssl_openssl.z.so",
        "/system/lib64/platformsdk/libssl_openssl.z.so",
        "/system/lib64/chipset-sdk/libssl_openssl.z.so", nullptr };
    static const char* kCryptoNames[] = {
        "libcrypto_openssl.z.so",
        "/system/lib64/platformsdk/libcrypto_openssl.z.so",
        "/system/lib64/chipset-sdk-sp/libcrypto_openssl.z.so", nullptr };
    g_tls.libssl    = wl_tls_dlopen(kSslNames);
    g_tls.libcrypto = wl_tls_dlopen(kCryptoNames);
    if (g_tls.libssl == nullptr || g_tls.libcrypto == nullptr) {
        fprintf(stderr, "[WESTLAKE-441] dlopen FAILED ssl=%p crypto=%p err=%s\n",
                g_tls.libssl, g_tls.libcrypto, dlerror());
        fflush(stderr); pthread_mutex_unlock(&g_tls_lock); return false;
    }

    int missing = 0;
    void* s = g_tls.libssl;
    WL_TLS_SYM(s, TLS_client_method);      WL_TLS_SYM(s, SSL_CTX_new);
    WL_TLS_SYM(s, SSL_CTX_load_verify_locations); WL_TLS_SYM(s, SSL_CTX_set_verify);
    WL_TLS_SYM(s, SSL_new);                WL_TLS_SYM(s, SSL_set_fd);
    WL_TLS_SYM(s, SSL_ctrl);               WL_TLS_SYM(s, SSL_set1_host);
    WL_TLS_SYM(s, SSL_connect);            WL_TLS_SYM(s, SSL_read);
    WL_TLS_SYM(s, SSL_write);              WL_TLS_SYM(s, SSL_get_error);
    WL_TLS_SYM(s, SSL_get_verify_result);  WL_TLS_SYM(s, SSL_get1_peer_certificate);
    WL_TLS_SYM(s, SSL_get_peer_cert_chain);
    WL_TLS_SYM(s, SSL_get_version);        WL_TLS_SYM(s, SSL_get_current_cipher);
    WL_TLS_SYM(s, SSL_CIPHER_get_name);    WL_TLS_SYM(s, SSL_shutdown);
    WL_TLS_SYM(s, SSL_free);
    void* c = g_tls.libcrypto;
    WL_TLS_SYM(c, i2d_X509);               WL_TLS_SYM(c, X509_free);
    WL_TLS_SYM(c, OPENSSL_sk_num);         WL_TLS_SYM(c, OPENSSL_sk_value);
    if (missing > 0) {
        fprintf(stderr, "[WESTLAKE-441] %d symbol(s) missing — TLS unavailable\n", missing);
        fflush(stderr); pthread_mutex_unlock(&g_tls_lock); return false;
    }

    g_tls.ctx = g_tls.SSL_CTX_new(g_tls.TLS_client_method());
    if (g_tls.ctx == nullptr) {
        fprintf(stderr, "[WESTLAKE-441] SSL_CTX_new FAILED\n");
        fflush(stderr); pthread_mutex_unlock(&g_tls_lock); return false;
    }
    const int loaded = g_tls.SSL_CTX_load_verify_locations(g_tls.ctx, kCaFile, nullptr);
    // SSL_VERIFY_PEER = 1. Keep verification ON: a silently-insecure client is worse than none.
    g_tls.SSL_CTX_set_verify(g_tls.ctx, 1, nullptr);
    fprintf(stderr, "[WESTLAKE-441] SSL_CTX ready ca=%s loaded=%d verify=PEER\n", kCaFile, loaded);
    fflush(stderr);
    g_tls.ready = (loaded == 1);
    if (!g_tls.ready) {
        fprintf(stderr, "[WESTLAKE-441] CA bundle did NOT load — refusing to run unverified\n");
        fflush(stderr);
    }
    pthread_mutex_unlock(&g_tls_lock);
    return g_tls.ready;
}

// §517: throw a NAMED exception. A read that timed out or failed is NOT end-of-stream, and the two
// must not be reported the same way — see WL_TLS_read.
static void wl_tls_throw_named(JNIEnv* env, const char* cls, const char* msg) {
    if (env->ExceptionCheck()) return;   // never mask an exception already in flight
    jclass c = env->FindClass(cls);
    if (c == nullptr) { env->ExceptionClear(); c = env->FindClass("java/io/IOException"); }
    if (c != nullptr) { env->ThrowNew(c, msg); env->DeleteLocalRef(c); }
}

static void wl_tls_throw_io(JNIEnv* env, const char* what, int err) {
    char buf[192];
    snprintf(buf, sizeof(buf), "WestlakeTLS: %s failed (ssl_err=%d)", what, err);
    jclass ioe = env->FindClass("javax/net/ssl/SSLException");
    if (ioe == nullptr) { env->ExceptionClear(); ioe = env->FindClass("java/io/IOException"); }
    if (ioe != nullptr) env->ThrowNew(ioe, buf);
}

// Drive a would-block SSL op. Returns true to retry, false if it really failed/timed out.
// §521: tri-state. Collapsing "deadline expired" and "poll() failed" into one boolean made both
// surface as SocketTimeoutException, which misreports a broken transport as a slow one.
enum WlWait { WL_WAIT_READY = 0, WL_WAIT_TIMEOUT = 1, WL_WAIT_ERROR = 2 };
static WlWait wl_tls_wait(int fd, int sslErr, int timeoutMs) {
    if (sslErr != 2 /*WANT_READ*/ && sslErr != 3 /*WANT_WRITE*/) return WL_WAIT_ERROR;
    struct pollfd p;
    p.fd = fd;
    p.events = (sslErr == 2) ? POLLIN : POLLOUT;
    p.revents = 0;
    const int r = poll(&p, 1, timeoutMs);
    if (r > 0)  return WL_WAIT_READY;
    if (r == 0) return WL_WAIT_TIMEOUT;
    return WL_WAIT_ERROR;
}

// §447: dump the first few HTTP bytes each way so we can see the actual request/response.
static int g_tls_dump_count = 0;
static bool wl_tls_is_http_head(const char* op, const jbyte* data, int n) {
    if (op == nullptr || data == nullptr || n <= 0) return false;
    const char* p = reinterpret_cast<const char*>(data);
    if (strncmp(op, "-->", 3) == 0) {
        static const char* const kMethods[] = {
            "GET ", "POST ", "PUT ", "PATCH ", "DELETE ", "HEAD ", "OPTIONS "
        };
        for (const char* method : kMethods) {
            const size_t len = strlen(method);
            if (n >= static_cast<int>(len) && memcmp(p, method, len) == 0) return true;
        }
        return false;
    }
    return n >= 5 && memcmp(p, "HTTP/", 5) == 0;
}

static void wl_tls_dump(const char* op, const jbyte* data, int n) {
    if (n <= 0) return;
    // §751: opt-in request-line tracing for late, user-driven network paths.  The original
    // six-chunk cap is consumed during cold start, so it cannot diagnose a request made minutes
    // later (for example after opening a media tab).  In diagnostic mode log only an HTTP request
    // or status line -- never a body, cookies, or authorization headers.  With the environment
    // variable absent, behavior remains exactly the §447 six bounded dumps.
    const char* httpHeads = getenv("WL_TLS_DUMP_HTTP_HEADS");
    const bool headsOnly = httpHeads != nullptr && httpHeads[0] != '\0' &&
                           strcmp(httpHeads, "0") != 0;
    if (headsOnly) {
        if (!wl_tls_is_http_head(op, data, n)) return;
        const int cap = (n < 1024) ? n : 1024;
        int lineLen = 0;
        while (lineLen < cap && data[lineLen] != '\r' && data[lineLen] != '\n') lineLen++;
        std::string line(reinterpret_cast<const char*>(data), static_cast<size_t>(lineLen));
        fprintf(stderr, "[WESTLAKE-HTTPHEAD-751] %s %s\n", op, line.c_str());
        fflush(stderr);
        return;
    }
    if (g_tls_dump_count >= 6) return;
    g_tls_dump_count++;
    const int cap = (n < 420) ? n : 420;
    std::string out;
    out.reserve((size_t)cap + 8);
    for (int i = 0; i < cap; i++) {
        const unsigned char c = (unsigned char)data[i];
        if (c == '\r') { out += "\\r"; }
        else if (c == '\n') { out += "\\n"; }
        else if (c >= 32 && c < 127) { out += (char)c; }
        else { out += '.'; }
    }
    fprintf(stderr, "[WESTLAKE-447] %s %d bytes: %s%s\n", op, n, out.c_str(),
            (n > cap) ? " ...(truncated)" : "");
    fflush(stderr);
}

// Opt-in diagnostic for a user-driven Search response. Request/status-line tracing proves only
// that the server answered; it cannot distinguish an empty response body from data discarded by
// the Java/JS bridge afterward. Track only connections that send GET /search/ and cap each raw,
// decrypted capture at 4 MiB. The switch is absent in production launches, and no request headers
// are written to disk.
static pthread_mutex_t g_tls_search_dump_mu = PTHREAD_MUTEX_INITIALIZER;
static std::map<int, size_t>* g_tls_search_dump_fds = nullptr;

static void wl_tls_search_dump_request(int fd, const jbyte* data, int n) {
    const char* enabled = getenv("WL_TLS_DUMP_SEARCH_BODY");
    if (enabled == nullptr || enabled[0] == '\0' || strcmp(enabled, "0") == 0 ||
        data == nullptr || n < 13 || memcmp(data, "GET /search/?", 13) != 0) {
        return;
    }
    pthread_mutex_lock(&g_tls_search_dump_mu);
    if (g_tls_search_dump_fds == nullptr) {
        g_tls_search_dump_fds = new std::map<int, size_t>();
    }
    (*g_tls_search_dump_fds)[fd] = 0;
    pthread_mutex_unlock(&g_tls_search_dump_mu);

    char path[160];
    snprintf(path, sizeof(path), "/data/local/tmp/asx/search-response-%d-fd%d.bin",
             static_cast<int>(getpid()), fd);
    FILE* output = fopen(path, "wb");
    if (output != nullptr) fclose(output);
    fprintf(stderr, "[WESTLAKE-SEARCHBODY-790] tracking fd=%d path=%s\n", fd, path);
    fflush(stderr);
}

static void wl_tls_search_dump_response(int fd, const jbyte* data, int n) {
    if (data == nullptr || n <= 0) return;
    constexpr size_t kLimit = 4U * 1024U * 1024U;
    size_t offset = 0;
    size_t writeSize = 0;
    pthread_mutex_lock(&g_tls_search_dump_mu);
    if (g_tls_search_dump_fds != nullptr) {
        std::map<int, size_t>::iterator it = g_tls_search_dump_fds->find(fd);
        if (it != g_tls_search_dump_fds->end() && it->second < kLimit) {
            offset = it->second;
            writeSize = static_cast<size_t>(n);
            if (writeSize > kLimit - offset) writeSize = kLimit - offset;
            it->second += writeSize;
        }
    }
    pthread_mutex_unlock(&g_tls_search_dump_mu);
    if (writeSize == 0) return;

    char path[160];
    snprintf(path, sizeof(path), "/data/local/tmp/asx/search-response-%d-fd%d.bin",
             static_cast<int>(getpid()), fd);
    FILE* output = fopen(path, offset == 0 ? "wb" : "ab");
    if (output != nullptr) {
        fwrite(data, 1, writeSize, output);
        fclose(output);
    }
}

// §446: bounded I/O tracing so a premature EOF can be told apart from a real one.
static int g_tls_io_logged = 0;
static void wl_tls_log_io(const char* op, const char* what, int n, int sslErr, int fd) {
    const bool interesting = (strcmp(what, "ok") != 0);
    if (!interesting && g_tls_io_logged >= 12) return;
    if (g_tls_io_logged >= 200) return;
    g_tls_io_logged++;
    fprintf(stderr, "[WESTLAKE-446] tls %s %s fd=%d n=%d ssl_err=%d errno=%s\n",
            op, what, fd, n, sslErr, (errno != 0) ? strerror(errno) : "-");
    fflush(stderr);
}

// ── §604: SSL handle lifetime registry — fixes the use-after-free that made audio impossible ──
// `WL_TLS_close` used to `SSL_free()` the object while the Java side kept the raw pointer in a
// jlong, and read/write/peerCert/info took it straight back with only a null check. Any use that
// raced or followed a close therefore ran OpenSSL on freed memory.
// Caught on the noice audio path (§604): the freed block had been recycled to hold UTF-16 Java
// string data, so `BIO_read` dispatched through a function pointer whose value WAS TEXT —
// `pc=0x6f006900640075` == `"udio"` (from an "audio/…" string) → SIGBUS, child dead.
// That is why tapping play produced no sound: the crash lands while fetching the MP3 segment, so
// everything downstream (MediaCodec → AudioTrack → OH_AudioRenderer) was never reached. The media
// pipeline was never at fault.
//
// Every live SSL gets an entry with an in-flight refcount. `close` marks it dead immediately, so no
// NEW user can pin it, and defers SSL_shutdown/SSL_free to the last in-flight user — a blocking
// read already inside OpenSSL therefore keeps operating on valid memory instead of faulting.
struct WlTlsEnt { int refs; bool closed; };
static pthread_mutex_t g_tls_reg_mu = PTHREAD_MUTEX_INITIALIZER;
static std::map<void*, WlTlsEnt>* g_tls_reg = nullptr;  // intentionally never torn down

static void wl_tls_reg_add(void* ssl) {
    if (ssl == nullptr) return;
    pthread_mutex_lock(&g_tls_reg_mu);
    if (g_tls_reg == nullptr) g_tls_reg = new std::map<void*, WlTlsEnt>();
    WlTlsEnt e; e.refs = 0; e.closed = false;
    (*g_tls_reg)[ssl] = e;
    pthread_mutex_unlock(&g_tls_reg_mu);
}

// Validate + pin. Returns nullptr for a handle that is closed, unknown, or never ours.
static void* wl_tls_pin(jlong handle) {
    void* ssl = (void*)(uintptr_t)handle;
    if (ssl == nullptr) return nullptr;
    void* out = nullptr;
    pthread_mutex_lock(&g_tls_reg_mu);
    if (g_tls_reg != nullptr) {
        std::map<void*, WlTlsEnt>::iterator it = g_tls_reg->find(ssl);
        if (it != g_tls_reg->end() && !it->second.closed) { it->second.refs++; out = ssl; }
    }
    pthread_mutex_unlock(&g_tls_reg_mu);
    if (out == nullptr) {
        fprintf(stderr, "[WESTLAKE-604] rejected stale TLS handle %p (use after close)\n", ssl);
        fflush(stderr);
    }
    return out;
}

static void wl_tls_unpin(void* ssl) {
    if (ssl == nullptr) return;
    bool reap = false;
    pthread_mutex_lock(&g_tls_reg_mu);
    if (g_tls_reg != nullptr) {
        std::map<void*, WlTlsEnt>::iterator it = g_tls_reg->find(ssl);
        if (it != g_tls_reg->end()) {
            if (it->second.refs > 0) it->second.refs--;
            if (it->second.closed && it->second.refs == 0) { g_tls_reg->erase(it); reap = true; }
        }
    }
    pthread_mutex_unlock(&g_tls_reg_mu);
    if (reap) { g_tls.SSL_shutdown(ssl); g_tls.SSL_free(ssl); }
}

// RAII pin, so the many early returns in read/write cannot leak a reference.
struct WlTlsUse {
    void* ssl;
    explicit WlTlsUse(jlong h) : ssl(wl_tls_pin(h)) {}
    ~WlTlsUse() { if (ssl != nullptr) wl_tls_unpin(ssl); }
    WlTlsUse(const WlTlsUse&) = delete;
    WlTlsUse& operator=(const WlTlsUse&) = delete;
};

static jlong WL_TLS_handshake(JNIEnv* env, jclass, jint fd, jstring jhost, jint timeoutMs) {
    if (!wl_tls_init()) { wl_tls_throw_io(env, "init", 0); return 0; }
    const char* host = (jhost != nullptr) ? env->GetStringUTFChars(jhost, nullptr) : nullptr;

    void* ssl = g_tls.SSL_new(g_tls.ctx);
    if (ssl == nullptr) {
        if (host) env->ReleaseStringUTFChars(jhost, host);
        wl_tls_throw_io(env, "SSL_new", 0); return 0;
    }
    g_tls.SSL_set_fd(ssl, (int)fd);
    if (host != nullptr) {
        // SNI: SSL_CTRL_SET_TLSEXT_HOSTNAME=55, TLSEXT_NAMETYPE_host_name=0
        g_tls.SSL_ctrl(ssl, 55, 0, const_cast<char*>(host));
        g_tls.SSL_set1_host(ssl, host);   // OpenSSL-side hostname verification
    }

    const int deadline = (timeoutMs > 0) ? timeoutMs : 30000;
    int rc;
    for (;;) {
        rc = g_tls.SSL_connect(ssl);
        if (rc == 1) break;
        const int e = g_tls.SSL_get_error(ssl, rc);
        if (wl_tls_wait((int)fd, e, deadline) != WL_WAIT_READY) {
            fprintf(stderr, "[WESTLAKE-441] handshake FAILED host=%s rc=%d ssl_err=%d errno=%s\n",
                    host ? host : "?", rc, e, strerror(errno));
            fflush(stderr);
            g_tls.SSL_free(ssl);
            if (host) env->ReleaseStringUTFChars(jhost, host);
            wl_tls_throw_io(env, "handshake", e);
            return 0;
        }
    }
    const long vr = g_tls.SSL_get_verify_result(ssl);
    if (vr != 0 /*X509_V_OK*/) {
        fprintf(stderr, "[WESTLAKE-441] CERT VERIFY FAILED host=%s result=%ld\n",
                host ? host : "?", vr);
        fflush(stderr);
        g_tls.SSL_free(ssl);
        if (host) env->ReleaseStringUTFChars(jhost, host);
        wl_tls_throw_io(env, "certificate verification", (int)vr);
        return 0;
    }
    const char* ver = g_tls.SSL_get_version(ssl);
    const void* cip = g_tls.SSL_get_current_cipher(ssl);
    fprintf(stderr, "[WESTLAKE-441] HANDSHAKE OK host=%s proto=%s cipher=%s verify=OK\n",
            host ? host : "?", ver ? ver : "?",
            cip ? g_tls.SSL_CIPHER_get_name(cip) : "?");
    fflush(stderr);
    if (host) env->ReleaseStringUTFChars(jhost, host);
    wl_tls_reg_add(ssl);   // §604: from here on the handle is validatable
    return (jlong)(uintptr_t)ssl;
}

static jint WL_TLS_read(JNIEnv* env, jclass, jlong handle, jint fd, jbyteArray buf,
                        jint off, jint len, jint timeoutMs) {
    WlTlsUse use(handle);            // §604: reject a stale handle instead of faulting on freed memory
    void* ssl = use.ssl;
    if (ssl == nullptr || buf == nullptr) return -1;
    if (len <= 0) return 0;
    jbyte* tmp = (jbyte*)malloc((size_t)len);
    if (tmp == nullptr) return -1;
    const int deadline = (timeoutMs > 0) ? timeoutMs : 60000;
    int n;
    for (;;) {
        errno = 0;
        n = g_tls.SSL_read(ssl, tmp, len);
        if (n > 0) break;
        const int e = g_tls.SSL_get_error(ssl, n);
        if (e == 6 /*SSL_ERROR_ZERO_RETURN*/) {          // peer closed cleanly
            wl_tls_log_io("read", "peer closed (ZERO_RETURN)", n, e, fd);
            free(tmp); return -1;
        }
        if (e == 2 /*WANT_READ*/ || e == 3 /*WANT_WRITE*/) {
            const WlWait w = wl_tls_wait((int)fd, e, deadline);
            if (w != WL_WAIT_READY) {
                // §517: was `return -1`, which Java reports to the caller as END-OF-STREAM. A
                // timeout mid-body then looks like a clean EOF: OkHttp raises "unexpected end of
                // stream" and abandons the partially written cache entry. That is exactly how a
                // 1,392,045-byte CDN body ended up frozen as a 1,056,459-byte .tmp, stalling
                // playback after one segment. Report it as what it is.
                wl_tls_log_io("read", (w == WL_WAIT_TIMEOUT) ? "poll timeout" : "poll error", n, e, fd);
                free(tmp);
                wl_tls_throw_named(env,
                    (w == WL_WAIT_TIMEOUT) ? "java/net/SocketTimeoutException"
                                           : "javax/net/ssl/SSLException",
                    (w == WL_WAIT_TIMEOUT) ? "WestlakeTLS: read timed out"
                                           : "WestlakeTLS: poll failed during read");
                return -1;
            }
            continue;
        }
        // §446: SSL_ERROR_SYSCALL with a retryable errno is NOT end-of-stream. Treating it as EOF
        // is what produced OkHttp's "EOFException: \n not found: limit=0" on a healthy connection.
        if (e == 5 /*SSL_ERROR_SYSCALL*/ && n < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            const WlWait w2 = wl_tls_wait((int)fd, 2 /*poll readable*/, deadline);
            if (w2 != WL_WAIT_READY) {
                wl_tls_log_io("read", "poll after EAGAIN failed", n, e, fd);
                free(tmp);
                wl_tls_throw_named(env,
                    (w2 == WL_WAIT_TIMEOUT) ? "java/net/SocketTimeoutException"
                                            : "javax/net/ssl/SSLException",
                    "WestlakeTLS: read failed after EAGAIN");
                return -1;
            }
            continue;
        }
        // §521 (corrects §517): SSL_ERROR_SYSCALL with n==0 and no errno is precisely how OpenSSL
        // reports an *UNEXPECTED* EOF — the peer vanished WITHOUT close_notify. It is NOT clean end
        // of stream; only SSL_ERROR_ZERO_RETURN is. Returning -1 here reinstated the exact silent
        // truncation §517 set out to remove: a body cut short mid-transfer looked to Java like a
        // stream that had ended normally. Report it as the transport failure it is and let the
        // caller decide (OkHttp surfaces "unexpected end of stream" and can retry).
        if (e == 5 /*SSL_ERROR_SYSCALL*/ && n == 0 && errno == 0) {
            wl_tls_log_io("read", "unexpected EOF (no close_notify)", n, e, fd);
            free(tmp);
            wl_tls_throw_named(env, "javax/net/ssl/SSLProtocolException",
                               "WestlakeTLS: connection closed without close_notify");
            return -1;
        }
        wl_tls_log_io("read", "fatal", n, e, fd);
        free(tmp);
        wl_tls_throw_named(env, "javax/net/ssl/SSLException", "WestlakeTLS: read failed");
        return -1;
    }
    env->SetByteArrayRegion(buf, off, n, tmp);
    wl_tls_dump("<-- recv", tmp, n);
    wl_tls_search_dump_response(fd, tmp, n);
    free(tmp);
    wl_tls_log_io("read", "ok", n, 0, fd);
    return n;
}

static jint WL_TLS_write(JNIEnv* env, jclass, jlong handle, jint fd, jbyteArray buf,
                         jint off, jint len, jint timeoutMs) {
    WlTlsUse use(handle);            // §604
    void* ssl = use.ssl;
    if (ssl == nullptr || buf == nullptr) return -1;
    if (len <= 0) return 0;
    jbyte* tmp = (jbyte*)malloc((size_t)len);
    if (tmp == nullptr) return -1;
    env->GetByteArrayRegion(buf, off, len, tmp);
    wl_tls_dump("--> send", tmp, len);
    wl_tls_search_dump_request(fd, tmp, len);
    const int deadline = (timeoutMs > 0) ? timeoutMs : 60000;
    int done = 0;
    while (done < len) {
        errno = 0;
        const int n = g_tls.SSL_write(ssl, tmp + done, len - done);
        if (n > 0) { done += n; continue; }
        const int e = g_tls.SSL_get_error(ssl, n);
        if (e == 2 /*WANT_READ*/ || e == 3 /*WANT_WRITE*/) {
            const WlWait w = wl_tls_wait((int)fd, e, deadline);
            if (w != WL_WAIT_READY) {
                wl_tls_log_io("write", "poll timeout/error", n, e, fd);
                free(tmp); return -1;
            }
            continue;
        }
        if (e == 5 /*SSL_ERROR_SYSCALL*/ && n < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            if (!wl_tls_wait((int)fd, 3 /*poll writable*/, deadline)) {
                wl_tls_log_io("write", "poll after EAGAIN failed", n, e, fd);
                free(tmp); return -1;
            }
            continue;
        }
        wl_tls_log_io("write", "fatal", n, e, fd);
        free(tmp); return -1;
    }
    free(tmp);
    wl_tls_log_io("write", "ok", done, 0, fd);
    return done;
}

static jbyteArray WL_TLS_peerCert(JNIEnv* env, jclass, jlong handle) {
    WlTlsUse use(handle);            // §604
    void* ssl = use.ssl;
    if (ssl == nullptr) return nullptr;
    void* x = g_tls.SSL_get1_peer_certificate(ssl);
    if (x == nullptr) return nullptr;
    const int n = g_tls.i2d_X509(x, nullptr);
    jbyteArray out = nullptr;
    if (n > 0 && n <= 1024 * 1024) {
        std::vector<unsigned char> der(static_cast<size_t>(n));
        unsigned char* cursor = der.data();
        if (g_tls.i2d_X509(x, &cursor) != n) { g_tls.X509_free(x); return nullptr; }
        out = env->NewByteArray(n);
        if (out != nullptr) env->SetByteArrayRegion(out, 0, n, (const jbyte*)der.data());
    }
    g_tls.X509_free(x);
    return out;
}

// Client-side OpenSSL returns the peer-sent chain with the leaf first. The
// root is normally not sent and must not be invented or trusted here. Keep the
// live-handle pin until all borrowed X509 objects have been DER-encoded.
static jbyteArray WL_TLS_peerChain(JNIEnv* env, jclass, jlong handle) {
    WlTlsUse use(handle);
    if (!use.ssl) {
        wl_tls_throw_named(env, "javax/net/ssl/SSLPeerUnverifiedException", "TLS session is closed");
        return nullptr;
    }
    void* chain = g_tls.SSL_get_peer_cert_chain(use.ssl);
    const int count = chain ? g_tls.OPENSSL_sk_num(chain) : 0;
    if (count <= 0 || count > 64) {
        wl_tls_throw_named(env, "javax/net/ssl/SSLPeerUnverifiedException", "Peer certificate chain unavailable");
        return nullptr;
    }
    std::vector<unsigned char> encoded;
    for (int i = 0; i < count; ++i) {
        void* cert = g_tls.OPENSSL_sk_value(chain, i);
        const int size = cert ? g_tls.i2d_X509(cert, nullptr) : 0;
        if (size <= 0 || size > 1024 * 1024 || encoded.size() + size > 4U * 1024U * 1024U) {
            wl_tls_throw_named(env, "javax/net/ssl/SSLPeerUnverifiedException", "Invalid peer certificate encoding");
            return nullptr;
        }
        const size_t offset = encoded.size();
        encoded.resize(offset + size);
        unsigned char* cursor = encoded.data() + offset;
        if (g_tls.i2d_X509(cert, &cursor) != size) {
            wl_tls_throw_named(env, "javax/net/ssl/SSLPeerUnverifiedException", "Peer certificate encoding failed");
            return nullptr;
        }
    }
    jbyteArray result = env->NewByteArray(static_cast<jsize>(encoded.size()));
    if (result) env->SetByteArrayRegion(result, 0, static_cast<jsize>(encoded.size()),
                                      reinterpret_cast<const jbyte*>(encoded.data()));
    return result;
}

static jstring WL_TLS_info(JNIEnv* env, jclass, jlong handle, jint which) {
    WlTlsUse use(handle);            // §604
    void* ssl = use.ssl;
    if (ssl == nullptr) return nullptr;
    if (which == 0) {
        const char* v = g_tls.SSL_get_version(ssl);
        return v ? env->NewStringUTF(v) : nullptr;
    }
    const void* c = g_tls.SSL_get_current_cipher(ssl);
    const char* n = c ? g_tls.SSL_CIPHER_get_name(c) : nullptr;
    return n ? env->NewStringUTF(n) : nullptr;
}

static void WL_TLS_close(JNIEnv*, jclass, jlong handle) {
    // §604: mark dead under the lock so no NEW user can pin it, but only free once nobody is
    // in flight — otherwise a concurrent SSL_read is left reading freed memory (the SIGBUS).
    // An unknown or already-closed handle is now ignored, so double close is harmless too.
    void* ssl = (void*)(uintptr_t)handle;
    if (ssl == nullptr) return;
    bool reap = false;
    pthread_mutex_lock(&g_tls_reg_mu);
    if (g_tls_reg != nullptr) {
        std::map<void*, WlTlsEnt>::iterator it = g_tls_reg->find(ssl);
        if (it != g_tls_reg->end() && !it->second.closed) {
            it->second.closed = true;
            if (it->second.refs == 0) { g_tls_reg->erase(it); reap = true; }
        }
    }
    pthread_mutex_unlock(&g_tls_reg_mu);
    if (reap) {
        g_tls.SSL_shutdown(ssl);
        g_tls.SSL_free(ssl);
    }
}

}

extern "C" int westlake_tls_child_register(JNIEnv* env) {
    jclass cls = env->FindClass("adapter/compat/WestlakeSSLSocket");
    if (!cls) return 0;
    JNINativeMethod methods[] = {
#define M(name, sig, fn) {const_cast<char*>(name), const_cast<char*>(sig), reinterpret_cast<void*>(fn)}
        M("nativeHandshake", "(ILjava/lang/String;I)J", WL_TLS_handshake),
        M("nativeRead", "(JI[BIII)I", WL_TLS_read),
        M("nativeWrite", "(JI[BIII)I", WL_TLS_write),
        M("nativePeerCert", "(J)[B", WL_TLS_peerCert),
        M("nativePeerChain", "(J)[B", WL_TLS_peerChain),
        M("nativeInfo", "(JI)Ljava/lang/String;", WL_TLS_info),
        M("nativeClose", "(J)V", WL_TLS_close),
#undef M
    };
    int ok = env->RegisterNatives(cls, methods, sizeof(methods)/sizeof(methods[0])) == JNI_OK;
    env->DeleteLocalRef(cls);
    fprintf(stderr, "[OH_TLS_CHAIN] unified TLS registry installed=%d\n", ok);
    return ok;
}
