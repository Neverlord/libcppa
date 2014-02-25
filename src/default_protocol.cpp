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
 * Copyright (C) 2011-2013                                                    *
 * Dominik Charousset <dominik.charousset@haw-hamburg.de>                     *
 *                                                                            *
 * This file is part of libcppa.                                              *
 * libcppa is free software: you can redistribute it and/or modify it under   *
 * the terms of the GNU Lesser General Public License as published by the     *
 * Free Software Foundation; either version 2.1 of the License,               *
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


#include <future>
#include <cstdint>
#include <iostream>

#include "cppa/logging.hpp"
#include "cppa/to_string.hpp"
#include "cppa/singletons.hpp"

#include "cppa/io/middleman.hpp"
#include "cppa/io/default_peer.hpp"
#include "cppa/io/ipv4_acceptor.hpp"
#include "cppa/io/ipv4_io_stream.hpp"
#include "cppa/io/default_protocol.hpp"
#include "cppa/io/default_peer_acceptor.hpp"

#include "cppa/detail/actor_registry.hpp"
#include "cppa/detail/singleton_manager.hpp"

#include "cppa/intrusive/blocking_single_reader_queue.hpp"

using namespace std;
using namespace cppa::detail;

namespace cppa { namespace io {

default_protocol::default_protocol(middleman* parent)
: super(parent), m_addressing(this) { }

atom_value default_protocol::identifier() const {
    return atom("DEFAULT");
}

void default_protocol::publish(const actor_ptr& whom, variant_args args) {
    CPPA_LOG_TRACE(CPPA_TARG(whom, to_string)
                   << ", args.size() = " << args.size());
    if (!whom) return;
    CPPA_REQUIRE(args.size() == 2 || args.size() == 1);
    auto i = args.begin();
    if (args.size() == 1) {
        auto port = get<uint16_t>(*i);
        CPPA_LOG_INFO("publish " << to_string(whom) << " on port " << port);
        publish(whom, ipv4_acceptor::create(port), {});
    }
    else if (args.size() == 2) {
        auto port = get<uint16_t>(*i++);
        auto& addr = get<string>(*i);
        CPPA_LOG_INFO("publish " << to_string(whom) << " on port " << port
                      << " with addr = " << addr);
        publish(whom, ipv4_acceptor::create(port, addr.c_str()), {});
    }
    else throw logic_error("wrong number of arguments, expected one or two");
}

void default_protocol::publish(const actor_ptr& whom,
                               std::unique_ptr<acceptor> ptr,
                               variant_args args             ) {
    CPPA_LOG_TRACE(CPPA_TARG(whom, to_string) << ", " << CPPA_MARG(ptr, get)
                   << ", args.size() = " << args.size());
    if (!whom) return;
    CPPA_REQUIRE(args.size() == 0);
    static_cast<void>(args); // keep compiler happy
    get_actor_registry()->put(whom->id(), whom);
    default_protocol* proto = this;
    auto impl = new default_peer_acceptor(this, move(ptr), whom);
    run_later([=] {
        CPPA_LOGC_TRACE("cppa::io::default_protocol",
                        "publish$add_acceptor", "");
        proto->m_acceptors[whom].push_back(impl);
        proto->continue_reader(impl);
    });
}

void default_protocol::unpublish(const actor_ptr& whom) {
    CPPA_LOG_TRACE("whom = " << to_string(whom));
    default_protocol* proto = this;
    run_later([=] {
        CPPA_LOGC_TRACE("cppa::io::default_protocol",
                        "unpublish$remove_acceptors", "");
        auto& acceptors = m_acceptors[whom];
        for (auto& ptr : acceptors) proto->stop_reader(ptr);
        m_acceptors.erase(whom);
    });
}

void default_protocol::register_peer(const process_information& node,
                                     default_peer* ptr) {
    CPPA_LOG_TRACE("node = " << to_string(node) << ", ptr = " << ptr);
    auto& entry = m_peers[node];
    if (entry.impl == nullptr) {
        if (entry.queue == nullptr) entry.queue.emplace();
        ptr->set_queue(entry.queue);
        entry.impl = ptr;
        if (!entry.queue->empty()) {
            auto tmp = entry.queue->pop();
            ptr->enqueue(tmp.first, tmp.second);
        }
        CPPA_LOG_INFO("peer " << to_string(node) << " added");
    }
    else {
        CPPA_LOG_WARNING("peer " << to_string(node) << " already defined, "
                         "multiple calls to remote_actor()?");
    }
}

default_peer* default_protocol::get_peer(const process_information& n) {
    CPPA_LOG_TRACE("n = " << to_string(n));
    auto i = m_peers.find(n);
    if (i != m_peers.end()) {
        CPPA_LOG_DEBUG("result = " << i->second.impl);
        return i->second.impl;
    }
    CPPA_LOGMF(CPPA_DEBUG, self, "result = nullptr");
    return nullptr;
}

void default_protocol::del_peer(default_peer* ptr) {
    m_peers.erase(ptr->node());
}

void default_protocol::del_acceptor(default_peer_acceptor* ptr) {
    auto i = m_acceptors.begin();
    auto e = m_acceptors.end();
    while (i != e) {
        auto& vec = i->second;
        auto last = vec.end();
        auto iter = std::find(vec.begin(), last, ptr);
        if (iter != last) vec.erase(iter);
        if (not vec.empty()) ++i;
        else i = m_acceptors.erase(i);
    }
}

void default_protocol::enqueue(const process_information& node,
                               const message_header& hdr,
                               any_tuple msg) {
    auto& entry = m_peers[node];
    if (entry.impl) {
        CPPA_REQUIRE(entry.queue != nullptr);
        if (!entry.impl->has_unwritten_data()) {
            CPPA_REQUIRE(entry.queue->empty());
            entry.impl->enqueue(hdr, msg);
            return;
        }
    }
    if (entry.queue == nullptr) entry.queue.emplace();
    entry.queue->emplace(hdr, msg);
}


actor_ptr default_protocol::remote_actor(variant_args args) {
    CPPA_LOG_TRACE("args.size() = " << args.size());
    CPPA_REQUIRE(args.size() == 2);
    auto i = args.begin();
    auto port = get<uint16_t>(*i++);
    auto& host = get<string>(*i);
    auto io = ipv4_io_stream::connect_to(host.c_str(), port);
    return remote_actor(stream_ptr_pair(io, io), {});
}

struct remote_actor_result { remote_actor_result* next; actor_ptr value; };

actor_ptr default_protocol::remote_actor(stream_ptr_pair io,
                                         variant_args args         ) {
    CPPA_LOG_TRACE("io = {" << io.first.get() << ", " << io.second.get() << "}, "
                   << "args.size() = " << args.size());
    CPPA_REQUIRE(args.size() == 0);
    static_cast<void>(args); // keep compiler happy when compiling w/o debug
    auto pinf = process_information::get();
    std::uint32_t process_id = pinf->process_id();
    // throws on error
    io.second->write(&process_id, sizeof(std::uint32_t));
    io.second->write(pinf->node_id().data(), pinf->node_id().size());
    actor_id remote_aid;
    std::uint32_t peer_pid;
    process_information::node_id_type peer_node_id;
    io.first->read(&remote_aid, sizeof(actor_id));
    io.first->read(&peer_pid, sizeof(std::uint32_t));
    io.first->read(peer_node_id.data(), peer_node_id.size());
    auto pinfptr = make_counted<process_information>(peer_pid, peer_node_id);
    if (*pinf == *pinfptr) {
        // dude, this is not a remote actor, it's a local actor!
        CPPA_LOGMF(CPPA_ERROR, self, "remote_actor() called to access a local actor");
#       ifndef CPPA_DEBUG_MODE
        std::cerr << "*** warning: remote_actor() called to access a local actor\n"
                  << std::flush;
#       endif
        return get_actor_registry()->get(remote_aid);
    }
    default_protocol* proto = this;
    intrusive::blocking_single_reader_queue<remote_actor_result> q;
    run_later([proto, io, pinfptr, remote_aid, &q] {
        CPPA_LOGC_TRACE("cppa::io::default_protocol",
                        "remote_actor$create_connection", "");
        auto pp = proto->get_peer(*pinfptr);
        CPPA_LOGF_INFO_IF(pp, "connection already exists (re-use old one)");
        if (!pp) proto->new_peer(io.first, io.second, pinfptr);
        auto res = proto->addressing()->get_or_put(*pinfptr, remote_aid);
        q.push_back(new remote_actor_result{0, res});
    });
    unique_ptr<remote_actor_result> result(q.pop());
    CPPA_LOGF_DEBUG("result = " << result->value.get());
    return result->value;
}

void default_protocol::last_proxy_exited(default_peer* pptr) {
    CPPA_REQUIRE(pptr != nullptr);
    CPPA_LOG_TRACE(CPPA_ARG(pptr)
                   << ", pptr->node() = " << to_string(pptr->node()));
    if (pptr->erase_on_last_proxy_exited() && pptr->queue().empty()) {
        stop_reader(pptr);
        auto i = m_peers.find(pptr->node());
        if (i != m_peers.end()) {
            CPPA_LOG_DEBUG_IF(i->second.impl != pptr,
                              "node " << to_string(pptr->node())
                              << " does not exist in m_peers");
            if (i->second.impl == pptr) {
                m_peers.erase(i);
            }
        }
    }
}

void default_protocol::new_peer(const input_stream_ptr& in,
                                const output_stream_ptr& out,
                                const process_information_ptr& node) {
    CPPA_LOG_TRACE("");
    auto ptr = new default_peer(this, in, out, node);
    continue_reader(ptr);
    if (node) register_peer(*node, ptr);
}

void default_protocol::continue_writer(default_peer* pptr) {
    CPPA_LOG_TRACE(CPPA_ARG(pptr));
    super::continue_writer(pptr);
}

default_actor_addressing* default_protocol::addressing() {
    return &m_addressing;
}

} } // namespace cppa::network
