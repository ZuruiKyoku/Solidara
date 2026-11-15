#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
#if defined(__x86_64__)
constexpr unsigned int nativeArchitecture = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
constexpr unsigned int nativeArchitecture = AUDIT_ARCH_AARCH64;
#else
#error Offline runner requires Linux x86_64 or aarch64
#endif

bool installSocketFilter() {
    // The executable and its descendants retain Unix-domain sockets for X11,
    // while the kernel rejects new IPv4 and IPv6 sockets before networking.
    sock_filter instructions[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, nativeArchitecture, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
#if defined(__x86_64__)
        // x32 uses the x86_64 audit architecture but a distinct syscall table.
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x40000000U, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
#endif
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_socket, 0, 4),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_INET, 1, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_INET6, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    sock_fprog program{static_cast<unsigned short>(sizeof(instructions) / sizeof(instructions[0])), instructions};
    return prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0
        && prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0;
}

bool internetSocketDenied(int domain) {
    errno = 0;
    const int descriptor = socket(domain, SOCK_STREAM, 0);
    const int error = errno;
    if (descriptor >= 0) close(descriptor);
    return descriptor < 0 && error == EPERM;
}
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: solidara_offline_runner executable [arguments...]\n");
        return 2;
    }
    if (!installSocketFilter()) {
        std::fprintf(stderr, "Cannot enforce offline socket policy: %s\n", std::strerror(errno));
        return 2;
    }
    if (!internetSocketDenied(AF_INET) || !internetSocketDenied(AF_INET6)) {
        std::fprintf(stderr, "Offline policy verification failed: Internet sockets were not rejected with EPERM\n");
        return 2;
    }
    const int localSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (localSocket < 0) {
        std::fprintf(stderr, "Offline policy incorrectly blocks Unix sockets: %s\n", std::strerror(errno));
        return 2;
    }
    close(localSocket);
    std::fprintf(stderr, "Offline policy verified: IPv4/IPv6 socket creation blocked; Unix sockets available\n");
    execvp(argv[1], argv + 1);
    std::fprintf(stderr, "Cannot execute offline test: %s\n", std::strerror(errno));
    return 127;
}
