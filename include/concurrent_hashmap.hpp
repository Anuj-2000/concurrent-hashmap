#ifndef CONCURRENT_HASHMAP_HPP
#define CONCURRENT_HASHMAP_HPP

#include <array>
#include <list>
#include <shared_mutex>
#include <functional>
#include <optional>
using namespace std;

template<typename Key,typename Value, size_t NumBuckets = 1024>
class ConcurrentHashMap {
private:
    struct Bucket {
        mutable shared_mutex mutex;
        list<pair<Key,Value>> items;
    };

    array<Bucket,NumBuckets> buckets_;

    size_t getBucketIndex(cont Key& key) const {
        return hash<Key>{}(key) % NumBuckets;
    }

    Bucket& getBucket(const Key& key){
        return buckets_[getBucketIndex(key)];
    }

    const Bucket& getBucket(const Key& key) const{
        return buckets_[getBucketIndex(key)];
    }

public:
    ConcurrentHashMap() = default;

    ConcurrentHashMap(const ConcurrentHashMap&) = delete;
    ConcurrentHashMap& operator = (const ConcurrentHashMap&) = delete;

    optional<Value> get(const Key& key) const {
        const auto& bucket = getBucket(key);
        shared_lock lock(bucket.mutex);

        for (const auto& [k, v] : bucket.items) {
            if (k == key) {
                return v;
            }
        }
        return nullopt;
    }

    void put(const Key& key, const Value& value){
        auto& bucket = getBucket(key);
        unique_lock lock(bucket.mutex);

        for(auto& [k, v] : bucket.items){
            if (k == key){
                v = value;
                return;
            }
        }

        bucket.item.emplace_back(key,value);
    }

    bool remove(const Key& key) {
        auto& bucket = getBucket(key);
        unique_lock lock(bucket.mutex);

        auto it = std::find_if(bucket.items.begin(), bucket.items.end(),
            [&key](const auto& pair) { return pair.first == key; });

        if (it != bucket.items.end()) {
            bucket.items.erase(it);
            return true;
        }
        return false;
    }

    bool contains(const Key& key) const {
        return get(key).has_value();
    }

    size_t size() const {
        size_t total = 0;
        for (const auto& bucket : buckets_) {
            std::shared_lock lock(bucket.mutex);
            total += bucket.items.size();
        }
        return total;
    }

    void clear() {
        for (auto& bucket : buckets_) {
            std::unique_lock lock(bucket.mutex);
            bucket.items.clear();
        }
    }
};

#endif // CONCURRENT_HASHMAP_HPP