#pragma once

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

#include "obme/Order.hpp"

namespace obme {

/// A FIFO queue of orders resting at a single price.
///
/// Implemented as an intrusive doubly linked list of nodes plus an
/// `order_id -> node` hash map. This gives:
///   * O(1) append-to-back (new orders join the end of the time-priority queue)
///   * O(1) cancel-by-id   (unlink the node directly; no linear scan)
///
/// Node storage is owned by `nodes_` (a map of `unique_ptr<Node>`), so there is
/// no raw `new`/`delete`; the linked-list pointers are non-owning views into
/// those nodes.
class PriceLevel {
public:
    PriceLevel() = default;
    explicit PriceLevel(Price price) : price_(price) {}

    // Owns unique_ptrs and holds raw list pointers into them: non-copyable,
    // movable (move keeps node addresses stable, so list pointers stay valid).
    PriceLevel(const PriceLevel&) = delete;
    PriceLevel& operator=(const PriceLevel&) = delete;
    PriceLevel(PriceLevel&& other) noexcept { move_from(other); }
    PriceLevel& operator=(PriceLevel&& other) noexcept {
        if (this != &other) move_from(other);
        return *this;
    }
    ~PriceLevel() = default;

    Price price() const noexcept { return price_; }
    Quantity total_quantity() const noexcept { return total_quantity_; }
    std::size_t size() const noexcept { return nodes_.size(); }
    bool empty() const noexcept { return head_ == nullptr; }
    bool contains(OrderId id) const { return nodes_.count(id) != 0; }

    /// Appends an order to the back of the FIFO (latest time priority).
    /// Precondition: `order.id` is not already present at this level.
    void add_order(const Order& order);

    /// Removes the order with the given id. Returns true if it was present.
    bool cancel_order(OrderId id);

    /// Reduces an existing order's quantity *in place*, preserving its time
    /// priority. `new_quantity` must be > 0 and <= the current quantity.
    /// Returns true if the order exists and the reduction was applied.
    bool reduce_order(OrderId id, Quantity new_quantity);

    /// The oldest (front-of-queue) order. Precondition: !empty().
    const Order& front() const;

    /// Returns a pointer to the order with the given id, or nullptr if absent.
    /// The pointer is valid until the order is cancelled or this level is moved.
    const Order* find(OrderId id) const;

    /// Snapshot of all orders in FIFO order (front first). For tests/inspection.
    std::vector<Order> snapshot() const;

private:
    struct Node {
        Order order;
        Node* prev{nullptr};
        Node* next{nullptr};
    };

    void move_from(PriceLevel& other) noexcept {
        price_ = other.price_;
        total_quantity_ = other.total_quantity_;
        head_ = other.head_;
        tail_ = other.tail_;
        nodes_ = std::move(other.nodes_);
        other.head_ = nullptr;
        other.tail_ = nullptr;
        other.total_quantity_ = 0;
    }

    void unlink(Node* node) noexcept;

    Price price_{0};
    Quantity total_quantity_{0};
    Node* head_{nullptr};  ///< oldest order (front of FIFO)
    Node* tail_{nullptr};  ///< newest order (back of FIFO)
    std::unordered_map<OrderId, std::unique_ptr<Node>> nodes_;
};

}  // namespace obme
