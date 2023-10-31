#ifndef DEFI_FFI_FFIHELPERS_H
#define DEFI_FFI_FFIHELPERS_H

#include <ain_rs_exports.h>
#include <dfi/res.h>

#define XResultThrowOnErr(x)                                 \
    [&]() {                                                  \
        CrossBoundaryResult result;                          \
        x;                                                   \
        if (!result.ok) {                                    \
            throw std::runtime_error(result.reason.c_str()); \
        }                                                    \
    }();

#define XResultStatus(x)                                         \
    [&]() {                                                      \
        CrossBoundaryResult result;                              \
        x;                                                       \
        if (!result.ok) {                                        \
            return Res::Err("XR:: %s\n", result.reason.c_str()); \
        }                                                        \
        return Res::Ok();                                        \
    }();

#define XResultStatusLogged(x)                                   \
    [&]() {                                                      \
        CrossBoundaryResult result;                              \
        x;                                                       \
        if (!result.ok) {                                        \
            LogPrintf("XR:: %s\n", result.reason.c_str());       \
            return Res::Err("XR:: %s\n", result.reason.c_str()); \
        }                                                        \
        return Res::Ok();                                        \
    }();

#define XResultValue(x)                                                                 \
    [&]() {                                                                             \
        CrossBoundaryResult result;                                                     \
        auto res = x;                                                                   \
        if (!result.ok) {                                                               \
            return ResVal<decltype(res)>(Res::Err("XR:: %s\n", result.reason.c_str())); \
        }                                                                               \
        return ResVal(std::move(res), Res::Ok());                                       \
    }();

#define XResultValueLogged(x)                                                           \
    [&]() {                                                                             \
        CrossBoundaryResult result;                                                     \
        auto res = x;                                                                   \
        if (!result.ok) {                                                               \
            LogPrintf("XR:: %s\n", result.reason.c_str());                              \
            return ResVal<decltype(res)>(Res::Err("XR:: %s\n", result.reason.c_str())); \
        }                                                                               \
        return ResVal(std::move(res), Res::Ok());                                       \
    }();

#endif  // DEFI_FFI_FFIHELPERS_H
