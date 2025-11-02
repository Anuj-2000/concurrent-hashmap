# Architecture & Design Decisions

## The Core Problem

When I first started thinking about this, the problem seemed simple: make `std::unordered_map` thread-safe. Just throw a mutex around it, right?

That's actually what we did initially at TCS. Something like this:

class SafeMap {
    std::mutex lock;
    std::unordered_map<int, double> data;
    
public:
    double get(int key) {
        std::lock_guard<std::mutex> guard(lock);
        return data[key];
    }
    
    void put(int key, double value) {
        std::lock_guard<std::mutex> guard(lock);
        data[key] = value;
    }
};


This works and it's simple. But here's what happened when we ran it under load - threads started spending most of their time just waiting for the lock. Even if Thread A wanted to look up account 12345 and Thread B wanted account 67890 (completely different data!), they'd still block each other.

With 8 worker threads, we were basically running single-threaded because of lock contention.

## Why Bucket-Level Locking?

The key insight was realizing that we don't need to lock the *entire* map - we only need to lock the specific bucket where the data lives.

Think of it like a filing cabinet. Instead of locking the whole cabinet every time someone needs a file, each drawer has its own lock. Multiple people can access different drawers simultaneously.

Here's how it works:

struct Bucket {
    std::shared_mutex mutex;
    std::list<std::pair<Key, Value>> items;
};

std::array<Bucket, 1024> buckets;  // 1024 separate "drawers"

When you want to get or put a key:
1. Hash the key to figure out which bucket it belongs to
2. Lock *only that bucket*
3. Do your operation
4. Unlock

Now if Thread A is working on bucket 42 and Thread B is working on bucket 789, they don't interfere with each other at all.

## Choosing the Number of Buckets

I went with 1024 buckets after some back-of-napkin math. Here's the reasoning:

With 8 threads randomly accessing keys, what's the probability that two threads hit the same bucket?

P(collision) ≈ (num_threads)² / (2 * num_buckets)
P(collision) ≈ 64 / 2048 
P(collision) ≈ 3%

So only about 3% of the time will threads actually contend for the same lock. That seemed like a good trade-off.

I tried a few different values:
- **256 buckets**: More contention (~12%), noticeably slower
- **1024 buckets**: Sweet spot, good performance
- **4096 buckets**: Slightly faster, but the memory overhead wasn't worth it

The memory cost is pretty minimal - each bucket needs about 40 bytes for the mutex and list overhead, so 1024 buckets is roughly 40KB. Not a big deal.

## Reader-Writer Locks (std::shared_mutex)

This was probably the biggest win. In our trading system, about 80-90% of operations are reads (checking balances, validating limits). Only 10-20% are writes (updating positions).

Regular mutexes don't distinguish between readers and writers - everyone gets exclusive access. But multiple threads can safely read the same data simultaneously as long as nobody's writing.

That's what `std::shared_mutex` gives us:

```cpp
// Reading - multiple threads can do this at once
std::shared_lock lock(bucket.mutex);
// ... read data ...

// Writing - requires exclusive access
std::unique_lock lock(bucket.mutex);
// ... modify data ...
```

In read-heavy workloads, this roughly 3-4x the throughput compared to a regular mutex.

## Hash Function & Distribution

I'm using `std::hash` with modulo to map keys to buckets:

```cpp
size_t bucket_index = std::hash<Key>{}(key) % 1024;
```

Some people prefer bit-masking (`hash & (N-1)`) instead of modulo, which requires the bucket count to be a power of 2. I went with modulo because:
1. It's flexible - can use any bucket count
2. Modern CPUs make modulo pretty fast anyway
3. It's simpler to understand

The hash distribution seems reasonably uniform for the types I've tested (integers, strings). If you're using custom types as keys, you'd need to make sure they have a good hash function.

## Why Lists Instead of Vectors?

Each bucket stores its key-value pairs in a `std::list`. I considered vectors, but lists make deletion simpler:

```cpp
// With list: O(1) erase once you have the iterator
bucket.items.erase(iterator);

// With vector: O(n) because it shifts everything
bucket.erase(iterator);  // shifts all elements after
```

For the typical small number of items per bucket (usually 1-5), the performance difference is negligible, but lists make the code cleaner.

The cache locality of vectors would be nice, but it's not worth the complexity for this use case.

## Thread Safety Guarantees

The operations are thread-safe in the sense that:
- Multiple threads can call `get()` on the same or different keys without issues
- Multiple threads can call `put()` or `remove()` without corrupting data
- Operations on different buckets never block each other

What's *not* guaranteed:
- If you call `size()`, the value might be stale immediately (another thread could add/remove)
- There's no "snapshot" consistency - if you iterate, the data might change during iteration
- No ordering guarantees across buckets

For my use case (trading system position lookup), these limitations are fine. We only care about individual key lookups being atomic.

## Real-World Context from TCS

In our production system, we had about 5,000 active accounts during peak hours. With 1024 buckets, that's roughly 5 accounts per bucket on average.

The access pattern wasn't perfectly uniform (some high-volume traders generated more traffic), but it was distributed enough that bucket-level locking worked well.

During the morning market open, we went from processing ~12K trades/sec (with global mutex) to ~85K trades/sec (with bucket-level locking). CPU utilization dropped from 95% (mostly lock contention) to about 65% (actual work).

The pattern worked so well that we ended up applying it to other hot paths in the system.

## What I'd Do Differently

A few things I'm thinking about:

**Dynamic Resizing**: Right now the bucket count is fixed at compile-time. If you know your data size ahead of time, that's fine. But for a truly general-purpose map, you'd want to rehash when the load factor gets too high. That's complex though - you'd need to lock all buckets during rehashing.

**Custom Allocators**: The `std::list` nodes are allocated one at a time, which could fragment memory. A pool allocator might help, but honestly I'd need to profile to know if it matters.

**Lock-Free Alternatives**: For specific use cases, lock-free data structures using atomics and CAS operations could be faster. But they're way harder to get right, and the complexity might not be worth it for most applications.

**NUMA Awareness**: On multi-socket systems, you could pin buckets to specific NUMA nodes. But that's pretty advanced and only matters for large-scale systems.

## Performance Expectations

I haven't run comprehensive benchmarks yet (that's next), but based on similar implementations and my TCS experience, I expect:

**Read-Heavy Workload (80% reads, 20% writes)**:
- Single thread: ~2M ops/sec (baseline)
- 8 threads with global mutex: ~1.2M ops/sec (worse due to contention!)
- 8 threads with bucket locking: ~9-10M ops/sec (8x speedup)

**Write-Heavy Workload (20% reads, 80% writes)**:
- Still expect 5-6x speedup, though less dramatic

The actual numbers will depend on hardware, workload patterns, and key distribution. I'll update this once I have real measurements.

## Comparing to Other Approaches

**Intel TBB's concurrent_hash_map**: Uses a similar bucket-level locking strategy. Probably more optimized than mine, but it's a heavy dependency to add just for one data structure.

**Lock-Free Hash Maps**: Use atomic compare-and-swap operations instead of locks. Potentially faster but much harder to implement correctly. Good if you need the absolute maximum performance and can invest the engineering time.

**Java's ConcurrentHashMap**: Similar bucket-level approach. They've added some clever optimizations over the years (like lock-free reads in newer versions). Worth studying if you want to go deeper.

## Trade-offs Summary

What you gain:
- Much better scalability with multiple threads
- Still reasonably simple to understand and debug
- Predictable performance characteristics

What you give up:
- More memory overhead (1024 mutexes vs 1)
- Slightly more complex code
- No cross-bucket atomic operations

For my use case, this was clearly the right trade-off. Your mileage may vary depending on your requirements.

---

This design isn't perfect, but it's a solid starting point for understanding concurrent data structures. I learned a ton implementing it, especially about lock granularity and the reader-writer pattern.

If you're working on similar problems, I'd recommend starting simple (global mutex) and only adding complexity when you've profiled and found actual bottlenecks. Premature optimization is real!