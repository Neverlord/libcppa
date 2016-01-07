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

#define CAF_SUITE composed_actor
#include "caf/test/unit_test.hpp"

#include "caf/all.hpp"

using namespace caf;

namespace {

behavior dbl_bhvr(event_based_actor* self) {
  return {
    [](int v) {
      return 2 * v;
    },
    [=] {
      self->quit();
    }
  };
}

using first_stage = typed_actor<replies_to<int>::with<double, double>>;
using second_stage = typed_actor<replies_to<double, double>::with<double>>;

first_stage::behavior_type first_stage_impl() {
  return [](int i) {
    return std::make_tuple(i * 2.0, i * 4.0);
  };
};

second_stage::behavior_type second_stage_impl() {
  return [](double x, double y) {
    return x * y;
  };
}

struct fixture {
  void wait_until_exited() {
    self->receive(
      [](const down_msg&) {
        CAF_CHECK(true);
      }
    );
  }

  template <class Actor>
  static bool exited(const Actor& handle) {
    auto ptr = actor_cast<abstract_actor*>(handle);
    auto dptr = dynamic_cast<monitorable_actor*>(ptr);
    CAF_REQUIRE(dptr != nullptr);
    return dptr->exited();
  }

  actor_system system;
  scoped_actor self{ system, true };
};

} // namespace <anonymous>

CAF_TEST_FIXTURE_SCOPE(bound_actor_tests, fixture)

CAF_TEST(identity) {
  actor_system system_of_g;
  actor_system system_of_f;
  auto g = system_of_g.spawn(first_stage_impl);
  auto f = system_of_f.spawn(second_stage_impl);
  CAF_CHECK(system_of_g.registry().running() == 1);
  auto composed = f * g;
  CAF_CHECK(system_of_g.registry().running() == 1);
  CAF_CHECK(&composed->home_system() == &g->home_system());
  CAF_CHECK(composed->node() == g->node());
  CAF_CHECK(composed->id() != g->id());
  CAF_CHECK(composed != g);
  CAF_CHECK(composed->message_types()
            == g->home_system().message_types(composed));
  anon_send_exit(composed, exit_reason::kill);
  anon_send_exit(f, exit_reason::kill);
  anon_send_exit(g, exit_reason::kill);
}

// spawned dead if `g` is already dead upon spawning
CAF_TEST(lifetime_1a) {
  auto g = system.spawn(dbl_bhvr);
  auto f = system.spawn(dbl_bhvr);
  self->monitor(g);
  anon_send_exit(g, exit_reason::kill);
  wait_until_exited();
  auto fg = f * g;
  CAF_CHECK(exited(fg));
  anon_send_exit(f, exit_reason::kill);
}

// spawned dead if `f` is already dead upon spawning
CAF_TEST(lifetime_1b) {
  auto g = system.spawn(dbl_bhvr);
  auto f = system.spawn(dbl_bhvr);
  self->monitor(f);
  anon_send_exit(f, exit_reason::kill);
  wait_until_exited();
  auto fg = f * g;
  CAF_CHECK(exited(fg));
  anon_send_exit(g, exit_reason::kill);
}

// `f.g` exits when `g` exits
CAF_TEST(lifetime_2a) {
  auto g = system.spawn(dbl_bhvr);
  auto f = system.spawn(dbl_bhvr);
  auto fg = f * g;
  self->monitor(fg);
  anon_send(g, message{});
  wait_until_exited();
  anon_send_exit(f, exit_reason::kill);
}

// `f.g` exits when `f` exits
CAF_TEST(lifetime_2b) {
  auto g = system.spawn(dbl_bhvr);
  auto f = system.spawn(dbl_bhvr);
  auto fg = f * g;
  self->monitor(fg);
  anon_send(f, message{});
  wait_until_exited();
  anon_send_exit(g, exit_reason::kill);
}

// 1) ignores down message not from constituent actors
// 2) exits by receiving an exit message
// 3) exit has no effect on constituent actors
CAF_TEST(lifetime_3) {
  auto g = system.spawn(dbl_bhvr);
  auto f = system.spawn(dbl_bhvr);
  auto fg = f * g;
  self->monitor(fg);
  anon_send(fg, down_msg{ self->address(),
                          exit_reason::kill });
  CAF_CHECK(! exited(fg));
  auto em_sender = system.spawn(dbl_bhvr);
  em_sender->link_to(fg.address());
  anon_send_exit(em_sender, exit_reason::kill);
  wait_until_exited();
  self->request(f, 1).receive(
    [](int v) {
      CAF_CHECK(v == 2);
    },
    [](error) {
      CAF_CHECK(false);
    }
  );
  self->request(g, 1).receive(
    [](int v) {
      CAF_CHECK(v == 2);
    },
    [](error) {
      CAF_CHECK(false);
    }
  );
  anon_send_exit(f, exit_reason::kill);
  anon_send_exit(g, exit_reason::kill);
}

CAF_TEST(request_response_promise) {
  auto g = system.spawn(dbl_bhvr);
  auto f = system.spawn(dbl_bhvr);
  auto fg = f * g;
  anon_send_exit(fg, exit_reason::kill);
  CAF_CHECK(exited(fg));
  self->request(fg, 1).receive(
    [](int) {
      CAF_CHECK(false);
    },
    [](error err) {
      CAF_CHECK(err.code() == sec::request_receiver_down);
    }
  );
  anon_send_exit(f, exit_reason::kill);
  anon_send_exit(g, exit_reason::kill);
}

// single composition of distinct actors
CAF_TEST(dot_composition_1) {
  auto first = system.spawn(first_stage_impl);
  auto second = system.spawn(second_stage_impl);
  auto first_then_second = second * first;
  self->request(first_then_second, 42).receive(
    [](double res) {
      CAF_CHECK(res == (42 * 2.0) * (42 * 4.0));
    }
  );
  anon_send_exit(first, exit_reason::kill);
  anon_send_exit(second, exit_reason::kill);
}

// multiple self composition
CAF_TEST(dot_composition_2) {
  auto dbl_actor = system.spawn(dbl_bhvr);
  auto dbl_x4_actor = dbl_actor * dbl_actor
                      * dbl_actor * dbl_actor;
  self->request(dbl_x4_actor, 1).receive(
    [](int v) {
      CAF_CHECK(v == 16);
    },
    [](error) {
      CAF_CHECK(false);
    }
  );
  anon_send_exit(dbl_actor, exit_reason::kill);
}

CAF_TEST_FIXTURE_SCOPE_END()
