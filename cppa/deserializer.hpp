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

#ifndef CPPA_DESERIALIZER_HPP
#define CPPA_DESERIALIZER_HPP

#include <string>
#include <cstddef>

#include "cppa/primitive_variant.hpp"
#include "cppa/uniform_type_info.hpp"

namespace cppa {

class object;
class actor_namespace;
class uniform_type_info;

/**
 * @ingroup TypeSystem
 * @brief Technology-independent deserialization interface.
 */
class deserializer {

    deserializer(const deserializer&) = delete;
    deserializer& operator=(const deserializer&) = delete;

 public:

    deserializer(actor_namespace* ns = nullptr);

    virtual ~deserializer();

    /**
     * @brief Begins deserialization of a new object.
     */
    virtual const uniform_type_info* begin_object() = 0;

    /**
     * @brief Ends deserialization of an object.
     */
    virtual void end_object() = 0;

    /**
     * @brief Begins deserialization of a sequence.
     * @returns The size of the sequence.
     */
    virtual size_t begin_sequence() = 0;

    /**
     * @brief Ends deserialization of a sequence.
     */
    virtual void end_sequence() = 0;

    /**
     * @brief Reads a primitive value from the data source.
     */
    virtual void read_value(primitive_variant& storage) = 0;

    /**
     * @brief Reads a value of type @p T from the data source.
     * @note @p T must be a primitive type.
     */
    template<typename T>
    inline T read() {
        primitive_variant val{T()};
        read_value(val);
        return std::move(get<T>(val));
    }

    template<typename T>
    inline T read(const uniform_type_info* uti) {
        T result;
        uti->deserialize(&result, uti);
        return result;
    }

    template<typename T>
    inline deserializer& read(T& storage) {
        primitive_variant val{T()};
        read_value(val);
        storage = std::move(get<T>(val));
        return *this;
    }

    template<typename T>
    inline deserializer& read(T& storage, const uniform_type_info* uti) {
        uti->deserialize(&storage, this);
        return *this;
    }

    /**
     * @brief Reads a raw memory block.
     */
    virtual void read_raw(size_t num_bytes, void* storage) = 0;

    inline actor_namespace* get_namespace() { return m_namespace; }

    template<class Buffer>
    void read_raw(size_t num_bytes, Buffer& storage) {
        storage.resize(num_bytes);
        read_raw(num_bytes, storage.data());
    }

 private:

    actor_namespace* m_namespace;

};

} // namespace cppa

#endif // CPPA_DESERIALIZER_HPP
