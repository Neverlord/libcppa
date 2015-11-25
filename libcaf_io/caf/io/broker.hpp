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

#ifndef CAF_IO_BROKER_HPP
#define CAF_IO_BROKER_HPP

#include <map>
#include <vector>

#include "caf/extend.hpp"
#include "caf/local_actor.hpp"
#include "caf/abstract_event_based_actor.hpp"

#include "caf/io/scribe.hpp"
#include "caf/io/doorman.hpp"
#include "caf/io/abstract_broker.hpp"

namespace caf {
namespace io {

/// Describes a dynamically typed broker.
/// @extends abstract_broker
/// @ingroup Broker
class broker : public abstract_event_based_actor<behavior, false,
                                                 abstract_broker> {
public:
  using super = abstract_event_based_actor<behavior, false, abstract_broker>;

  template <class F, class... Ts>
  typename infer_handle_from_fun<F>::type
  fork(F fun, connection_handle hdl, Ts&&... xs) {
    CAF_ASSERT(context() != nullptr);
    auto sptr = this->take(hdl);
    CAF_ASSERT(sptr->hdl() == hdl);
    using impl = typename infer_handle_from_fun<F>::impl;
    actor_config cfg{context()};
    detail::init_fun_factory<impl, F> fac;
    cfg.init_fun = fac(std::move(fun), hdl, std::forward<Ts>(xs)...);
    auto res = this->system().spawn_class<impl, no_spawn_options>(cfg);
    auto forked = static_cast<impl*>(actor_cast<abstract_actor*>(res));
    sptr->set_parent(forked);
    CAF_ASSERT(sptr->parent() == forked);
    forked->add_scribe(sptr);
    return res;
  }

  void initialize() override;

  broker(actor_config& cfg);

protected:
  virtual behavior make_behavior();
};

using broker_ptr = intrusive_ptr<broker>;

} // namespace io
} // namespace caf

#endif // CAF_IO_BROKER_HPP
