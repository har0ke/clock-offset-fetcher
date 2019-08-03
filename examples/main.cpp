#include "clock_offset_udp_server.h"


class OffsetPrinter {

public:

    void cancel() {
        canceled = true;
        print_thread.join();
    }

    explicit OffsetPrinter(cofetcher::ClockOffsetService &offset_service)
        : print_thread([this, &offset_service]{
            print_offsets(offset_service);
        }) {

    }


    void print_offsets(cofetcher::ClockOffsetService &offset_service) {
        while(!canceled) {
            for(auto &pair : offset_service.get_offsets()) {
                std::cout << pair.first.address().to_string() << ":" << pair.first.port() << " - " << pair.second / 1000 << "us" << std::endl;
            }
            std::cout << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

private:

    bool canceled = false;
    std::thread print_thread;

};

int main(int argc, char *argv[]) {

    if (argc < 2) {
        std::cout << "Too few arguments. \nUsage: cofetcher_example <service_port> [<sync_port1> <syc_port2> ...]" << std::endl;
        exit(0);
    }
    cofetcher::ClockOffsetService service(std::stol(argv[1]), 20);

    std::list<cofetcher::ClockOffsetService::tr_handle> handles;
    for(int i = 2; i < argc; i++) {
        cofetcher::endpoint endpoint(asio::ip::make_address("127.0.0.1"), std::stol(argv[i]));
        handles.push_back(service.init_iterative_time_request(endpoint));
    }

    OffsetPrinter op(service);

    service.run();

    for(auto &handle : handles) {
        service.cancel_iterative_time_requests(handle);
    }

    op.cancel();

}

