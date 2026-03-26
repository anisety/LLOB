#include <iostream>
#include <cassert>
#include "lob.hpp"

static int tests_run = 0;
static int tests_failed = 0;
static LimitOrderBook g_book;

#define REQUIRE(cond) do { ++tests_run; if (!(cond)) { std::cerr << "Test failed: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; ++tests_failed; } } while (0)

static void run_tests() {
    {
        LimitOrderBook& book = g_book;
        book.reset();

        auto r1 = book.placeLimitOrder(true, 100, 10, 1);
        REQUIRE(r1.tradeCount == 0);

        auto r2 = book.placeLimitOrder(true, 100, 10, 2);
        REQUIRE(r2.tradeCount == 0);

        auto r3 = book.placeLimitOrder(true, 100, 10, 3);
        REQUIRE(r3.tradeCount == 0);

        REQUIRE(book.getBestBid() == 100);
        REQUIRE(book.getBestAsk() == 0);

        auto m = book.placeMarketOrder(false, 25, 1000);
        REQUIRE(m.tradeCount == 3);
        REQUIRE(m.trades[0].makerOrderId == 1);
        REQUIRE(m.trades[0].quantity == 10);
        REQUIRE(m.trades[1].makerOrderId == 2);
        REQUIRE(m.trades[1].quantity == 10);
        REQUIRE(m.trades[2].makerOrderId == 3);
        REQUIRE(m.trades[2].quantity == 5);

        auto o1 = book.getOrderById(1);
        auto o2 = book.getOrderById(2);
        auto o3 = book.getOrderById(3);
        REQUIRE(o1 == nullptr);
        REQUIRE(o2 == nullptr);
        REQUIRE(o3 != nullptr);
        REQUIRE(o3->remaining == 5);

        REQUIRE(book.getBestBid() == 100);
    }

    {
        LimitOrderBook& book = g_book;
        book.reset();

        auto b1 = book.placeLimitOrder(true, 200, 50, 10);
        REQUIRE(b1.tradeCount == 0);

        auto s1 = book.placeLimitOrder(false, 200, 20, 11);
        REQUIRE(s1.tradeCount == 1);
        REQUIRE(s1.trades[0].quantity == 20);
        REQUIRE(book.getOrderById(10)->remaining == 30);

        REQUIRE(book.getBestBid() == 200);
        REQUIRE(book.getBestAsk() == 0);

        auto s2 = book.placeLimitOrder(false, 200, 40, 12);
        REQUIRE(s2.tradeCount == 1);
        REQUIRE(s2.trades[0].quantity == 30);
        REQUIRE(book.getOrderById(10) == nullptr);
        REQUIRE(book.getOrderById(12)->remaining == 10);
    }

    {
        LimitOrderBook& book = g_book;
        book.reset();

        book.placeLimitOrder(true, 150, 10, 20);
        book.placeLimitOrder(true, 150, 10, 21);
        book.placeLimitOrder(true, 150, 10, 22);

        REQUIRE(book.getBestBid() == 150);

        bool ok = book.cancelOrder(21);
        REQUIRE(ok);
        REQUIRE(book.getOrderById(21) == nullptr);

        const auto* o20 = book.getOrderById(20);
        const auto* o22 = book.getOrderById(22);
        REQUIRE(o20 != nullptr);
        REQUIRE(o22 != nullptr);
        REQUIRE(o20->next == o22);
        REQUIRE(o22->prev == o20);

        book.cancelOrder(20);
        book.cancelOrder(22);
        REQUIRE(book.getBestBid() == 0);
    }
}

int main() {
    run_tests();
    if (tests_failed == 0) {
        std::cout << "All tests passed (" << tests_run << ")\n";
        return 0;
    }
    std::cout << tests_failed << " tests failed out of " << tests_run << "\n";
    return 1;
}
