#include "pc/pc_haptics.h"
#include <cassert>
#include <limits>
static bool near(double a,double b) { return std::abs(a-b)<0.00001; }
int main() {
    PcHapticShape shape;
    PcHapticSample sample{10,true,{3,1,4,0,0xA0000000u},{}};
    auto level=shape.update(sample,1000000000,false,0.8);
    assert(near(level.heavy,0.55*0.8));
    assert(near(level.light,0.55*0.9*0.8));
    sample.frame=11; sample.shock.frame=2;
    level=shape.update(sample,1016683350,false,0.8);
    assert(level.heavy>0 && level.heavy<0.44); // release through a gap, not full continuous rumble
    sample.frame=12; sample.shock.frame=3;
    level=shape.update(sample,1033366700,false,0.8);
    assert(near(level.heavy,0.44)); // second pulse
    assert(shape.update(sample,1034000000,true,0.8).heavy==0); // menu suppression
    sample.allowed=false;
    assert(shape.update(sample,1035000000,false,0.8).heavy==0); // native Vibration Off/paused
    sample.allowed=true;
    assert(shape.update(sample,1300000000,false,0.8).heavy==0); // logic stalled >150 ms
    sample.frame=13; sample.shock={-1,-99,0,0,0};
    assert(shape.update(sample,1310000000,false,0.8).heavy==0); // canceled shock never finishes from cache
    sample.quake={2,3,8,0,0xF0000000u};
    level=shape.update(sample,1320000000,false,1);
    assert(near(level.heavy,0.6*0.7)); // half-density quake, gap texture
    assert(near(level.light,0.6*0.25));
    sample.frame=16;
    level=shape.update(sample,1336683350,false,1);
    assert(near(level.heavy,0.6));
    assert(shape.update(sample,1336683350,false,0).heavy==0);
    assert(shape.update(sample,1336683350,false,std::numeric_limits<double>::quiet_NaN()).heavy==0);
    sample.quake.length=33;
    assert(shape.update(sample,1340000000,false,1).heavy==0); // invalid pattern is silent
    assert(!PcHapticShape::bit(0xFFFFFFFF,32,32));
    assert(!PcHapticShape::bit(0xFFFFFFFF,32,-1));
    assert(PcHapticShape::count(0xFFFFFFFF,32)==32);
    PcHapticExpiry expiry;
    assert(!expiry.expired(1000000000));
    expiry.arm(1000000000);
    assert(!expiry.expired(1119999999));
    expiry.arm(1100000000); // refresh extends the deadline
    assert(!expiry.expired(1120000000));
    assert(expiry.expired(1220000000));
    assert(!expiry.expired(1220000001)); // watchdog cancellation is one-shot
}
