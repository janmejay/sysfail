#include <gtest/gtest.h>
#include <sysfail.hh>
#include <expected>
#include <chrono>
#include <random>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cisq.hh>

using namespace testing;
using namespace std::chrono_literals;

namespace sysfail {
    TEST(SysFail, LoadSessionWithoutFailureInjection) {
        TmpFile tFile;
        tFile.write("foo bar baz quux");

        sysfail::Session s({});
        auto success = 0;
        for (int i = 0; i < 10; i++) {
            auto r = tFile.read();
            if (r.has_value()) {
                success++;
                EXPECT_EQ(r.value(), "foo bar baz quux");
            }
        }
        EXPECT_EQ(success, 10);
    }

    TEST(SysFail, LoadSessionWithSysReadBlocked) {
        TmpFile tFile;
        tFile.write("foo bar baz quux");

        sysfail::Plan p(
            {
                {SYS_read, {1.0, 0, 0us, {{EIO, 1.0}}}}
            },
            [](pid_t pid) {
                return true;
            }
        );

        sysfail::Session s(p);
        auto success = 0;
        for (int i = 0; i < 10; i++) {
            auto r = tFile.read();
            if (r.has_value()) {
                success++;
                EXPECT_EQ(r.value(), "foo bar baz quux");
            }
        }
        EXPECT_EQ(success, 0);
    }

    TEST(SysFail, SysOpenAndReadFailureInjection) {
        TmpFile tFile;
        tFile.write("foo bar baz quux");

        sysfail::Plan p(
            {
                {SYS_read, {0.33, 0, 0us, {{EIO, 1.0}}}},
                {SYS_openat, {0.25, 0, 0us, {{EINVAL, 1.0}}}}
            },
            [](pid_t pid) {
                return true;
            }
        );

        {
            sysfail::Session s(p);
            auto success = 0;
            for (int i = 0; i < 1000; i++) {
                auto r = tFile.read();
                if (r.has_value()) {
                    success++;
                }
            }
            // _OPEN_AND_READ_ERR_
            // 50 +- 10% (margin of error) around mean 50% expected success rate
            // Read happens after open
            // P(open succeeds) = (1 - 0.25) = 0.75
            // P(read success | open success) = 0.67
            // P(read success) = P(read success | open success) * P (open success) =
            //     0.67 * 0.75 = 0.50
            EXPECT_GT(success, 400);
            EXPECT_LT(success, 600);
        }

        auto success = 0;
        for (int i = 0; i < 100; i++) {
            auto r = tFile.read();
            if (r.has_value()) {
                success++;
            }
        }
        EXPECT_EQ(success, 100);
    }

    TEST(SysFail, SysSlowReadFastWrite) {
        TmpFile tFile;

        sysfail::Plan p(
            {
                {SYS_read, {0, 0.5, 10ms, {}}},
                {SYS_write, {0, 0, 0ms, {}}}
            },
            [](pid_t pid) {
                return true;
            }
        );

        {
            sysfail::Session s(p);
            auto read_tm = 0ns, write_tm = 0ns;
            for (int i = 0; i < 100; i++) {
                auto str = "foo bar " + i;
                auto write_start = std::chrono::system_clock::now();
                tFile.write(str);
                write_tm += std::chrono::system_clock::now() - write_start;
                auto read_start = std::chrono::system_clock::now();
                auto r = tFile.read();
                read_tm += std::chrono::system_clock::now() - read_start;
                EXPECT_EQ(r.value(), str);
            }
            EXPECT_GT(static_cast<double>(read_tm.count())/write_tm.count(), 2);
        }

        auto read_tm = 0ns, write_tm = 0ns;
        for (int i = 0; i < 100; i++) {
            auto str = "baz quux " + i;
            auto write_start = std::chrono::system_clock::now();
            tFile.write(str);
            write_tm += std::chrono::system_clock::now() - write_start;
            auto read_start = std::chrono::system_clock::now();
            auto r = tFile.read();
            read_tm += std::chrono::system_clock::now() - read_start;
            EXPECT_EQ(r.value(), str);
        }

        EXPECT_LT(static_cast<double>(read_tm.count())/write_tm.count(), 1);
    }

    TEST(SysFail, SeveralThreadsTest) {
        TmpFile f;
        f.write("foo");

        auto test_thd = gettid();

        sysfail::Plan p(
            {
                {SYS_read, {0.33, 0, 0us, {{EIO, 1.0}}}},
                {SYS_openat, {0.25, 0, 0us, {{EINVAL, 1.0}}}},
                {SYS_write, {0.8, 0, 0us, {{EINVAL, 1.0}}}}
            },
            [test_thd](pid_t tid) {
                return tid % 2 == 0 && tid != test_thd;
            }
        );

        const auto thds = 10;
        const auto attempts = 1000;

        std::random_device rd;
        thread_local std::mt19937 rnd_eng(rd());

        struct Result {
            pid_t thd_id;
            int success;
            bool reader;
        };

        struct Results {
            std::vector<Result> result;
            std::mutex mtx;
        };

        Results r;
        {
            Session s(p);
            std::vector<std::thread> threads;
            std::atomic<long> w_ctr = 0;
            for (auto i = 0; i < 10; i++) {
                std::binomial_distribution<bool> rw_dist;
                auto reader = rw_dist(rnd_eng);
                auto disable_explicitly = rw_dist(rnd_eng);
                threads.push_back(std::thread([&]() {
                    s.add();
                    int success = 0;
                    for (auto a = 0; a < attempts; a++) {
                        if (reader) {
                            auto r = f.read();
                            if (r.has_value()) {
                                EXPECT_TRUE(
                                    r.value() == "foo" ||
                                    r.value().empty() ||
                                    r.value().starts_with("bar-"))
                                    << "value: " << r.value();
                                success++;
                            }
                        } else {
                            auto w = f.write(
                                    std::string("bar-") +
                                    std::to_string(w_ctr.fetch_add(1)));
                            if (w.has_value()) {
                                success++;
                            }
                        }
                    }
                    std::lock_guard<std::mutex> l(r.mtx);
                    r.result.emplace_back(
                        gettid(),
                        success,
                        reader);
                    if (disable_explicitly) {
                        s.remove();
                    }
                }));
            }
            for (auto& t : threads) {
                t.join();
            }
        }

        for (const auto& r : r.result) {
            if (r.thd_id % 2 == 0) {
                if (r.reader) {
                    // refer _OPEN_AND_READ_ERR_ above
                    EXPECT_GT(r.success, 0.4 * attempts);
                    EXPECT_LT(r.success, 0.6 * attempts);
                } else {
                    // 0.75 * 0.8 = 0.15 (similar to _OPEN_AND_READ_ERR_) +-
                    // 0.05 margin of error
                    EXPECT_GT(r.success, 0.1 * attempts);
                    EXPECT_LT(r.success, 0.2 * attempts);
                }
            } else { // we didn't inject failures for odd threads
                EXPECT_EQ(r.success, attempts);
            }
        }
    }

    void elapse(std::chrono::microseconds us) {
        struct timespec start, current;
        long elapsed_time;

        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start) == -1) {
            throw new std::runtime_error("Failed to get starting time");
        }

        do {
            if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &current) == -1) {
                throw new std::runtime_error("Failed to get current time");
            }

            elapsed_time = (current.tv_sec - start.tv_sec) * 1e6 + (current.tv_nsec - start.tv_nsec) / 1e3;
        } while (us.count() > elapsed_time);
    }

    TEST(SysFail, SysfailDisable) {
        TmpFile f;
        f.write("foo");

        auto test_thd = gettid();

        sysfail::Plan p(
            {
                {SYS_read, {1.0, 0, 0us, {{EIO, 1.0}}}},
            },
            [](pid_t tid) {
                return true;
            }
        );

        std::binary_semaphore chk_sem(1), start_sem(1);

        sysfail::Session s(p);
        std::thread t([&]() {
            start_sem.release();
            s.add();
            auto r = f.read();
            EXPECT_FALSE(r.has_value());

            // the main thread only turned off sysfail for itself, not for
            // other threads
            chk_sem.acquire();
            r = f.read();
            EXPECT_FALSE(r.has_value());

            chk_sem.release();
        });
        // wait for failure-injection to start again after clone3
        // elapse(1ms);

        start_sem.acquire();
        auto r = f.read();
        EXPECT_FALSE(r.has_value());
        s.remove();

        chk_sem.release();
        r = f.read();
        EXPECT_TRUE(r.has_value());
        chk_sem.acquire();

        s.add();
        r = f.read();
        EXPECT_FALSE(r.has_value());
        t.join();
    }
}
