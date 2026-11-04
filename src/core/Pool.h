#pragma once

namespace ls {

// Fixed-capacity object pool. Memory is reserved once, so spawning never touches
// the heap. Live items stay packed at the front of the array, which keeps loops
// fast. Removing swaps the last item into the gap, so loop backward when removing.
template <typename T, int Capacity>
struct Pool {
    T items[Capacity];
    int count = 0;

    static constexpr int capacity() { return Capacity; }

    // Returns a reset item, or nullptr when the pool is full.
    T* add() {
        if (count >= Capacity) return nullptr;
        items[count] = T{};
        return &items[count++];
    }

    void removeAt(int index) { items[index] = items[--count]; }
    void clear() { count = 0; }

    T& operator[](int index) { return items[index]; }
    const T& operator[](int index) const { return items[index]; }
    T* begin() { return items; }
    T* end() { return items + count; }
    const T* begin() const { return items; }
    const T* end() const { return items + count; }
};

}  // namespace ls
