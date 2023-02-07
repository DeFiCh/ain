#ifndef DEFI_MASTERNODES_RES_H
#define DEFI_MASTERNODES_RES_H

#include <functional>
#include <optional>
#include <string>
#include <tinyformat.h>

struct Res {
    bool ok;
    std::string msg;
    uint32_t code;
    std::string dbgMsg;  // CValidationState support

    Res() = delete;

    operator bool() const { return ok; }

    template <typename... Args>
    static Res Err(const std::string &err, const Args &...args) {
        return Res{false, tfm::format(err, args...), 0, {}};
    }

    template <typename... Args>
    static Res ErrCode(uint32_t code, const std::string &err, const Args &...args) {
        return Res{false, tfm::format(err, args...), code, {}};
    }

    // extended version just for CValidationState support
    template <typename... Args>
    static Res ErrDbg(const std::string &debugMsg, const std::string &err, const Args &...args) {
        return {false, tfm::format(err, args...), 0, debugMsg};
    }

    template <typename... Args>
    static Res Ok(const std::string &msg, const Args &...args) {
        return Res{true, tfm::format(msg, args...), 0, {}};
    }

    template <typename T, size_t... I>
    static Res Err(const T &t, std::index_sequence<I...>) {
        using arg0 = std::tuple_element_t<0, std::decay_t<decltype(t)>>;
        if constexpr (std::is_convertible_v<arg0, uint32_t>) {
            return Res::ErrCode(std::get<I>(t)...);
        } else {
            return Res::Err(std::get<I>(t)...);
        }
    }

    static Res Ok() { return Res{true, {}, 0, {}}; }
};

template <typename T>
struct ResVal : public Res {
    std::optional<T> val{};

    ResVal() = delete;

    ResVal(const Res &errRes)
        : Res(errRes) {
        assert(!this->ok);  // if value is not provided, then it's always an error
    }
    ResVal(T value, const Res &okRes)
        : Res(okRes),
          val(std::move(value)) {
        assert(this->ok);  // if value if provided, then it's never an error
    }

    operator bool() const { return ok; }

    operator T() const {
        assert(ok);
        return *val;
    }

    const T &operator*() const {
        assert(ok);
        return *val;
    }

    const T *operator->() const {
        assert(ok);
        return &(*val);
    }

    T &operator*() {
        assert(ok);
        return *val;
    }

    template <typename F>
    T ValOrException(F &&func) const {
        if (!ok) {
            throw func(code, msg);
        }
        return *val;
    }

    T ValOrDefault(T default_) const {
        if (!ok) {
            return std::move(default_);
        }
        return *val;
    }
};

template <typename T, typename... Args>
Res CheckRes(T &&res, std::tuple<Args...> &&args) {
    if (res) {
        return Res::Ok();
    }
    constexpr auto size = sizeof...(Args);
    if constexpr (size == 0) {
        static_assert(std::is_convertible_v<T, Res>);
        return std::forward<T>(res);
    } else if constexpr (std::is_invocable_r_v<std::string, std::tuple_element_t<0, std::tuple<Args...>>, std::string>) {
        static_assert(std::is_convertible_v<T, Res>);
        return Res::Err(std::invoke(std::get<0>(args), res.msg));
    } else if constexpr (size == 1 && std::is_invocable_r_v<std::string, std::tuple_element_t<0, std::tuple<Args...>>>) {
        return Res::Err(std::invoke(std::get<0>(args)));
    } else {
        return Res::Err(args, std::make_index_sequence<size>{});
    }
}

#define Require(x, ...)                                                       \
    do {                                                                      \
        if (auto __res = ::CheckRes(x, std::make_tuple(__VA_ARGS__)); !__res) \
            return __res;                                                     \
    } while (0)

#endif  // DEFI_MASTERNODES_RES_H
