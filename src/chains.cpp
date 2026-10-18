#include "detectors.hpp"
#include "cell_mask.hpp"

/* Two uses of graph traversal that must not be confused:
 * Simple Coloring follows strong links for ONE digit, grouping cells by color.
 * XY-Chain follows implications between bivalue cells and changes digits along
 * the path. Both read the board and return eliminations; neither assigns a
 * trial value, modifies State during traversal, or backtracks a speculative solve.
 */
namespace sudoku {

// Answer-only Simple Coloring scan. Strong-link components and both standard
// contradiction rules are unchanged; only explanations are omitted.
// Work on one digit at a time. A unit with exactly two possible cells makes a
// strong link: precisely one endpoint contains the digit in any valid completion.
// Alternating colors represent the two possibilities across a connected component.
Actions find_coloring_actions(const State& state) {
    // adjacency[c] is a board-cell mask of strong-link neighbors of c.
    // unseen contains only vertices participating in a link; isolated candidate
    // cells can be elimination targets but do not start colored components.
    std::array<uint16_t, 81> eliminations{};
    for (int digit = 1; digit <= 9; ++digit) {
        std::array<detail::CellMask, 81> adjacency{};
        detail::CellMask unseen{};
        for (int unit = 0; unit < 27; ++unit) {
            const auto cells = state.candidate_positions(unit, digit);
            if (cells.size() != 2) continue;
            // "Exactly two" is essential. With three positions, knowing one
            // is false does not force any particular other one to be true.
            // Store an undirected edge. The bitset also deduplicates a link
            // discovered through more than one shared unit.
            adjacency[cells[0]] = detail::with(adjacency[cells[0]], cells[1]);
            adjacency[cells[1]] = detail::with(adjacency[cells[1]], cells[0]);
            unseen = detail::with(unseen, cells[0]);
            unseen = detail::with(unseen, cells[1]);
        }
        while (detail::any(unseen)) {
            // Color one component with a queue. The two color sets are local
            // to this component; disconnected components are not combined.
            int start = unseen.low ? __builtin_ctzll(unseen.low) :
                64 + __builtin_ctzll(unseen.high);
            std::array<int8_t, 81> color{};
            color.fill(-1);
            // -1 = unvisited, 0/1 = the two alternative truth groups. Starting
            // with color 0 is arbitrary; exchanging the names changes no result.
            color[start] = 0;
            Cells queue{start};
            detail::CellMask colors[2]{};
            int edge_count = 0;
            for (size_t offset = 0; offset < queue.size(); ++offset) {
                // The vector doubles as a FIFO queue. Coloring happens when
                // enqueuing, so the same vertex is not enqueued repeatedly.
                const int node = queue[offset];
                colors[color[node]] = detail::with(colors[color[node]], node);
                const auto node_bit = detail::bit(node);
                unseen = detail::without(unseen, node_bit);
                auto neighbors = adjacency[node];
                edge_count += __builtin_popcountll(neighbors.low) +
                    __builtin_popcountll(neighbors.high);
                detail::visit_cells(neighbors, [&](int next) {
                    if (color[next] < 0) {
                        color[next] = static_cast<int8_t>(1 - color[node]);
                        queue.push_back(next);
                    }
                });
            }
            // Each undirected edge was counted twice; require at least two links.
            if (edge_count < 4) continue;
            for (int bit = 0; bit < 2; ++bit) {
                // Color wrap: same-color cells are all true together or false
                // together. If two see each other, that whole color is false.
                // Example: A--B--C makes A and C the same color. If A and C
                // also share a unit, both cannot hold d, so their color loses d.
                bool conflict = false;
                detail::visit_cells(colors[bit], [&](int cell) {
                    if (detail::any(detail::intersect(
                            colors[bit], detail::peer_masks[cell]))) conflict = true;
                });
                if (conflict) detail::visit_cells(colors[bit], [&](int cell) {
                    eliminations[cell] |= static_cast<uint16_t>(1u << (digit - 1));
                });
            }
            for (int cell = 0; cell < 81; ++cell) {
                // Color trap: an outside cell seeing both colors conflicts
                // with the true color whichever of the two possibilities holds.
                // This outside cell need not have a strong link of its own.
                // Merely seeing one color is insufficient; that color could be
                // the false one. Both visibility intersections must be nonempty.
                if (!state.candidates[cell].count(digit) ||
                    detail::contains(colors[0], cell) ||
                    detail::contains(colors[1], cell)) continue;
                if (detail::any(detail::intersect(
                        colors[0], detail::peer_masks[cell])) &&
                    detail::any(detail::intersect(
                        colors[1], detail::peer_masks[cell])))
                    eliminations[cell] |= static_cast<uint16_t>(1u << (digit - 1));
            }
        }
    }
    return actions_from_masks(eliminations);
}

// XY-Chain: build bivalue links once per scan, then follow paths of 4..8 cells.
// Keep only the union of eliminations justified by those paths.
// If the starting cell is not its chosen endpoint digit, its other candidate
// forces the next cell, and so on. A chain ending in the endpoint digit proves
// that at least one end holds it; their common peers cannot contain that digit.
Actions find_xy_chain_actions(const State& state) {
    // Arriving over the shared incoming digit excludes it from next, forcing
    // next's other candidate outgoing. Cache these links only for this scan.
    struct Edge { int next; int incoming; int outgoing; };
    // Worked implication: current and next share digit 2, next has {2,5}.
    // If current is forced to 2, next cannot be 2 and is forced to 5.
    // Record {next, incoming=2, outgoing=5}; next/current must be peers.
    std::array<std::vector<Edge>, 81> links;
    Cells bivalue;
    std::array<bool, 81> eligible{};
    for (int cell = 0; cell < 81; ++cell)
        if (!state.values[cell] && state.candidates[cell].size() == 2) {
            eligible[cell] = true;
            bivalue.push_back(cell);
        }
    for (int cell : bivalue)
        for (int next : topology.peers[cell]) {
            if (!eligible[next]) continue;
            const unsigned shared = state.candidates[cell].mask() & state.candidates[next].mask();
            // Identical bivalue masks share TWO digits and do not define one
            // unambiguous implication edge here. Keep exactly one shared digit.
            if (__builtin_popcount(shared) != 1) continue;
            const unsigned remaining = state.candidates[next].mask() & ~shared;
            links[cell].push_back(
                {next, __builtin_ctz(shared) + 1, __builtin_ctz(remaining) + 1});
        }

    std::array<uint16_t, 81> eliminations{};
    for (int start : bivalue)
        for (int endpoint : state.candidates[start]) {
            // The two candidate choices produce different implications. Keep
            // only possible elimination targets that already see the start.
            auto visited = detail::bit(start);
            detail::CellMask possible_targets{};
            // endpoint is the digit to eliminate from common peers of the
            // finished chain's ends. possible_targets is a set of CELLS, not
            // a candidate digit mask and not a list of chosen path vertices.
            for (int target : topology.peers[start])
                if (state.candidates[target].count(endpoint))
                    possible_targets = detail::with(possible_targets, target);
            const auto visit = [&](const auto& self, int current, int outgoing,
                                   int depth, detail::CellMask seen) -> void {
                // seen belongs to this path by value. Sibling paths cannot
                // contaminate each other, and no path may revisit a cell.
                // depth counts visited CELLS, including start (initial depth 1).
                // outgoing is the digit currently forced at current under the
                // assumption that start does not take endpoint.
                for (const auto& edge : links[current]) {
                    if (detail::contains(seen, edge.next) ||
                        edge.incoming != outgoing) continue;
                    const auto next_seen = detail::with(seen, edge.next);
                    if (edge.outgoing == endpoint && depth + 1 >= 4) {
                        // A valid closure has 4..8 distinct cells. Targets must
                        // also see the last cell, and cannot lie inside the path.
                        // Proof: either start=endpoint, or the propagated choices
                        // force the last cell=endpoint. A common peer conflicts
                        // with at least one end in either case, so it loses d.
                        const auto targets = detail::without(
                            detail::intersect(possible_targets,
                                detail::peer_masks[edge.next]), next_seen);
                        detail::visit_cells(targets, [&](int target) {
                            eliminations[target] |=
                                static_cast<uint16_t>(1u << (endpoint - 1));
                        });
                    } else if (depth + 1 < 8) {
                        // Preserve the frozen chain-length bound. This recursion
                        // explores logical links only; it never edits the board.
                        self(self, edge.next, edge.outgoing, depth + 1, next_seen);
                    }
                }
            };
            const unsigned outgoing =
                state.candidates[start].mask() & ~(1u << (endpoint - 1));
            // Start the implication from the OTHER of this bivalue cell's two
            // digits. Example: start {1,8}, endpoint 8 -> initially force 1.
            // Links are rebuilt next time this detector sees a new board state.
            visit(visit, start, __builtin_ctz(outgoing) + 1, 1, visited);
        }
    return actions_from_masks(eliminations);
}
} // namespace sudoku
