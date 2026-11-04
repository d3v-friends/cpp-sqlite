#pragma once
#include <optional>
#include <ctls/ctls.hpp>

namespace sql {
    template <class T> std::optional<T> cursor<T>::next() {
        // todo 구현하기
        ILOG_ERROR("not implemented");
        return std::nullopt;
    }
} // namespace sql