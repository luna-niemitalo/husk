#include "canon_bone_naming.hpp"

#include <cmath>
#include <map>
#include <optional>

#include "canon_bone_stem.hpp"

namespace husk::canon {

namespace {

// Real bloodknightcharger.m2 ArmL/ArmR (keyBoneId 0/1) and bloodelffemale.m2
// ForearmL/ForearmR (keyBoneId 81/80) both mirror with equal X/Z and
// opposite-sign Y (e.g. ArmL y=+0.201479, ArmR y=-0.201479) -- verified
// against real fixture data before writing this, not assumed. Left is
// positive Y, right is negative Y, in raw M2 space.
constexpr float kMirrorEpsilon = 1e-3f;

bool approxEqual(float a, float b) { return std::fabs(a - b) < kMirrorEpsilon; }

// A joint sitting on the sagittal plane itself (y == 0, e.g. spine/head) is
// never a mirror candidate -- it has no opposite-sign counterpart to find.
bool positionsMirror(const m2::Vec3& a, const m2::Vec3& b) {
    if (std::fabs(a.y) < kMirrorEpsilon) return false;
    return approxEqual(a.x, b.x) && approxEqual(a.z, b.z) && approxEqual(a.y, -b.y);
}

class LabelBuilder {
public:
    explicit LabelBuilder(const Skeleton& skeleton)
        : joints_(skeleton.joints), children_(joints_.size()), subtreeSize_(joints_.size(), 0) {
        for (size_t i = 0; i < joints_.size(); ++i) {
            int parent = joints_[i].parent;
            if (parent != -1) children_[static_cast<size_t>(parent)].push_back(i);
        }
        // Post-order over an explicit recursion, not array order: M2 bone
        // arrays don't guarantee a parent's index precedes its children's.
        std::vector<bool> done(joints_.size(), false);
        for (size_t i = 0; i < joints_.size(); ++i) computeSubtreeSize(i, done);
    }

    std::vector<std::string> run() {
        std::vector<std::string> labels(joints_.size());
        labels_ = &labels;
        // Ascending bone-index order -- DESIGN.md's "same model always
        // produces the same labels" via a fixed, arbitrary-but-documented
        // traversal order.
        for (size_t root = 0; root < joints_.size(); ++root) {
            if (joints_[root].parent == -1) startChain(root);
        }
        labels_ = nullptr;
        return labels;
    }

private:
    const std::vector<Joint>& joints_;
    std::vector<std::vector<size_t>> children_;
    std::vector<size_t> subtreeSize_;
    std::vector<std::string>* labels_ = nullptr;
    // node index -> stem it shares with its mirror (or owns alone),
    // populated as each chain start is discovered. Lookups against this
    // map never depend on discovery order: findMirrorPartner is a pure
    // geometry+topology query, so whichever of a mirror pair is discovered
    // second always finds the first one's entry already here.
    std::map<size_t, std::string> stemOf_;
    StemGenerator stemGen_;

    size_t computeSubtreeSize(size_t i, std::vector<bool>& done) {
        if (done[i]) return subtreeSize_[i];
        size_t total = 1;
        for (size_t c : children_[i]) total += computeSubtreeSize(c, done);
        subtreeSize_[i] = total;
        done[i] = true;
        return total;
    }

    // DESIGN.md: "two bones whose bind-pose pivots mirror... with
    // structurally mirrored parent chains too (not position alone)".
    // Parent chains "mirror" when they share one physical parent (the
    // common branch-off point) or recurse to a parent pair that itself
    // mirrors; two roots need no parent check at all. Doesn't additionally
    // verify full subtree isomorphism (matching child counts down the
    // whole limb) -- DESIGN.md's own wording scopes the check to the
    // parent chain, not the full descendant tree, so this is the literal
    // spec, not a shortcut past it.
    bool isMirror(size_t a, size_t b) const {
        if (a == b) return false;
        if (!positionsMirror(joints_[a].globalPosition, joints_[b].globalPosition)) return false;
        int pa = joints_[a].parent;
        int pb = joints_[b].parent;
        if (pa == -1 && pb == -1) return true;
        if (pa == -1 || pb == -1) return false;
        if (pa == pb) return true;
        return isMirror(static_cast<size_t>(pa), static_cast<size_t>(pb));
    }

    // Lowest-index match wins when more than one node coincidentally
    // satisfies isMirror -- keeps this deterministic without asserting a
    // real skeleton never has such a coincidence.
    std::optional<size_t> findMirrorPartner(size_t node) const {
        for (size_t j = 0; j < joints_.size(); ++j) {
            if (isMirror(node, j)) return j;
        }
        return std::nullopt;
    }

    std::string chooseStem(size_t node, const std::optional<size_t>& partner) {
        if (partner) {
            auto it = stemOf_.find(*partner);
            if (it != stemOf_.end()) {
                stemOf_[node] = it->second;
                return it->second;
            }
        }
        std::string stem = stemGen_.next();
        stemOf_[node] = stem;
        return stem;
    }

    // Starts a brand-new, un-nested chain: a forest root, or a child that
    // mirrors a sibling (a bilateral fork, e.g. two legs off one pelvis --
    // DESIGN.md's own a1a_L/a1a_R example carries no ancestor prefix at
    // all, so a mirrored fork's label starts fresh here too, unlike an
    // asymmetric branch's "_sub_"-nested one).
    void startChain(size_t node) {
        auto partner = findMirrorPartner(node);
        std::string stem = chooseStem(node, partner);
        std::string prefix = stem;
        if (partner) prefix += joints_[node].globalPosition.y > 0.0f ? "_L" : "_R";
        labelChain(node, prefix);
    }

    // Walks one chain from `start`, labeling each position
    // `<prefix>_<idx>.0`. At a branch point: a child mirroring a sibling
    // starts its own independent chain (startChain, above); among the
    // rest, the largest-subtree child continues this chain (ties to
    // lowest index -- kids is built in ascending order, so keeping the
    // first-seen max already implements that; DESIGN.md doesn't name a
    // tiebreak, this is the documented choice), and every other child is
    // either folded (<=2 bones, DESIGN.md's ".32nd auxiliary slot") or
    // promoted to its own nested "_sub_" chain (>2 bones).
    void labelChain(size_t start, const std::string& prefix) {
        size_t cur = start;
        for (size_t idx = 0;; ++idx) {
            std::string ownLabel = prefix + "_" + std::to_string(idx) + ".0";
            (*labels_)[cur] = ownLabel;

            const auto& kids = children_[cur];
            if (kids.empty()) return;
            if (kids.size() == 1) {
                cur = kids[0];
                continue;
            }

            std::vector<size_t> solo;
            for (size_t k : kids) {
                auto partner = findMirrorPartner(k);
                if (partner && joints_[*partner].parent == joints_[k].parent) {
                    startChain(k);
                } else {
                    solo.push_back(k);
                }
            }
            if (solo.empty()) return;  // every child was a mirrored fork; nothing continues `cur`

            size_t mainChild = solo[0];
            for (size_t k : solo) {
                if (subtreeSize_[k] > subtreeSize_[mainChild]) mainChild = k;
            }

            size_t subCounter = 1;
            for (size_t k : solo) {
                if (k == mainChild) continue;
                if (subtreeSize_[k] <= 2) {
                    foldSubtree(k, prefix, idx, subCounter);
                } else {
                    std::string stem = chooseStem(k, findMirrorPartner(k));
                    labelChain(k, ownLabel + "_sub_" + stem);
                }
            }

            cur = mainChild;
        }
    }

    // A short (<=2 bone) branch never gets its own index/stem -- every
    // bone in it becomes an auxiliary subindex slot at the branch point's
    // own position, in a fixed pre-order so a 2-bone branch's own child
    // gets the next slot after its parent (DESIGN.md's ".32nd auxiliary
    // slot" wording: one running counter per position, not per branch).
    void foldSubtree(size_t node, const std::string& prefix, size_t index, size_t& subCounter) {
        (*labels_)[node] = prefix + "_" + std::to_string(index) + "." + std::to_string(subCounter);
        ++subCounter;
        for (size_t c : children_[node]) foldSubtree(c, prefix, index, subCounter);
    }
};

}  // namespace

std::vector<std::string> computeStructuralLabels(const Skeleton& skeleton) {
    LabelBuilder builder(skeleton);
    return builder.run();
}

}  // namespace husk::canon
