// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only
#include "TrimDial.h"

#include <cmath>
#include <cstdio>
#include <limits>

int main() {
    int failures = 0;
    const auto check = [&failures](bool ok, const char* label) {
        std::printf("TRIM_DIAL %s %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failures;
    };
    const auto near = [](double a, double b) {
        return std::abs(a - b) < 1.0e-9;
    };
    check(near(lean::trimDialPosition(0.0), 0.0), "minimum is start");
    check(near(lean::trimDialPosition(0.5), 0.25), "half-unity on left");
    check(near(lean::trimDialPosition(1.0), 0.5), "unity gain exactly at noon");
    check(near(lean::trimDialPosition(1.125), 0.75), "boost midpoint");
    check(near(lean::trimDialPosition(1.25), 1.0), "maximum is end");
    check(near(lean::trimDialPosition(-1.0), 0.0), "out-of-range low clamped");
    check(near(lean::trimDialPosition(5.0), 1.0), "out-of-range high clamped");
    check(near(lean::trimDialPosition(std::numeric_limits<double>::quiet_NaN()), 0.5),
          "nonfinite safe");
    return failures == 0 ? 0 : 1;
}
