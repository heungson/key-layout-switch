#pragma once
#include <stdint.h>

namespace dual {
enum class InputMode { English, Korean, Other, Unknown };

// Bridge brief IME query failures only within the exact same input context.
// A failed query never extends the lifetime of the last confirmed result.
class ModeContinuity {
    uintptr_t foreground_ = 0, focus_ = 0, layout_ = 0;
    InputMode confirmed_ = InputMode::Unknown;
    uint64_t confirmedAt_ = 0;
public:
    static constexpr uint64_t GraceMs = 250;
    void Invalidate() { confirmed_ = InputMode::Unknown; }
    InputMode Resolve(InputMode observed, uintptr_t foreground, uintptr_t focus,
                      uintptr_t layout, uint64_t now, bool queryFailed) {
        if (foreground_ != foreground || focus_ != focus || layout_ != layout) {
            Invalidate();
            foreground_ = foreground; focus_ = focus; layout_ = layout;
        }
        if (observed != InputMode::Unknown) {
            confirmed_ = observed; confirmedAt_ = now;
            return observed;
        }
        if (queryFailed && foreground && focus && layout &&
            (confirmed_ == InputMode::English || confirmed_ == InputMode::Korean) &&
            now >= confirmedAt_ && now-confirmedAt_ <= GraceMs)
            return confirmed_;
        Invalidate();
        return InputMode::Unknown;
    }
};

// Physical US-QWERTY scan codes -> physical US-Dvorak scan codes.
// Windows handles Shift, Caps Lock, key repeat and Korean composition.
struct Mapping { uint16_t source, target; };
inline constexpr Mapping mappings[] = {
    {0x0c,0x1a},{0x0d,0x1b},
    {0x10,0x28},{0x11,0x33},{0x12,0x34},{0x13,0x19},{0x14,0x15},
    {0x15,0x21},{0x16,0x22},{0x17,0x2e},{0x18,0x13},{0x19,0x26},
    {0x1a,0x35},{0x1b,0x0d},
    {0x1f,0x18},{0x20,0x12},{0x21,0x16},{0x22,0x17},{0x23,0x20},
    {0x24,0x23},{0x25,0x14},{0x26,0x31},{0x27,0x1f},{0x28,0x0c},
    {0x2c,0x27},{0x2d,0x10},{0x2e,0x24},{0x2f,0x25},{0x30,0x2d},
    {0x31,0x30},{0x33,0x11},{0x34,0x2f},{0x35,0x2c}
};

inline uint16_t DvorakScan(uint16_t source) {
    for (const auto& entry : mappings) if (entry.source == source) return entry.target;
    return 0;
}

struct Policy {
    bool enabled = false;
    bool preserveShortcuts = true;
    bool shortcutHeld = false;
    bool scrollBypass = false;
    bool extended = false;
    InputMode mode = InputMode::Unknown;
};

inline bool ShouldMap(const Policy& policy) {
    return policy.enabled && !policy.extended && !policy.scrollBypass &&
        policy.mode == InputMode::English &&
        !(policy.preserveShortcuts && policy.shortcutHeld);
}

struct Route { bool held = false; uint16_t target = 0; };
// Latch the route on the FIRST down. A matching up must go to the same
// destination even if focus, IME, modifiers or pause state change meanwhile.
class KeyRouter {
public:
    Route held[128] = {};
    uint16_t Down(uint16_t source, const Policy& policy) {
        if (source >= 128 || policy.extended) return 0;
        auto& route = held[source];
        if (!route.held) {
            route.held = true;
            route.target = ShouldMap(policy) ? DvorakScan(source) : 0;
        }
        return route.target;
    }
    uint16_t Up(uint16_t source, bool extended) {
        if (source >= 128 || extended) return 0;
        auto& route = held[source];
        const auto target = route.target;
        route = {};
        return target;
    }
    // If injection fails, allow the original key and its future releases.
    void Failed(uint16_t source) { if (source < 128) held[source].target = 0; }
};
}
