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

#define CAF_SUITE message_lifetime
#include "caf/test/unit_test.hpp"

#include <atomic>
#include <iostream>

#include "caf/all.hpp"

using std::cout;
using std::endl;
using namespace caf;

namespace {

class testee : public event_based_actor {
 public:
  testee();
  ~testee();
  behavior make_behavior() override;
};

testee::testee() {
  // nop
}

testee::~testee() {
  // nop
}

behavior testee::make_behavior() {
  return {
    others >> [=] {
      CAF_CHECK_EQUAL(current_message().cvals()->get_reference_count(), 2);
      quit();
      return std::move(current_message());
    }
  };
}

class tester : public event_based_actor {
 public:
  tester(actor aut);
  ~tester();
  behavior make_behavior() override;
 private:
  actor m_aut;
  message m_msg;
};

tester::tester(actor aut) :
    m_aut(std::move(aut)),
    m_msg(make_message(1, 2, 3)) {
  // nop
}

tester::~tester() {
  // nop
}

behavior tester::make_behavior() {
  monitor(m_aut);
  send(m_aut, m_msg);
  return {
    on(1, 2, 3) >> [=] {
      CAF_CHECK_EQUAL(current_message().cvals()->get_reference_count(), 2);
      CAF_CHECK(current_message().cvals().get() == m_msg.cvals().get());
    },
    [=](const down_msg& dm) {
      CAF_CHECK(dm.source == m_aut);
      CAF_CHECK_EQUAL(dm.reason, exit_reason::normal);
      CAF_CHECK_EQUAL(current_message().cvals()->get_reference_count(), 1);
      quit();
    },
    others >> [&] {
      CAF_TEST_ERROR("Unexpected message: " << to_string(current_message()));
    }
  };
}

void message_lifetime_in_scoped_actor() {
  auto msg = make_message(1, 2, 3);
  scoped_actor self;
  self->send(self, msg);
  self->receive(
    on(1, 2, 3) >> [&] {
      CAF_CHECK_EQUAL(msg.cvals()->get_reference_count(), 2);
      CAF_CHECK_EQUAL(self->current_message().cvals()->get_reference_count(), 2);
      CAF_CHECK(self->current_message().cvals().get() == msg.cvals().get());
    }
  );
  CAF_CHECK_EQUAL(msg.cvals()->get_reference_count(), 1);
  msg = make_message(42);
  self->send(self, msg);
  self->receive(
    [&](int& value) {
      CAF_CHECK_EQUAL(msg.cvals()->get_reference_count(), 1);
      CAF_CHECK_EQUAL(self->current_message().cvals()->get_reference_count(), 1);
      CAF_CHECK(self->current_message().cvals().get() != msg.cvals().get());
      value = 10;
    }
  );
  CAF_CHECK_EQUAL(msg.get_as<int>(0), 42);
}

template <spawn_options Os>
void test_message_lifetime() {
  message_lifetime_in_scoped_actor();
  // put some preassure on the scheduler (check for thread safety)
  for (size_t i = 0; i < 100; ++i) {
    spawn<tester>(spawn<testee, Os>());
  }
}

} // namespace <anonymous>

CAF_TEST(test_message_lifetime_in_scoped_actor) {
  message_lifetime_in_scoped_actor();
}


CAF_TEST(test_message_lifetime_no_spawn_options) {
  CAF_MESSAGE("test_message_lifetime<no_spawn_options>");
  test_message_lifetime<no_spawn_options>();
}

CAF_TEST(test_message_lifetime_priority_aware) {
  CAF_MESSAGE("test_message_lifetime<priority_aware>");
  test_message_lifetime<priority_aware>();
  await_all_actors_done();
  shutdown();
}
