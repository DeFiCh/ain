#include "grpc/util.h"

double FromAmount(CAmount amount) {
    bool sign = amount < 0;
    auto n_abs = (sign ? -amount : amount);
    return n_abs / (double)COIN;
}
