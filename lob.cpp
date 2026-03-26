#include "lob.hpp"

inline bool LOBPriceLevel::empty() const noexcept { return head == nullptr; }

inline void LOBPriceLevel::push_back(LOBOrder* order) noexcept {
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

inline void LOBPriceLevel::remove(LOBOrder* order) noexcept {
    assert(order->level == this);
    if (order->prev) order->prev->next = order->next;
    else head = order->next;
    if (order->next) order->next->prev = order->prev;
    else tail = order->prev;
    totalQuantity -= order->remaining;
    order->prev = order->next = nullptr;
    order->level = nullptr;
}

LOBMatchResult::LOBMatchResult() : tradeCount(0) {}

inline void LOBMatchResult::add(uint64_t maker, uint64_t taker, int32_t price, int64_t qty) {
    assert(tradeCount < trades.size());
    trades[tradeCount++] = LOBTrade{maker, taker, price, qty};
}

LimitOrderBook::LimitOrderBook() {
    reset();
}

void LimitOrderBook::reset() {
    for (int32_t idx = 0; idx < LOB_LEVELS; ++idx) {
        bidLevels[idx].price = LOB_MIN_PRICE + idx * LOB_TICK_SIZE;
        bidLevels[idx].head = nullptr;
        bidLevels[idx].tail = nullptr;
        bidLevels[idx].totalQuantity = 0;
        askLevels[idx].price = LOB_MIN_PRICE + idx * LOB_TICK_SIZE;
        askLevels[idx].head = nullptr;
        askLevels[idx].tail = nullptr;
        askLevels[idx].totalQuantity = 0;
    }

    for (int32_t i = 0; i < LOB_MAX_ORDERS; ++i) {
        poolFree[i] = i;
        orderById[i] = nullptr;
    }
    freeCount = LOB_MAX_ORDERS;
    bestBidIndex = -1;
    bestAskIndex = -1;
}

bool LimitOrderBook::cancelOrder(uint64_t orderId) noexcept {
    if (orderId == 0 || orderId > LOB_MAX_ORDERS) return false;
    LOBOrder* order = orderById[orderId - 1];
    if (!order) return false;

    auto* lvl = order->level;
    bool sideIsBuy = order->isBuy;
    lvl->remove(order);
    orderById[orderId - 1] = nullptr;
    deallocate(order);
    updateBestOnEmpty(sideIsBuy, lvl->price);
    return true;
}

LOBMatchResult LimitOrderBook::placeLimitOrder(bool isBuy, int32_t price, int64_t quantity, uint64_t orderId) {
    assert(quantity > 0);
    assert(price >= LOB_MIN_PRICE && price <= LOB_MAX_PRICE);
    assert((price - LOB_MIN_PRICE) % LOB_TICK_SIZE == 0);
    assert(orderId != 0 && orderId <= (uint64_t)LOB_MAX_ORDERS);
    assert(orderById[orderId - 1] == nullptr);

    LOBMatchResult result;
    int64_t remaining = quantity;

    int32_t best = isBuy ? bestAskIndex : bestBidIndex;

    while (remaining > 0 && best >= 0) {
        auto& level = isBuy ? askLevels[best] : bidLevels[best];
        if (isBuy ? (price < level.price) : (price > level.price)) break;

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
        } else {
            break;
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

LOBMatchResult LimitOrderBook::placeMarketOrder(bool isBuy, int64_t quantity, uint64_t takerOrderId) {
    assert(quantity > 0);
    assert(takerOrderId <= (uint64_t)LOB_MAX_ORDERS);

    LOBMatchResult result;
    int64_t remaining = quantity;
    int32_t best = isBuy ? bestAskIndex : bestBidIndex;

    while (remaining > 0 && best >= 0) {
        auto& level = isBuy ? askLevels[best] : bidLevels[best];
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
        } else {
            break;
        }
    }

    return result;
}

const LOBOrder* LimitOrderBook::getOrderById(uint64_t orderId) const noexcept {
    if (orderId == 0 || orderId > (uint64_t)LOB_MAX_ORDERS) return nullptr;
    return orderById[orderId - 1];
}

int32_t LimitOrderBook::getBestBid() const noexcept { return bestBidIndex >= 0 ? bidLevels[bestBidIndex].price : 0; }
int32_t LimitOrderBook::getBestAsk() const noexcept { return bestAskIndex >= 0 ? askLevels[bestAskIndex].price : 0; }

const LOBPriceLevel& LimitOrderBook::levelForPrice(bool isBuy, int32_t price) const noexcept {
    int32_t idx = priceToIndex(price);
    assert(idx >= 0 && idx < LOB_LEVELS);
    return isBuy ? bidLevels[idx] : askLevels[idx];
}

inline int32_t LimitOrderBook::priceToIndex(int32_t price) noexcept {
    return (price - LOB_MIN_PRICE) / LOB_TICK_SIZE;
}

LOBOrder* LimitOrderBook::allocate(uint64_t) noexcept {
    assert(freeCount > 0);
    int32_t idx = poolFree[--freeCount];
    return &poolStorage[idx];
}

void LimitOrderBook::deallocate(LOBOrder* order) noexcept {
    ptrdiff_t idx = order - poolStorage.data();
    assert(idx >= 0 && idx < LOB_MAX_ORDERS);
    poolFree[freeCount++] = static_cast<int32_t>(idx);
}

void LimitOrderBook::addOrderToBook(LOBOrder* order) noexcept {
    int32_t idx = priceToIndex(order->price);
    auto& level = order->isBuy ? bidLevels[idx] : askLevels[idx];
    level.push_back(order);

    if (order->isBuy) {
        if (bestBidIndex < 0 || order->price > bidLevels[bestBidIndex].price) {
            bestBidIndex = idx;
        }
    } else {
        if (bestAskIndex < 0 || order->price < askLevels[bestAskIndex].price) {
            bestAskIndex = idx;
        }
    }

    if (bestBidIndex >= 0) adjustBestBid();
    if (bestAskIndex >= 0) adjustBestAsk();
}

void LimitOrderBook::updateBestAfterLevelCleared(bool forBuy, int32_t clearedIdx) noexcept {
    if (forBuy) {
        if (bestAskIndex == clearedIdx) {
            bestAskIndex = findNextAsk(clearedIdx + 1);
        }
    } else {
        if (bestBidIndex == clearedIdx) {
            bestBidIndex = findPrevBid(clearedIdx - 1);
        }
    }
}

void LimitOrderBook::updateBestOnEmpty(bool isBuy, int32_t price) noexcept {
    int32_t idx = priceToIndex(price);
    if (isBuy) {
        if (bestBidIndex == idx && bidLevels[idx].empty()) {
            bestBidIndex = findPrevBid(idx - 1);
        }
    } else {
        if (bestAskIndex == idx && askLevels[idx].empty()) {
            bestAskIndex = findNextAsk(idx + 1);
        }
    }
}

int32_t LimitOrderBook::findPrevBid(int32_t from) const noexcept {
    for (int32_t i = from; i >= 0; --i) {
        if (!bidLevels[i].empty()) return i;
    }
    return -1;
}

int32_t LimitOrderBook::findNextAsk(int32_t from) const noexcept {
    for (int32_t i = from; i < LOB_LEVELS; ++i) {
        if (!askLevels[i].empty()) return i;
    }
    return -1;
}

void LimitOrderBook::adjustBestBid() noexcept {
    if (bestBidIndex < 0) return;
    if (!bidLevels[bestBidIndex].empty()) return;
    bestBidIndex = findPrevBid(bestBidIndex - 1);
}

void LimitOrderBook::adjustBestAsk() noexcept {
    if (bestAskIndex < 0) return;
    if (!askLevels[bestAskIndex].empty()) return;
    bestAskIndex = findNextAsk(bestAskIndex + 1);
}
