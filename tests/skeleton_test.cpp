#include <cassert>
#include <cmath>
#include <cstdio>
#include "jfg_skeleton.h"

using jfg::skeleton::Joint;

// Made-up skeletons with the shapes the game's characters have (parent, bind offset x across the body, y up).
int main() {
    // A humanoid: hips (0), spine (1), chest (2), arms (3-6), head (7) on the chest, legs (8-11). The head is the
    // highest joint and sits on the centre line.
    const std::vector<Joint> humanoid = {
        {-1, 0.0f, 100.0f}, {0, 0.0f, 10.0f},  {1, 0.0f, 20.0f},   {2, 20.0f, 0.0f},   {3, 20.0f, -10.0f},
        {2, -20.0f, 0.0f},  {5, -20.0f, -10.0f}, {2, 0.0f, 40.0f}, {0, 10.0f, -50.0f}, {8, 0.0f, -50.0f},
        {0, -10.0f, -50.0f}, {10, 0.0f, -50.0f},
    };
    assert(jfg::skeleton::find_head(humanoid) == 7);
    std::vector<bool> members = jfg::skeleton::subtree(humanoid, 7);
    for (int j = 0; j < (int)humanoid.size(); j++) assert(members[j] == (j == 7));

    // Hair buns (8, 9) above the head stand highest, off the centre line; they lead back to the head (7) and come
    // with it, as does a ponytail (10).
    std::vector<Joint> buns = humanoid;
    buns.resize(8);
    buns.push_back({7, 15.0f, 40.0f});
    buns.push_back({7, -15.0f, 40.0f});
    buns.push_back({7, -20.0f, 25.0f});
    buns.push_back({0, 10.0f, -100.0f});
    assert(jfg::skeleton::find_head(buns) == 7);
    members = jfg::skeleton::subtree(buns, 7);
    for (int j = 0; j < (int)buns.size(); j++) assert(members[j] == (j >= 7 && j <= 10));

    // An insect: an antenna tip (12) is highest, two links out from the head (8); mandibles (9, 10) hang below it.
    const std::vector<Joint> insect = {
        {-1, 0.0f, 100.0f}, {0, 15.0f, -20.0f}, {1, 25.0f, -40.0f}, {0, -15.0f, -20.0f}, {3, -25.0f, -40.0f},
        {0, 0.0f, 10.0f},   {5, 30.0f, -10.0f}, {5, -30.0f, -10.0f}, {5, 0.0f, 40.0f},  {8, 20.0f, -15.0f},
        {8, -20.0f, -15.0f}, {8, 15.0f, 10.0f}, {11, 20.0f, 25.0f}, {8, -15.0f, 10.0f}, {13, -20.0f, 25.0f},
    };
    assert(jfg::skeleton::find_head(insect) == 8);
    members = jfg::skeleton::subtree(insect, 8);
    for (int j = 0; j < (int)insect.size(); j++) assert(members[j] == (j >= 8));

    // On all fours: an ear (3) is highest and leads back to the head (2), which carries both ears and the jaw (5).
    // A gun on the back (7) stands on the centre line above the head but below the ears; the tail (9, 10) is lower.
    const std::vector<Joint> quadruped = {
        {-1, 0.0f, 60.0f}, {0, 0.0f, 0.0f},   {1, 0.0f, 20.0f},   {2, 20.0f, 45.0f},  {2, -20.0f, 45.0f},
        {2, 0.0f, -10.0f}, {1, 0.0f, 10.0f},  {6, 0.0f, 40.0f},   {0, 0.0f, 0.0f},    {8, 0.0f, 10.0f},
        {9, 0.0f, 15.0f},  {1, 25.0f, -30.0f}, {11, 0.0f, -30.0f}, {1, -25.0f, -30.0f}, {13, 0.0f, -30.0f},
        {8, 25.0f, -30.0f}, {15, 0.0f, -30.0f}, {8, -25.0f, -30.0f}, {17, 0.0f, -30.0f},
    };
    assert(jfg::skeleton::find_head(quadruped) == 2);
    members = jfg::skeleton::subtree(quadruped, 2);
    for (int j = 0; j < (int)quadruped.size(); j++) assert(members[j] == (j >= 2 && j <= 5));

    // An animated prop (a keyboard): a frame (1) holding every key on the centre line's chain. Not a head.
    std::vector<Joint> keyboard = {{-1, 0.0f, 0.0f}, {0, 0.0f, 10.0f}};
    for (int key = 0; key < 43; key++) keyboard.push_back({1, -100.0f + 5.0f * key, key == 0 ? 5.0f : 1.0f});
    assert(jfg::skeleton::find_head(keyboard) == -1);

    // A skeleton whose highest point leads back to the root has no head to grow.
    assert(jfg::skeleton::find_head({{-1, 0.0f, 10.0f}, {0, 5.0f, 1.0f}, {0, -5.0f, -10.0f}}) == -1);
    // A joint listed before its parent: not a layout this understands.
    assert(jfg::skeleton::find_head({{-1, 0.0f, 0.0f}, {2, 0.0f, 1.0f}, {0, 0.0f, 1.0f}}) == -1);
    assert(jfg::skeleton::find_head({}) == -1);

    // Doubling about the pivot doubles the axes and keeps the pivot where it is.
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1};
    const float pivot[3] = {10, 20, 30};
    jfg::skeleton::scale_about(m, 2.0f, pivot);
    assert(m[0] == 2 && m[5] == 2 && m[10] == 2 && m[15] == 1);
    assert(m[12] == 10 && m[13] == 20 && m[14] == 30);
    // A joint 4 above the pivot moves to 8 above it.
    float child[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 24, 30, 1};
    jfg::skeleton::scale_about(child, 2.0f, pivot);
    assert(std::fabs(child[13] - 28.0f) < 1e-5f && child[12] == 10 && child[14] == 30);
    std::puts("skeleton_test: OK");
}
