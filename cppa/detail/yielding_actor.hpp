/******************************************************************************\
 *           ___        __                                                    *
 *          /\_ \    __/\ \                                                   *
 *          \//\ \  /\_\ \ \____    ___   _____   _____      __               *
 *            \ \ \ \/\ \ \ '__`\  /'___\/\ '__`\/\ '__`\  /'__`\             *
 *             \_\ \_\ \ \ \ \L\ \/\ \__/\ \ \L\ \ \ \L\ \/\ \L\.\_           *
 *             /\____\\ \_\ \_,__/\ \____\\ \ ,__/\ \ ,__/\ \__/.\_\          *
 *             \/____/ \/_/\/___/  \/____/ \ \ \/  \ \ \/  \/__/\/_/          *
 *                                          \ \_\   \ \_\                     *
 *                                           \/_/    \/_/                     *
 *                                                                            *
 * Copyright (C) 2011, 2012                                                   *
 * Dominik Charousset <dominik.charousset@haw-hamburg.de>                     *
 *                                                                            *
 * This file is part of libcppa.                                              *
 * libcppa is free software: you can redistribute it and/or modify it under   *
 * the terms of the GNU Lesser General Public License as published by the     *
 * Free Software Foundation, either version 3 of the License                  *
 * or (at your option) any later version.                                     *
 *                                                                            *
 * libcppa is distributed in the hope that it will be useful,                 *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                       *
 * See the GNU Lesser General Public License for more details.                *
 *                                                                            *
 * You should have received a copy of the GNU Lesser General Public License   *
 * along with libcppa. If not, see <http://www.gnu.org/licenses/>.            *
\******************************************************************************/


#ifndef YIELDING_ACTOR_HPP
#define YIELDING_ACTOR_HPP

#include "cppa/config.hpp"

#ifndef CPPA_DISABLE_CONTEXT_SWITCHING

#include <stack>

#include "cppa/either.hpp"
#include "cppa/pattern.hpp"

#include "cppa/detail/yield_interface.hpp"
#include "cppa/detail/nestable_receive_actor.hpp"
#include "cppa/detail/abstract_scheduled_actor.hpp"

namespace cppa { namespace detail {

class yielding_actor
        : public nestable_receive_actor<yielding_actor,
                                        abstract_scheduled_actor> {

    typedef nestable_receive_actor<yielding_actor,
                                   abstract_scheduled_actor>
            super;

 public:

    yielding_actor(std::function<void()> fun);

    void dequeue(behavior& bhvr); //override

    void dequeue(partial_function& fun); //override

    void resume(util::fiber* from, scheduler::callback* callback); //override

    inline void push_timeout() {
        ++m_active_timeout_id;
    }

    inline void pop_timeout() {
        --m_active_timeout_id;
    }

 private:

    typedef std::unique_ptr<recursive_queue_node> queue_node_ptr;

    static void run(void* _this);

    void yield_until_not_empty();

    util::fiber m_fiber;
    std::function<void()> m_behavior;

};

} } // namespace cppa::detail

#endif // CPPA_DISABLE_CONTEXT_SWITCHING

#endif // YIELDING_ACTOR_HPP
