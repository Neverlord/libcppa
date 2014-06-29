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
 * Copyright (C) 2011 - 2014                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the Boost Software License, Version 1.0. See             *
 * accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt  *
\******************************************************************************/

#ifndef CPPA_IO_BASP_BROKER_HPP
#define CPPA_IO_BASP_BROKER_HPP

#include <map>
#include <set>
#include <string>
#include <future>
#include <vector>

#include "cppa/actor_namespace.hpp"
#include "cppa/binary_serializer.hpp"
#include "cppa/binary_deserializer.hpp"

#include "cppa/io/basp.hpp"
#include "cppa/io/broker.hpp"
#include "cppa/io/remote_actor_proxy.hpp"

namespace cppa {
namespace io {

/**
 * @brief A broker implementation for the Binary Actor System Protocol (BASP).
 */
class basp_broker : public broker, public actor_namespace::backend {

    using super = broker;

 public:

    using id_type = node_id;

    basp_broker();

    behavior make_behavior() override;

    template<class SocketAcceptor>
    void publish(abstract_actor_ptr whom, SocketAcceptor fd) {
        auto hdl = add_acceptor(std::move(fd));
        announce_published_actor(hdl, whom);
    }

    actor_proxy_ptr make_proxy(const id_type&, actor_id) override;

    // dispatches a message from a local actor to a remote node
    void dispatch(const actor_addr& from, const actor_addr& to,
                  message_id mid, const message& msg);

    struct client_handshake_data {
        id_type remote_id;
        std::promise<abstract_actor_ptr>* result;
        std::string* error_msg;
        const std::set<std::string>* expected_ifs;

    };

    void init_client(connection_handle hdl, client_handshake_data* data);

 private:

    void erase_proxy(const id_type& nid, actor_id aid);

    // dispatches a message from a remote node to a local actor
    void dispatch(const basp::header& msg, message&& payload);

    enum connection_state {
        // client just started, await handshake from server
        await_server_handshake,
        // server accepted new connection and sent handshake, await response
        await_client_handshake,
        // connection established, read series of broker messages
        await_header,
        // currently waiting for payload of a received message
        await_payload,
        // connection is going to be shut down because of an error
        close_connection

    };

    struct connection_context {
        connection_state state;
        connection_handle hdl;
        id_type remote_id;
        client_handshake_data* handshake_data;
        basp::header hdr;
        // keep a reference to the published actor of
        // the remote node to prevent this particular
        // proxy instance from expiring; this prevents
        // a bug where re-using an "old" connection via
        // remote_actor() could return an expired proxy
        actor published_actor;

    };

    void read(binary_deserializer& bs, basp::header& msg);

    void write(binary_serializer& bs, const basp::header& msg);

    void send(const connection_context& ctx, const basp::header& msg,
              message payload);

    void send_kill_proxy_instance(const id_type& nid, actor_id aid,
                                  uint32_t reason);

    connection_state handle_basp_header(connection_context& ctx,
                                        const buffer_type* payload = nullptr);

    optional<skip_message_t> add_monitor(connection_context& ctx,
                                                actor_id aid);

    optional<skip_message_t> kill_proxy(connection_context& ctx,
                                               actor_id aid,
                                               std::uint32_t reason);

    void announce_published_actor(accept_handle hdl,
                                  const abstract_actor_ptr& whom);

    void new_data(connection_context& ctx, buffer_type& buf);

    void init_handshake_as_client(connection_context& ctx,
                                  client_handshake_data* ptr);

    void init_handshake_as_sever(connection_context& ctx,
                                 actor_addr published_actor);

    void serialize_msg(const actor_addr& sender, message_id mid,
                       const message& msg, buffer_type& wr_buf);

    bool try_set_default_route(const id_type& nid, connection_handle hdl);

    void add_route(const id_type& nid, connection_handle hdl);

    connection_handle get_route(const id_type& dest);

    using blacklist_entry = std::pair<id_type, connection_handle>;

    using routing_table_entry = std::pair<connection_handle /* default route */,
                                          std::set<connection_handle>>;

    struct blacklist_less {
        inline bool operator()(const blacklist_entry& lhs,
                               const blacklist_entry& rhs) const {
            if (lhs.first < rhs.first) return lhs.second < rhs.second;
            return false;
        }

    };

    using routing_table = std::map<id_type,              /* dest */
                                   routing_table_entry>; /* hops */

    using pending_request = std::pair<actor_addr,  /* sender  */
                                      message_id>; /* req. ID */

    actor_namespace m_namespace; // manages proxies
    std::map<connection_handle, connection_context> m_ctx;
    std::map<accept_handle, abstract_actor_ptr>
    m_published_actors;
    routing_table m_routes; // stores non-direct routes
    std::set<blacklist_entry, blacklist_less> m_blacklist; // stores invalidated
                                                           // routes
    std::set<pending_request> m_pending_requests;
    std::map<id_type, connection_handle> m_nodes;

    // needed to keep track to which node we are talking to at the moment
    connection_context* m_current_context;

    // cache some UTIs to make serialization a bit faster
    const uniform_type_info* m_meta_hdr;
    const uniform_type_info* m_meta_msg;
    const uniform_type_info* m_meta_id_type;

};

} // namespace io
} // namespace cppa

#endif // CPPA_IO_BASP_BROKER_HPP
