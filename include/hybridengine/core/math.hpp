#pragma once
#include <cmath>

namespace HybridEngine::Core {

// M0 数学最小集（双精度）
struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double X, double Y, double Z) : x(X), y(Y), z(Z) {}
    double Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 Cross(const Vec3& o) const { return {y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x}; }
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
};

// Quat（M0 最小：w/x/y/z + Euler 构造）
struct Quat {
    double w = 1, x = 0, y = 0, z = 0;
    static Quat Identity() { return Quat{}; }   // t2：显式恒等四元数（默认参数/人类直觉——值语义不变）
    static Quat Euler(double degX, double degY, double degZ);
};

// Mat4（M0 最小：Row-major 双精度；RT/InverseRT）
// Mat4 应用向量（无平移——方向/法向用）
struct Mat4 {
    double m[4][4] = {};
    static Mat4 Identity();
    static Mat4 RT(const Vec3& t, const Quat& r, const Vec3& s);
    Mat4 Mul(const Mat4& o) const;
    static Mat4 InverseRT(const Mat4& m);
    Vec3 TransformDir(const Vec3& v) const {
        return { m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z,
                 m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z,
                 m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z };
    }
    Vec3 TransformPoint(const Vec3& v) const {
        return { m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z + m[0][3],
                 m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z + m[1][3],
                 m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z + m[2][3] };
    }
};

} // namespace HybridEngine::Core
