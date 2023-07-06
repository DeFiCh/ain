#ifndef DEFI_FFI_FFIHELPERS_H
#define DEFI_FFI_FFIHELPERS_H

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
        LogPrintf(result.reason.c_str()); \
        return false; \
    } \
    return true; \
}();

#endif  // DEFI_FFI_FFIHELPERS_H

