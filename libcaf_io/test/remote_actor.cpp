/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2015                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#define CAF_SUITE remote_actor
#include "caf/test/unit_test.hpp"

#include <thread>
#include <string>
#include <cstring>
#include <sstream>
#include <iostream>
#include <functional>

#include "caf/all.hpp"
#include "caf/io/all.hpp"

#include "caf/detail/logging.hpp"
#include "caf/detail/singletons.hpp"
#include "caf/detail/run_program.hpp"

#include "../libcaf_io/test/ping_pong.hpp"

using namespace std;
using namespace caf;

namespace {

using spawn5_done_atom = atom_constant<atom("Spawn5Done")>;
using spawn_ping_atom = atom_constant<atom("SpawnPing")>;
using get_group_atom = atom_constant<atom("GetGroup")>;
using sync_msg_atom = atom_constant<atom("SyncMsg")>;
using ping_ptr_atom = atom_constant<atom("PingPtr")>;
using gclient_atom = atom_constant<atom("GClient")>;
using spawn5_atom = atom_constant<atom("Spawn5")>;
using foo_atom = atom_constant<atom("foo")>;
using bar_atom = atom_constant<atom("bar")>;

atomic<long> s_destructors_called;
atomic<long> s_on_exit_called;

constexpr size_t num_pings = 10;

using string_pair = std::pair<std::string, std::string>;

using actor_vector = vector<actor>;

void reflector(event_based_actor* self) {
  self->become(others >> [=] {
    CAF_MESSAGE("reflect and quit");
    self->quit();
    return self->current_message();
  });
}

void spawn5_server_impl(event_based_actor* self, actor client, group grp) {
  CAF_CHECK(grp != invalid_group);
  self->spawn_in_group(grp, reflector);
  self->spawn_in_group(grp, reflector);
  CAF_MESSAGE("send {'Spawn5'} and await {'ok', actor_vector}");
  self->sync_send(client, spawn5_atom::value, grp).then(
    [=](ok_atom, const actor_vector& vec) {
      CAF_MESSAGE("received vector with " << vec.size() << " elements");
      self->send(grp, "Hello reflectors!", 5.0);
      if (vec.size() != 5) {
        CAF_MESSAGE("remote client did not spawn five reflectors!");
      }
      for (auto& a : vec) {
        CAF_MESSAGE("monitor actor: " << to_string(a));
        self->monitor(a);
      }
      CAF_MESSAGE("wait for reflected messages");
      // receive seven reply messages (2 local, 5 remote)
      auto replies = std::make_shared<int>(0);
      self->become(
        on("Hello reflectors!", 5.0) >> [=] {
          if (++*replies == 7) {
            CAF_MESSAGE("wait for DOWN messages");
            auto downs = std::make_shared<int>(0);
            self->become(
              [=](const down_msg& dm) {
                if (dm.reason != exit_reason::normal) {
                  CAF_TEST_ERROR("reflector exited for non-normal exit reason!");
                }
                if (++*downs == 5) {
                  CAF_MESSAGE("down increased to 5, about to quit");
                  self->send(client, spawn5_done_atom::value);
                  self->quit();
                }
              },
              others >> [=] {
                CAF_TEST_ERROR("Unexpected message: "
                               << to_string(self->current_message()));
                self->quit(exit_reason::user_defined);
              },
              after(chrono::seconds(2)) >> [=] {
                CAF_TEST_ERROR("did only receive " << *downs << " down messages");
                self->quit(exit_reason::user_defined);
              }
            );
          }
        },
        after(std::chrono::seconds(2)) >> [=] {
          CAF_TEST_ERROR("Unexpected timeout");
          self->quit(exit_reason::user_defined);
        }
      );
    },
    others >> [=] {
      CAF_TEST_ERROR("Unexpected message: "
                     << to_string(self->current_message()));
      self->quit(exit_reason::user_defined);
    },
    after(chrono::seconds(10)) >> [=] {
      CAF_TEST_ERROR("Unexpected timeout");
      self->quit(exit_reason::user_defined);
    }
  );
}

// receive seven reply messages (2 local, 5 remote)
void spawn5_server(event_based_actor* self, actor client, bool inverted) {
  if (!inverted) {
    spawn5_server_impl(self, client, group::get("local", "foobar"));
  } else {
    CAF_MESSAGE("request group");
    self->sync_send(client, get_group_atom::value).then(
      [=](const group& remote_group) {
        spawn5_server_impl(self, client, remote_group);
      }
    );
  }
}

void spawn5_client(event_based_actor* self) {
  self->become(
    [](get_group_atom) -> group {
      CAF_MESSAGE("received {'GetGroup'}");
      return group::get("local", "foobar");
    },
    [=](spawn5_atom, const group & grp)->message {
      CAF_MESSAGE("received {'Spawn5'}");
      actor_vector vec;
      for (int i = 0; i < 5; ++i) {
        vec.push_back(spawn_in_group(grp, reflector));
      }
      CAF_MESSAGE("spawned all reflectors");
      return make_message(ok_atom::value, std::move(vec));
    },
    [=](spawn5_done_atom) {
      CAF_MESSAGE("received {'Spawn5Done'}");
      self->quit();
    }
  );
}

template <class F>
void await_down(event_based_actor* self, actor ptr, F continuation) {
  self->become(
    [=](const down_msg& dm) -> optional<skip_message_t> {
      if (dm.source == ptr) {
        continuation();
        return none;
      }
      return skip_message(); // not the 'DOWN' message we are waiting for
    }
  );
}

class client : public event_based_actor {
 public:
  client(actor server) : m_server(std::move(server)) {
    // nop
  }

  behavior make_behavior() override {
    return spawn_ping();
  }

  void on_exit() {
    ++s_on_exit_called;
  }

  ~client() {
    ++s_destructors_called;
  }

 private:
  behavior spawn_ping() {
    CAF_MESSAGE("send {'SpawnPing'}");
    send(m_server, spawn_ping_atom::value);
    return {
      [=](ping_ptr_atom, const actor& ping) {
        CAF_MESSAGE("received ping pointer, spawn pong");
        auto pptr = spawn<monitored + detached + blocking_api>(pong, ping);
        await_down(this, pptr, [=] { send_sync_msg(); });
      }
    };
  }

  void send_sync_msg() {
    CAF_MESSAGE("sync send {'SyncMsg', 4.2fSyncMsg}");
    sync_send(m_server, sync_msg_atom::value, 4.2f).then(
      [=](ok_atom) {
        send_foobars();
      }
    );
  }

  void send_foobars(int i = 0) {
    if (i == 0) {
      CAF_MESSAGE("send foobars");
    }
    if (i == 100)
      test_group_comm();
    else {
      sync_send(m_server, foo_atom::value, bar_atom::value, i).then(
        [=](foo_atom, bar_atom, int res) {
          CAF_CHECK_EQUAL(res, i);
          send_foobars(i + 1);
        }
      );
    }
  }

  void test_group_comm() {
    CAF_MESSAGE("test group communication via network");
    sync_send(m_server, gclient_atom::value).then(
      [=](gclient_atom, actor gclient) {
        CAF_MESSAGE("received " << to_string(current_message()));
        auto s5a = spawn<monitored>(spawn5_server, gclient, false);
        await_down(this, s5a, [=] { test_group_comm_inverted(); });
      }
    );
  }

  void test_group_comm_inverted() {
    CAF_MESSAGE("test group communication via network (inverted setup)");
    become(
      [=](gclient_atom) -> message {
        CAF_MESSAGE("received `gclient_atom`");
        auto cptr = current_sender();
        auto s5c = spawn<monitored>(spawn5_client);
        // set next behavior
        await_down(this, s5c, [=] {
          CAF_MESSAGE("set next behavior");
          quit();
        });
        return make_message(gclient_atom::value, s5c);
      }
    );
  }

  actor m_server;
};

class server : public event_based_actor {
 public:
  behavior make_behavior() override {
    if (m_run_in_loop) {
      trap_exit(true);
    }
    return await_spawn_ping();
  }

  server(bool run_in_loop = false) : m_run_in_loop(run_in_loop) {
    // nop
  }

  void on_exit() {
    ++s_on_exit_called;
  }

  ~server() {
    ++s_destructors_called;
  }

 private:
  behavior await_spawn_ping() {
    CAF_MESSAGE("await {'SpawnPing'}");
    return {
      [=](spawn_ping_atom) -> message {
        CAF_MESSAGE("received {'SpawnPing'}");
        auto client = current_sender();
        if (!client) {
          CAF_MESSAGE("last_sender() invalid!");
        }
        CAF_MESSAGE("spawn event-based ping actor");
        auto pptr = spawn<monitored>(event_based_ping, num_pings);
        CAF_MESSAGE("wait until spawned ping actor is done");
        await_down(this, pptr, [=] {
          CAF_CHECK_EQUAL(pongs(), num_pings);
          become(await_sync_msg());
        });
        return make_message(ping_ptr_atom::value, pptr);
      },
      [](const exit_msg&) {
        // simply ignored if trap_exit is true
      }
    };
  }

  behavior await_sync_msg() {
    CAF_MESSAGE("await {'SyncMsg'}");
    return {
      [=](sync_msg_atom, float f) -> atom_value {
        CAF_MESSAGE("received {'SyncMsg', " << f << "}");
        CAF_CHECK_EQUAL(f, 4.2f);
        become(await_foobars());
        return ok_atom::value;
      },
      [](const exit_msg&) {
        // simply ignored if trap_exit is true
      }
    };
  }

  behavior await_foobars() {
    CAF_MESSAGE("await foobars");
    auto foobars = make_shared<int>(0);
    return {
      [=](foo_atom, bar_atom, int i) -> message {
        ++*foobars;
        if (i == 99) {
          CAF_CHECK_EQUAL(*foobars, 100);
          become(test_group_comm());
        }
        return std::move(current_message());
      },
      [](const exit_msg&) {
        // simply ignored if trap_exit is true
      }
    };
  }

  behavior test_group_comm() {
    CAF_MESSAGE("test group communication via network");
    return {
      [=](gclient_atom) -> message {
        CAF_MESSAGE("received `gclient_atom`");
        auto cptr = current_sender();
        auto s5c = spawn<monitored>(spawn5_client);
        await_down(this, s5c, [=] {
          CAF_MESSAGE("test_group_comm_inverted");
          test_group_comm_inverted(actor_cast<actor>(cptr));
        });
        return make_message(gclient_atom::value, s5c);
      },
      [](const exit_msg&) {
        // simply ignored if trap_exit is true
      }
    };
  }

  void test_group_comm_inverted(actor cptr) {
    CAF_MESSAGE("test group communication via network (inverted setup)");
    sync_send(cptr, gclient_atom::value).then(
      [=](gclient_atom, actor gclient) {
        await_down(this, spawn<monitored>(spawn5_server, gclient, true), [=] {
          CAF_MESSAGE("`await_down` finished");
          if (!m_run_in_loop) {
            quit();
          } else {
            become(await_spawn_ping());
          }
        });
      }
    );
  }

  bool m_run_in_loop;
};

void test_remote_actor(const char* app_path, bool run_remote_actor) {
  scoped_actor self;
  auto serv = self->spawn<server, monitored>(!run_remote_actor);
  // publish on two distinct ports and use the latter one afterwards
  auto port1 = io::publish(serv, 0, "127.0.0.1");
  CAF_CHECK(port1 > 0);
  CAF_MESSAGE("first publish succeeded on port " << port1);
  auto port2 = io::publish(serv, 0, "127.0.0.1");
  CAF_CHECK(port2 > 0);
  CAF_MESSAGE("second publish succeeded on port " << port2);
  // publish local groups as well
  auto gport = io::publish_local_groups(0);
  CAF_CHECK(gport > 0);
  // check whether accessing local actors via io::remote_actors works correctly,
  // i.e., does not return a proxy instance
  auto serv2 = io::remote_actor("127.0.0.1", port2);
  CAF_CHECK(serv2 != invalid_actor && !serv2->is_remote());
  CAF_CHECK(serv == serv2);
  thread child;
  if (run_remote_actor) {
    child = detail::run_program(self, app_path, "-s remote_actor -- -c ", port2,
                                port1, gport);
  } else {
    CAF_MESSAGE("please run client with: "
              << "-c " << port2 << " " << port1 << " " << gport);
  }
  self->receive(
    [&](const down_msg& dm) {
      CAF_CHECK_EQUAL(dm.source, serv);
      CAF_CHECK_EQUAL(dm.reason, exit_reason::normal);
    }
  );
  // wait until separate process (in sep. thread) finished execution
  self->await_all_other_actors_done();
  if (run_remote_actor) {
    child.join();
    self->receive(
      [](const std::string& output) {
        cout << endl << endl << "*** output of client program ***"
             << endl << output << endl;
      }
    );
  }
}

} // namespace <anonymous>

CAF_TEST(test_remote_actor) {
  auto argv = caf::test::engine::argv();
  auto argc = caf::test::engine::argc();
  announce<actor_vector>("actor_vector");
  cout << "this node is: " << to_string(caf::detail::singletons::get_node_id())
       << endl;
  if (argv) {
    message_builder{argv, argv + argc}.apply({
      on("-c", spro<uint16_t>, spro<uint16_t>, spro<uint16_t>)
      >> [](uint16_t p1, uint16_t p2, uint16_t gport) {
        scoped_actor self;
        auto serv = io::remote_actor("localhost", p1);
        auto serv2 = io::remote_actor("localhost", p2);
        // remote_actor is supposed to return the same server
        // when connecting to the same host again
        {
          CAF_CHECK(serv == io::remote_actor("localhost", p1));
          CAF_CHECK(serv2 == io::remote_actor("127.0.0.1", p2));
        }
        // connect to published groups
        io::remote_group("whatever", "127.0.0.1", gport);
        auto c = self->spawn<client, monitored>(serv);
        self->receive(
          [&](const down_msg& dm) {
            CAF_CHECK_EQUAL(dm.source, c);
            CAF_CHECK_EQUAL(dm.reason, exit_reason::normal);
          }
        );
      },
      on("-s") >> [&] {
        CAF_MESSAGE("don't run remote actor (server mode)");
        test_remote_actor(argv[0], false);
      },
      others >> [&] {
        CAF_TEST_ERROR("usage: " << argv[0]
                                 << " [-s PORT|-c PORT1 PORT2 GROUP_PORT]");
      }
    });
  }
  else {
    test_remote_actor(caf::test::engine::path(), true);
  }
  await_all_actors_done();
  shutdown();
  // we either spawn a server or a client, in both cases
  // there must have been exactly one dtor called
  CAF_CHECK_EQUAL(s_destructors_called.load(), 1);
  CAF_CHECK_EQUAL(s_on_exit_called.load(), 1);
}
