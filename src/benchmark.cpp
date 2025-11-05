#include "../include/concurrent_hashmap.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <random>
#include <iomanip>
#include <atomic>

// Baseline comparison: unordered_map with global mutex
template<typename Key, typename Value>
class MutexHashMap {
private:
    std::unordered_map<Key, Value> map_;
    mutable std::mutex mutex_;

public:
    std::optional<Value> get(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void put(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        map_[key] = value;
    }

    bool remove(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.erase(key) > 0;
    }
};

// Configuration for benchmarks
struct BenchmarkConfig {
    int num_threads = 8;
    int operations_per_thread = 100000;
    double read_ratio = 0.7;  // 70% reads, 30% writes
};

// Run benchmark on any map implementation
template<typename HashMap>
void runBenchmark(HashMap& map, const std::string& name, const BenchmarkConfig& config) {
    std::cout << "\n=== " << name << " ===" << std::endl;
    std::cout << "Threads: " << config.num_threads << std::endl;
    std::cout << "Operations per thread: " << config.operations_per_thread << std::endl;
    std::cout << "Read ratio: " << (config.read_ratio * 100) << "%" << std::endl;

    // Pre-populate with some data
    for (int i = 0; i < 1000; i++) {
        map.put(i, i * 10);
    }

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    std::atomic<long long> total_ops{0};

    // Spawn worker threads
    for (int t = 0; t < config.num_threads; t++) {
        threads.emplace_back([&map, &config, &total_ops, t]() {
            std::mt19937 rng(t);  // Different seed per thread
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            std::uniform_int_distribution<int> key_dist(0, 9999);

            for (int i = 0; i < config.operations_per_thread; i++) {
                int key = key_dist(rng);

                if (dist(rng) < config.read_ratio) {
                    // Read operation
                    map.get(key);
                } else {
                    // Write operation
                    if (dist(rng) < 0.8) {
                        map.put(key, key * 10);
                    } else {
                        map.remove(key);
                    }
                }
                total_ops++;
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Calculate metrics
    double throughput = (total_ops * 1000.0) / duration.count();
    double latency_us = (duration.count() * 1000.0) / total_ops;

    std::cout << "Duration: " << duration.count() << " ms" << std::endl;
    std::cout << "Total operations: " << total_ops << std::endl;
    std::cout << "Throughput: " << std::fixed << std::setprecision(2)
              << (throughput / 1000000.0) << "M ops/sec" << std::endl;
    std::cout << "Average latency: " << std::fixed << std::setprecision(2)
              << latency_us << " μs" << std::endl;
}

// Correctness tests
void testCorrectness() {
    std::cout << "\n=== Correctness Tests ===" << std::endl;

    ConcurrentHashMap<int, std::string> map;

    // Test 1: Basic operations
    map.put(1, "one");
    map.put(2, "two");
    map.put(3, "three");

    auto val1 = map.get(1);
    auto val2 = map.get(2);
    auto val3 = map.get(3);
    auto val4 = map.get(4);

    if (val1 && *val1 == "one" && val2 && *val2 == "two" &&
        val3 && *val3 == "three" && !val4) {
        std::cout << "✓ Basic put/get operations" << std::endl;
    } else {
        std::cout << "✗ Basic operations FAILED" << std::endl;
        return;
    }

    // Test 2: Update
    map.put(1, "ONE");
    auto updated = map.get(1);
    if (updated && *updated == "ONE") {
        std::cout << "✓ Update operation" << std::endl;
    } else {
        std::cout << "✗ Update FAILED" << std::endl;
        return;
    }

    // Test 3: Remove
    bool removed = map.remove(2);
    auto after_remove = map.get(2);
    if (removed && !after_remove) {
        std::cout << "✓ Remove operation" << std::endl;
    } else {
        std::cout << "✗ Remove FAILED" << std::endl;
        return;
    }

    // Test 4: Contains
    if (map.contains(1) && !map.contains(2)) {
        std::cout << "✓ Contains operation" << std::endl;
    } else {
        std::cout << "✗ Contains FAILED" << std::endl;
        return;
    }

    // Test 5: Concurrent operations
    std::cout << "Running concurrent stress test (10 threads × 1000 ops)..." << std::endl;
    ConcurrentHashMap<int, int> concurrent_map;
    std::vector<std::thread> threads;

    for (int t = 0; t < 10; t++) {
        threads.emplace_back([&concurrent_map, t]() {
            for (int i = 0; i < 1000; i++) {
                int key = t * 1000 + i;
                concurrent_map.put(key, key * 2);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Verify all entries
    bool all_correct = true;
    threads.clear();

    for (int t = 0; t < 10; t++) {
        threads.emplace_back([&concurrent_map, &all_correct, t]() {
            for (int i = 0; i < 1000; i++) {
                int key = t * 1000 + i;
                auto val = concurrent_map.get(key);
                if (!val || *val != key * 2) {
                    all_correct = false;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    if (all_correct) {
        std::cout << "✓ Concurrent operations (10,000 ops)" << std::endl;
    } else {
        std::cout << "✗ Concurrent operations FAILED" << std::endl;
        return;
    }

    std::cout << "\n✅ All correctness tests passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Concurrent Hash Map - Benchmark Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    // Run correctness tests first
    testCorrectness();

    // Benchmark configuration
    BenchmarkConfig config;
    config.num_threads = 8;
    config.operations_per_thread = 100000;
    config.read_ratio = 0.7;

    std::cout << "\n\n========================================" << std::endl;
    std::cout << "  Performance Benchmarks" << std::endl;
    std::cout << "========================================" << std::endl;

    // Benchmark 1: Our implementation
    std::cout << "\n--- Test 1: Read-Heavy Workload (70% reads) ---" << std::endl;
    ConcurrentHashMap<int, int> concurrent_map;
    runBenchmark(concurrent_map, "ConcurrentHashMap (Bucket-Level Locking)", config);

    // Benchmark 2: Baseline (global mutex)
    MutexHashMap<int, int> mutex_map;
    runBenchmark(mutex_map, "MutexHashMap (Global Mutex)", config);

    // Benchmark 3: Write-heavy workload
    std::cout << "\n--- Test 2: Write-Heavy Workload (30% reads) ---" << std::endl;
    config.read_ratio = 0.3;

    ConcurrentHashMap<int, int> concurrent_map2;
    runBenchmark(concurrent_map2, "ConcurrentHashMap (Bucket-Level Locking)", config);

    MutexHashMap<int, int> mutex_map2;
    runBenchmark(mutex_map2, "MutexHashMap (Global Mutex)", config);

    // Benchmark 4: Balanced workload
    std::cout << "\n--- Test 3: Balanced Workload (50% reads) ---" << std::endl;
    config.read_ratio = 0.5;

    ConcurrentHashMap<int, int> concurrent_map3;
    runBenchmark(concurrent_map3, "ConcurrentHashMap (Bucket-Level Locking)", config);

    MutexHashMap<int, int> mutex_map3;
    runBenchmark(mutex_map3, "MutexHashMap (Global Mutex)", config);

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Benchmarks Complete!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
