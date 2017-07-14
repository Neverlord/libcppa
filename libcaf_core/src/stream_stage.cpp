/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2017                                                  *
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

#include "caf/stream_stage.hpp"

#include "caf/sec.hpp"
#include "caf/logger.hpp"
#include "caf/upstream_path.hpp"
#include "caf/downstream_path.hpp"
#include "caf/upstream_policy.hpp"
#include "caf/downstream_policy.hpp"

namespace caf {

stream_stage::stream_stage(upstream_policy* in_ptr, downstream_policy* out_ptr)
  : in_ptr_(in_ptr),
    out_ptr_(out_ptr) {
  // nop
}
bool stream_stage::done() const {
  return in_ptr_->closed() && out_ptr_->closed();
}

error stream_stage::upstream_batch(strong_actor_ptr& hdl, int64_t xs_id,
                                   long xs_size, message& xs) {
  CAF_LOG_TRACE(CAF_ARG(hdl) << CAF_ARG(xs_size) << CAF_ARG(xs));
  auto path = in().find(hdl);
  if (path) {
    if (xs_size > path->assigned_credit)
      return sec::invalid_stream_state;
    path->last_batch_id = xs_id;
    path->assigned_credit -= xs_size;
    auto err = process_batch(xs);
    if (err == none) {
      push();
      auto current_size = out().buf_size();
      auto desired_size = out().credit();
      if (current_size < desired_size)
        in().assign_credit(desired_size - current_size);
    }
    return err;
  }
  return sec::invalid_upstream;
}

error stream_stage::downstream_ack(strong_actor_ptr& hdl, int64_t,
                                   long demand) {
  CAF_LOG_TRACE(CAF_ARG(hdl) << CAF_ARG(demand));
  auto path = out_ptr_->find(hdl);
  if (path) {
    downstream_demand(path, demand);
    return none;
  }
  return sec::invalid_downstream;
}

void stream_stage::abort(strong_actor_ptr& cause, const error& reason) {
  in_ptr_->abort(cause, reason);
  out_ptr_->abort(cause, reason);
}

void stream_stage::last_upstream_closed() {
  if (out().buf_size() == 0)
    out().close();
}

void stream_stage::downstream_demand(downstream_path* path, long demand) {
  auto hdl = path->hdl;
  path->open_credit += demand;
  if(out().buf_size() > 0)
    push();
  else if (in().closed())
    out().remove_path(hdl); // don't pass path->hdl: path can become invalid
  auto current_size = out().buf_size();
  auto desired_size = out().credit();
  if (current_size < desired_size)
    in().assign_credit(desired_size - current_size);
}

} // namespace caf
