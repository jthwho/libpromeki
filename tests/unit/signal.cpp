/**
 * @file      signal.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/signal.h>
#include <promeki/signal.tpp>
#include <promeki/slot.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/objectbase.h>
#include <promeki/objectbase.tpp>
#include <promeki/variant.h>

using namespace promeki;

// ============================================================================
// Signal construction
// ============================================================================

TEST_CASE("Signal_DefaultConstruction") {
    Signal<int> sig;
    CHECK(sig.owner() == nullptr);
    CHECK(sig.prototype() == nullptr);
}

TEST_CASE("Signal_ConstructionWithOwner") {
    int owner = 42;
    Signal<int> sig(&owner, "void(int)");
    CHECK(sig.owner() == &owner);
    CHECK(sig.prototype() != nullptr);
}

// ============================================================================
// Connect and emit with lambda
// ============================================================================

TEST_CASE("Signal_ConnectAndEmitLambda") {
    Signal<int> sig;
    int received = 0;
    sig.connect([&received](int val) { received = val; });
    sig.emit(42);
    CHECK(received == 42);
}

TEST_CASE("Signal_EmitMultipleArgs") {
    Signal<int, const String &> sig;
    int receivedInt = 0;
    String receivedStr;
    sig.connect([&](int i, const String &s) {
        receivedInt = i;
        receivedStr = s;
    });
    sig.emit(7, "hello");
    CHECK(receivedInt == 7);
    CHECK(receivedStr == "hello");
}

TEST_CASE("Signal_EmitNoArgs") {
    Signal<> sig;
    int count = 0;
    sig.connect([&count]() { count++; });
    sig.emit();
    sig.emit();
    sig.emit();
    CHECK(count == 3);
}

// ============================================================================
// Multiple slots
// ============================================================================

TEST_CASE("Signal_MultipleSlots") {
    Signal<int> sig;
    int sum = 0;
    sig.connect([&sum](int val) { sum += val; });
    sig.connect([&sum](int val) { sum += val * 2; });
    sig.emit(10);
    CHECK(sum == 30); // 10 + 20
}

TEST_CASE("Signal_EmitOrder") {
    Signal<int> sig;
    List<int> order;
    sig.connect([&order](int) { order.pushToBack(1); });
    sig.connect([&order](int) { order.pushToBack(2); });
    sig.connect([&order](int) { order.pushToBack(3); });
    sig.emit(0);
    REQUIRE(order.size() == 3);
    CHECK(order[0] == 1);
    CHECK(order[1] == 2);
    CHECK(order[2] == 3);
}

// ============================================================================
// Disconnect by ID
// ============================================================================

TEST_CASE("Signal_DisconnectByID") {
    Signal<int> sig;
    int count = 0;
    size_t id = sig.connect([&count](int) { count++; });
    sig.emit(0);
    CHECK(count == 1);
    sig.disconnect(id);
    sig.emit(0);
    CHECK(count == 1); // Should not have changed
}

// ============================================================================
// Connect and disconnect with object pointer
// ============================================================================

TEST_CASE("Signal_ConnectWithObjectPointer") {
    Signal<int> sig;
    int received = 0;
    int obj = 0; // Just using address as an object pointer
    sig.connect([&received](int val) { received = val; }, &obj);
    sig.emit(99);
    CHECK(received == 99);
}

TEST_CASE("Signal_DisconnectFromObject") {
    Signal<int> sig;
    int count1 = 0, count2 = 0;
    int obj1 = 0, obj2 = 0;
    sig.connect([&count1](int) { count1++; }, &obj1);
    sig.connect([&count2](int) { count2++; }, &obj2);

    sig.emit(0);
    CHECK(count1 == 1);
    CHECK(count2 == 1);

    sig.disconnectFromObject(&obj1);
    sig.emit(0);
    CHECK(count1 == 1); // Disconnected
    CHECK(count2 == 2); // Still connected
}

// ============================================================================
// Connect to member function
// ============================================================================

struct TestReceiver {
    int lastValue = 0;
    int callCount = 0;
    void onSignal(int val) {
        lastValue = val;
        callCount++;
    }
};

TEST_CASE("Signal_ConnectMemberFunction") {
    Signal<int> sig;
    TestReceiver recv;
    sig.connect(&recv, &TestReceiver::onSignal);
    sig.emit(55);
    CHECK(recv.lastValue == 55);
    CHECK(recv.callCount == 1);
    sig.emit(77);
    CHECK(recv.lastValue == 77);
    CHECK(recv.callCount == 2);
}

// ============================================================================
// Pack
// ============================================================================

TEST_CASE("Signal_Pack") {
    auto packed = Signal<int, String>::pack(42, "test");
    CHECK(packed.size() == 2);
    CHECK(packed[0].get<int>() == 42);
    CHECK(packed[1].get<String>() == "test");
}

// ============================================================================
// Emit with no connections (should not crash)
// ============================================================================

TEST_CASE("Signal_EmitNoConnections") {
    Signal<int, const String &> sig;
    sig.emit(42, "test");
    CHECK(true);
}

// ============================================================================
// Slot construction
// ============================================================================

TEST_CASE("Slot_DefaultConstruction") {
    Slot<int> slot([](int) {});
    CHECK(slot.owner() == nullptr);
    CHECK(slot.prototype() == nullptr);
    CHECK(slot.id() == -1);
}

TEST_CASE("Slot_ConstructionWithMetadata") {
    int owner = 0;
    Slot<int> slot([](int) {}, &owner, "void(int)", 5);
    CHECK(slot.owner() == &owner);
    CHECK(slot.prototype() != nullptr);
    CHECK(slot.id() == 5);
}

// ============================================================================
// Slot exec
// ============================================================================

TEST_CASE("Slot_Exec") {
    int received = 0;
    Slot<int> slot([&received](int val) { received = val; });
    slot.exec(42);
    CHECK(received == 42);
}

TEST_CASE("Slot_ExecMultipleArgs") {
    int ri = 0;
    String rs;
    Slot<int, const String &> slot([&](int i, const String &s) {
        ri = i;
        rs = s;
    });
    slot.exec(7, "hello");
    CHECK(ri == 7);
    CHECK(rs == "hello");
}

// ============================================================================
// Slot exec from VariantList
// ============================================================================

TEST_CASE("Slot_ExecFromVariantList") {
    int received = 0;
    Slot<int> slot([&received](int val) { received = val; });
    VariantList args = { Variant(99) };
    slot.exec(args);
    CHECK(received == 99);
}

// ============================================================================
// Slot pack
// ============================================================================

TEST_CASE("Slot_Pack") {
    auto packed = Slot<int, String>::pack(42, "test");
    CHECK(packed.size() == 2);
    CHECK(packed[0].get<int>() == 42);
    CHECK(packed[1].get<String>() == "test");
}

// ============================================================================
// Slot setID
// ============================================================================

TEST_CASE("Slot_SetID") {
    Slot<int> slot([](int) {});
    CHECK(slot.id() == -1);
    slot.setID(42);
    CHECK(slot.id() == 42);
}
