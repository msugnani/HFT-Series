#include "hft/pool.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        throw std::runtime_error(msg);
    }
}

struct Node {
    int v;
};

}  // namespace

int main() {
    try {
        hft::Pool<Node> pool(4);
        require(pool.capacity() == 4, "capacity");
        require(pool.available() == 4, "all free");

        Node* a = pool.try_acquire();
        Node* b = pool.try_acquire();
        Node* c = pool.try_acquire();
        Node* d = pool.try_acquire();
        require(a && b && c && d, "acquire 4");
        require(pool.try_acquire() == nullptr, "exhausted");
        require(pool.available() == 0, "none free");

        a->v = 1;
        b->v = 2;
        pool.release(a);
        require(pool.available() == 1, "one free");
        Node* e = pool.try_acquire();
        require(e == a, "LIFO free stack reuses a");
        pool.release(e);
        pool.release(b);
        pool.release(c);
        pool.release(d);
        require(pool.available() == 4, "all returned");
    } catch (const std::exception& ex) {
        std::cerr << "test_pool FAILED: " << ex.what() << '\n';
        return 1;
    }
    std::cout << "test_pool OK\n";
    return 0;
}
