#pragma once

#include <print>
#include <common.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct AABB
{
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    static AABB FromCenterExtents(const glm::vec3 &c, const glm::vec3 &e)
    {
        return {c - e, c + e};
    }

    bool IsValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

    glm::vec3 Center() const { return (min + max) * 0.5f; }
    glm::vec3 Extents() const { return (max - min) * 0.5f; } // half-size

    void Merge(const glm::vec3 &p)
    {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
    void Merge(const AABB &o)
    {
        min = glm::min(min, o.min);
        max = glm::max(max, o.max);
    }

    // Returns the axis-aligned box that encloses this box after transformation by m.
    AABB Transformed(const glm::mat4 &m) const
    {
        const glm::vec3 c = Center();
        const glm::vec3 e = Extents();

        const glm::vec3 newCenter = glm::vec3(m * glm::vec4(c, 1.0f));

        // abs() of the upper-left 3x3: how much each local axis contributes to each world axis
        const glm::mat3 absM(glm::abs(glm::vec3(m[0])), glm::abs(glm::vec3(m[1])), glm::abs(glm::vec3(m[2])));
        const glm::vec3 newExtents = absM * e;

        return FromCenterExtents(newCenter, newExtents);
    }
};

struct Transform
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 ToMatrix() const
    {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m *= glm::mat4_cast(rotation);
        m = glm::scale(m, scale);
        return m;
    }
};

struct Renderable
{
    std::uint32_t mesh_id{UINT32_MAX};
    std::uint32_t material_id{UINT32_MAX};
    AABB local_bounds;

    explicit operator bool() const { return mesh_id != UINT32_MAX; }
};

struct DrawItem
{
    std::uint32_t mesh_id;
    std::uint32_t material_id;
    glm::mat4 world;
    AABB world_bounds;
};

class Node
{
public:
    explicit Node(std::string name = "Node") : name(std::move(name)) {}

    // non-copyable, a node has a unique place in tree
    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;

    // takes ownership
    Node *AddChild(std::unique_ptr<Node> child)
    {
        child->parent_ = this;
        child->MarkDirty();
        children_.push_back(std::move(child));
        return children_.back().get();
    }

    // gives ownership back to caller, detached sub-tree
    std::unique_ptr<Node> RemoveChild(Node *child)
    {
        // clang-format off
        auto itr = std::find_if(children_.begin(), children_.end(), [child](const std::unique_ptr<Node> &c){ return c.get() == child; });
        // clang-format on

        if (itr == children_.end())
        {
            return nullptr;
        }

        std::unique_ptr<Node> detached = std::move(*itr);
        children_.erase(itr);
        detached->parent_ = nullptr;
        detached->MarkDirty();
        return detached;
    }

    Node *Parent() const { return parent_; }
    const std::vector<std::unique_ptr<Node>> &Children() const { return children_; }

    // local transform, modify only through setters
    const Transform &GetTransform() const { return transform_; }

    void SetPosition(const glm::vec3 &p)
    {
        transform_.position = p;
        MarkDirty();
    }

    void SetRotation(const glm::quat &r)
    {
        transform_.rotation = r;
        MarkDirty();
    }

    void SetScale(const glm::vec3 &s)
    {
        transform_.scale = s;
        MarkDirty();
    }

    // world space
    const glm::mat4 WorldMatrix() const
    {
        if (dirty_)
        {
            const glm::mat4 local = transform_.ToMatrix();
            world_ = parent_ ? parent_->WorldMatrix() * local : local;
            dirty_ = false;
        }
        return world_;
    }

    glm::vec3 WorldPosition() const { return glm::vec3(WorldMatrix()[3]); }

    Renderable GetRenderable() const { return renderable_; }

    void ClearRenderable()
    {
        renderable_ = {UINT32_MAX, UINT32_MAX};
        MarkDirty();
    }

    void SetRenderable(const Renderable &renderable)
    {
        renderable_ = renderable;
        MarkDirty();
    }

    std::string name;

private:
    void MarkDirty()
    {
        if (dirty_)
        {
            return;
        }
        dirty_ = true;
        for (auto &child : children_)
        {
            child->MarkDirty();
        }
    }

    Transform transform_; // local, relative to parent
    mutable glm::mat4 world_{1.0f};
    mutable bool dirty_{true};

    Node *parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;

    Renderable renderable_;
};

static inline void CollectDrawItems(const Node &node, std::vector<DrawItem> &out_items)
{
    if (const auto &renderable = node.GetRenderable())
    {
        const glm::mat4 &w = node.WorldMatrix();
        out_items.push_back({renderable.mesh_id, renderable.material_id, w, renderable.local_bounds.Transformed(w)});
    }

    for (const auto &child : node.Children())
    {
        CollectDrawItems(*child, out_items);
    }
}