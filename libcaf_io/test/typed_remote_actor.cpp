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

#include "caf/config.hpp"

#define CAF_SUITE io_typed_remote_actor
#include "caf/test/unit_test.hpp"

#include <thread>
#include <string>
#include <cstring>
#include <sstream>
#include <iostream>
#include <functional>

#include "caf/all.hpp"
#include "caf/io/all.hpp"

using namespace std;
using namespace caf;

struct ping {
  int32_t value;
};

template <class T>
void serialize(T& in_or_out, ping& x, const unsigned int) {
  in_or_out & x.value;
}

bool operator==(const ping& lhs, const ping& rhs) {
  return lhs.value == rhs.value;
}

struct pong {
  int32_t value;
};

template <class T>
void serialize(T& in_or_out, pong& x, const unsigned int) {
  in_or_out & x.value;
}

bool operator==(const pong& lhs, const pong& rhs) {
  return lhs.value == rhs.value;
}

using server_type = typed_actor<replies_to<ping>::with<pong>>;

using client_type = typed_actor<>;

server_type::behavior_type server() {
  return {
    [](const ping & p) -> pong {
      CAF_CHECK_EQUAL(p.value, 42);
      return pong{p.value};
    }
  };
}

void run_client(int argc, char** argv, uint16_t port) {
  actor_system_config cfg{argc, argv};
  cfg.load<io::middleman>()
     .add_message_type<ping>("ping")
     .add_message_type<pong>("pong");
  actor_system system{cfg};
  // check whether invalid_argument is thrown
  // when trying to connect to get an untyped
  // handle to the server
  try {
    system.middleman().remote_actor("127.0.0.1", port);
  }
  catch (network_error& e) {
    CAF_MESSAGE(e.what());
  }
  CAF_MESSAGE("connect to typed_remote_actor");
  auto serv = system.middleman().typed_remote_actor<server_type>("127.0.0.1",
                                                                 port);
  CAF_REQUIRE(serv);
  scoped_actor self{system};
  self->request(serv, ping{42})
    .receive([](const pong& p) { CAF_CHECK_EQUAL(p.value, 42); });
  anon_send_exit(serv, exit_reason::user_shutdown);
  self->monitor(serv);
  self->receive([&](const down_msg& dm) {
    CAF_CHECK_EQUAL(dm.reason, exit_reason::user_shutdown);
    CAF_CHECK(dm.source == serv);
  });
}

void run_server(int argc, char** argv) {
  actor_system_config cfg{argc, argv};
  cfg.load<io::middleman>()
     .add_message_type<ping>("ping")
     .add_message_type<pong>("pong");
  actor_system system{cfg};
  auto mport = system.middleman().publish(system.spawn(server), 0, "127.0.0.1");
  CAF_REQUIRE(mport);
  auto port = *mport;
  CAF_MESSAGE("running on port " << port << ", start client");
  std::thread child{[=] { run_client(argc, argv, port); }};
  child.join();
}

CAF_TEST(test_typed_remote_actor) {
  auto argc = test::engine::argc();
  auto argv = test::engine::argv();
  run_server(argc, argv);
}
