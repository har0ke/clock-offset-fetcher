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
        tr_handle::type list_iterator;
        {
            std::lock_guard<std::mutex> guard(tr_handles_mutex);
            tr_handles.emplace_back(service);
            list_iterator = --tr_handles.end();
        }
        iterative_time_request(endpoint, list_iterator);
        list_iterator->expires_from_now(std::chrono::seconds(0));
        return tr_handle(list_iterator);
    }

    void ClockOffsetService::iterative_time_request(const asio::ip::udp::endpoint endpoint,
                                                    tr_handle::type handle) {
        handle->expires_from_now(std::chrono::seconds((int) dist(mt)));
        handle->async_wait([this, endpoint, handle](const asio::error_code &error) {
            std::lock_guard<std::mutex> guard(tr_handles_mutex);
            // need to check for handle for the case that the handle was erased while entering this method
            for (auto it = tr_handles.begin(); it != tr_handles.end(); it++) {
                if (it == handle) {
                    this->init_single_time_request(endpoint);
                    this->iterative_time_request(endpoint, handle);
                    break;
                }
            }
        });
    }

    void ClockOffsetService::cancel_iterative_time_requests(const ClockOffsetService::tr_handle &handle) {
        handle.handle_value->cancel();

        std::lock_guard<std::mutex> guard(tr_handles_mutex);
        tr_handles.erase(handle.handle_value);
    }

    std::size_t ClockOffsetService::num_iterative_time_request() {
        std::lock_guard<std::mutex> guard(tr_handles_mutex);
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
        auto offset_maps_it = offset_maps.find(endpoint);
        if (offset_maps_it != offset_maps.end()) {
            std::list<int> &offsets = offset_maps_it->second;
            for (int32_t &o : offsets) {
                mean += (float) o / offsets.size();
                s2 += (float) o * o / offsets.size();
            }
            double s = std::sqrt(s2);
            double corrected_mean = mean;
            for (int32_t &o : offsets) {
                if (std::abs(o - mean) > 2 * s) {
                    corrected_mean -= (float) o / offsets.size();
                }
            }
            return (int32_t) corrected_mean;
        }
        return 0;
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
        return callback_handle(--callbacks.end());
    }

    /**
     * remove a subscription
     * @param callback the callback that is receiving offsets
     */
    void ClockOffsetService::unsubscribe(ClockOffsetService::callback_handle &callback) {
        std::lock_guard<std::mutex> guard(callbacks_mutex);
        callbacks.erase(callback.handle_value);
    }

    /**
     * @return number of callbacks that are subscribing to new offsets.
     */
    std::size_t ClockOffsetService::num_callbacks() {
        std::lock_guard<std::mutex> guard(callbacks_mutex);
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
            std::lock(offset_maps_mutex, callbacks_mutex);
            {
                std::lock_guard<std::mutex> guard1(offset_maps_mutex, std::adopt_lock);
                offset_maps[sender_endpoint].push_back(offset);
                while (offset_maps[sender_endpoint].size() > offset_counts)
                    offset_maps[sender_endpoint].pop_front();
            }
            {
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

    void ClockOffsetService::send(time_pkg package, const asio::ip::udp::endpoint &endpoint) {
        std::vector<char> data((char *) &package, (char *) &package + sizeof(time_pkg));
        socket.async_send_to(asio::buffer(data), endpoint, send_handler);
    }

}