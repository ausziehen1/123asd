// Arduino IDE 1.x inserts these signatures ahead of sketch-local definitions.
// Compile against the real shared header so a future type move fails this check.
#include <Arduino.h>
#include "../firmware/RecoilEngine/EngineSafety.h"
static bool r32ReadDiagSnapshot(R32DiagSnapshot& s);
static inline R32TimeDynamics r32TimeDynamicsRaw(uint32_t dtUs);
static inline R32TimeDynamics r32TimeDynamics(uint32_t dtUs);
static inline BulletTarget r32EvalSpline(const R32CachedSpline& c, float t);
static inline uint32_t packControl(int profile, CombatMode mode, bool enabled);
static inline ControlSnapshot readControl();
#include "../firmware/RecoilEngine/RecoilEngine.ino"
