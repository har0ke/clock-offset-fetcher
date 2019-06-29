//
// Created by oke on 6/29/19.
//

#ifndef COFETCHER_CLOCK_OFFSET_UDP_SERVER_H
#define COFETCHER_CLOCK_OFFSET_UDP_SERVER_H

#include "asio.hpp"
#include "clock_offset.h"
#include <iostream>
#include <map>
#include <queue>
#include <list>
#include <random>

namespace cofetcher {

    typedef asio::ip::udp::endpoint endpoint;

    class ClockOffsetService { // TODO: handle failed sends

    public:
        typedef asio::steady_timer const * const_tr_handle;
        typedef std::shared_ptr<asio::steady_timer> shared_tr_handle;
        
        typedef const_tr_handle tr_handle;
        
        /**
         * Constructor
         * @param port port to run udp server on
         * @param offset_counts maximum amount of offsets to keep for each servie
         */
        // TODO: discard all offsets older than a specified time
        ClockOffsetService(uint16_t port, uint16_t offset_counts, uint16_t max_repetition_interval = 5);

        /**
         * keep sending time request to a specific endpoint
         * @param endpoint endpoint to send the requests to
         * @return handle to cancel new time requests
         */
        tr_handle init_iterative_time_request(const asio::ip::udp::endpoint &endpoint);

        /**
         * cancle time requests to an endpoint
         * @param handle handle of request to cancel
         */
        void cancel_iterative_time_requests(const tr_handle &handle);

        /**
         * asynchronously send a time request to an endpoint
         * @param endpoint endpoint to send time request to
         */
        void init_single_time_request(const asio::ip::udp::endpoint &endpoint);

        /**
         * @return handles for every active iterative time request
         */
        std::vector<tr_handle> get_time_request_handles();

        /**
         * fetch offset for a specific endpoint
         * @param endpoint endpoint to fetch offset for
         * @return the offset to the clock of an endpoint
         */
        int32_t get_offset_for(const asio::ip::udp::endpoint &endpoint);

        /**
         * @return average offsets that were collected for each endpoint
         */
        std::map<asio::ip::udp::endpoint, int32_t> get_offsets();

        /**
         * listen for and send own time requests.
         */
        void run();

        template <typename Rep, typename Period>
        void run_for(std::chrono::duration<Rep, Period> d) {
            service.run_for(d);
        }


    private:
        // keep sending time requests to endpoint
        void iterative_time_request(const asio::ip::udp::endpoint &endpoint, shared_tr_handle &timer);

        // initiate new receive
        void receive();

        // handle a received time package
        void receive_handler(const asio::error_code &error, std::size_t bytes_transferred);

        // send a time package to a endpoint
        void send(time_pkg package, const asio::ip::udp::endpoint &endpoint);

        // io service that runs this service
        asio::io_service service;

        // socket of service
        asio::ip::udp::socket socket;

        // buffer for receive operations
        asio::ip::udp::endpoint sender_endpoint;
        std::array<char, sizeof(time_pkg)> buffer{};

        // map to collect offsets of all endpoints in question
        // TODO: user of the library should get more control over this data
        std::map<asio::ip::udp::endpoint, std::list<int32_t>> offset_maps;
        // parameter on how many offsets should be saved for each endpoint
        uint16_t offset_counts;

        // tr_handles for iterative time requests
        std::mutex tr_handles_mutex;
        std::list<shared_tr_handle> tr_handles;

        // to randomly send time requests for iterative time request so not all requests are aligned
        std::random_device rd;
        std::mt19937 mt;
        std::uniform_real_distribution<float> dist;
    };

}

#endif //COFETCHER_CLOCK_OFFSET_UDP_SERVER_H
