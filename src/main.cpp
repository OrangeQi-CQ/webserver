#include "webserver.hpp"
#include "config.hpp"

int main() {
    WebServer server(PORT, THREAD_POOL_SIZE);
    server.start();
    return 0;
}