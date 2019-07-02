//
// Created by oke on 6/29/19.
//

#include "clock_offset_udp_server.h"

constexpr const char * COSERVER_TAG = "ClockOffsetFetcherUDPServer";

namespace cofetcher {

    ClockOffsetService::ClockOffsetService(uint16_t port, uint16_t offset_counts, uint16_t max_repetition_interval)
            : service(), socket(service, asio::ip::udp::endpoint(asio::ip::udp::v4(), port)),
              tr_handles(), rd(), mt(rd()), dist(std::max(1., max_repetition_interval - 6.), std::min(1., (double) max_repetition_interval)), offset_counts(offset_counts) {
        receive();
    }

    ClockOffsetService::tr_handle
    ClockOffsetService::init_iterative_time_request(const asio::ip::udp::endpoint &endpoint) {

        std::lock_guard<std::mutex> guard(tr_handles_mutex);
        tr_handles.emplace_back(service, std::chrono::seconds(1));
        tr_handle &handle = --tr_handles.end();
        iterative_time_request(endpoint, handle);
        handle->expires_from_now(std::chrono::seconds(0));
        return handle;
    }

    void ClockOffsetService::iterative_time_request(const asio::ip::udp::endpoint &endpoint,
                                                    tr_handle &handle) {
        handle->expires_from_now(std::chrono::seconds((int) dist(mt)));
        handle->async_wait([this, endpoint, handle](const asio::error_code &error) {
            std::lock_guard<std::mutex> guard(tr_handles_mutex);
            for(auto it = tr_handles.begin(); it != tr_handles.end(); it++) {
                if (it == handle) {
                    this->init_single_time_request(endpoint);
                    this->iterative_time_request(endpoint, handle);
                }
            }
        });
    }

    void ClockOffsetService::cancel_iterative_time_requests(const ClockOffsetService::tr_handle &handle) {
        handle->cancel();

        std::lock_guard<std::mutex> guard(tr_handles_mutex);
        tr_handles.erase(handle);
    }

    std::size_t ClockOffsetService::num_iterative_time_request() {
        return tr_handles.size();
    }

    void ClockOffsetService::init_single_time_request(const asio::ip::udp::endpoint &endpoint) {
        time_pkg pkg = create_package();
        send(pkg, endpoint);
    }

    int32_t ClockOffsetService::get_offset_for(const asio::ip::udp::endpoint &endpoint) {
        double s2 = 0;
        double mean = 0;
        std::lock_guard<std::mutex> guard(offset_maps_mutex);
        for (int32_t &o : offset_maps[endpoint]) {
            mean += (float) o / offset_maps[endpoint].size();
            s2 += (float) o * o / offset_maps[endpoint].size();
        }
        double s = std::sqrt(s2);
        double corrected_mean = mean;
        for (int32_t &o : offset_maps[endpoint]) {
            if (std::abs(o - mean) > 2 * s) {
                corrected_mean -= (float) o / offset_maps[endpoint].size();
            }
        }
        return (int32_t) corrected_mean;
    }

    std::map<asio::ip::udp::endpoint, int32_t> ClockOffsetService::get_offsets() {
        std::list<endpoint> endpoints;

        {
            std::lock_guard<std::mutex> guard(offset_maps_mutex);
            for (auto &it : offset_maps) endpoints.push_back(it.first);
        }

        std::map<asio::ip::udp::endpoint, int32_t> offsets;
        for (auto &endpoint : endpoints) {
            offsets[endpoint] = get_offset_for(endpoint);
        }

        return offsets;
    }

    void ClockOffsetService::run() {
        service.run();
    }


    /**
     * subscribe to new offsets
     * @param callback callback to call if new offset was received.
     *      don't do much work in callback or messages might be delayed.
     */
    ClockOffsetService::callback_handle ClockOffsetService::subscribe(cofetcher_callback callback) {
        std::lock_guard<std::mutex> guard(callbacks_mutex);
        callbacks.push_back(callback);
        return --callbacks.end();
    }

    /**
     * remove a subscription
     * @param callback the callback that is receiving offsets
     */
    void ClockOffsetService::unsubscribe(ClockOffsetService::callback_handle &callback) {
        std::lock_guard<std::mutex> guard(callbacks_mutex);
        callbacks.erase(callback);
    }

    /**
     * @return number of callbacks that are subscribing to new offsets.
     */
    std::size_t ClockOffsetService::get_callback_num() const {
        return callbacks.size();
    }

    void ClockOffsetService::receive() {
        socket.async_receive_from(asio::buffer(buffer), sender_endpoint,
                                  [this](const asio::error_code &error, std::size_t bytes_transferred) {
                                      this->receive_handler(error, bytes_transferred);
                                  });
    }


    void ClockOffsetService::receive_handler(const asio::error_code &error, std::size_t bytes_transferred) {

        if(error) {
#ifdef COFETCHER_DEBUG
            std::cerr << COSERVER_TAG << "Error(" << error << ") occoured receiving a message. Ignoring." << std::endl;
#endif
            return;
        }

        if(bytes_transferred != sizeof(time_pkg)) {
#ifdef COFETCHER_DEBUG
            std::cerr << COSERVER_TAG << "Received message with invalid size. Ignoring";
#endif
            return;
        }

        time_pkg &package = *(time_pkg *) buffer.begin();
        if (handle_package(package)) {
            send(package, sender_endpoint);
        }

        int32_t offset;
        if (get_offset(package, offset)) {
            {
                std::lock_guard<std::mutex> guard1(offset_maps_mutex);
                offset_maps[sender_endpoint].push_back(offset);
                while (offset_maps[sender_endpoint].size() > offset_counts)
                    offset_maps[sender_endpoint].pop_front();
            }
            {
                std::lock_guard<std::mutex> guard(callbacks_mutex);
                if (!callbacks.empty()) {
                    int32_t filtered_offset = get_offset_for(sender_endpoint);
                    for (auto &callback : callbacks) {
                        callback(sender_endpoint, offset, filtered_offset);
                    }
                }
            }
        }

        receive();
    }

    void send_handler(const asio::error_code &error, std::size_t bytes_transferred) {

        if(error) {
#ifdef COFETCHER_DEBUG
            std::cerr << COSERVER_TAG << "Error(" << error << ") occoured sending a message. Ignoring." << std::endl;
#endif
        }

        if(bytes_transferred != sizeof(time_pkg)) {
#ifdef COFETCHER_DEBUG
            std::cerr << COSERVER_TAG << "Send message with invalid size.";
#endif
        }

    }

    void ClockOffsetService::send(time_pkg package, const asio::ip::udp::endpoint &endpoint) {
        std::vector<char> data((char *) &package, (char *) &package + sizeof(time_pkg));
        socket.async_send_to(asio::buffer(data), endpoint, send_handler);
    }

}