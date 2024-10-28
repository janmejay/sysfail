#include <mutex>
#include <string>
#include <sys/syscall.h>
#include <expected>
#include <stdexcept>

// C-isq is needed because libc wrappers often do a completely different thing
// than expectd. Initial version of this library had tests that had larger
// tolerances because of this. The test expected SYS_read to be called whereas
// libc had the file mmap-ed.
// This makes behavior predictable and hence tests become reliable.
// Impl still calls libc-wrappers, but of need be we can call dispatch syscalls
// directly like sysfail SIGSYS handler does, because this is built as a
// shared-lib too and its syscalls will be intercepted just like libc's.
namespace Cisq {
    class Err : public std::runtime_error {
        int op_errno;

    public:
        Err(const char* msg, int op_errno);

        Err& operator=(const Err& other);

        int err() const;
    };

    std::string err_msg(const char* msg, int op_errno);

    struct TmpFile {
        const std::string path;
        std::mutex m;

        TmpFile();

        ~TmpFile();

        std::expected<std::string, Err> read();

        std::expected<void, Err> write(const std::string& content);
    };

    template <typename T> struct Pipe {
        int rd_fd, wr_fd;

        Pipe() {
            int fds[2];
            auto ret = pipe2(fds, O_DIRECT);
            if (ret < 0) {
                throw Err("Failed to create pipe", errno);
            }
            rd_fd = fds[0];
            wr_fd = fds[1];
            ret = fcntl(wr_fd, F_SETPIPE_SZ, 1 * 1024 * 1024);
            if (ret < 0) {
                throw Err("Failed to set pipe size", errno);
            }
        }

        ~Pipe() {
            syscall(SYS_close, rd_fd);
            syscall(SYS_close, wr_fd);
        }

        std::expected<T, Err> read() {
            T t;
            auto bytes = syscall(SYS_read, rd_fd, &t, sizeof(t));
            if (bytes < 0) {
                return std::unexpected(Err("Failed to read pipe", errno));
            }
            return t;
        }

        std::expected<int, Err> write(const T& t) {
            auto bytes = syscall(SYS_write, wr_fd, &t, sizeof(t));

            if (bytes < 0) {
                return std::unexpected(Err("Failed to write file", errno));
            }

            return static_cast<int>(bytes);
        }
    };

    class AsyncRead {
        const int fd;

    public:
        AsyncRead(const int fd);

        ~AsyncRead();
    };

    std::expected<std::chrono::system_clock::time_point, Err> tm_adjtimex();
}

