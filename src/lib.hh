#ifndef _LIB_H
#define _LIB_H

#include <iostream>
#include <sys/prctl.h>
#include <sys/prctl.h>
#include <ucontext.h>
#include <cassert>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <random>
#include <thread>
#include <linux/unistd.h>
#include <oneapi/tbb/concurrent_hash_map.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>

#include "sysfail.hh"
#include "sysfail.h"
#include "map.hh"
#include "syscall.hh"
#include "log.hh"

extern "C" {
    extern void sysfail_restore(greg_t*);
}

namespace sysfail {
    void continue_syscall(ucontext_t *ctx);

    static void handle_sigsys(int sig, siginfo_t *info, void *ucontext);
    static void enable_sysfail(int sig, siginfo_t *info, void *ucontext);

    struct ActiveOutcome {
        double fail_p;
        double delay_p;
        std::chrono::microseconds max_delay;
        std::map<double, Errno> error_by_cumulative_p;

        ActiveOutcome(const Outcome& _o);
    };

    struct ActivePlan {
        std::unordered_map<Syscall, const ActiveOutcome> outcomes;
        const std::function<bool(pid_t)> selector;

        ActivePlan(const Plan& _plan);
    };

    using signal_t = int;
    using sigaction_t = void (*) (int, siginfo_t *, void *);

    void enable_handler(signal_t signal, sigaction_t hdlr);

    struct ThdState {
        char on;
    };

    using ThdSt = oneapi::tbb::concurrent_hash_map<pid_t, ThdState>;

    const int SIG_ARM = SIGRTMIN + 4;
    const int SIG_REARM = SIGRTMIN + 5;

    struct ActiveSession {
        ActivePlan plan;
        AddrRange self_text;
        volatile char on;
        std::random_device rd;
        ThdSt thd_st;

        ActiveSession(const Plan& _plan, AddrRange&& _self_addr);

        pid_t disarm();

        void rearm();

        void thd_enable();

        void thd_disable();

        void fail_maybe(ucontext_t *ctx);

    // TODO: refactor to support adding / removing other threads
    //   private:
    //     void thd_enable(pid_t tid);

    //     void thd_disable(pid_t tid);
    };

    std::shared_ptr<ActiveSession> session = nullptr;
}

#endif