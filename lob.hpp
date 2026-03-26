#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <cstddef>

static constexpr int32_t LOB_MIN_PRICE = 1;
static constexpr int32_t LOB_MAX_PRICE = 100000;
static constexpr int32_t LOB_TICK_SIZE = 1;
static constexpr int32_t LOB_LEVELS = (LOB_MAX_PRICE - LOB_MIN_PRICE) / LOB_TICK_SIZE + 1;
static constexpr int32_t LOB_MAX_ORDERS = 100000;

struct alignas(64) LOBOrder {
    uint64_t orderId;
    bool isBuy;
    int32_t price;
    int64_t remaining;
    LOBOrder* prev;
    LOBOrder* next;
    struct LOBPriceLevel* level;
};

struct alignas(64) LOBPriceLevel {
    int32_t price;
    LOBOrder* head;
    LOBOrder* tail;
    int64_t totalQuantity;

    constexpr LOBPriceLevel() : price(0), head(nullptr), tail(nullptr), totalQuantity(0) {}

    inline bool empty() const noexcept;
    inline void push_back(LOBOrder* order) noexcept;
    inline void remove(LOBOrder* order) noexcept;
};

struct LOBTrade {
    uint64_t makerOrderId;
    uint64_t takerOrderId;
    int32_t price;
    int64_t quantity;
};

struct LOBMatchResult {
    std::array<LOBTrade, 1024> trades;
    size_t tradeCount;

    LOBMatchResult();
    inline void add(uint64_t maker, uint64_t taker, int32_t price, int64_t qty);
};

class LimitOrderBook {
public:
    LimitOrderBook();
    void reset();

    bool cancelOrder(uint64_t orderId) noexcept;
    LOBMatchResult placeLimitOrder(bool isBuy, int32_t price, int64_t quantity, uint64_t orderId);
    LOBMatchResult placeMarketOrder(bool isBuy, int64_t quantity, uint64_t takerOrderId = 0);

    const LOBOrder* getOrderById(uint64_t orderId) const noexcept;
    int32_t getBestBid() const noexcept;
    int32_t getBestAsk() const noexcept;
    const LOBPriceLevel& levelForPrice(bool isBuy, int32_t price) const noexcept;

private:
    alignas(64) std::array<LOBPriceLevel, LOB_LEVELS> bidLevels;
    alignas(64) std::array<LOBPriceLevel, LOB_LEVELS> askLevels;
    alignas(64) std::array<LOBOrder*, LOB_MAX_ORDERS> orderById;
    alignas(64) std::array<int32_t, LOB_MAX_ORDERS> poolFree;
    alignas(64) std::array<LOBOrder, LOB_MAX_ORDERS> poolStorage;
    int32_t freeCount;
    int32_t bestBidIndex;
    int32_t bestAskIndex;

    static inline int32_t priceToIndex(int32_t price) noexcept;
    LOBOrder* allocate(uint64_t orderId) noexcept;
    void deallocate(LOBOrder* order) noexcept;

    void addOrderToBook(LOBOrder* order) noexcept;
    void updateBestAfterLevelCleared(bool forBuy, int32_t clearedIdx) noexcept;
    void updateBestOnEmpty(bool isBuy, int32_t price) noexcept;

    int32_t findPrevBid(int32_t from) const noexcept;
    int32_t findNextAsk(int32_t from) const noexcept;
    void adjustBestBid() noexcept;
    void adjustBestAsk() noexcept;
};
