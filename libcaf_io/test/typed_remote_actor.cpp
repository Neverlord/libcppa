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

#define CAF_SUITE typed_remote_actor
#include "caf/test/unit_test.hpp"

#include <thread>
#include <string>
#include <cstring>
#include <sstream>
#include <iostream>
#include <functional>

#include "caf/all.hpp"
#include "caf/io/all.hpp"

#include "caf/detail/run_program.hpp"

using namespace std;
using namespace caf;

struct ping {
  int32_t value;
};

bool operator==(const ping& lhs, const ping& rhs) {
  return lhs.value == rhs.value;
}

struct pong {
  int32_t value;

};

bool operator==(const pong& lhs, const pong& rhs) {
  return lhs.value == rhs.value;
}

using server_type = typed_actor<replies_to<ping>::with<pong>>;

using client_type = typed_actor<>;

server_type::behavior_type server() {
  return {
    [](const ping & p) -> pong {
      CAF_MESSAGE("received `ping`");
      return pong{p.value};
    }
  };
}

void run_client(const char* host, uint16_t port) {
  // check whether invalid_argument is thrown
  // when trying to connect to get an untyped
  // handle to the server
  try {
    io::remote_actor(host, port);
  }
  catch (network_error& e) {
    CAF_MESSAGE(e.what());
  }
  CAF_MESSAGE("connect to typed_remote_actor");
  auto serv = io::typed_remote_actor<server_type>(host, port);
  scoped_actor self;
  self->sync_send(serv, ping{42})
    .await([](const pong& p) { CAF_CHECK_EQUAL(p.value, 42); });
  anon_send_exit(serv, exit_reason::user_shutdown);
  self->monitor(serv);
  self->receive([&](const down_msg& dm) {
    CAF_CHECK_EQUAL(dm.reason, exit_reason::user_shutdown);
    CAF_CHECK(dm.source == serv);
  });
}

uint16_t run_server() {
  auto port = io::typed_publish(spawn_typed(server), 0, "127.0.0.1");
  CAF_MESSAGE("running on port " << port);
  return port;
}

CAF_TEST(test_typed_remote_actor) {
  auto argv = caf::test::engine::argv();
  auto argc = caf::test::engine::argc();
  announce<ping>("ping", &ping::value);
  announce<pong>("pong", &pong::value);
  if (argv) {
    message_builder{argv, argv + argc}.apply({
      on("-c", spro<uint16_t>)>> [](uint16_t port) {
        CAF_MESSAGE("run in client mode");
        run_client("localhost", port);
      },
      on("-s") >> [] {
        run_server();
      }
    });
  }
  else {
    auto port = run_server();
    // execute client_part() in a separate process,
    // connected via localhost socket
    scoped_actor self;
    auto child = detail::run_program(self, caf::test::engine::path(),
                                     "-s typed_remote_actor -- -c", port);
    CAF_MESSAGE("block till child process has finished");
    child.join();
    self->await_all_other_actors_done();
    self->receive(
      [](const std::string& output) {
        cout << endl << endl << "*** output of client program ***"
             << endl << output << endl;
      }
    );
  }
  shutdown();
}
