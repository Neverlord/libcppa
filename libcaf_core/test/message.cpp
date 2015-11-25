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
#include <vector>
#include <string>
#include <numeric>
#include <iostream>

#include <set>
#include <unordered_set>

#include "caf/config.hpp"

#define CAF_SUITE message
#include "caf/test/unit_test.hpp"

#include "caf/all.hpp"

using std::map;
using std::string;
using std::vector;
using std::make_tuple;

using namespace caf;

CAF_TEST(apply) {
  auto f1 = [] {
    CAF_TEST_ERROR("f1 invoked!");
  };
  auto f2 = [](int i) {
    CAF_CHECK_EQUAL(i, 42);
  };
  auto m = make_message(42);
  m.apply(f1);
  m.apply(f2);
}

CAF_TEST(drop) {
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
    CAF_CHECK(m1.drop(i) == messages[i]);
  }
}

CAF_TEST(slice) {
  auto m1 = make_message(1, 2, 3, 4, 5);
  auto m2 = m1.slice(2, 2);
  CAF_CHECK(m2 == make_message(3, 4));
}

CAF_TEST(extract1) {
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
  CAF_CHECK(m2.extract(f) == m1);
  CAF_CHECK(m3.extract(f) == m1);
  CAF_CHECK(m4.extract(f) == m1);
  CAF_CHECK(m5.extract(f) == m1);
  CAF_CHECK(m6.extract(f) == m1);
  CAF_CHECK(m7.extract(f) == m1);
}

CAF_TEST(extract2) {
  auto m1 = make_message(1);
  CAF_CHECK(m1.extract([](int) {}).empty());
  auto m2 = make_message(1.0, 2, 3, 4.0);
  auto m3 = m2.extract({
    [](int, int) { },
    [](double, double) { }
  });
  // check for false positives through collapsing
  CAF_CHECK(m3 == make_message(1.0, 4.0));
}

CAF_TEST(extract_opts) {
  auto f = [](std::vector<std::string> xs) {
    std::string filename;
    size_t log_level;
    auto res = message_builder(xs.begin(), xs.end()).extract_opts({
      {"version,v", "print version"},
      {"log-level,l", "set the log level", log_level},
      {"file,f", "set output file", filename},
      {"whatever", "do whatever"}
    });
    CAF_CHECK_EQUAL(res.opts.count("file"), 1);
    CAF_CHECK(res.remainder.empty());
    CAF_CHECK_EQUAL(filename, "hello.txt");
    CAF_CHECK_EQUAL(log_level, 5);
  };
  f({"--file=hello.txt", "-l", "5"});
  f({"-f", "hello.txt", "--log-level=5"});
  f({"-f", "hello.txt", "-l", "5"});
  f({"-f", "hello.txt", "-l5"});
  f({"-fhello.txt", "-l", "5"});
  f({"-l5", "-fhello.txt"});
  CAF_MESSAGE("ensure that failed parsing doesn't consume input");
  auto msg = make_message("-f", "42", "-b", "1337");
  auto foo = 0;
  auto bar = 0;
  auto r = msg.extract_opts({
    {"foo,f", "foo desc", foo}
  });
  CAF_CHECK(r.opts.count("foo") > 0);
  CAF_CHECK(foo == 42);
  CAF_CHECK(bar == 0);
  CAF_CHECK(! r.error.empty()); // -b is an unknown option
  CAF_CHECK(! r.remainder.empty() && r.remainder == make_message("-b", "1337"));
  r = r.remainder.extract_opts({
    {"bar,b", "bar desc", bar}
  });
  CAF_CHECK(r.opts.count("bar") > 0);
  CAF_CHECK(bar == 1337);
  CAF_CHECK(r.error.empty());
}

CAF_TEST(type_token) {
  auto m1 = make_message(get_atom::value);
  CAF_CHECK_EQUAL(m1.type_token(), detail::make_type_token<get_atom>());
}

CAF_TEST(concat) {
  auto m1 = make_message(get_atom::value);
  auto m2 = make_message(uint32_t{1});
  auto m3 = message::concat(m1, m2);
  CAF_CHECK(m3 == m1 + m2);
  CAF_CHECK(m3 == make_message(get_atom::value, uint32_t{1}));
  auto m4 = make_message(get_atom::value, uint32_t{1},
                         get_atom::value, uint32_t{1});
  CAF_CHECK(message::concat(m3, message{}, m1, m2) == m4);
}

namespace {

struct s1 {
  int value[3] = {10, 20, 30};
};

bool operator==(const s1& lhs, const s1& rhs) {
  for (size_t i = 0; i < 3; ++i)
    if (lhs.value[i] != rhs.value[i])
      return false;
  return true;
}

template <class T>
void serialize(T& in_out, s1& x, const unsigned int) {
  in_out & x.value;
}

std::string to_string(const s1& x) {
  return deep_to_string(x.value);
}

struct s2 {
  int value[4][2] = {{1, 10}, {2, 20}, {3, 30}, {4, 40}};
};

bool operator==(const s2& lhs, const s2& rhs) {
  for (size_t i = 0; i < 4; ++i)
    for (size_t j = 0; j < 2; ++j)
      if (lhs.value[i][j] != rhs.value[i][j])
        return false;
  return true;
}

template <class T>
void serialize(T& in_out, s2& x, const unsigned int) {
  in_out & x.value;
}

std::string to_string(const s2& x) {
  return deep_to_string(x.value);
}

struct s3 {
  std::array<int, 4> value;
  s3() {
    std::iota(value.begin(), value.end(), 1);
  }
};

bool operator==(const s3& lhs, const s3& rhs) {
  return lhs.value == rhs.value;
}

template <class T>
void serialize(T& in_out, s3& x, const unsigned int) {
  in_out & x.value;
}

std::string to_string(const s3& x) {
  return deep_to_string(x.value);
}

template <class... Ts>
std::string msg_as_string(Ts&&... xs) {
  return to_string(make_message(std::forward<Ts>(xs)...));
}

} // namespace <anonymous>

CAF_TEST(compare_custom_types) {
  CAF_CHECK(make_message(s1{}) == make_message(s1{}));
  s2 tmp;
  tmp.value[0][1] = 100;
  CAF_CHECK(make_message(s2{}) != make_message(tmp));
}

CAF_TEST(empty_to_string) {
  message msg;
  CAF_CHECK(to_string(msg), "<empty-message>");
}

CAF_TEST(integers_to_string) {
  using ivec = vector<int>;
  CAF_CHECK_EQUAL(msg_as_string(1, 2, 3), "(1, 2, 3)");
  CAF_CHECK_EQUAL(msg_as_string(ivec{1, 2, 3}), "([1, 2, 3])");
  CAF_CHECK_EQUAL(msg_as_string(ivec{1, 2}, 3, 4, ivec{5, 6, 7}),
                  "([1, 2], 3, 4, [5, 6, 7])");
}

CAF_TEST(strings_to_string) {
  using svec = vector<string>;
  auto msg1 = make_message("one", "two", "three");
  CAF_CHECK(to_string(msg1) == R"__(("one", "two", "three"))__");
  auto msg2 = make_message(svec{"one", "two", "three"});
  CAF_CHECK(to_string(msg2) == R"__((["one", "two", "three"]))__");
  auto msg3 = make_message(svec{"one", "two"}, "three", "four",
                           svec{"five", "six", "seven"});
  CAF_CHECK(to_string(msg3) ==
          R"__((["one", "two"], "three", "four", ["five", "six", "seven"]))__");
  auto msg4 = make_message("this is a \"test\"");
  CAF_CHECK(to_string(msg4) == R"__(("this is a \"test\""))__");
}

CAF_TEST(maps_to_string) {
  map<int, int> m1{{1, 10}, {2, 20}, {3, 30}};
  auto msg1 = make_message(move(m1));
  CAF_CHECK(to_string(msg1) == "([(1, 10), (2, 20), (3, 30)])");
}

CAF_TEST(tuples_to_string) {
  auto msg1 = make_message(make_tuple(1, 2, 3), 4, 5);
  CAF_CHECK(to_string(msg1) == "((1, 2, 3), 4, 5)");
  auto msg2 = make_message(make_tuple(string{"one"}, 2, uint32_t{3}), 4, true);
  CAF_CHECK(to_string(msg2) == "((\"one\", 2, 3), 4, true)");
}

CAF_TEST(arrays_to_string) {
  CAF_CHECK(make_message(s1{}) == make_message(s1{}));
  CAF_CHECK(msg_as_string(s1{}) == "((10, 20, 30))");
  auto msg2 = make_message(s2{});
  s2 tmp;
  tmp.value[0][1] = 100;
  CAF_CHECK(! (msg2 == make_message(tmp)));
  CAF_CHECK(to_string(msg2) == "(((1, 10), (2, 20), (3, 30), (4, 40)))");
  CAF_CHECK(make_message(s3{}) == make_message(s3{}));
  CAF_CHECK(msg_as_string(s3{}) == "((1, 2, 3, 4))");
}
