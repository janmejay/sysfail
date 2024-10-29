#ifndef _SYSFAIL_HH
#define _SYSFAIL_HH

#include <chrono>
#include <memory>
#include <map>
#include <functional>
#include <shared_mutex>
#include <sys/syscall.h>
#include <chrono>
#include <variant>
#include <signal.h>
#include <stdexcept>

namespace sysfail {
    using Syscall = int;
    using Errno = int;
    using Signal = int;

    struct Probability {
        const double p;          // [0, 1] 0 => never fail, 1 => always fail
        const double after_bias; // [0, 1] 0 => before syscall, 1 => after syscall
        Probability(
            double p,
            double after_bias = 0
        ) : p(p), after_bias(after_bias) {
            if (p < 0 || p > 1) {
                throw std::invalid_argument("Probability must be in [0, 1]");
            }
            if (after_bias < 0 || after_bias > 1) {
                throw std::invalid_argument("Bias must be in [0, 1]");
            }
        }
    };

    using InvocationPredicate = const std::function<bool(const greg_t*)>;

    namespace invp {
        using A = greg_t;
        using Zero = std::function<bool(Syscall)>;
        using One = std::function<bool(Syscall, A)>;
        using Two = std::function<bool(Syscall, A, A)>;
        using Three = std::function<bool(Syscall, A, A, A)>;
        using Four = std::function<bool(Syscall, A, A, A, A)>;
        using Five = std::function<bool(Syscall, A, A, A, A, A)>;
        using Six = std::function<bool(Syscall, A, A, A, A, A, A)>;

        using P = std::variant<Zero, One, Two, Three, Four, Five, Six>;

        InvocationPredicate p(P p);
    }

    struct Outcome {
        const Probability fail;
        const Probability delay;
        const std::chrono::microseconds max_delay;
        const std::map<Errno, double> error_weights;
        InvocationPredicate eligible;
    };

    struct AddrRange;

    namespace thread_discovery {
        using namespace std::chrono_literals;

        // Poll /proc/<pid>/task every itvl
        struct ProcPoll {
            const std::chrono::microseconds itvl;

            ProcPoll( std::chrono::microseconds itvl = 10ms) : itvl(itvl) {}
        };

        // Either add / remove threads manually or use Session API to trigger a
        // single isolated poll of /proc/<pid>/task
        struct None {};

        using Strategy = std::variant<ProcPoll, None>;
    }

    struct Plan {
        const std::unordered_map<Syscall, const Outcome> outcomes;
        const std::function<bool(pid_t)> selector;
        const thread_discovery::Strategy thd_disc;
        Plan(
            const std::unordered_map<Syscall, const Outcome>& outcomes,
            const std::function<bool(pid_t)>& selector,
            const thread_discovery::Strategy& thd_disc
        ) : outcomes(outcomes), selector(selector), thd_disc(thd_disc) {}
        Plan(const Plan& plan):
            outcomes(plan.outcomes),
            selector(plan.selector),
            thd_disc(plan.thd_disc) {}
        Plan() :
            outcomes({}),
            selector([](pid_t) { return false; }),
            thd_disc(thread_discovery::None{}) {}
    };

    class Session {
        std::shared_mutex lck;
    public:
        explicit Session(const Plan& _plan);
        ~Session();
        void add();
        void remove();
        void add(pid_t tid);
        void remove(pid_t tid);
        void discover_threads();
    };
}

#endif