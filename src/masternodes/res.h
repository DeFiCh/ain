#ifndef DEFI_MASTERNODES_RES_H
#define DEFI_MASTERNODES_RES_H

#include <optional>
#include <string>
#include <tinyformat.h>

struct Res
{
    bool ok;
    std::string msg;
    uint32_t code;
    std::string dbgMsg; // CValidationState support

    Res() = delete;

    operator bool() const {
        return ok;
    }

    template<typename... Args>
    static Res Err(std::string const & err, const Args&... args) {
        return Res{false, tfm::format(err, args...), 0, {} };
    }

    template<typename... Args>
    static Res ErrCode(uint32_t code, std::string const & err, const Args&... args) {
        return Res{false, tfm::format(err, args...), code, {} };
    }

    // extended version just for CValidationState support
    template<typename... Args>
    static Res ErrDbg(std::string const & debugMsg, std::string const & err, const Args&... args) {
        return {false, tfm::format(err, args...), 0, debugMsg };
    }

    template<typename... Args>
    static Res Ok(std::string const & msg, const Args&... args) {
        return Res{true, tfm::format(msg, args...), 0, {} };
    }

    static Res Ok() {
        return Res{true, {}, 0, {} };
    }
};

template <typename T>
struct ResVal : public Res
{
    std::optional<T> val{};

    ResVal() = delete;

    ResVal(Res const & errRes) : Res(errRes) {
        assert(!this->ok); // if value is not provided, then it's always an error
    }
    ResVal(T value, Res const & okRes) : Res(okRes), val(std::move(value)) {
        assert(this->ok); // if value if provided, then it's never an error
    }

    operator bool() const {
        return ok;
    }

    operator T() const {
        assert(ok);
        return *val;
    }

    template <typename F>
    T ValOrException(F&& func) const {
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

#endif //DEFI_MASTERNODES_RES_H
