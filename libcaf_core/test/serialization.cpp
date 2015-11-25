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

#define CAF_SUITE serialization
#include "caf/test/unit_test.hpp"

#include <new>
#include <set>
#include <list>
#include <stack>
#include <tuple>
#include <locale>
#include <memory>
#include <string>
#include <limits>
#include <vector>
#include <cstring>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <iterator>
#include <typeinfo>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <type_traits>

#include "caf/message.hpp"
#include "caf/serializer.hpp"
#include "caf/ref_counted.hpp"
#include "caf/deserializer.hpp"
#include "caf/proxy_registry.hpp"
#include "caf/primitive_variant.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/actor_system_config.hpp"

#include "caf/detail/ieee_754.hpp"
#include "caf/detail/int_list.hpp"
#include "caf/detail/safe_equal.hpp"
#include "caf/detail/type_traits.hpp"
#include "caf/detail/get_mac_addresses.hpp"

using namespace std;
using namespace caf;

namespace {

using strmap = map<string, u16string>;

struct raw_struct {
  string str;
};

template <class T>
void serialize(T& in_out, raw_struct& x, const unsigned int) {
  in_out & x.str;
}

bool operator==(const raw_struct& lhs, const raw_struct& rhs) {
  return lhs.str == rhs.str;
}

enum class test_enum : uint32_t {
  a,
  b,
  c
};

std::string to_string(test_enum x) {
  switch (x) {
    case test_enum::a: return "a";
    case test_enum::b: return "b";
    case test_enum::c: return "c";
  }
  return "???";
}

struct test_array {
  int value[4];
  int value2[2][4];
};

template <class T>
void serialize(T& in_out, test_array& x, const unsigned int) {
  in_out & x.value;
  in_out & x.value2;
}

bool operator==(const test_array& lhs, const test_array& rhs) {
  return std::equal(lhs.value, lhs.value + 4, rhs.value)
         && std::equal(lhs.value2[0], lhs.value2[0] + 4, rhs.value2[0])
         && std::equal(lhs.value2[1], lhs.value2[1] + 4, rhs.value2[1]);
}

struct test_empty_non_pod {
  test_empty_non_pod() = default;
  test_empty_non_pod(const test_empty_non_pod&) = default;
  virtual void foo() {
    // nop
  }
  virtual ~test_empty_non_pod() {
    // nop
  }
  bool operator==(const test_empty_non_pod&) const {
    return false;
  }
};

template <class T>
void serialize(T&, test_empty_non_pod&, const unsigned int) {
  // nop
}

struct fixture {
  int32_t i32 = -345;
  float f32 = 3.45f;
  double f64 = 54.3;
  test_enum te = test_enum::b;
  string str = "Lorem ipsum dolor sit amet.";
  raw_struct rs;
  test_array ta {
    {0, 1, 2, 3},
    {
      {0, 1, 2, 3},
      {4, 5, 6, 7}
    },
  };

  actor_system system;
  scoped_execution_unit context;
  message msg;

  template <class IO>
  void apply(IO&) {
    // end of recursion
  }

  template <class IO, class T, class... Ts>
  void apply(IO& in_out, T& x, Ts&... xs) {
    in_out & x;
    apply(in_out, xs...);
  }

  template <class T, class... Ts>
  vector<char> serialize(T& x, Ts&... xs) {
    vector<char> buf;
    binary_serializer bs{&context, std::back_inserter(buf)};
    apply(bs, x, xs...);
    return buf;
  }

  template <class T, class... Ts>
  void deserialize(const vector<char>& buf, T& x, Ts&... xs) {
    binary_deserializer bd{&context, buf.data(), buf.size()};
    apply(bd, x, xs...);
  }

  fixture()
      : system(actor_system_config{}
               .add_message_type<test_enum>("test_enum")
               .add_message_type<raw_struct>("raw_struct")
               .add_message_type<test_array>("test_array")
               .add_message_type<test_empty_non_pod>("test_empty_non_pod")),
        context(&system) {
    rs.str.assign(string(str.rbegin(), str.rend()));
    msg = make_message(i32, te, str, rs);
  }

  ~fixture() {
    system.await_all_actors_done();
  }
};

struct is_message {
  explicit is_message(message& msgref) : msg(msgref) {
    // nop
  }

  message& msg;

  template <class T, class... Ts>
  bool equal(T&& v, Ts&&... vs) {
    bool ok = false;
    // work around for gcc 4.8.4 bug
    auto tup = tie(v, vs...);
    message_handler impl {
      [&](T const& u, Ts const&... us) {
        ok = tup == tie(u, us...);
      }
    };
    impl(msg);
    return ok;
  }
};

} // namespace <anonymous>

CAF_TEST_FIXTURE_SCOPE(serialization_tests, fixture)

CAF_TEST(ieee_754_conversion) {
  // check conversion of float
  float f1 = 3.1415925f;         // float value
  auto p1 = caf::detail::pack754(f1); // packet value
  CAF_CHECK_EQUAL(p1, 0x40490FDA);
  auto u1 = caf::detail::unpack754(p1); // unpacked value
  CAF_CHECK_EQUAL(f1, u1);
  // check conversion of double
  double f2 = 3.14159265358979311600;  // double value
  auto p2 = caf::detail::pack754(f2); // packet value
  CAF_CHECK_EQUAL(p2, 0x400921FB54442D18);
  auto u2 = caf::detail::unpack754(p2); // unpacked value
  CAF_CHECK_EQUAL(f2, u2);
}

CAF_TEST(i32_values) {
  auto buf = serialize(i32);
  int32_t x;
  deserialize(buf, x);
  CAF_CHECK_EQUAL(i32, x);
}

CAF_TEST(float_values) {
  auto buf = serialize(f32);
  float x;
  deserialize(buf, x);
  CAF_CHECK_EQUAL(f32, x);
}

CAF_TEST(double_values) {
  auto buf = serialize(f64);
  double x;
  deserialize(buf, x);
  CAF_CHECK_EQUAL(f64, x);
}

CAF_TEST(enum_classes) {
  auto buf = serialize(te);
  test_enum x;
  deserialize(buf, x);
  CAF_CHECK(te == x);
}

CAF_TEST(strings) {
  auto buf = serialize(str);
  string x;
  deserialize(buf, x);
  CAF_CHECK_EQUAL(str, x);
}

CAF_TEST(custom_struct) {
  auto buf = serialize(rs);
  raw_struct x;
  deserialize(buf, x);
  CAF_CHECK(rs == x);
}

CAF_TEST(atoms) {
  atom_value x;
  auto foo = atom("foo");
  using bar_atom = atom_constant<atom("bar")>;
  auto buf = serialize(foo);
  deserialize(buf, x);
  CAF_CHECK(x == foo);
  buf = serialize(bar_atom::value);
  deserialize(buf, x);
  CAF_CHECK(x == bar_atom::value);
}

CAF_TEST(arrays) {
  auto buf = serialize(ta);
  test_array x;
  deserialize(buf, x);
  for (auto i = 0; i < 4; ++i)
    CAF_CHECK(ta.value[i] == x.value[i]);
  for (auto i = 0; i < 2; ++i)
    for (auto j = 0; j < 4; ++j)
      CAF_CHECK(ta.value2[i][j] == x.value2[i][j]);
}

CAF_TEST(empty_non_pods) {
  test_empty_non_pod x;
  auto buf = serialize(x);
  deserialize(buf, x);
  CAF_CHECK(true);
}

CAF_TEST(messages) {
  auto buf = serialize(msg);
  message x;
  deserialize(buf, x);
  CAF_CHECK(msg == x);
  CAF_CHECK(is_message(x).equal(i32, te, str, rs));
}

CAF_TEST(multiple_messages) {
  auto m = make_message(rs, te);
  auto buf = serialize(te, m, msg);
  test_enum t;
  message m1;
  message m2;
  deserialize(buf, t, m1, m2);
  CAF_CHECK(tie(t, m1, m2) == tie(te, m, msg));
  CAF_CHECK(is_message(m1).equal(rs, te));
  CAF_CHECK(is_message(m2).equal(i32, te, str, rs));
}

CAF_TEST_FIXTURE_SCOPE_END()
