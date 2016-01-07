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

#ifndef CAF_COMPOSED_ACTOR_HPP
#define CAF_COMPOSED_ACTOR_HPP

#include "caf/actor_addr.hpp"
#include "caf/mailbox_element.hpp"
#include "caf/monitorable_actor.hpp"

namespace caf {

/// An actor decorator implementing "dot operator"-like compositions,
/// i.e., `f.g(x) = f(g(x))`. Composed actors are hidden actors.
/// A composed actor exits when either of its constituent actors exits;
/// Constituent actors have no dependency on the composed actor
/// by default, and exit of a composed actor has no effect on its
/// constituent actors. A composed actor is hosted on the same actor
/// system and node as `g`, the first actor on the forwarding chain.
class composed_actor : public monitorable_actor {
public:
  using message_types_set = std::set<std::string>;

  composed_actor(actor_addr f, actor_addr g, message_types_set msg_types);

  // non-system messages are processed and then forwarded;
  // system messages are handled and consumed on the spot;
  // in either case, the processing is done synchronously
  void enqueue(mailbox_element_ptr what, execution_unit* host) override;

  message_types_set message_types() const override;

private:
  void handle_system_message(const message& msg, execution_unit* host);
  static bool is_system_message(const message& msg);

  actor_addr f_;
  actor_addr g_;
  message_types_set msg_types_;
};

} // namespace caf

#endif // CAF_COMPOSED_ACTOR_HPP
