// Standalone host contract test for the adapter's private MotionEvent implementation.
#include "input/Input.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace android;
static unsigned checks;
static void eq(float actual, float expected, const char* reason) {
    ++checks;
    if (std::abs(actual - expected) > 0.002f || !std::isfinite(actual)) {
        std::fprintf(stderr, "FAIL %s: %g != %g\n", reason, actual, expected);
        std::exit(1);
    }
}
static PointerCoords point(float x, float y) {
    PointerCoords p{};
    p.setAxisValue(AMOTION_EVENT_AXIS_X, x);
    p.setAxisValue(AMOTION_EVENT_AXIS_Y, y);
    p.setAxisValue(AMOTION_EVENT_AXIS_PRESSURE, 0.75f);
    p.setAxisValue(AMOTION_EVENT_AXIS_SIZE, 0.25f);
    return p;
}
static MotionEvent event(int source = AINPUT_SOURCE_TOUCHSCREEN, int flags = 0) {
    MotionEvent e;
    PointerProperties props[2] = {{3, ToolType::FINGER}, {7, ToolType::FINGER}};
    PointerCoords coords[2] = {point(10, 20), point(30, 40)};
    e.initialize(1, 2, source, 0, INVALID_HMAC, AMOTION_EVENT_ACTION_DOWN, 0,
            flags, 0, 0, 0, MotionClassification::NONE, 0, 0, 1, 2,
            NAN, NAN, 100, 110, 2, props, coords);
    return e;
}
int main() {
    const std::array<float, 9> move = {1,0,100, 0,1,-144, 0,0,1};
    const std::array<float, 9> scale = {2,0,0, 0,3,0, 0,0,1};
    const std::array<float, 9> rotate = {0,-1,0, 1,0,0, 0,0,1};
    auto e = event();
    e.transform(move);
    eq(e.getAxisValue(0,0),110,"translated X");
    eq(e.getAxisValue(1,0),-124,"translated Y");
    eq(e.getRawAxisValue(0,0),10,"raw X preserved");
    eq(e.getRawAxisValue(1,0),20,"raw Y preserved");
    eq(e.getAxisValue(1,1),-104,"second pointer translated");
    e.transform(scale);
    eq(e.getAxisValue(0,0),220,"composition order X");
    eq(e.getAxisValue(1,0),-372,"composition order Y");
    e.offsetLocation(-5,7);
    eq(e.getAxisValue(0,0),215,"offset after scale");
    eq(e.getAxisValue(1,0),-365,"offset after scale Y");
    e.transform(rotate);
    eq(e.getAxisValue(0,0),365,"rotation X");
    eq(e.getAxisValue(1,0),215,"rotation Y");
    PointerCoords later[2] = {point(11,21), point(31,41)};
    e.addSample(120,later);
    eq(e.getAxisValue(0,0),362,"current transformed sample");
    eq(e.getHistoricalAxisValue(0,0,0),365,"historical transformed sample");
    eq(e.getHistoricalRawAxisValue(0,0,0),10,"historical raw preserved");
    MotionEvent copy;
    copy.copyFrom(&e,true);
    eq(copy.getAxisValue(0,0),362,"copy retains transform");
    eq(copy.getHistorySize(),1,"copy retains history");
    copy.copyFrom(&e,false);
    eq(copy.getAxisValue(0,0),362,"current-only copy retains transform");
    eq(copy.getHistorySize(),0,"current-only copy drops history");
    eq(copy.getPointerId(1),7,"copy preserves pointer IDs");
    auto raw = event();
    raw.offsetLocation(3,4);
    raw.applyTransform(move);
    eq(raw.getRawAxisValue(0,0),110,"applyTransform changes raw X");
    eq(raw.getAxisValue(0,0),113,"applyTransform preserves local offset");
    eq(raw.getRawAxisValue(1,1),-104,"applyTransform second pointer");
    auto scaled = event();
    scaled.offsetLocation(1,2);
    scaled.scale(2);
    eq(scaled.getAxisValue(0,0),22,"global scale includes offset");
    eq(scaled.getAxisValue(AMOTION_EVENT_AXIS_PRESSURE,0),.75f,"pressure is not a length");
    eq(scaled.getAxisValue(AMOTION_EVENT_AXIS_SIZE,0),.25f,"normalized size unchanged");
    eq(scaled.getXPrecision(),2,"X precision scales");
    eq(scaled.getYPrecision(),4,"Y precision scales");
    for (int source : {AINPUT_SOURCE_JOYSTICK, AINPUT_SOURCE_TOUCHPAD, AINPUT_SOURCE_MOUSE_RELATIVE}) {
        auto ignored = event(source);
        ignored.transform(move);
        eq(ignored.getAxisValue(0,0),10,"relative input ignores window transform");
        ignored.applyTransform(move);
        eq(ignored.getRawAxisValue(1,0),20,"relative raw input unchanged");
    }
    auto ball = event(AINPUT_SOURCE_TRACKBALL);
    ball.transform(move); ball.transform(scale);
    eq(ball.getAxisValue(0,0),20,"trackball scales without translation");
    auto cursor = event(AINPUT_SOURCE_MOUSE);
    cursor.transform(scale); cursor.transform(move);
    cursor.setCursorPosition(111,222);
    eq(cursor.getXCursorPosition(),111,"cursor inverse X");
    eq(cursor.getYCursorPosition(),222,"cursor inverse Y");
    auto vectors = event(AINPUT_SOURCE_TOUCHSCREEN,0x80);
    PointerCoords vector[2] = {point(10,20),point(30,40)};
    vector[0].setAxisValue(AMOTION_EVENT_AXIS_RELATIVE_X,2);
    vector[0].setAxisValue(AMOTION_EVENT_AXIS_RELATIVE_Y,3);
    vector[0].setAxisValue(AMOTION_EVENT_AXIS_ORIENTATION,0);
    vectors.addSample(130,vector);
    vectors.transform(move); vectors.transform(rotate);
    eq(vectors.getAxisValue(AMOTION_EVENT_AXIS_RELATIVE_X,0),-3,"relative vector rotation without translation");
    eq(vectors.getAxisValue(AMOTION_EVENT_AXIS_RELATIVE_Y,0),2,"relative vector Y");
    eq(std::abs(vectors.getAxisValue(AMOTION_EVENT_AXIS_ORIENTATION,0)),1.5707963f,"orientation rotation");
    // Copying a fresh event must remove the previous transform and history.
    auto fresh = event(); e.copyFrom(&fresh,false);
    eq(e.getAxisValue(0,0),10,"fresh event resets transform");
    std::printf("INPUT_TRANSFORM_HOST_PASS checks=%u\n",checks);
}
