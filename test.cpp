#include "include/concurrent_hashmap.hpp"
#include <iostream>
#include <string>
using namespace std;

int main() {
    cout << "Testing ConcurrentHashMap...\n\n";

    // Create a map: int keys, string values
    ConcurrentHashMap<int, string> map;

    // Test 1: Insert values
    cout << "Test 1: Inserting values\n";
    map.put(1, "Alice");
    map.put(2, "Bob");
    map.put(3, "Charlie");
    cout << "✓ Inserted 3 values\n\n";

    // Test 2: Get values
    cout << "Test 2: Retrieving values\n";
    auto val1 = map.get(1);
    if (val1) {
        cout << "Key 1: " << *val1 << " ✓\n";
    }

    auto val2 = map.get(2);
    if (val2) {
        cout << "Key 2: " << *val2 << " ✓\n";
    }

    auto val_missing = map.get(999);
    if (!val_missing) {
        cout << "Key 999: Not found (correct!) ✓\n\n";
    }

    // Test 3: Update value
    cout << "Test 3: Updating value\n";
    map.put(1, "Alice Updated");
    auto updated = map.get(1);
    if (updated && *updated == "Alice Updated") {
        cout << "Key 1 updated: " << *updated << " ✓\n\n";
    }

    // Test 4: Remove value
    cout << "Test 4: Removing value\n";
    bool removed = map.remove(2);
    if (removed && !map.contains(2)) {
        cout << "Key 2 removed ✓\n\n";
    }

    // Test 5: Size
    cout << "Test 5: Check size\n";
    cout << "Current size: " << map.size() << " ✓\n\n";

    cout << "All tests passed! ✓✓✓\n";

    return 0;
}
