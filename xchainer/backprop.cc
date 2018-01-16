#include "xchainer/backprop.h"

#include <functional>
#include <memory>
#include <queue>
#include <unordered_map>

#include <gsl/gsl>
#include <nonstd/optional.hpp>

#include "xchainer/array.h"
#include "xchainer/array_node.h"
#include "xchainer/op_node.h"

namespace xchainer {
namespace {

class BackwardImpl {
    using Comparer = bool(*)(const std::shared_ptr<const OpNode>&, const std::shared_ptr<const OpNode>&);
    using CandidateOpNodes = std::priority_queue<std::shared_ptr<const OpNode>, std::vector<std::shared_ptr<const OpNode>>, Comparer>;
    using PreviousArrayNodeMap = std::unordered_map<std::shared_ptr<const OpNode>, std::shared_ptr<ArrayNode>>;

public:
    BackwardImpl() : candidate_op_nodes_(BackwardImpl::Compare){};

    void run(Array& output) {
        std::shared_ptr<ArrayNode> array_node = output.mutable_node();

        if (!array_node->grad()) {
            array_node->set_grad(Array::OnesLike(output));
        }

        PushNextOpNode(array_node);

        while (!candidate_op_nodes_.empty()) {
            std::shared_ptr<const OpNode> op_node = candidate_op_nodes_.top();
            candidate_op_nodes_.pop();

            std::vector<nonstd::optional<Array>> gxs = ComputeNextGradients(op_node);
            AccumulateNextGradients(op_node, gxs);

            for (const auto& next_array_node : op_node->next_nodes()) {
                PushNextOpNode(next_array_node);
            }
        }
    };

private:
    std::vector<nonstd::optional<Array>> ComputeNextGradients(const std::shared_ptr<const OpNode>& op_node) {
        const std::shared_ptr<ArrayNode>& previous_array_node = previous_array_node_map_.at(op_node);

        const nonstd::optional<Array>& gy = previous_array_node->grad();
        assert(gy);

        std::vector<nonstd::optional<Array>> gxs;
        for (const auto& backward_function : op_node->backward_functions()) {
            if (backward_function) {
                gxs.emplace_back(backward_function(*gy));
            } else {
                gxs.emplace_back(nonstd::nullopt);
            }
        }

        previous_array_node->ClearGrad();

        return gxs;
    }

    void AccumulateNextGradients(const std::shared_ptr<const OpNode>& op_node, const std::vector<nonstd::optional<Array>>& gxs) {
        gsl::span<const std::shared_ptr<ArrayNode>> next_array_nodes = op_node->next_nodes();
        assert(next_array_nodes.size() == gxs.size());
        auto next_size = next_array_nodes.size();
        for (decltype(next_size) i = 0; i < next_size; ++i) {
            const nonstd::optional<Array>& gx = gxs[i];
            const std::shared_ptr<ArrayNode>& next_array_node = next_array_nodes[i];
            if (gx) {
                const nonstd::optional<Array>& grad = next_array_node->grad();
                if (grad) {
                    next_array_node->set_grad(*grad + *gx);
                } else {
                    next_array_node->set_grad(std::move(*gx));
                }
            }
        }
    }

    void PushNextOpNode(const std::shared_ptr<ArrayNode>& array_node) {
        const std::shared_ptr<const OpNode>& next_op_node = array_node->next_node();
        if (next_op_node) {
            candidate_op_nodes_.push(next_op_node);
            previous_array_node_map_.emplace(next_op_node, array_node);
        }
    }

    static bool Compare(const std::shared_ptr<const OpNode>& lhs, const std::shared_ptr<const OpNode>& rhs) { return lhs->rank() < rhs->rank(); };

    CandidateOpNodes candidate_op_nodes_;
    PreviousArrayNodeMap previous_array_node_map_;
};

}  // namespace

void Backward(Array& output) {
    // TODO(takagi): Operations that have multiple outputs
    // TODO(takagi): Begin backprop from multiple outputs
    BackwardImpl impl;
    impl.run(output);
}

}  // namespace xchainer
