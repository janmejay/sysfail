/*
 * Copyright © 2024 Rubrik, Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <chrono>
#include <thread>

#include "thdmon.hh"
#include "helpers.hh"

namespace fs = std::filesystem;

sysfail::ThdMon::ThdMon(
    const thread_discovery::Strategy& config,
    ThdEvtHdlr handler
) : handler(handler) {
    // Found the hard way that inotify does not work for /proc
    // so for now we poll!
    // TODO: replace this with netlink cn_proc based monitoring

    if (! fs::exists(tasks_dir)) {
        std::string msg("Couldn't find process' task-dir: ");
        msg += tasks_dir.string();
        throw std::runtime_error(msg);
    }

    std::visit(cases(
        [&](const thread_discovery::ProcPoll& p) {
            poll_itvl = p.itvl;

            poller_thd = std::thread(&ThdMon::process, this);
            poll_initialized.acquire();
        },
        [&](const thread_discovery::None& n) {
            scan_tasks();
        }),
        config);
}

sysfail::ThdMon::~ThdMon() {
    if (poller_thd.joinable()) {
        {
            std::lock_guard<std::mutex> l(stop_ctrl.stop_mtx);
            stop_ctrl.stop = true;
            stop_ctrl.stop_cv.notify_one();
        }
        poller_thd.join();
    }
}

void sysfail::ThdMon::process() {
    using namespace std::chrono_literals;

    pid_t self = gettid();
    known_thds.insert({self, gen});
    handler(self, DiscThdSt::Self);
    bool run = true;
    std::unique_lock<std::mutex> l(stop_ctrl.stop_mtx);
    for (; run; gen++) {
        scan_tasks();
        if (gen == 0) {
            poll_initialized.release();
        }
        stop_ctrl.stop_cv.wait_for(
            l,
            poll_itvl,
            [&](){ return stop_ctrl.stop; });
        run = ! stop_ctrl.stop;
    }
}

void sysfail::ThdMon::scan_tasks() {
    for (const auto& entry : fs::directory_iterator(tasks_dir)) {
        auto name = entry.path().filename().string();
        pid_t tid = std::stoi(name);
        auto it = known_thds.find(tid);
        if (it == known_thds.end()) {
            handler( tid, gen == 0 ? DiscThdSt::Existing : DiscThdSt::Spawned);
            known_thds.insert({tid, gen});
        } else {
            it->second = gen;
        }
    }
    std::vector<pid_t> to_remove;
    for (auto [tid, g] : known_thds) {
        if (g < gen) {
            to_remove.push_back(tid);
        }
    }
    for (auto tid : to_remove) {
        handler(tid, DiscThdSt::Terminated);
        known_thds.erase(tid);
    }
}

void sysfail::ThdMon::rescan_threads() {
    if (poller_thd.joinable()) {
        std::lock_guard<std::mutex> l(stop_ctrl.stop_mtx);
        gen++;
        scan_tasks();
    } else {
        scan_tasks();
    }
}