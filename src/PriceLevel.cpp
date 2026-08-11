#include "obme/PriceLevel.hpp"

#include <cassert>

namespace obme {

void PriceLevel::add_order(const Order& order) {
    assert(nodes_.count(order.id) == 0 && "duplicate order id at price level");
    assert(order.quantity > 0 && "order quantity must be positive");

    auto node = std::make_unique<Node>();
    node->order = order;
    Node* raw = node.get();

    // Link at the back of the FIFO.
    raw->prev = tail_;
    raw->next = nullptr;
    if (tail_ != nullptr) {
        tail_->next = raw;
    } else {
        head_ = raw;  // first node
    }
    tail_ = raw;

    total_quantity_ += order.quantity;
    nodes_.emplace(order.id, std::move(node));
}

bool PriceLevel::cancel_order(OrderId id) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return false;
    }
    Node* node = it->second.get();
    total_quantity_ -= node->order.quantity;
    unlink(node);
    nodes_.erase(it);  // frees the node (RAII)
    return true;
}

bool PriceLevel::reduce_order(OrderId id, Quantity new_quantity) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return false;
    }
    Node* node = it->second.get();
    if (new_quantity <= 0 || new_quantity > node->order.quantity) {
        return false;  // not a valid in-place reduction
    }
    total_quantity_ -= (node->order.quantity - new_quantity);
    node->order.quantity = new_quantity;
    return true;
}

const Order& PriceLevel::front() const {
    assert(head_ != nullptr && "front() on empty price level");
    return head_->order;
}

const Order* PriceLevel::find(OrderId id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second->order;
}

std::vector<Order> PriceLevel::snapshot() const {
    std::vector<Order> out;
    out.reserve(nodes_.size());
    for (const Node* n = head_; n != nullptr; n = n->next) {
        out.push_back(n->order);
    }
    return out;
}

void PriceLevel::unlink(Node* node) noexcept {
    if (node->prev != nullptr) {
        node->prev->next = node->next;
    } else {
        head_ = node->next;  // node was head
    }
    if (node->next != nullptr) {
        node->next->prev = node->prev;
    } else {
        tail_ = node->prev;  // node was tail
    }
    node->prev = nullptr;
    node->next = nullptr;
}

}  // namespace obme
