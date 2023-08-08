#ifndef DEFI_FFI_FFIHELPERS_H
#define DEFI_FFI_FFIHELPERS_H

#include <masternodes/res.h>
#include <ain_rs_exports.h>

#define CrossBoundaryCheckedThrow(x) { \
    CrossBoundaryResult result; \
    x; \
    if (!result.ok) { \
        throw std::runtime_error(result.reason.c_str()); \
    } \
}

#define CrossBoundaryChecked(x) [&]() { \
    CrossBoundaryResult result; \
    x; \
    if (!result.ok) { \
        LogPrintf("%s\n", result.reason.c_str()); \
        return false; \
    } \
    return true; \
}();

#define CrossBoundaryResVal(x) [&]() { \
    CrossBoundaryResult result; \
    auto res = x; \
    if (!result.ok) { \
        return ResVal<decltype(res)>(Res::Err("%s\n", result.reason.c_str())); \
    } \
    return ResVal(std::move(res), Res::Ok()); \
}();

#define CrossBoundaryResValChecked(x) [&]() { \
    CrossBoundaryResult result; \
    auto res = x; \
    if (!result.ok) { \
        LogPrintf("%s\n", result.reason.c_str()); \
        return ResVal<decltype(res)>(Res::Err("%s\n", result.reason.c_str())); \
    } \
    return ResVal(std::move(res), Res::Ok()); \
}();

#endif  // DEFI_FFI_FFIHELPERS_H

