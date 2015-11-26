// showcases how to add custom POD message types to CAF

#include <tuple>
#include <string>
#include <vector>
#include <cassert>
#include <utility>
#include <iostream>

#include "caf/all.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/binary_deserializer.hpp"

using std::cout;
using std::cerr;
using std::endl;
using std::vector;

using namespace caf;

// POD struct
struct foo {
  std::vector<int> a;
  int b;
};

// foo needs to be comparable ...
bool operator==(const foo& lhs, const foo& rhs) {
  return lhs.a == rhs.a && lhs.b == rhs.b;
}

// ... and to be serializable
template <class T>
void serialize(T& in_or_out, foo& x, const unsigned int) {
  in_or_out & x.a;
  in_or_out & x.b;
}

// also, CAF gives us `deep_to_string` for implementing `to_string` easily
std::string to_string(const foo& x) {
  // `to_string(foo{{1, 2, 3}, 4})` prints: "foo([1, 2, 3], 4)"
  return "foo" + deep_to_string(std::forward_as_tuple(x.a, x.b));
}

// a pair of two ints
using foo_pair = std::pair<int, int>;

// another alias for pairs of two ints
using foo_pair2 = std::pair<int, int>;

// a struct with a nested container
struct foo2 {
  int a;
  vector<vector<double>> b;
};

// foo2 also needs to be comparable ...
bool operator==(const foo2& lhs, const foo2& rhs) {
  return lhs.a == rhs.a && lhs.b == rhs.b;
}

// ... and to be serializable
template <class T>
void serialize(T& in_or_out, foo2& x, const unsigned int) {
  in_or_out & x.a;
  in_or_out & x.b; // traversed automatically and recursively
}

// `deep_to_string` also traverses nested containers
std::string to_string(const foo2& x) {
  return "foo" + deep_to_string(std::forward_as_tuple(x.a, x.b));
}

// receives our custom message types
void testee(event_based_actor* self, size_t remaining) {
  auto set_next_behavior = [=] {
    if (remaining > 1)
      testee(self, remaining - 1);
    else
      self->quit();
  };
  self->become (
    // note: we sent a foo_pair2, but match on foo_pair
    // that works because both are aliases for std::pair<int, int>
    [=](const foo_pair& val) {
      aout(self) << "foo_pair" << deep_to_string(val) << endl;
      set_next_behavior();
    },
    [=](const foo& val) {
      aout(self) << to_string(val) << endl;
      set_next_behavior();
    }
  );
}

int main(int, char**) {
  actor_system_config cfg;
  cfg.add_message_type<foo>("foo");
  cfg.add_message_type<foo2>("foo2");
  cfg.add_message_type<foo_pair>("foo_pair");
  // this actor system can now serialize our custom types when running
  // a distributed CAF application or we can serialize them manually
  actor_system system{cfg};
  // two variables for testing serialization
  foo2 f1;
  foo2 f2;
  // init some test data
  f1.a = 5;
  f1.b.resize(1);
  f1.b.back().push_back(42);
  // I/O buffer
  vector<char> buf;
  // write f1 to buffer
  binary_serializer bs{system, buf};
  bs << f1;
  // read f2 back from buffer
  binary_deserializer bd{system, buf};
  bd >> f2;
  // must be equal
  assert(f1 == f2);
  // spawn a testee that receives two messages of user-defined type
  auto t = system.spawn(testee, 2);
  scoped_actor self{system};
  // send t a foo
  self->send(t, foo{std::vector<int>{1, 2, 3, 4}, 5});
  // send t a foo_pair2
  self->send(t, foo_pair2{3, 4});
  self->await_all_other_actors_done();
}
