#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>

template <typename T>
class RawMemory {
private:
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

public:
    RawMemory() = default;

    RawMemory(const RawMemory&) = delete;

    RawMemory& operator=(const RawMemory&) = delete;

    RawMemory(RawMemory&& other) noexcept : buf_(nullptr) {
        Swap(other);
    }

    RawMemory& operator=(RawMemory&& other) noexcept {
        if (this != &other) {
            RawMemory tmp(std::move(other));
            Swap(tmp);
        }

        return *this;
    }

    explicit RawMemory(size_t capacity)
        : buf_(Allocate(capacity)), capacity_(capacity) {
    }

    ~RawMemory() {
        Deallocate(buf_);
    }

    T* operator+(size_t offset) noexcept {
        return const_cast<T*>(std::as_const(*this).GetAddress() + offset);
    }

    const T* operator+(size_t offset) const noexcept {
        assert(offset <= capacity_);

        return buf_ + offset;
    }

    T& operator[](size_t index) noexcept {
        return const_cast<T&>(std::as_const(*this)[index]);
    }

    const T& operator[](size_t index) const noexcept {
        assert(index < capacity_);

        return buf_[index];
    }

    void Swap(RawMemory& other) {
        std::swap(buf_, other.buf_);
        std::swap(capacity_, other.capacity_);
    }

    T* GetAddress() noexcept {
        return buf_;
    }

    [[nodiscard]] const T* GetAddress() const noexcept {
        return buf_;
    }

    [[nodiscard]] size_t Capacity() const {
        return capacity_;
    }

private:
    T* buf_ = nullptr;

    size_t capacity_ = 0U;
};

template <typename T>
class Vector {
private:
    static void Destroy(T* buf) noexcept {
        buf->~T();
    }

    static void DestroyN(T* buf, size_t n) noexcept {
        for (size_t i : std::views::iota(0U, n)) {
            Destroy(buf + i);
        }
    }

    static void CopyConstruct(T* buf, const T& elem) {
        new (buf) T(elem);
    }

    template <typename... Args>
    void MoveOrCopyRawMemory(Args... args) {
        if constexpr (std::is_nothrow_move_constructible_v<T> ||
                      !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(std::forward<Args>(args)...);
        } else {
            std::uninitialized_copy_n(std::forward<Args>(args)...);
        }
    }

public:
    using iterator = T*;

    using const_iterator = const T*;

    iterator begin() noexcept {
        return const_cast<iterator>(std::as_const(*this).begin());
    }

    iterator end() noexcept {
        return const_cast<iterator>(std::as_const(*this).end());
    }

    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator end() const noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator cbegin() const noexcept {
        return begin();
    }

    const_iterator cend() const noexcept {
        return end();
    }

    Vector() = default;

    explicit Vector(size_t size) : data_(size), size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size_);
    }

    Vector(const Vector& other) : data_(other.size_), size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_,
                                  data_.GetAddress());
    }

    Vector(Vector&& other) noexcept
        : data_(std::move(other.data_)), size_(std::exchange(other.size_, 0U)) {
    }

    Vector& operator=(const Vector& other) {
        if (this != &other) {
            if (other.size_ > data_.Capacity()) {
                Vector tmp(other);

                Swap(tmp);
            } else {
                if (other.size_ <= size_) {
                    std::ranges::copy_n(other.data_.GetAddress(), other.size_,
                                        data_.GetAddress());

                    std::destroy_n(data_.GetAddress() + other.size_,
                                   size_ - other.size_);
                } else {
                    std::ranges::copy_n(other.data_.GetAddress(), size_,
                                        data_.GetAddress());

                    std::uninitialized_copy_n(other.data_.GetAddress() + size_,
                                              other.size_ - size_,
                                              data_.GetAddress() + size_);
                }

                size_ = other.size_;
            }
        }

        return *this;
    }

    Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            Swap(other);
        }

        return *this;
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    T& operator[](size_t index) noexcept {
        return const_cast<T&>(std::as_const(*this)[index]);
    }

    const T& operator[](size_t index) const noexcept {
        assert(index < size_);

        return *(data_ + index);
    }

    [[nodiscard]] size_t Size() const noexcept {
        return size_;
    }

    [[nodiscard]] size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_data(new_capacity);

        if constexpr (std::is_nothrow_move_constructible_v<T> ||
                      !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_,
                                      new_data.GetAddress());
        } else {
            std::uninitialized_copy_n(data_.GetAddress(), size_,
                                      new_data.GetAddress());
        }

        std::destroy_n(data_.GetAddress(), size_);

        data_.Swap(new_data);
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    void Resize(size_t new_size) {
        if (new_size == size_) {
            return;
        }

        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
        } else {
            Reserve(new_size);

            std::uninitialized_value_construct_n(data_.GetAddress() + size_,
                                                 new_size - size_);
        }

        size_ = new_size;
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    void PopBack() noexcept {
        assert(size_ > 0U);

        (data_.GetAddress() + size_ - 1)->~T();
        --size_;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *Emplace(cend(), std::forward<Args>(args)...);
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        assert(static_cast<size_t>(pos - cbegin()) <= size_);

        size_t index = pos - cbegin();

        if (size_ < Capacity()) {
            if (pos == end()) {
                new (begin() + index) T(std::forward<Args>(args)...);
            } else {
                T tmp_obj(std::forward<Args>(args)...);

                MoveOrCopyRawMemory(end() - 1, 1U, end());

                std::ranges::move_backward(begin() + index, end() - 1, end());

                *(begin() + index) = std::move(tmp_obj);
            }
        } else {
            size_t new_capacity = (Capacity() == 0 ? 1 : Capacity() * 2U);

            RawMemory<T> tmp_mem(new_capacity);

            new (tmp_mem.GetAddress() + index) T(std::forward<Args>(args)...);

            try {
                MoveOrCopyRawMemory(begin() + index, size_ - index,
                                    tmp_mem.GetAddress() + index + 1);
            } catch (...) {
                (tmp_mem.GetAddress() + index)->~T();

                throw;
            }

            try {
                MoveOrCopyRawMemory(begin(), index, tmp_mem.GetAddress());
            } catch (...) {
                std::destroy_n(tmp_mem.GetAddress() + index, size_ - index + 1);

                throw;
            }

            std::destroy_n(data_.GetAddress(), size_);

            data_.Swap(tmp_mem);
        }

        ++size_;

        return begin() + index;
    }

    iterator Erase(const_iterator pos) {
        size_t index = pos - cbegin();

        std::ranges::move(begin() + index + 1, end(), begin() + index);

        (end() - 1)->~T();

        --size_;

        return begin() + index;
    }

private:
    RawMemory<T> data_;

    size_t size_ = 0;
};
