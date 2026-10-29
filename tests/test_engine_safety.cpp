#include "../firmware/RecoilEngine/EngineSafety.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
using namespace recoil;
static void near(double a, double b, double tolerance=1e-4) { assert(std::abs(a-b) < tolerance); }

void nominalEma() {
    float rate=0, original=0;
    std::mt19937 rng(17);
    std::uniform_real_distribution<float> d(-10,10);
    for (int i=0;i<10000;++i) {
        float x=d(rng);
        original += .3f*(x-original);
        near(filterIncrement(x,1,.3f,.3f,rate), original, 1e-5);
    }
}
void constantVelocityAndTimingGaps() {
    float rate=1;
    double total=0;
    for (int i=0;i<101;++i) {
        float dt=i==0?5:1;
        total += filterIncrement(dt,dt,1-std::pow(.7f,dt),.3f,rate);
    }
    near(total,105);
}
void conservationWithVariableDt() {
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> timing(.01f,5.f), input(-2.f,2.f);
    float rate=0; double in=0, out=0;
    for (int i=0;i<10000;++i) {
        float dt=timing(rng), increment=input(rng)*dt;
        in+=increment;
        out+=filterIncrement(increment,dt,1-std::pow(.7f,dt),.3f,rate);
    }
    // Account for remaining filter state; it drains into the output tail.
    near(out + (1.-.3f)/.3f*rate, in, .002);
    for (int i=0;i<100;++i) out+=filterIncrement(0,1,.3f,.3f,rate);
    near(out,in,.002);
}
void backlogExpiry() {
    HidBacklogGuard q(25000);
    assert(!q.onBlocked(0)); // zero is a legitimate timestamp
    assert(!q.expired(24999));
    assert(q.expired(25000)); // ready-path check, even without another busy tick
    assert(q.onBlocked(26000));
    assert(q.onBlocked(30000)); // stale remains latched; no periodic re-accumulation
    assert(q.expired(100000));
    q.reset(); assert(!q.expired(100000));
    assert(!q.onBlocked(100000));
    assert(!q.expired(100001));
    q.reset(); assert(!q.blocked && !q.stale);
}
void springDeadline() {
    assert(!springExpired(39999,15000,25000));
    assert(springExpired(40000,15000,25000));
    assert(springExpired(100000,15000,25000));
}
void fnGestures() {
    using F=FnGesture;
    F f(600,30,250);
    assert(f.update(1,0,1,0)==F::NONE);
    assert(f.update(0,1,0,50)==F::NONE);
    assert(f.update(1,0,1,200)==F::NONE);
    assert(f.update(0,1,0,250)==F::DOUBLE_CLICK); // no extra 250ms wait
    assert(f.update(0,0,0,1000)==F::NONE);
    // Second press begins within window but release is after first's deadline.
    F g(600,30,250);
    g.update(1,0,1,100); g.update(0,1,0,150); g.update(1,0,1,390);
    assert(g.update(0,0,1,410)==F::NONE);
    assert(g.update(0,1,0,450)==F::DOUBLE_CLICK);
    // Long hold fires once and consumes a pending click.
    F h(600,30,250);
    h.update(1,0,1,0); h.update(0,1,0,40); h.update(1,0,1,100);
    assert(h.update(0,0,1,700)==F::LONG_HOLD);
    assert(h.update(0,0,1,710)==F::NONE);
    assert(h.update(0,1,0,720)==F::NONE);
    // Release sampled just after the hold boundary still means LONG, not click.
    F releaseLong(600,30,250);
    releaseLong.update(1,0,1,100);
    assert(releaseLong.update(0,1,0,700)==F::LONG_HOLD);
    // A late second press starts a new sequence.
    F k(600,30,250);
    k.update(1,0,1,0); k.update(0,1,0,40); k.update(1,0,1,291);
    assert(k.update(0,1,0,340)==F::NONE);
    // millis rollover remains valid.
    F w(600,30,250);
    w.update(1,0,1,0xfffffff0u);
    assert(w.update(0,1,0,0x20u)==F::NONE);
    w.update(1,0,1,0x40u);
    assert(w.update(0,1,0,0x70u)==F::DOUBLE_CLICK);
    // Electrical press/release without a tracked press cannot create a command.
    F boot(600,30,250);
    assert(boot.update(0,1,0,1000)==F::NONE);
}
int main() {
    nominalEma(); constantVelocityAndTimingGaps(); conservationWithVariableDt();
    backlogExpiry(); springDeadline(); fnGestures();
    std::puts("PASS: 6 engine safety regression groups");
}
