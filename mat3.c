#include "mat3.h"

#include <math.h>

Mat3 mat3_scaling_rotation_translation(Vec2 scale, float rot, Vec2 trans)
{
    return mat3(scale.x * cos(rot), scale.x * sin(rot), 0.0f,
            scale.y * -sin(rot), scale.y * cos(rot), 0.0f,
            trans.x, trans.y, 1.0f);
}

Mat3 mat3_inverse(Mat3 m)
{
    float det;
    Mat3 inv;

    inv.m[0][0] = m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1];
    inv.m[0][1] = m.m[0][2] * m.m[2][1] - m.m[0][1] * m.m[2][2];
    inv.m[0][2] = m.m[0][1] * m.m[1][2] - m.m[0][2] * m.m[1][1];
    inv.m[1][0] = m.m[1][2] * m.m[2][0] - m.m[1][0] * m.m[2][2];
    inv.m[1][1] = m.m[0][0] * m.m[2][2] - m.m[0][2] * m.m[2][0];
    inv.m[1][2] = m.m[0][2] * m.m[1][0] - m.m[0][0] * m.m[1][2];
    inv.m[2][0] = m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0];
    inv.m[2][1] = m.m[0][1] * m.m[2][0] - m.m[0][0] * m.m[2][1];
    inv.m[2][2] = m.m[0][0] * m.m[1][1] - m.m[0][1] * m.m[1][0];

    det = m.m[0][0] * inv.m[0][0]
        + m.m[0][1] * inv.m[1][0]
        + m.m[0][2] * inv.m[2][0];

    if (det <= 10e-8)
        return inv; /* TODO: figure out what to do if not invertible */

    inv.m[0][0] /= det;
    inv.m[0][1] /= det;
    inv.m[0][2] /= det;
    inv.m[1][0] /= det;
    inv.m[1][1] /= det;
    inv.m[1][2] /= det;
    inv.m[2][0] /= det;
    inv.m[2][1] /= det;
    inv.m[2][2] /= det;

    return inv;
}

#undef mat3
Mat3 mat3(float m00, float m01, float m02,
        float m10, float m11, float m12,
        float m20, float m21, float m22)
{
    return (Mat3)
    {
        {
            { m00, m01, m02 },
            { m10, m11, m12 },
            { m20, m21, m22 }
        }
    };
}
