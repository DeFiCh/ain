#ifndef DEFI_MASTERNODES_RES_H
#define DEFI_MASTERNODES_RES_H

#include <tinyformat.h>
#include <optional>
#include <string>

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

#endif  // DEFI_MASTERNODES_RES_H
