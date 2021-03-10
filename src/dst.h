#pragma once

#include <time.h>

// Untested yet!
// Israel - according to 2013 law
int isDST(time_t timestamp) {
    struct tm* tmstruct = localtime(&timestamp);
    if (10 < tmstruct->tm_mon || tmstruct->tm_mon < 3) {
        return 0;
    }
    if (3 < tmstruct->tm_mon && tmstruct->tm_mon < 10) {
        return 1;
    }

    int last_sunday_mday = tmstruct->tm_mday - (tmstruct->tm_wday - 1) + 7;
    // Assume 31 days, October and March
    if (last_sunday_mday > 31) {
        last_sunday_mday -= 7;
    }

    if (tmstruct->tm_mon == 3) {
        if (tmstruct->tm_mday < 23) {
            return 0;
        }

        if (tmstruct->tm_mday > last_sunday_mday - 2) {
            return 1;
        }
        if (tmstruct->tm_mday < last_sunday_mday - 2) {
            return 0;
        }
        // tmstruct->tm_mday == last_sunday_mday-2
        if (tmstruct->tm_hour > 2) {
            return 1;
        }
        return 0;
    }
    if (tmstruct->tm_mon == 10) {
        if (tmstruct->tm_mday < 25) {
            return 1;
        }
        if (tmstruct->tm_mday > last_sunday_mday) {
            return 0;
        }
        if (tmstruct->tm_mday < last_sunday_mday) {
            return 1;
        }
        // tmstruct->tm_mday == last_sunday_mday
        if (tmstruct->tm_hour > 2) {
            return 0;
        }
        if (tmstruct->tm_hour > 1) {
            return -1; // ambiguous
        }
        return 1;
    }
    return -2; // unreachable
}