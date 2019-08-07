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
        tr_handle::type handle_value;
        {
            // create new handle
            std::lock_guard<std::mutex> guard(tr_handles_mutex);
            tr_handles.emplace_back(service);
            handle_value = &*--tr_handles.end();
        }
        iterative_time_request(endpoint, handle_value);
        // initiate first request as soon as possible
        handle_value->expires_from_now(std::chrono::seconds(0));
        return tr_handle(handle_value);
    }

    void ClockOffsetService::iterative_time_request(const asio::ip::udp::endpoint endpoint,
                                                    const tr_handle::type handle) {
        handle->expires_from_now(std::chrono::seconds((int) dist(mt)));
        handle->async_wait([this, endpoint, handle](const asio::error_code &error) {
            std::lock_guard<std::mutex> guard(tr_handles_mutex);
            // need to check for handle for the case that the handle was erased while entering this method
            // in that case handle would be a invalid pointer
            auto it = std::find_if(tr_handles.begin(), tr_handles.end(),
                    [&handle](const auto &item) { return &item == handle; });
            if(it != tr_handles.end()) {
                this->init_single_time_request(endpoint);
                this->iterative_time_request(endpoint, handle);
            }
        });
    }

    void ClockOffsetService::cancel_iterative_time_requests(const ClockOffsetService::tr_handle &handle) {
        handle.handle_value->cancel();

        std::lock_guard<std::mutex> guard(tr_handles_mutex);
        tr_handles.erase(std::find_if(tr_handles.begin(), tr_handles.end(),
            [&handle](const auto &item) { return &item == handle.handle_value; }));
    }

    std::size_t ClockOffsetService::num_iterative_time_request() {
        std::lock_guard<std::mutex> guard(tr_handles_mutex);
        return tr_handles.size();
    }

    void ClockOffsetService::init_single_time_request(const asio::ip::udp::endpoint &endpoint) {
        time_pkg pkg = create_package();
        send_async(pkg, endpoint);
    }

    int32_t ClockOffsetService::get_offset_for(const asio::ip::udp::endpoint &endpoint) {
        std::lock_guard<std::mutex> guard(offset_maps_mutex);
        auto offset_maps_it = offset_maps.find(endpoint);
        if (offset_maps_it != offset_maps.end()) {
            return calculate_offset(offset_maps_it->second);
        }
        return 0;
    }

    int32_t ClockOffsetService::calculate_offset(const std::list<int> &offsets) {

        // calculate mean and sigma^2
        double sigma2 = 0;
        double mean = 0;
        for (const int32_t &o : offsets) {
            mean += (float) o / offsets.size();
            sigma2 += (float) o * o / offsets.size();
        }
        double sigma = std::sqrt(sigma2);

        // correct mean by subtracting all offsets outside 2 * sigma
        double corrected_mean = mean;
        for (const int32_t &o : offsets) {
            if (std::abs(o - mean) > 2 * sigma) {
                corrected_mean -= (float) o / offsets.size();
            }
        }

        return (int32_t) corrected_mean;
    }

    std::map<asio::ip::udp::endpoint, int32_t> ClockOffsetService::get_offsets() {
        std::map<asio::ip::udp::endpoint, int32_t> offsets;
        std::lock_guard<std::mutex> guard(offset_maps_mutex);
        for (auto &it : offset_maps) {
            offsets[it.first] = calculate_offset(it.second);
        }
        return offsets;
    }

    void ClockOffsetService::run() {
        service.run();
    }

    ClockOffsetService::callback_handle ClockOffsetService::subscribe(cofetcher_callback callback) {
        std::lock_guard<std::mutex> guard(callbacks_mutex);
        callbacks.push_back(callback);
        return callback_handle(&*--callbacks.end());
    }

    void ClockOffsetService::unsubscribe(ClockOffsetService::callback_handle &callback) {
        std::lock_guard<std::mutex> guard(callbacks_mutex);
        callbacks.erase(std::find_if(callbacks.begin(), callbacks.end(),
            [&callback](const cofetcher_callback& item) { return &item == callback.handle_value; }));
    }

    std::size_t ClockOffsetService::num_callbacks() {
        std::lock_guard<std::mutex> guard(callbacks_mutex);
        return callbacks.size();
    }

    void ClockOffsetService::receive() {
        socket.async_receive_from(asio::buffer(buffer), sender_endpoint,
            [this](const asio::error_code &error, std::size_t bytes_transferred) {
                receive_handler(error, bytes_transferred);
            }
        );
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

        // handle and respond to package first
        time_pkg &package = *(time_pkg *) buffer.data();
        if (handle_package(package)) {
            // send right away, response performance is important.
            send_sync(package, sender_endpoint);
        }

        int32_t offset;
        if (get_offset(package, offset)) {
            std::lock(offset_maps_mutex, callbacks_mutex);
            {
                // add offset to offset map
                std::lock_guard<std::mutex> guard1(offset_maps_mutex, std::adopt_lock);
                offset_maps[sender_endpoint].push_back(offset);
                // enforce maximum size
                while (offset_maps[sender_endpoint].size() > offset_counts)
                    offset_maps[sender_endpoint].pop_front();
            }
            {
                // TODO: callbacks should not be called here... might block another receive operation. additional thread?
                // TODO: prioritize? https://www.boost.org/doc/libs/1_41_0/doc/html/boost_asio/example/invocation/prioritised_handlers.cpp
                // execute callbacks
                std::lock_guard<std::mutex> guard(callbacks_mutex, std::adopt_lock);
                if (!callbacks.empty()) {
                    int32_t filtered_offset = get_offset_for(sender_endpoint);
                    for (auto callback_it = callbacks.begin(); callback_it != callbacks.end(); /* nothing */) {
                        bool remove_callback = false;
                        callback_it->operator()(sender_endpoint, offset, filtered_offset, remove_callback);
                        if(remove_callback) {
                            callback_it = callbacks.erase(callback_it);
                        } else {
                            ++callback_it;
                        }
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

    void ClockOffsetService::send_async(const time_pkg &package, const asio::ip::udp::endpoint &endpoint) {
        // TODO: currently using a lambda to capture buffer
        service.post([this, package, endpoint]() {
            send_sync(package, endpoint);
        });
    }
    void ClockOffsetService::send_sync(const time_pkg &package, const asio::ip::udp::endpoint &endpoint) {
        std::vector<char> data((char*)& package, (char*)& package + sizeof(time_pkg));
        asio::error_code error_code;
        std::size_t bytes_transferred = socket.send_to(asio::buffer(data), endpoint, 0, error_code);
        send_handler(error_code, bytes_transferred);
    }

}