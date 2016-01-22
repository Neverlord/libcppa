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

#include <map>

#include "caf/config.hpp"

#define CAF_SUITE typed_response_promise
#include "caf/test/unit_test.hpp"

#include "caf/all.hpp"

using namespace caf;

namespace {

using foo_actor = typed_actor<replies_to<int>::with<int>,
                              replies_to<get_atom, int>::with<int>,
                              replies_to<get_atom, int, int>::with<int, int>,
                              replies_to<get_atom, double>::with<double>,
                              replies_to<get_atom, double, double>
                              ::with<double, double>,
                              reacts_to<put_atom, int, int>,
                              reacts_to<put_atom, int, int, int>>;

using foo_promise = typed_response_promise<int>;
using foo2_promise = typed_response_promise<int, int>;
using foo3_promise = typed_response_promise<double>;

class foo_actor_impl : public foo_actor::base {
public:
  foo_actor_impl(actor_config& cfg) : foo_actor::base(cfg) {
    // nop
  }

  behavior_type make_behavior() override {
    return {
      [=](int x) -> foo_promise {
         auto resp = response(x * 2);
         CAF_CHECK(! resp.pending());
         resp.deliver(x * 4); // has no effect
         return resp;
      },
      [=](get_atom, int x) -> foo_promise {
        auto calculator = spawn([](event_based_actor* self) -> behavior {
          return {
            [self](int promise_id, int value) -> message {
              self->quit();
              return make_message(put_atom::value, promise_id, value * 2);
            }
          };
        });
        send(calculator, next_id_, x);
        auto& entry = promises_[next_id_++];
        entry = make_response_promise();
        return entry;
      },
      [=](get_atom, int x, int y) -> foo2_promise {
        auto calculator = spawn([](event_based_actor* self) -> behavior {
          return {
            [self](int promise_id, int v0, int v1) -> message {
              self->quit();
              return make_message(put_atom::value, promise_id, v0 * 2, v1 * 2);
            }
          };
        });
        send(calculator, next_id_, x, y);
        auto& entry = promises2_[next_id_++];
        entry = make_response_promise();
        // verify move semantics
        CAF_CHECK(entry.pending());
        foo2_promise tmp(std::move(entry));
        CAF_CHECK(! entry.pending());
        CAF_CHECK(tmp.pending());
        entry = std::move(tmp);
        CAF_CHECK(entry.pending());
        CAF_CHECK(! tmp.pending());
        return entry;
      },
      [=](get_atom, double) -> foo3_promise {
        foo3_promise resp = make_response_promise();
        resp.deliver(make_error(sec::unexpected_message));
        return resp;
      },
      [=](get_atom, double x, double y) {
        return response(x * 2, y * 2);
      },
      [=](put_atom, int promise_id, int x) {
        auto i = promises_.find(promise_id);
        if (i == promises_.end())
          return;
        i->second.deliver(x);
        promises_.erase(i);
      },
      [=](put_atom, int promise_id, int x, int y) {
        auto i = promises2_.find(promise_id);
        if (i == promises2_.end())
          return;
        i->second.deliver(x, y);
        promises2_.erase(i);
      }
    };
  }

private:
  int next_id_ = 0;
  std::map<int, foo_promise> promises_;
  std::map<int, foo2_promise> promises2_;
};

struct fixture {
  fixture()
      : self(system, true),
        foo(system.spawn<foo_actor_impl>()) {
    // nop
  }

  ~fixture() {
    self->send_exit(foo, exit_reason::kill);
  }

  actor_system system;
  scoped_actor self;
  foo_actor foo;
};

} // namespace <anonymous>

CAF_TEST_FIXTURE_SCOPE(typed_spawn_tests, fixture)

CAF_TEST(typed_response_promise) {
  typed_response_promise<int> resp;
  resp.deliver(1); // delivers on an invalid promise has no effect
  CAF_CHECK_EQUAL(static_cast<void*>(&static_cast<response_promise&>(resp)),
                  static_cast<void*>(&resp));
  self->request(foo, get_atom::value, 42).receive(
    [](int x) {
      CAF_CHECK_EQUAL(x, 84);
    }
  );
  self->request(foo, get_atom::value, 42, 52).receive(
    [](int x, int y) {
      CAF_CHECK_EQUAL(x, 84);
      CAF_CHECK_EQUAL(y, 104);
    }
  );
  self->request(foo, get_atom::value, 3.14, 3.14).receive(
    [](double x, double y) {
      CAF_CHECK_EQUAL(x, 3.14 * 2);
      CAF_CHECK_EQUAL(y, 3.14 * 2);
    },
    [](const error& err) {
      CAF_ERROR("unexpected error response message received: " << err);
    }
  );
}

CAF_TEST(typed_response_promise_chained) {
  auto composed = foo * foo * foo;
  self->request(composed, 1).receive(
    [](int v) {
      CAF_CHECK_EQUAL(v, 8);
    },
    [](const error& err) {
      CAF_ERROR("unexpected error response message received: " << err);
    }
  );
}

// verify that only requests get an error response message
CAF_TEST(error_response_message) {
  self->request(foo, get_atom::value, 3.14).receive(
    [](double) {
      CAF_ERROR("unexpected ordinary response message received");
    },
    [](const error& err) {
      CAF_CHECK_EQUAL(err.code(), static_cast<uint8_t>(sec::unexpected_message));
    }
  );
  self->send(foo, get_atom::value, 3.14);
  self->send(foo, get_atom::value, 42);
  self->receive(
    [](int x) {
      CAF_CHECK_EQUAL(x, 84);
    },
    [](double x) {
      CAF_ERROR("unexpected ordinary response message received: " << x);
    }
  );
}

// verify that delivering to a satisfied promise has no effect
CAF_TEST(satisfied_promise) {
  self->send(foo, 1);
  self->send(foo, get_atom::value, 3.14, 3.14);
  int i = 0;
  self->receive_for(i, 2) (
    [](int x) {
      CAF_CHECK_EQUAL(x, 1 * 2);
    },
    [](double x, double y) {
      CAF_CHECK_EQUAL(x, 3.14 * 2);
      CAF_CHECK_EQUAL(y, 3.14 * 2);
    }
  );
}

CAF_TEST_FIXTURE_SCOPE_END()
