#ifndef JFG_SKELETON_H
#define JFG_SKELETON_H
#include <vector>
// Big head mode's skeleton maths; src/jfg/big_head.cpp applies it to the game's joint matrices.
namespace jfg::skeleton {
struct Joint {
    int parent; // an earlier joint's index, or -1 for a root
    float x;    // bind-pose offset from the parent: x across the body, y up (models face -z)
    float y;
};

// The head: from the joint highest in the bind pose, the first joint up its chain that sits on the body's centre line
// (x about 0), so that hair, ears and antennae lead back to the head they hang from. -1 when there is no head: the
// chain ends at a root, the "head" carries more than half the model (an animated prop such as the name-entry
// keyboard), or the skeleton is not in parent-before-child order.
int find_head(const std::vector<Joint>& joints);

// The joint and every joint below it.
std::vector<bool> subtree(const std::vector<Joint>& joints, int joint);

// Scales a row-major 4x4 joint matrix (rows 0-2 the joint's axes, row 3 its translation) by k about the point pivot.
void scale_about(float m[16], float k, const float pivot[3]);
}
#endif
