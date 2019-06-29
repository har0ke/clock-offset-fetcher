//
// Created by oke on 6/29/19.
//
#include <chrono>

typedef struct tp {
    int64_t initiator_time;
    int64_t receiver_time;

    int32_t initiator_round_trip_time;
    int32_t receiver_round_trip_time;

    int32_t package_nr;
} time_pkg;


inline int64_t get_current_nanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

time_pkg create_package() {
    time_pkg package;
    package.initiator_time = get_current_nanoseconds();
    package.package_nr = 0;
    return package;
}

bool get_offset(time_pkg &package, int32_t &new_offset) {
    switch (package.package_nr) {
        case 2: // handle as initiator
            new_offset = package.receiver_time - package.initiator_time - package.initiator_round_trip_time / 2 ;
            break;
        case 3: // handle as receiver
            new_offset = package.initiator_time + package.initiator_round_trip_time
                         - package.receiver_time - package.receiver_round_trip_time / 2;
            break;
        case 4: // handle as initiator
            new_offset = package.initiator_time + package.initiator_round_trip_time
                         - package.receiver_time - package.receiver_round_trip_time / 2;
            new_offset = -new_offset;
            break;
        default:
            return false;
    }
    return true;
}

bool handle_package(time_pkg &package) {
    switch (package.package_nr) {
        case 0: // handle as receiver
            package.receiver_time = get_current_nanoseconds();
            break;
        case 1: // handle as initiator
            package.initiator_round_trip_time = get_current_nanoseconds() - package.initiator_time;
            break;
        case 2: // handle as receiver
            package.receiver_round_trip_time = get_current_nanoseconds() - package.receiver_time;
            break;
        case 3:
            package.package_nr++;
            // return false
        default:
            return false;
    }
    package.package_nr++;
    return true;
}
