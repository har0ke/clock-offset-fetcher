//
// Created by oke on 01.07.19.
//


#include "gtest/gtest.h"
#include "clock_offset_udp_server.h"

TEST(sample_test_case, iterative_time_requests)
{
    cofetcher::ClockOffsetService service1(3000, 1, 1);
    cofetcher::ClockOffsetService service2(3001, 20, 1);

    std::list<cofetcher::ClockOffsetService::tr_handle> handles;

    cofetcher::endpoint endpoint1(asio::ip::make_address("0.0.0.0"), 3001);
    cofetcher::ClockOffsetService::tr_handle h1 =
            service1.init_iterative_time_request(endpoint1);
    ASSERT_EQ(service1.get_time_request_handles().size(), 1);

    cofetcher::endpoint endpoint2(asio::ip::make_address("0.0.0.0"), 3000);
    cofetcher::ClockOffsetService::tr_handle h2 =
            service2.init_iterative_time_request(endpoint2);
    ASSERT_EQ(service2.get_time_request_handles().size(), 1);

    cofetcher::endpoint endpoint3(asio::ip::make_address("0.0.0.0"), 3002);
    cofetcher::ClockOffsetService::tr_handle h3 =
            service2.init_iterative_time_request(endpoint3);
    ASSERT_EQ(service2.get_time_request_handles().size(), 2);

    service2.cancel_iterative_time_requests(h3);
    ASSERT_EQ(service2.get_time_request_handles().size(), 1);

    std::thread thread([&]{
        service1.run_for(std::chrono::seconds(2));
    });

    std::thread thread2([&]{
        service2.run_for(std::chrono::seconds(2));
    });

    thread.join();
    thread2.join();

    auto offsets1 = service1.get_offsets();
    ASSERT_EQ(offsets1.size(), 1);
    ASSERT_LT(std::abs(offsets1.begin()->second), 1 * 1000 * 1000);

    auto offsets2 = service1.get_offsets();
    ASSERT_EQ(offsets1.size(), 1);
    ASSERT_LT(std::abs(offsets2.begin()->second), 1 * 1000 * 1000);

    service1.cancel_iterative_time_requests(h1);
    ASSERT_EQ(service1.get_time_request_handles().size(), 0);
    service2.cancel_iterative_time_requests(h2);
    ASSERT_EQ(service2.get_time_request_handles().size(), 0);
}

TEST(sample_test_case, callbacks) {

    cofetcher::ClockOffsetService service1(3000, 1, 1);

    service1.init_iterative_time_request(cofetcher::endpoint(asio::ip::make_address("0.0.0.0"), 3000));

    int callback_calls = 0;

    auto callback = service1.subscribe([&callback_calls](cofetcher::endpoint &endpoint, int32_t offset, int32_t filterd_offset) {
        callback_calls++;
    });

    ASSERT_EQ(service1.get_callback_num(), 1);

    service1.run_for(std::chrono::milliseconds(200));


    ASSERT_GT(callback_calls, 0);

    service1.unsubscribe(callback);

    ASSERT_EQ(service1.get_callback_num(), 0);
}
