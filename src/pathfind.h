// pathfind.h - A* shortest path on a small waypoint graph (header only, reusable)
//
// Used by the gardener to walk door -> path -> pond -> flower rows without
// crossing the house or the pond. The playground and hospital reuse it later.

#ifndef PATHFIND_H
#define PATHFIND_H

#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace pf
{

struct Vec2 { float x = 0.0f, y = 0.0f; };

inline float dist(const Vec2& a, const Vec2& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

struct Graph
{
    std::vector<Vec2>             pos;
    std::vector<std::vector<int>> adj;

    int add(float x, float y)
    {
        pos.push_back(Vec2{x, y});
        adj.emplace_back();
        return (int)pos.size() - 1;
    }
    void link(int a, int b)
    {
        adj[a].push_back(b);
        adj[b].push_back(a);
    }
};

inline int nearestNode(const Graph& g, const Vec2& p)
{
    int best = 0;
    float bd = std::numeric_limits<float>::max();
    for (int i = 0; i < (int)g.pos.size(); i++)
    {
        const float d = dist(g.pos[i], p);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

// A*:  f(n) = g(n) + h(n),  g = distance walked, h = straight-line distance to the goal.
// Returns the node list start..goal (empty if unreachable). cost = total walking distance.
inline std::vector<int> aStar(const Graph& g, int start, int goal, float* cost = nullptr)
{
    const int   n   = (int)g.pos.size();
    const float INF = std::numeric_limits<float>::max();
    std::vector<float> gs(n, INF);
    std::vector<int>   from(n, -1);
    std::vector<char>  closed(n, 0);

    typedef std::pair<float, int> Item;                       // (f, node)
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    gs[start] = 0.0f;
    open.push(Item(dist(g.pos[start], g.pos[goal]), start));

    while (!open.empty())
    {
        const int cur = open.top().second;
        open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        if (cur == goal) break;
        for (int nb : g.adj[cur])
        {
            const float ng = gs[cur] + dist(g.pos[cur], g.pos[nb]);
            if (ng < gs[nb])
            {
                gs[nb]   = ng;
                from[nb] = cur;
                open.push(Item(ng + dist(g.pos[nb], g.pos[goal]), nb));
            }
        }
    }

    std::vector<int> path;
    if (gs[goal] == INF) { if (cost) *cost = INF; return path; }
    for (int c = goal; c != -1; c = from[c]) path.insert(path.begin(), c);
    if (cost) *cost = gs[goal];
    return path;
}

}   // namespace pf

#endif
