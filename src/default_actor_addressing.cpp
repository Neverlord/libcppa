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


#include <cstdint>

#include "cppa/logging.hpp"
#include "cppa/to_string.hpp"
#include "cppa/serializer.hpp"
#include "cppa/deserializer.hpp"
#include "cppa/primitive_variant.hpp"

#include "cppa/io/default_actor_proxy.hpp"
#include "cppa/io/default_actor_addressing.hpp"

#include "cppa/detail/actor_registry.hpp"
#include "cppa/detail/singleton_manager.hpp"

using namespace std;

namespace cppa { namespace io {

default_actor_addressing::default_actor_addressing(default_protocol* parent)
: m_parent(parent), m_pinf(process_information::get()) { }

atom_value default_actor_addressing::technology_id() const {
    return atom("DEFAULT");
}

void default_actor_addressing::write(serializer* sink, const actor_ptr& ptr) {
    CPPA_REQUIRE(sink != nullptr);
    if (ptr == nullptr) {
        CPPA_LOG_DEBUG("serialize nullptr");
        sink->write_value(static_cast<actor_id>(0));
        process_information::serialize_invalid(sink);
    }
    else {
        // local actor?
        if (!ptr->is_proxy()) {
            get_actor_registry()->put(ptr->id(), ptr);
        }
        auto pinf = m_pinf;
        if (ptr->is_proxy()) {
            auto dptr = ptr.downcast<default_actor_proxy>();
            if (dptr) pinf = dptr->process_info();
            else CPPA_LOG_ERROR("downcast failed");
        }
        sink->write_value(ptr->id());
        sink->write_value(pinf->process_id());
        sink->write_raw(process_information::node_id_size,
                        pinf->node_id().data());
    }
}

actor_ptr default_actor_addressing::read(deserializer* source) {
    CPPA_REQUIRE(source != nullptr);
    process_information::node_id_type nid;
    auto aid = source->read<uint32_t>();
    auto pid = source->read<uint32_t>();
    source->read_raw(process_information::node_id_size, nid.data());
    // local actor?
    auto pinf = process_information::get();
    if (aid == 0 && pid == 0) {
        return nullptr;
    }
    else if (pid == pinf->process_id() && nid == pinf->node_id()) {
        return get_actor_registry()->get(aid);
    }
    else {
        process_information tmp{pid, nid};
        return get_or_put(tmp, aid);
    }
}

size_t default_actor_addressing::count_proxies(const process_information& inf) {
    auto i = m_proxies.find(inf);
    return (i != m_proxies.end()) ? i->second.size() : 0;
}

actor_ptr default_actor_addressing::get(const process_information& inf,
                                        actor_id aid) {
    auto& submap = m_proxies[inf];
    auto i = submap.find(aid);
    if (i != submap.end()) {
        auto result = i->second.promote();
        CPPA_LOGMF_IF(!result, CPPA_INFO, self, "proxy instance expired; "
                      << CPPA_TARG(inf, to_string) << ", "<< CPPA_ARG(aid));
        if (!result) submap.erase(i);
        return result;
    }
    return nullptr;
}

void default_actor_addressing::put(const process_information& node,
                                   actor_id aid,
                                   const actor_proxy_ptr& proxy) {
    auto& submap = m_proxies[node];
    auto i = submap.find(aid);
    if (i == submap.end()) {
        submap.insert(make_pair(aid, proxy));
        m_parent->enqueue(node,
                          {nullptr, nullptr},
                          make_any_tuple(atom("MONITOR"),
                                         process_information::get(),
                                         aid));
    }
    else {
        CPPA_LOGMF(CPPA_ERROR, self, "proxy for " << aid << ":"
                   << to_string(node) << " already exists");
    }
}

actor_ptr default_actor_addressing::get_or_put(const process_information& inf,
                                               actor_id aid) {
    auto result = get(inf, aid);
    if (result == nullptr) {
        CPPA_LOGMF(CPPA_INFO, self, "created new proxy instance; "
                   << CPPA_TARG(inf, to_string) << ", " << CPPA_ARG(aid));
        if (m_parent == nullptr) {
            CPPA_LOG_ERROR("m_parent == nullptr (cannot create proxy without MM)");
        }
        else {
            auto ptr = make_counted<default_actor_proxy>(aid, new process_information(inf), m_parent);
            put(inf, aid, ptr);
            result = ptr;
        }
    }
    return result;
}

auto default_actor_addressing::proxies(process_information& i) -> proxy_map& {
    return m_proxies[i];
}

void default_actor_addressing::erase(process_information& inf) {
    CPPA_LOGMF(CPPA_TRACE, self, CPPA_TARG(inf, to_string));
    m_proxies.erase(inf);
}

void default_actor_addressing::erase(process_information& inf, actor_id aid) {
    CPPA_LOGMF(CPPA_TRACE, self, CPPA_TARG(inf, to_string) << ", " << CPPA_ARG(aid));
    auto i = m_proxies.find(inf);
    if (i != m_proxies.end()) {
        i->second.erase(aid);
    }
}

} } // namespace cppa::network
