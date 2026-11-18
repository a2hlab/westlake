// ============================================================================
// android_input_Input_aosp.cpp
//
// 2026-05-18 (Plan A): adapter-private implementation of MotionEvent /
// KeyEvent / PointerCoords / PointerProperties classes.
//
// Mirrors AOSP frameworks/native/libs/input/Input.cpp's surface but with
// class layout under adapter control (see include/input/Input.h header for
// rationale).  Phase 1 implements what HelloWorld single-tap needs:
//   - MotionEvent::initialize / addSample / copyFrom
//   - distinct raw and transformed axis values, including history
//   - offsetLocation / scale (simple in-place mutation)
//   - composed view transforms and explicitly applied raw-sample transforms
//
// Out of scope (Phase 2):
//   - real history-sample retrieval (samples vector currently keeps current +
//     historical but only "current" path is actively exercised)
//   - Parcel serialization (readFromParcel/writeToParcel are stubs)
//   - axis-label lookup (delegated to InputEventLookup which itself is a stub
//     in this phase)
// ============================================================================

#include "input/Input.h"

#include <atomic>
#include <cmath>
#include <cstring>

namespace android {

namespace {
constexpr std::array<float, 9> IDENTITY = {1, 0, 0, 0, 1, 0, 0, 0, 1};
// frameworks/native/libs/input/android/os/IInputConstants.aidl.
constexpr int32_t SUPPORTS_ORIENTATION = 0x80;
constexpr int32_t SUPPORTS_DIRECTIONAL_ORIENTATION = 0x100;

float rounded(float value) { return std::round(value * 1024.f) / 1024.f; }
bool ignoresTransform(int32_t source) {
    return (source & AINPUT_SOURCE_CLASS_JOYSTICK) == AINPUT_SOURCE_CLASS_JOYSTICK
            || (source & AINPUT_SOURCE_CLASS_POSITION) == AINPUT_SOURCE_CLASS_POSITION
            || (source & AINPUT_SOURCE_MOUSE_RELATIVE) == AINPUT_SOURCE_MOUSE_RELATIVE;
}

float transformedAxis(const std::array<float, 9>& m, int32_t source, int32_t flags,
                      const PointerCoords& pc, int32_t axis) {
    if (ignoresTransform(source)) return pc.getAxisValue(axis);
    if (axis == AMOTION_EVENT_AXIS_X || axis == AMOTION_EVENT_AXIS_Y
            || axis == AMOTION_EVENT_AXIS_RELATIVE_X || axis == AMOTION_EVENT_AXIS_RELATIVE_Y) {
        bool relative = axis == AMOTION_EVENT_AXIS_RELATIVE_X || axis == AMOTION_EVENT_AXIS_RELATIVE_Y;
        float x = pc.getAxisValue(relative ? AMOTION_EVENT_AXIS_RELATIVE_X : AMOTION_EVENT_AXIS_X);
        float y = pc.getAxisValue(relative ? AMOTION_EVENT_AXIS_RELATIVE_Y : AMOTION_EVENT_AXIS_Y);
        bool translate = !relative && (source & AINPUT_SOURCE_CLASS_POINTER) != 0;
        int row = (axis == AMOTION_EVENT_AXIS_X || axis == AMOTION_EVENT_AXIS_RELATIVE_X) ? 0 : 3;
        // AOSP ui::Transform maps XY without a perspective divide.
        return rounded(m[row] * x + m[row + 1] * y + (translate ? m[row + 2] : 0.f));
    }
    if (axis == AMOTION_EVENT_AXIS_ORIENTATION) {
        if (!(flags & SUPPORTS_ORIENTATION)) return 0.f;
        float a = pc.getAxisValue(axis), x = std::sin(a), y = -std::cos(a);
        float tx = m[0] * x + m[1] * y, ty = m[3] * x + m[4] * y;
        if (!(flags & SUPPORTS_DIRECTIONAL_ORIENTATION) && ty > 0.f) { tx = -tx; ty = -ty; }
        return std::atan2(tx, -ty);
    }
    return pc.getAxisValue(axis);
}
}  // namespace

// ---------------- PointerCoords ----------------

#if !defined(__aarch64__)
void PointerCoords::clear() {
    bits = 0;
    for (float& v : values) v = 0.f;
    isResampled = false;
}
#endif

void PointerCoords::setAxisValue(int32_t axis, float value) {
    if (axis < 0 || axis >= 32) return;
    values[axis] = value;
    bits |= (uint64_t(1) << axis);
}

float PointerCoords::getAxisValue(int32_t axis) const {
    if (axis < 0 || axis >= 32) return 0.f;
    if (!(bits & (uint64_t(1) << axis))) return 0.f;
    return values[axis];
}

// ---------------- PointerProperties ----------------

#if !defined(__aarch64__)
void PointerProperties::clear() {
    id = -1;
    toolType = ToolType::UNKNOWN;
}
#endif

// ---------------- InputEvent ----------------

int32_t InputEvent::nextId() {
    static std::atomic<int32_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

// ---------------- MotionEvent ----------------

void MotionEvent::initialize(int32_t id, int32_t deviceId, int32_t source, int32_t displayId,
                             const std::array<uint8_t, 32>& hmac, int32_t action,
                             int32_t actionButton, int32_t flags, int32_t edgeFlags,
                             int32_t metaState, int32_t buttonState,
                             MotionClassification classification, float xOffset, float yOffset,
                             float xPrecision, float yPrecision, float rawXCursorPosition,
                             float rawYCursorPosition, int64_t downTime, int64_t eventTime,
                             size_t pointerCount, const PointerProperties* pointerProperties,
                             const PointerCoords* pointerCoords) {
    mId = id;
    mDeviceId = deviceId;
    mSource = source;
    mDisplayId = displayId;
    mHmac = hmac;
    mAction = action;
    mActionButton = actionButton;
    mFlags = flags;
    mEdgeFlags = edgeFlags;
    mMetaState = metaState;
    mButtonState = buttonState;
    mClassification = classification;
    mTransform = IDENTITY;
    mTransform[2] = xOffset;
    mTransform[5] = yOffset;
    mXPrecision = xPrecision;
    mYPrecision = yPrecision;
    mXCursorPosition = rawXCursorPosition;
    mYCursorPosition = rawYCursorPosition;
    mDownTime = downTime;

    mPointerProperties.clear();
    mPointerProperties.reserve(pointerCount);
    for (size_t i = 0; i < pointerCount; ++i) {
        mPointerProperties.push_back(pointerProperties[i]);
    }

    mSampleEventTimes.clear();
    mSamplePointerCoords.clear();
    addSample(eventTime, pointerCoords);
}

void MotionEvent::addSample(int64_t eventTime, const PointerCoords* pointerCoords) {
    mSampleEventTimes.push_back(eventTime);
    size_t n = mPointerProperties.size();
    for (size_t i = 0; i < n; ++i) {
        mSamplePointerCoords.push_back(pointerCoords[i]);
    }
}

void MotionEvent::copyFrom(const MotionEvent* other, bool keepHistory) {
    mId = other->mId;
    mDeviceId = other->mDeviceId;
    mSource = other->mSource;
    mDisplayId = other->mDisplayId;
    mHmac = other->mHmac;
    mAction = other->mAction;
    mActionButton = other->mActionButton;
    mFlags = other->mFlags;
    mEdgeFlags = other->mEdgeFlags;
    mMetaState = other->mMetaState;
    mButtonState = other->mButtonState;
    mClassification = other->mClassification;
    mTransform = other->mTransform;
    mXPrecision = other->mXPrecision;
    mYPrecision = other->mYPrecision;
    mXCursorPosition = other->mXCursorPosition;
    mYCursorPosition = other->mYCursorPosition;
    mDownTime = other->mDownTime;
    mPointerProperties = other->mPointerProperties;

    if (keepHistory) {
        mSampleEventTimes = other->mSampleEventTimes;
        mSamplePointerCoords = other->mSamplePointerCoords;
    } else {
        mSampleEventTimes.clear();
        mSamplePointerCoords.clear();
        size_t n = mPointerProperties.size();
        if (!other->mSampleEventTimes.empty()) {
            mSampleEventTimes.push_back(other->mSampleEventTimes.back());
            size_t base = other->mSamplePointerCoords.size() - n;
            for (size_t i = 0; i < n; ++i) {
                mSamplePointerCoords.push_back(other->mSamplePointerCoords[base + i]);
            }
        }
    }
}

int64_t MotionEvent::getEventTime() const {
    return mSampleEventTimes.empty() ? 0 : mSampleEventTimes.back();
}

int64_t MotionEvent::getHistoricalEventTime(size_t historicalIndex) const {
    if (historicalIndex >= mSampleEventTimes.size()) return 0;
    return mSampleEventTimes[historicalIndex];
}

const PointerProperties* MotionEvent::getPointerProperties(size_t pointerIndex) const {
    if (pointerIndex >= mPointerProperties.size()) return nullptr;
    return &mPointerProperties[pointerIndex];
}

int32_t MotionEvent::getPointerId(size_t pointerIndex) const {
    if (pointerIndex >= mPointerProperties.size()) return -1;
    return mPointerProperties[pointerIndex].id;
}

ToolType MotionEvent::getToolType(size_t pointerIndex) const {
    if (pointerIndex >= mPointerProperties.size()) return ToolType::UNKNOWN;
    return mPointerProperties[pointerIndex].toolType;
}

ssize_t MotionEvent::findPointerIndex(int32_t pointerId) const {
    for (size_t i = 0; i < mPointerProperties.size(); ++i) {
        if (mPointerProperties[i].id == pointerId) return static_cast<ssize_t>(i);
    }
    return -1;
}

size_t MotionEvent::getHistorySize() const {
    // History size = total samples - 1 (last sample is "current").
    return mSampleEventTimes.empty() ? 0 : mSampleEventTimes.size() - 1;
}

bool MotionEvent::isResampled(size_t pointerIndex, size_t historicalIndex) const {
    size_t n = mPointerProperties.size();
    if (n == 0) return false;
    size_t base = historicalIndex * n + pointerIndex;
    if (base >= mSamplePointerCoords.size()) return false;
    return mSamplePointerCoords[base].isResampled;
}

bool MotionEvent::isTouchEvent() const {
    return (mSource & AINPUT_SOURCE_CLASS_POINTER) != 0;
}

// Raw samples remain unchanged by transform(), including historical samples.
float MotionEvent::getRawAxisValue(int32_t axis, size_t pointerIndex) const {
    size_t historyIdx = mSampleEventTimes.empty() ? 0 : mSampleEventTimes.size() - 1;
    return getHistoricalRawAxisValue(axis, pointerIndex, historyIdx);
}

float MotionEvent::getAxisValue(int32_t axis, size_t pointerIndex) const {
    return getHistoricalAxisValue(axis, pointerIndex, getHistorySize());
}

float MotionEvent::getHistoricalRawAxisValue(int32_t axis, size_t pointerIndex,
                                             size_t historicalIndex) const {
    size_t n = mPointerProperties.size();
    if (n == 0) return 0.f;
    size_t base = historicalIndex * n + pointerIndex;
    if (base >= mSamplePointerCoords.size()) return 0.f;
    return mSamplePointerCoords[base].getAxisValue(axis);
}

float MotionEvent::getHistoricalAxisValue(int32_t axis, size_t pointerIndex,
                                          size_t historicalIndex) const {
    const PointerCoords* pc = getHistoricalRawPointerCoords(pointerIndex, historicalIndex);
    return pc ? transformedAxis(mTransform, mSource, mFlags, *pc, axis) : 0.f;
}

const PointerCoords* MotionEvent::getRawPointerCoords(size_t pointerIndex) const {
    size_t historyIdx = mSampleEventTimes.empty() ? 0 : mSampleEventTimes.size() - 1;
    return getHistoricalRawPointerCoords(pointerIndex, historyIdx);
}

const PointerCoords* MotionEvent::getHistoricalRawPointerCoords(size_t pointerIndex,
                                                                size_t historicalIndex) const {
    size_t n = mPointerProperties.size();
    if (n == 0 || pointerIndex >= n) return nullptr;
    size_t base = historicalIndex * n + pointerIndex;
    if (base >= mSamplePointerCoords.size()) return nullptr;
    return &mSamplePointerCoords[base];
}

void MotionEvent::offsetLocation(float xOffset, float yOffset) {
    mTransform[2] += xOffset;
    mTransform[5] += yOffset;
}

void MotionEvent::scale(float scaleFactor) {
    mTransform[2] *= scaleFactor;
    mTransform[5] *= scaleFactor;
    mXPrecision *= scaleFactor;
    mYPrecision *= scaleFactor;
    for (PointerCoords& pc : mSamplePointerCoords) {
        for (int axis : {AMOTION_EVENT_AXIS_X, AMOTION_EVENT_AXIS_Y,
                AMOTION_EVENT_AXIS_TOUCH_MAJOR, AMOTION_EVENT_AXIS_TOUCH_MINOR,
                AMOTION_EVENT_AXIS_TOOL_MAJOR, AMOTION_EVENT_AXIS_TOOL_MINOR,
                AMOTION_EVENT_AXIS_RELATIVE_X, AMOTION_EVENT_AXIS_RELATIVE_Y}) {
            if (pc.bits & (uint64_t(1) << axis)) {
                pc.values[axis] *= scaleFactor;
            }
        }
    }
}

void MotionEvent::transform(const std::array<float, 9>& matrix) {
    std::array<float, 9> result{};
    for (size_t row = 0; row < 3; ++row)
        for (size_t col = 0; col < 3; ++col)
            for (size_t k = 0; k < 3; ++k)
                result[row * 3 + col] += matrix[row * 3 + k] * mTransform[k * 3 + col];
    mTransform = result;
}

void MotionEvent::applyTransform(const std::array<float, 9>& matrix) {
    if (!ignoresTransform(mSource)) {
        for (PointerCoords& pc : mSamplePointerCoords) {
            const PointerCoords original = pc;
            for (int axis : {AMOTION_EVENT_AXIS_X, AMOTION_EVENT_AXIS_Y,
                    AMOTION_EVENT_AXIS_RELATIVE_X, AMOTION_EVENT_AXIS_RELATIVE_Y,
                    AMOTION_EVENT_AXIS_ORIENTATION})
                pc.setAxisValue(axis, transformedAxis(matrix, mSource, mFlags, original, axis));
        }
    }
    if (!std::isnan(mXCursorPosition) && !std::isnan(mYCursorPosition)) {
        float x = mXCursorPosition, y = mYCursorPosition;
        mXCursorPosition = matrix[0] * x + matrix[1] * y + matrix[2];
        mYCursorPosition = matrix[3] * x + matrix[4] * y + matrix[5];
    }
}

float MotionEvent::getXCursorPosition() const {
    return rounded(mTransform[0] * mXCursorPosition + mTransform[1] * mYCursorPosition + mTransform[2]);
}

float MotionEvent::getYCursorPosition() const {
    return rounded(mTransform[3] * mXCursorPosition + mTransform[4] * mYCursorPosition + mTransform[5]);
}

void MotionEvent::setCursorPosition(float x, float y) {
    float determinant = mTransform[0] * mTransform[4] - mTransform[1] * mTransform[3];
    x -= mTransform[2]; y -= mTransform[5];
    mXCursorPosition = (mTransform[4] * x - mTransform[1] * y) / determinant;
    mYCursorPosition = (-mTransform[3] * x + mTransform[0] * y) / determinant;
}

int32_t MotionEvent::readFromParcel(void* /*parcel*/) {
    // Phase 1: cross-process input dispatch is not used; HelloWorld receives
    // events via in-process InputEventReceiver worker socketpair, not Parcel.
    return -1;
}

int32_t MotionEvent::writeToParcel(void* /*parcel*/) const { return -1; }

const char* MotionEvent::getLabel(int32_t /*axis*/) {
    // Phase 1 stub.  See InputEventLookup::getAxisLabel for the canonical
    // table-driven impl when extended.
    return nullptr;
}

std::optional<int> MotionEvent::getAxisFromLabel(const char* /*label*/) {
    return std::nullopt;
}

}  // namespace android
