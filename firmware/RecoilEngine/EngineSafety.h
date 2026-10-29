#pragma once
#include <stdint.h>

// Arduino IDE 1.x inserts function prototypes before later sketch-local types.
// Shared signature types must be visible from an included header at that point.
// Definitions are unchanged; this is a source-layout fix only.
struct BulletTarget {
    float x;
    float y;
};

enum CombatMode : int {
    MODE_CLOSE = 0,
    MODE_LONG  = 1
};

struct ControlSnapshot {
    int profile;
    CombatMode mode;
    bool enabled;
};

struct R32DiagSnapshot {
    uint32_t ticks, minDtUs, maxDtUs, lateTicks, maxLatenessUs;
    uint32_t hidQueued, hidBusy, hidRejected, hidDrops, hidUnmountedTicks;
    uint32_t hidMaxBusyStreak, hidMaxPendingAgeUs, hidMaxPendingAbs;
    uint32_t warmStarts, springStarts, watchdogActive;
    uint32_t schedulerMissedSlots, hidReadyAfterBusy, hidMaxQueuedAgeUs;
    uint32_t hidTransferCompleted, hidTransferFailed;
};

struct R32TimeDynamics {
    float emaAlpha, mirrorAlpha, driftRho, driftInnovation, microScale;
};

struct R32CachedSpline {
    int shot=-1, pattern=-1, mode=-1;
    bool sustain=false;
    BulletTarget a{}, b{}, c{}, d{};
};

namespace recoil {
// State is a rate in counts per nominal tick. The displacement correction is a
// telescoping state difference: variable dt cannot manufacture extra distance.
// At dt == nominal and alpha == nominalAlpha this equals the original EMA.
inline float filterIncrement(float increment, float dtInTicks, float alpha,
                             float nominalAlpha, float& rate) {
    const float previous = rate;
    rate += alpha * (increment / dtInTicks - rate);
    const float lag = (1.0f - nominalAlpha) / nominalAlpha;
    return increment + lag * (previous - rate);
}

struct HidBacklogGuard {
    const uint64_t maxAgeUs;
    uint64_t blockedSinceUs = 0;
    bool blocked = false;
    bool stale = false;

    explicit HidBacklogGuard(uint64_t maxAge) : maxAgeUs(maxAge) {}
    bool expired(uint64_t now) const {
        return stale || (blocked && now - blockedSinceUs >= maxAgeUs);
    }
    // Keep stale latched until recovery: do not build a new backlog every 25 ms.
    bool onBlocked(uint64_t now) {
        if (!blocked) { blocked = true; blockedSinceUs = now; }
        if (expired(now)) stale = true;
        return stale;
    }
    void reset() { blockedSinceUs = 0; blocked = stale = false; }
};

inline bool springExpired(uint64_t elapsedUs, uint64_t durationUs, uint64_t graceUs) {
    return elapsedUs >= durationUs + graceUs;
}

// Explicit FN gestures: press-to-press-window accounting is actually measured
// from first release to second press. A valid second release executes at once.
class FnGesture {
public:
    enum Event { NONE, DOUBLE_CLICK, LONG_HOLD };
    FnGesture(uint32_t holdMs, uint32_t minClickMs, uint32_t doubleWindowMs)
        : holdMs_(holdMs), minClickMs_(minClickMs), windowMs_(doubleWindowMs) {}
    Event update(bool pressEdge, bool releaseEdge, bool pressed, uint32_t now) {
        if (pressEdge) {
            if (waiting_ && uint32_t(now - releaseAt_) > windowMs_) waiting_ = false;
            tracking_ = true;
            pressAt_ = now;
            longFired_ = false;
        }
        if (tracking_ && (pressed || releaseEdge) && !longFired_ &&
            uint32_t(now - pressAt_) >= holdMs_) {
            longFired_ = true;
            if (releaseEdge) tracking_ = false;
            waiting_ = false;
            return LONG_HOLD;
        }
        if (releaseEdge) {
            const bool click = tracking_ && !longFired_ &&
                               uint32_t(now - pressAt_) >= minClickMs_;
            tracking_ = false;
            if (click) {
                if (waiting_) { waiting_ = false; return DOUBLE_CLICK; }
                waiting_ = true;
                releaseAt_ = now;
            }
        }
        if (waiting_ && !tracking_ && uint32_t(now - releaseAt_) > windowMs_)
            waiting_ = false;
        return NONE;
    }
private:
    const uint32_t holdMs_, minClickMs_, windowMs_;
    uint32_t pressAt_ = 0, releaseAt_ = 0;
    bool tracking_ = false, waiting_ = false, longFired_ = false;
};
} // namespace recoil
