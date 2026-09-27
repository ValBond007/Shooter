// Small 2D vector helpers for gm::Vec2f (the game does not depend on raylib
// for its simulation, only for drawing).
#pragma once

#include <cmath>

#include "game_memory.h"

using gm::Vec2f;

inline Vec2f operator+(Vec2f a, Vec2f b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2f operator-(Vec2f a, Vec2f b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2f operator*(Vec2f a, float s) { return {a.x * s, a.y * s}; }
inline Vec2f operator*(float s, Vec2f a) { return {a.x * s, a.y * s}; }
inline Vec2f operator/(Vec2f a, float s) { return {a.x / s, a.y / s}; }
inline Vec2f& operator+=(Vec2f& a, Vec2f b) { a.x += b.x; a.y += b.y; return a; }
inline Vec2f& operator-=(Vec2f& a, Vec2f b) { a.x -= b.x; a.y -= b.y; return a; }
inline Vec2f& operator*=(Vec2f& a, float s) { a.x *= s; a.y *= s; return a; }

inline float Dot(Vec2f a, Vec2f b) { return a.x * b.x + a.y * b.y; }
inline float LengthSq(Vec2f a) { return a.x * a.x + a.y * a.y; }
inline float Length(Vec2f a) { return std::sqrt(LengthSq(a)); }
inline float Distance(Vec2f a, Vec2f b) { return Length(a - b); }

inline Vec2f Normalize(Vec2f a) {
    float len = Length(a);
    return len > 1e-6f ? a / len : Vec2f{0.0f, 0.0f};
}

inline Vec2f Perp(Vec2f a) { return {-a.y, a.x}; }  // rotated 90 degrees
inline Vec2f FromAngle(float angle) { return {std::cos(angle), std::sin(angle)}; }
inline float AngleOf(Vec2f a) { return std::atan2(a.y, a.x); }

inline Vec2f Lerp(Vec2f a, Vec2f b, float t) { return a + (b - a) * t; }

// Move `current` towards `target` by at most `maxDelta`.
inline Vec2f MoveTowards(Vec2f current, Vec2f target, float maxDelta) {
    Vec2f diff = target - current;
    float dist = Length(diff);
    if (dist <= maxDelta || dist < 1e-6f) return target;
    return current + diff / dist * maxDelta;
}

inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
