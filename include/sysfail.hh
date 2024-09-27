#ifndef _SYSFAIL_HH
#define _SYSFAIL_HH

#include <chrono>
#include <memory>
#include <map>
#include <functional>
#include <sys/syscall.h>

namespace sysfail {
    using Syscall = int;
    using Errno = int;

    const int OneMillion = 1000000;

    struct Outcome {
        double fail_probability;
        double delay_probability;
        std::chrono::microseconds max_delay;
        std::map<Errno, double> error_weights;
    };

    struct AddrRange;
    struct Plan {
        const std::unordered_map<Syscall, const Outcome> outcomes;
        const std::function<bool(pid_t)> selector;
        Plan(
            const std::unordered_map<Syscall, const Outcome>& _outcomes,
            const std::function<bool(pid_t)>& _selector
        ) : outcomes(_outcomes), selector(_selector) {}
        Plan(Plan&& _plan): outcomes(_plan.outcomes), selector(_plan.selector) {}
        Plan() : outcomes({}), selector([](pid_t) { return false; }) {}
    };
    class Session {
    private:
        char on;
    public:
        // Must not be called only once
        explicit Session(const Plan& _plan);
        ~Session();
    };
}

#endif