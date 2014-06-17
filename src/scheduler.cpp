/******************************************************************************\
 *           ___        __                                                    *
 *          /\_ \    __/\ \                                                   *
 *          \//\ \  /\_\ \ \____    ___   _____   _____      __               *
 *            \ \ \ \/\ \ \ '__`\  /'___\/\ '__`\/\ '__`\  /'__`\             *
 *             \_\ \_\ \ \ \ \L\ \/\ \__/\ \ \L\ \ \ \L\ \/\ \L\.\_           *
 *             /\____\\ \_\ \_,__/\ \____\\ \ ,__/\ \ ,__/\ \__/.\_\          *
 *             \/____/ \/_/\/___/  \/____/ \ \ \/  \ \ \/  \/__/\/_/          *
 *                                          \ \_\   \ \_\                     *
 *                                           \/_/    \/_/                     *
 *                                                                            *
 * Copyright (C) 2011 - 2014                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the Boost Software License, Version 1.0. See             *
 * accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt  *
\******************************************************************************/


#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <condition_variable>

#include "cppa/on.hpp"
#include "cppa/policy.hpp"
#include "cppa/logging.hpp"
#include "cppa/anything.hpp"
#include "cppa/to_string.hpp"
#include "cppa/scheduler.hpp"
#include "cppa/local_actor.hpp"
#include "cppa/scoped_actor.hpp"
#include "cppa/system_messages.hpp"

#include "cppa/detail/proper_actor.hpp"
#include "cppa/detail/actor_registry.hpp"
#include "cppa/detail/singleton_manager.hpp"

using std::move;

namespace cppa {

namespace scheduler {

/******************************************************************************
 *                     utility and implementation details                     *
 ******************************************************************************/

namespace {

typedef std::uint32_t ui32;

typedef std::chrono::high_resolution_clock hrc;

typedef hrc::time_point time_point;

typedef policy::policies<policy::no_scheduling, policy::not_prioritizing,
                         policy::no_resume, policy::nestable_invoke>
        timer_actor_policies;

class delayed_msg {

 public:

    delayed_msg(message_header&& arg1,
                any_tuple&&      arg2)
    : hdr(move(arg1)), msg(move(arg2)) { }

    delayed_msg(delayed_msg&&) = default;
    delayed_msg(const delayed_msg&) = default;
    delayed_msg& operator=(delayed_msg&&) = default;
    delayed_msg& operator=(const delayed_msg&) = default;

    inline void eval() {
        hdr.deliver(std::move(msg));
    }

 private:

    message_header hdr;
    any_tuple      msg;

};

template<class Map>
inline void insert_dmsg(Map& storage, const util::duration& d,
                        message_header&& hdr, any_tuple&& tup) {
    auto tout = hrc::now();
    tout += d;
    delayed_msg dmsg{move(hdr), move(tup)};
    storage.insert(std::make_pair(std::move(tout), std::move(dmsg)));
}

class timer_actor final : public detail::proper_actor<blocking_actor,
                                                      timer_actor_policies> {

 public:

    inline unique_mailbox_element_pointer dequeue() {
        await_data();
        return next_message();
    }

    inline unique_mailbox_element_pointer try_dequeue(const time_point& tp) {
        if (scheduling_policy().await_data(this, tp)) {
            return next_message();
        }
        return unique_mailbox_element_pointer{};
    }

    void act() override {
        // setup & local variables
        bool done = false;
        unique_mailbox_element_pointer msg_ptr;
        auto tout = hrc::now();
        std::multimap<decltype(tout), delayed_msg> messages;
        // message handling rules
        auto mfun = (
            on(atom("SEND"), arg_match) >> [&](const util::duration& d,
                                               message_header& hdr,
                                               any_tuple& tup) {
                insert_dmsg(messages, d, move(hdr), move(tup));
            },
            on(atom("DIE")) >> [&] {
                done = true;
            },
            others() >> [&]() {
                CPPA_LOG_WARNING("coordinator::timer_loop: UNKNOWN MESSAGE: "
                                 << to_string(msg_ptr->msg));
            }
        );
        // loop
        while (!done) {
            while (!msg_ptr) {
                if (messages.empty()) msg_ptr = dequeue();
                else {
                    tout = hrc::now();
                    // handle timeouts (send messages)
                    auto it = messages.begin();
                    while (it != messages.end() && (it->first) <= tout) {
                        it->second.eval();
                        messages.erase(it);
                        it = messages.begin();
                    }
                    // wait for next message or next timeout
                    if (it != messages.end()) {
                        msg_ptr = try_dequeue(it->first);
                    }
                }
            }
            mfun(msg_ptr->msg);
            msg_ptr.reset();
        }
    }

};

void printer_loop(blocking_actor* self) {
    std::map<actor_addr, std::string> out;
    auto flush_output = [&out](const actor_addr& s) {
        auto i = out.find(s);
        if (i != out.end()) {
            auto& line = i->second;
            if (!line.empty()) {
                std::cout << line << std::flush;
                line.clear();
            }
        }
    };
    auto flush_if_needed = [](std::string& str) {
        if (str.back() == '\n') {
            std::cout << str << std::flush;
            str.clear();
        }
    };
    bool running = true;
    self->receive_while (gref(running)) (
        on(atom("add"), arg_match) >> [&](std::string& str) {
            auto s = self->last_sender();
            if (!str.empty() && s) {
                auto i = out.find(s);
                if (i == out.end()) {
                    i = out.insert(make_pair(s, move(str))).first;
                    // monitor actor to flush its output on exit
                    self->monitor(s);
                    flush_if_needed(i->second);
                }
                else {
                    auto& ref = i->second;
                    ref += move(str);
                    flush_if_needed(ref);
                }
            }
        },
        on(atom("flush")) >> [&] {
            flush_output(self->last_sender());
        },
        on_arg_match >> [&](const down_msg& dm) {
            flush_output(dm.source);
            out.erase(dm.source);
        },
        on(atom("DIE")) >> [&] {
            running = false;
        },
        others() >> [&] {
            CPPA_LOGF_WARNING("unexpected message: "
                              << to_string(self->last_dequeued()));
        }
    );
}

} // namespace <anonymous>

/******************************************************************************
 *                      implementation of coordinator                         *
 ******************************************************************************/

class coordinator::shutdown_helper : public resumable {

 public:

    void attach_to_scheduler() override { }

    void detach_from_scheduler() override { }

    resumable::resume_result resume(detail::cs_thread*, execution_unit* ptr) {
        CPPA_LOG_DEBUG("shutdown_helper::resume => shutdown worker");
        auto wptr = dynamic_cast<worker*>(ptr);
        CPPA_REQUIRE(wptr != nullptr);
        std::unique_lock<std::mutex> guard(mtx);
        last_worker = wptr;
        cv.notify_all();
        return resumable::shutdown_execution_unit;
    }

    shutdown_helper() : last_worker(nullptr) { }

    ~shutdown_helper();

    std::mutex mtx;
    std::condition_variable cv;
    worker* last_worker;

};

// avoid weak-vtables warning by providing dtor out-of-line
coordinator::shutdown_helper::~shutdown_helper() { }

void coordinator::initialize() {
    // launch threads of utility actors
    auto ptr = m_timer.get();
    m_timer_thread = std::thread([ptr](){
        ptr->act();
    });
    m_printer_thread = std::thread{printer_loop, m_printer.get()};
    // create & start workers
    auto hwc = static_cast<size_t>(std::thread::hardware_concurrency());
    m_workers.resize(hwc);
    for (size_t i = 0; i < hwc; ++i) {
        m_workers[i].start(i, this);
    }
}

void coordinator::destroy() {
    CPPA_LOG_TRACE("");
    // shutdown workers
    shutdown_helper sh;
    std::vector<worker*> alive_workers;
    for (auto& w : m_workers) alive_workers.push_back(&w);
    CPPA_LOG_DEBUG("enqueue shutdown_helper into each worker");
    while (!alive_workers.empty()) {
        alive_workers.back()->external_enqueue(&sh);
        // since jobs can be stolen, we cannot assume that we have
        // actually shut down the worker we've enqueued sh to
        { // lifetime scope of guard
            std::unique_lock<std::mutex> guard(sh.mtx);
            sh.cv.wait(guard, [&]{ return sh.last_worker != nullptr; });
        }
        auto first = alive_workers.begin();
        auto last = alive_workers.end();
        auto i = std::find(first, last, sh.last_worker);
        sh.last_worker = nullptr;
        alive_workers.erase(i);
    }
    // shutdown utility actors
    CPPA_LOG_DEBUG("send 'DIE' messages to timer & printer");
    auto msg = make_any_tuple(atom("DIE"));
    m_timer->enqueue({invalid_actor_addr, nullptr}, msg, nullptr);
    m_printer->enqueue({invalid_actor_addr, nullptr}, msg, nullptr);
    CPPA_LOG_DEBUG("join threads of utility actors");
    m_timer_thread.join();
    m_printer_thread.join();
    // join each worker thread for good manners
    CPPA_LOG_DEBUG("join threads of workers");
    for (auto& w : m_workers) w.m_this_thread.join();
    CPPA_LOG_DEBUG("detach all resumables from all workers");
    for (auto& w : m_workers) {
        auto next = [&] { return w.m_exposed_queue.try_pop(); };
        for (auto job = next(); job != nullptr; job = next()) {
            job->detach_from_scheduler();
        }
    }
    // cleanup
    delete this;
}

coordinator::coordinator()
: m_timer(new timer_actor), m_printer(true) , m_next_worker(0) { }

coordinator* coordinator::create_singleton() {
    return new coordinator;
}

actor coordinator::printer() const {
    return m_printer.get();
}

void coordinator::enqueue(resumable* what) {
    size_t nw = m_next_worker++;
    m_workers[nw % m_workers.size()].external_enqueue(what);
}

/******************************************************************************
 *                          implementation of worker                          *
 ******************************************************************************/

#define CPPA_LOG_DEBUG_WORKER(msg)                                             \
    CPPA_LOG_DEBUG("worker " << m_id << ": " << msg)

worker::worker(worker&& other) {
    *this = std::move(other); // delegate to move assignment operator
}

worker& worker::operator=(worker&& other) {
    // cannot be moved once m_this_thread is up and running
    auto running = [](std::thread& t) {
        return t.get_id() != std::thread::id{};
    };
    if (running(m_this_thread) || running(other.m_this_thread)) {
        throw std::runtime_error("running workers cannot be moved");
    }
    m_job_list = std::move(other.m_job_list);
    auto next = [&] { return other.m_exposed_queue.try_pop(); };
    for (auto j = next(); j != nullptr; j = next()) {
        m_exposed_queue.push_back(j);
    }
    return *this;
}

void worker::start(size_t id, coordinator* parent) {
    m_id = id;
    m_last_victim = id;
    m_parent = parent;
    auto this_worker = this;
    m_this_thread = std::thread([this_worker](){
        this_worker->run();
    });
}

void worker::run() {
    CPPA_LOG_TRACE(CPPA_ARG(m_id));
    // local variables
    detail::cs_thread fself;
    job_ptr job = nullptr;
    // some utility functions
    auto local_poll = [&]() -> bool {
        if (!m_job_list.empty()) {
            job = m_job_list.back();
            m_job_list.pop_back();
            CPPA_LOG_DEBUG_WORKER("got job from m_job_list");
            return true;
        }
        return false;
    };
    auto aggressive_poll = [&]() -> bool {
        for (int i = 1; i < 101; ++i) {
            job = m_exposed_queue.try_pop();
            if (job) {
                CPPA_LOG_DEBUG_WORKER("got job with aggressive polling");
                return true;
            }
            // try to steal every 10 poll attempts
            if ((i % 10) == 0) {
                job = raid();
                if (job) {
                    CPPA_LOG_DEBUG_WORKER("got job with aggressive polling");
                    return true;
                }
            }
            std::this_thread::yield();
        }
        return false;
    };
    auto moderate_poll = [&]() -> bool {
        for (int i = 1; i < 550; ++i) {
            job =  m_exposed_queue.try_pop();
            if (job) {
                CPPA_LOG_DEBUG_WORKER("got job with moderate polling");
                return true;
            }
            // try to steal every 5 poll attempts
            if ((i % 5) == 0) {
                job = raid();
                if (job) {
                    CPPA_LOG_DEBUG_WORKER("got job with moderate polling");
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        return false;
    };
    auto relaxed_poll = [&]() -> bool {
        for (;;) {
            job =  m_exposed_queue.try_pop();
            if (job) {
                CPPA_LOG_DEBUG_WORKER("got job with relaxed polling");
                return true;
            }
            // always try to steal at this stage
            job = raid();
            if (job) {
                CPPA_LOG_DEBUG_WORKER("got job with relaxed polling");
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };
    // scheduling loop
    for (;;) {
        local_poll() || aggressive_poll() || moderate_poll() || relaxed_poll();
        CPPA_PUSH_AID_FROM_PTR(dynamic_cast<abstract_actor*>(job));
        switch (job->resume(&fself, this)) {
            case resumable::done: {
                job->detach_from_scheduler();
                break;
            }
            case resumable::resume_later: {
                break;
            }
            case resumable::shutdown_execution_unit: {
                // give others the opportunity to steal unfinished jobs
                for (auto ptr : m_job_list) m_exposed_queue.push_back(ptr);
                m_job_list.clear();
                return;
            }
        }
        job = nullptr;
        // give others the opportunity to steal from us
        if (m_job_list.size() > 1 && m_exposed_queue.empty()) {
            m_exposed_queue.push_back(m_job_list.front());
            m_job_list.erase(m_job_list.begin());
        }
    }
}

worker::job_ptr worker::try_steal() {
    return m_exposed_queue.try_pop();
}

worker::job_ptr worker::raid() {
    // try once to steal from anyone
    auto inc = [](size_t arg) -> size_t { return arg + 1; };
    auto dec = [](size_t arg) -> size_t { return arg - 1; };
    // reduce probability of 'steal collisions' by letting
    // half the workers pick victims by increasing IDs and
    // the other half by decreasing IDs
    size_t (*next)(size_t) = (m_id % 2) == 0 ? inc : dec;
    auto n = m_parent->num_workers();
    for (size_t i = 0; i < n; ++i) {
        m_last_victim = next(m_last_victim) % n;
        if (m_last_victim != m_id) {
            auto job = m_parent->worker_by_id(m_last_victim).try_steal();
            if (job) {
                CPPA_LOG_DEBUG_WORKER("successfully stolen a job from "
                                      << m_last_victim);
                return job;
            }
        }
    }
    return nullptr;
}

void worker::external_enqueue(job_ptr ptr) {
    m_exposed_queue.push_back(ptr);
}

void worker::exec_later(job_ptr ptr) {
    CPPA_REQUIRE(std::this_thread::get_id() == m_this_thread.get_id());
    // give others the opportunity to steal from us
    if (m_exposed_queue.empty()) {
        if (m_job_list.empty()) {
            m_exposed_queue.push_back(ptr);
        }
        else {
            m_exposed_queue.push_back(m_job_list.front());
            m_job_list.erase(m_job_list.begin());
            m_job_list.push_back(ptr);
        }
    }
    else m_job_list.push_back(ptr);
}

} // namespace scheduler

} // namespace cppa
