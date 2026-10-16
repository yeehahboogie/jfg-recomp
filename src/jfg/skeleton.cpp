#include "jfg_skeleton.h"

#include <cmath>

namespace jfg::skeleton {
int find_head(const std::vector<Joint>& joints) {
    const int count = (int)joints.size();
    std::vector<float> x(count), y(count);
    int highest = -1;
    float extent = 0.0f;
    for (int j = 0; j < count; j++) {
        const int parent = joints[j].parent;
        if (parent >= j) {
            return -1;
        }
        x[j] = (parent < 0 ? 0.0f : x[parent]) + joints[j].x;
        y[j] = (parent < 0 ? 0.0f : y[parent]) + joints[j].y;
        extent = std::fmax(extent, std::fabs(y[j]));
        if (highest < 0 || y[j] > y[highest]) {
            highest = j;
        }
    }
    // Centre-line joints sit within 1% of the model's height of x = 0; mirrored parts are much further out.
    const float centre = 0.01f * extent;
    int head = highest;
    while (head >= 0 && std::fabs(x[head]) > centre) {
        head = joints[head].parent;
    }
    if (head < 0 || joints[head].parent < 0) {
        return -1;
    }
    int members = 0;
    for (bool m : subtree(joints, head)) members += m;
    if (2 * members > count) {
        return -1;
    }
    return head;
}

std::vector<bool> subtree(const std::vector<Joint>& joints, int joint) {
    std::vector<bool> members(joints.size(), false);
    members[joint] = true;
    for (int j = joint + 1; j < (int)joints.size(); j++) {
        const int parent = joints[j].parent;
        if (parent >= 0 && parent < j && members[parent]) {
            members[j] = true;
        }
    }
    return members;
}

void scale_about(float m[16], float k, const float pivot[3]) {
    for (int i = 0; i < 12; i++) {
        m[i] *= k;
    }
    for (int c = 0; c < 3; c++) {
        m[12 + c] = k * m[12 + c] + (1.0f - k) * pivot[c];
    }
}
}
