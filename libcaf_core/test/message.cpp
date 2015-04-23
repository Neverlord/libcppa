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

#define CAF_SUITE message
#include "caf/test/unit_test.hpp"

#include "caf/all.hpp"

using std::cout;
using std::endl;
using namespace caf;

CAF_TEST(test_drop) {
  auto m1 = make_message(1, 2, 3, 4, 5);
  std::vector<message> messages{
    m1,
    make_message(2, 3, 4, 5),
    make_message(3, 4, 5),
    make_message(4, 5),
    make_message(5),
    message{}
  };
  for (size_t i = 0; i < messages.size(); ++i) {
    CAF_CHECK_EQUAL(to_string(m1.drop(i)), to_string(messages[i]));
  }
}

CAF_TEST(test_slice) {
  auto m1 = make_message(1, 2, 3, 4, 5);
  auto m2 = m1.slice(2, 2);
  CAF_CHECK_EQUAL(to_string(m2), to_string(make_message(3, 4)));
}

CAF_TEST(test_extract1) {
  auto f = [](int lhs1, int lhs2, int lhs3, int rhs1, int rhs2, int val) {
    auto m1 = make_message(lhs1, lhs2, lhs3);
    auto m2 = make_message(rhs1, rhs2);
    auto m3 = m1.extract(on(val) >> [] {});
    CAF_CHECK_EQUAL(to_string(m2), to_string(m3));
  };
  f(1, 2, 3, 2, 3, 1);
  f(1, 2, 3, 1, 3, 2);
  f(1, 2, 3, 1, 2, 3);
}

CAF_TEST(test_extract2) {
  auto m1 = make_message(1.0, 2.0, 3.0);
  auto m2 = make_message(1, 2, 1.0, 2.0, 3.0);
  auto m3 = make_message(1.0, 1, 2, 2.0, 3.0);
  auto m4 = make_message(1.0, 2.0, 1, 2, 3.0);
  auto m5 = make_message(1.0, 2.0, 3.0, 1, 2);
  auto m6 = make_message(1, 2, 1.0, 2.0, 3.0, 1, 2);
  auto m7 = make_message(1.0, 1, 2, 3, 4, 2.0, 3.0);
  message_handler f{
    [](int, int) { },
    [](float, float) { }
  };
  CAF_CHECK_EQUAL(to_string(m2.extract(f)), to_string(m1));
  CAF_CHECK_EQUAL(to_string(m3.extract(f)), to_string(m1));
  CAF_CHECK_EQUAL(to_string(m4.extract(f)), to_string(m1));
  CAF_CHECK_EQUAL(to_string(m5.extract(f)), to_string(m1));
  CAF_CHECK_EQUAL(to_string(m6.extract(f)), to_string(m1));
  CAF_CHECK_EQUAL(to_string(m7.extract(f)), to_string(m1));
}

CAF_TEST(test_extract3) {
  auto m1 = make_message(1);
  CAF_CHECK_EQUAL(to_string(m1.extract([](int) {})), to_string(message{}));
  auto m2 = make_message(1.0, 2, 3, 4.0);
  auto m3 = m2.extract({
    [](int, int) { },
    [](double, double) { }
  });
  // check for false positives through collapsing
  CAF_CHECK_EQUAL(to_string(m3), to_string(make_message(1.0, 4.0)));
}

CAF_TEST(test_extract_opts) {
  auto f = [](std::vector<std::string> xs) {
    std::string filename;
    auto res = message_builder(xs.begin(), xs.end()).extract_opts({
      {"version,v", "print version"},
      {"file,f", "set output file", filename},
      {"whatever", "do whatever"}
    });
    CAF_CHECK_EQUAL(res.opts.count("file"), 1);
    CAF_CHECK_EQUAL(to_string(res.remainder), to_string(message{}));
    CAF_CHECK_EQUAL(filename, "hello.txt");
  };
  f({"--file=hello.txt"});
  f({"-f", "hello.txt"});
}

void test_type_token() {
  auto m1 = make_message(get_atom::value);
  CAF_CHECK_EQUAL(m1.type_token(), detail::make_type_token<get_atom>());
}

CAF_TEST(test_concat) {
  auto m1 = make_message(get_atom::value);
  auto m2 = make_message(uint32_t{1});
  auto m3 = message::concat(m1, m2);
  CAF_CHECK_EQUAL(to_string(m3), to_string(m1 + m2));
  CAF_CHECK_EQUAL(to_string(m3), to_string(make_message(get_atom::value,
                                                        uint32_t{1})));
  auto m4 = make_message(get_atom::value, uint32_t{1},
                         get_atom::value, uint32_t{1});
  CAF_CHECK_EQUAL(to_string(message::concat(m3, message{}, m1, m2)),
                  to_string(m4));
  shutdown();
}
