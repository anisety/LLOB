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

    inline bool empty() const noexcept { return head == nullptr; }

    inline void push_back(LOBOrder* order) noexcept {
        order->next = nullptr;
        if (!head) {
            head = tail = order;
            order->prev = nullptr;
        } else {
            tail->next = order;
            order->prev = tail;
            tail = order;
        }
        order->level = this;
        totalQuantity += order->remaining;
    }

    inline void remove(LOBOrder* order) noexcept {
        assert(order->level == this);
        if (order->prev) order->prev->next = order->next;
        else head = order->next;
        if (order->next) order->next->prev = order->prev;
        else tail = order->prev;
        totalQuantity -= order->remaining;
        order->prev = order->next = nullptr;
        order->level = nullptr;
    }
};

struct LOBTrade {
    uint64_t makerOrderId;
    uint64_t takerOrderId;
    int32_t price;
    int64_t quantity;
};

struct LOBMatchResult {
    std::array<LOBTrade, 1024> trades; // enough for tests
    size_t tradeCount;

    LOBMatchResult() : tradeCount(0) {}

    inline void add(uint64_t maker, uint64_t taker, int32_t price, int64_t qty) {
        assert(tradeCount < trades.size());
        trades[tradeCount++] = LOBTrade{maker, taker, price, qty};
    }
};

class LimitOrderBook {
public:
    LimitOrderBook() { reset(); }

    void reset() {
        for (int32_t idx = 0; idx < LOB_LEVELS; ++idx) {
            priceLevels[idx].price = LOB_MIN_PRICE + idx * LOB_TICK_SIZE;
            priceLevels[idx].head = nullptr;
            priceLevels[idx].tail = nullptr;
            priceLevels[idx].totalQuantity = 0;
        }

        for (int32_t i = 0; i < LOB_MAX_ORDERS; ++i) {
            poolFree[i] = i;
            orderById[i] = nullptr;
        }
        freeCount = LOB_MAX_ORDERS;
        bestBidIndex = -1;
        bestAskIndex = -1;
    }

    bool cancelOrder(uint64_t orderId) noexcept {
        if (orderId == 0 || orderId > LOB_MAX_ORDERS) return false;
        LOBOrder* order = orderById[orderId - 1];
        if (!order) return false;
        auto* lvl = order->level;
        lvl->remove(order);
        if (order->remaining > 0) {
            // removed from level quantity is tracked by price level
        }
        orderById[orderId - 1] = nullptr;
        deallocate(order);
        updateBestOnEmpty(lvl->price);
        return true;
    }

    LOBMatchResult placeLimitOrder(bool isBuy, int32_t price, int64_t quantity, uint64_t orderId) {
        assert(quantity > 0);
        assert(price >= LOB_MIN_PRICE && price <= LOB_MAX_PRICE);
        assert((price - LOB_MIN_PRICE) % LOB_TICK_SIZE == 0);
        assert(orderId != 0 && orderId <= (uint64_t)LOB_MAX_ORDERS);
        assert(orderById[orderId - 1] == nullptr);

        LOBMatchResult result;
        int64_t remaining = quantity;
        auto* opposite = isBuy ? &priceLevels : &priceLevels;
        int32_t best = isBuy ? bestAskIndex : bestBidIndex;

        while (remaining > 0) {
            if (best < 0) break;
            int32_t levelPrice = priceLevels[best].price;
            if (isBuy ? (price < levelPrice) : (price > levelPrice)) break;

            auto& level = priceLevels[best];
            while (remaining > 0 && level.head) {
                LOBOrder* maker = level.head;
                int64_t tradeQty = (remaining < maker->remaining) ? remaining : maker->remaining;
                remaining -= tradeQty;
                maker->remaining -= tradeQty;
                level.totalQuantity -= tradeQty;

                result.add(maker->orderId, orderId, level.price, tradeQty);

                if (maker->remaining == 0) {
                    level.remove(maker);
                    orderById[maker->orderId - 1] = nullptr;
                    deallocate(maker);
                }
            }

            if (level.empty()) {
                updateBestAfterLevelCleared(isBuy, best);
                best = isBuy ? bestAskIndex : bestBidIndex;
            }
        }

        if (remaining > 0) {
            LOBOrder* newOrder = allocate(orderId);
            newOrder->isBuy = isBuy;
            newOrder->price = price;
            newOrder->remaining = remaining;
            newOrder->orderId = orderId;
            newOrder->prev = newOrder->next = nullptr;
            newOrder->level = nullptr;
            addOrderToBook(newOrder);
            orderById[orderId - 1] = newOrder;
        }

        return result;
    }

    LOBMatchResult placeMarketOrder(bool isBuy, int64_t quantity, uint64_t takerOrderId = 0) {
        assert(quantity > 0);
        assert(takerOrderId <= (uint64_t)LOB_MAX_ORDERS);

        LOBMatchResult result;
        int64_t remaining = quantity;
        int32_t best = isBuy ? bestAskIndex : bestBidIndex;

        while (remaining > 0 && best >= 0) {
            auto& level = priceLevels[best];
            while (remaining > 0 && level.head) {
                LOBOrder* maker = level.head;
                int64_t tradeQty = (remaining < maker->remaining) ? remaining : maker->remaining;
                remaining -= tradeQty;
                maker->remaining -= tradeQty;
                level.totalQuantity -= tradeQty;

                result.add(maker->orderId, takerOrderId, level.price, tradeQty);

                if (maker->remaining == 0) {
                    level.remove(maker);
                    orderById[maker->orderId - 1] = nullptr;
                    deallocate(maker);
                }
            }

            if (level.empty()) {
                updateBestAfterLevelCleared(isBuy, best);
                best = isBuy ? bestAskIndex : bestBidIndex;
            }
        }

        return result;
    }

    const LOBOrder* getOrderById(uint64_t orderId) const noexcept {
        if (orderId == 0 || orderId > (uint64_t)LOB_MAX_ORDERS) return nullptr;
        return orderById[orderId - 1];
    }

    int32_t getBestBid() const noexcept { return bestBidIndex >= 0 ? priceLevels[bestBidIndex].price : 0; }
    int32_t getBestAsk() const noexcept { return bestAskIndex >= 0 ? priceLevels[bestAskIndex].price : 0; }

    const LOBPriceLevel& levelForPrice(int32_t price) const noexcept {
        int32_t idx = priceToIndex(price);
        assert(idx >= 0 && idx < LOB_LEVELS);
        return priceLevels[idx];
    }

private:
    alignas(64) std::array<LOBPriceLevel, LOB_LEVELS> priceLevels;
    alignas(64) std::array<LOBOrder*, LOB_MAX_ORDERS> orderById;
    alignas(64) std::array<int32_t, LOB_MAX_ORDERS> poolFree;
    alignas(64) std::array<LOBOrder, LOB_MAX_ORDERS> poolStorage;
    int32_t freeCount;
    int32_t bestBidIndex;
    int32_t bestAskIndex;

    static inline int32_t priceToIndex(int32_t price) noexcept {
        return (price - LOB_MIN_PRICE) / LOB_TICK_SIZE;
    }

    LOBOrder* allocate(uint64_t orderId) noexcept {
        assert(freeCount > 0);
        int32_t idx = poolFree[--freeCount];
        LOBOrder* o = &poolStorage[idx];
        (void)orderId;
        return o;
    }

    void deallocate(LOBOrder* order) noexcept {
        ptrdiff_t idx = order - poolStorage.data();
        assert(idx >= 0 && idx < LOB_MAX_ORDERS);
        poolFree[freeCount++] = static_cast<int32_t>(idx);
    }

    void addOrderToBook(LOBOrder* order) noexcept {
        int32_t idx = priceToIndex(order->price);
        auto& level = priceLevels[idx];
        level.push_back(order);

        if (order->isBuy) {
            if (bestBidIndex < 0 || order->price > priceLevels[bestBidIndex].price) {
                bestBidIndex = idx;
            }
            if (bestAskIndex < 0) {
                bestAskIndex = idx;
            }
        } else {
            if (bestAskIndex < 0 || order->price < priceLevels[bestAskIndex].price) {
                bestAskIndex = idx;
            }
            if (bestBidIndex < 0) {
                bestBidIndex = idx;
            }
        }

        if (bestBidIndex >= 0) adjustBestBid();
        if (bestAskIndex >= 0) adjustBestAsk();
    }

    void updateBestAfterLevelCleared(bool forBuy, int32_t clearedIdx) noexcept {
        if (forBuy) {
            // buy side is taking asks: update best ask
            if (bestAskIndex == clearedIdx) {
                bestAskIndex = findNextAsk(clearedIdx + 1);
            }
        } else {
            // sell side takes bids: update best bid
            if (bestBidIndex == clearedIdx) {
                bestBidIndex = findPrevBid(clearedIdx - 1);
            }
        }
    }

    void updateBestOnEmpty(int32_t price) noexcept {
        int32_t idx = priceToIndex(price);
        if (bestBidIndex == idx && priceLevels[idx].empty()) {
            bestBidIndex = findPrevBid(idx - 1);
        }
        if (bestAskIndex == idx && priceLevels[idx].empty()) {
            bestAskIndex = findNextAsk(idx + 1);
        }
    }

    int32_t findPrevBid(int32_t from) const noexcept {
        for (int32_t i = from; i >= 0; --i) {
            if (!priceLevels[i].empty()) return i;
        }
        return -1;
    }

    int32_t findNextAsk(int32_t from) const noexcept {
        for (int32_t i = from; i < LOB_LEVELS; ++i) {
            if (!priceLevels[i].empty()) return i;
        }
        return -1;
    }

    void adjustBestBid() noexcept {
        if (bestBidIndex < 0) return;
        if (!priceLevels[bestBidIndex].empty()) return;
        bestBidIndex = findPrevBid(bestBidIndex - 1);
    }

    void adjustBestAsk() noexcept {
        if (bestAskIndex < 0) return;
        if (!priceLevels[bestAskIndex].empty()) return;
        bestAskIndex = findNextAsk(bestAskIndex + 1);
    }
};
