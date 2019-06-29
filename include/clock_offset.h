//
// Created by oke on 6/29/19.
//

#ifndef COFETCHER_CLOCK_OFFSET_H
#define COFETCHER_CLOCK_OFFSET_H

#include <chrono>

typedef struct tp {
    int64_t initiator_time;
    int64_t receiver_time;

    int32_t initiator_round_trip_time;
    int32_t receiver_round_trip_time;

    int32_t package_nr;
} time_pkg;


time_pkg create_package();

bool get_offset(time_pkg &package, int32_t &new_offset);

bool handle_package(time_pkg &package);

#endif //COFETCHER_CLOCK_OFFSET_H
