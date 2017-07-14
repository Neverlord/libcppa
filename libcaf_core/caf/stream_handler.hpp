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

#ifndef CAF_STREAM_HANDLER_HPP
#define CAF_STREAM_HANDLER_HPP

#include <vector>
#include <cstdint>
#include <cstddef>

#include "caf/fwd.hpp"
#include "caf/ref_counted.hpp"

namespace caf {

/// Manages a single stream with any number of down- and upstream actors.
class stream_handler : public ref_counted {
public:
  ~stream_handler() override;

  // -- handler for downstream events ------------------------------------------

  /// Add a new downstream actor to the stream with in-flight
  /// `stream_msg::open` message.
  /// @param hdl Handle to the new downstream actor.
  /// @pre `hdl != nullptr`
  virtual error add_downstream(strong_actor_ptr& hdl);

  /// Confirms a downstream actor after receiving its `stream_msg::ack_open`.
  /// @param hdl Handle to the new downstream actor.
  /// @param initial_demand Credit received with `ack_open`.
  /// @param redeployable Denotes whether the runtime can redeploy
  ///                     the downstream actor on failure.
  /// @pre `hdl != nullptr`
  virtual error confirm_downstream(const strong_actor_ptr& rebind_from,
                                   strong_actor_ptr& hdl, long initial_demand,
                                   bool redeployable);

  /// Handles cumulative ACKs with new demand from a downstream actor.
  /// @pre `hdl != nullptr`
  /// @pre `new_demand > 0`
  virtual error downstream_ack(strong_actor_ptr& hdl, int64_t batch_id,
                               long new_demand);

  /// Push new data to downstream actors by sending batches. The amount of
  /// pushed data is limited by `hint` or the available credit if
  /// `hint == nullptr`.
  virtual error push();

  // -- handler for upstream events --------------------------------------------

  /// Add a new upstream actor to the stream and return an initial credit.
  virtual expected<long> add_upstream(strong_actor_ptr& hdl,
                                      const stream_id& sid,
                                      stream_priority prio);

  /// Handles data from an upstream actor.
  virtual error upstream_batch(strong_actor_ptr& hdl, int64_t xs_id,
                               long xs_size, message& xs);

  /// Closes an upstream.
  virtual error close_upstream(strong_actor_ptr& hdl);

  // -- handler for stream-wide events -----------------------------------------

  /// Signals an error at the up- or downstream actor `hdl`. This function is
  /// called with `hdl == nullptr` if the parent actor shuts down.
  virtual void abort(strong_actor_ptr& hdl, const error& reason) = 0;

  // -- accessors --------------------------------------------------------------

  virtual bool done() const = 0;

  /// Returns a type-erased `stream<T>` as handshake token for downstream
  /// actors. Returns an empty message for sinks.
  virtual message make_output_token(const stream_id&) const;

  /// Returns the downstream policy if this handler is a sink or stage.
  virtual optional<downstream_policy&> dp();

  /// Returns the upstream policy if this handler is a source or stage.
  virtual optional<upstream_policy&> up();
};

/// A reference counting pointer to a `stream_handler`.
/// @relates stream_handler
using stream_handler_ptr = intrusive_ptr<stream_handler>;

} // namespace caf

#endif // CAF_STREAM_HANDLER_HPP
