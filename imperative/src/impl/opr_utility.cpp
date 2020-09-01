/**
 * \file imperative/src/impl/opr_utility.cpp
 * MegEngine is Licensed under the Apache License, Version 2.0 (the "License")
 *
 * Copyright (c) 2014-2020 Megvii Inc. All rights reserved.
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT ARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */

#include "megbrain/imperative/opr_utility.h"

// FIXME; setup_config_cn is copied from src/opr/impl/utility.cpp
namespace {
mgb::OperatorNodeConfig setup_config_cn(const mgb::OperatorNodeConfig& config_,
                                        const mgb::CompNode& cn) {
    auto prev_cn = config_.get_single_comp_node();
    mgb_assert(!prev_cn.valid() || cn == prev_cn);
    auto config = config_;
    config.comp_node(cn);
    return config;
}
}  // namespace
namespace mgb {
namespace opr {

/* ================ InputCallback ================== */

MGB_DYN_TYPE_OBJ_FINAL_IMPL(InputCallback);

InputCallback::InputCallback(cg::ComputingGraph& graph, callback_t callback,
                             const VarNodeArray& inputs,
                             const TensorShape& output_shape,
                             const OperatorNodeConfig& config)
        : Super(&graph, config, "input_callback", inputs),
          m_output_shape(output_shape), m_callback(callback) {
    for (VarNode* i : inputs) {
        add_input({i});
    }
    DType dt = config.output_dtype();
    mgb_assert(dt.valid());
    add_output(None)->add_flag(VarNode::Flag::NO_SYS_MEM_ALLOC).dtype(dt);
    add_output(None)
            ->add_flag(VarNode::Flag::ALLOW_EMPTY_SHAPE)
            .add_flag(VarNode::Flag::NO_SYS_MEM_ALLOC)
            .dtype(DType::from_enum(DTypeEnum::Byte));
}

SymbolVarArray InputCallback::make(cg::ComputingGraph& graph,
                                   callback_t callback, CompNode comp_node,
                                   DType dtype, const TensorShape& shape,
                                   const SymbolVarArray& inputs) {
    mgb_assert(comp_node.valid());
    mgb_assert(dtype.valid());
    OperatorNodeConfig config;
    config.comp_node(comp_node);
    config.output_dtype(dtype);
    auto vinputs = to_var_node_array(inputs);
    auto opr = graph.insert_opr(
            std::make_unique<InputCallback>(graph, callback, vinputs, shape, config));
    return to_symbol_var_array(opr->output());
}

void InputCallback::init_output_static_infer_desc() {
    if (m_output_shape.ndim) {
        using namespace cg::static_infer;
        auto &&mgr = owner_graph()->static_infer_manager();
        auto infer_shape = [this](TensorShape &dest, const InpVal &) {
            dest = m_output_shape;
            return true;
        };
        mgr.register_shape_infer(output(0),
                {SourceType::CONSTANT, {}, infer_shape});
    }
}

cg::OperatorNodeBase::NodeProp* InputCallback::do_make_node_prop() const {
    NodeProp* prop = Super::do_make_node_prop();
    prop->add_flag(NodeProp::Flag::NO_AUTOMATIC_DUP);
    SmallVector<NodeProp::DepType> dep_types(input().size(),
                                             NodeProp::DepType::DEV_COMP_ORDER);
    prop->reset_dep_type(input(), dep_types);
    return prop;
}

void InputCallback::scn_do_execute() {
    auto dev_tensor = m_callback();
    if (m_output_shape.ndim) {
        mgb_assert(dev_tensor.shape().eq_shape(m_output_shape));
    }
    output(0)->reset_dev_tensor_from_tensor(dev_tensor);
}

cg::OperatorNodeBase* InputCallback::shallow_copy(
        const serialization::OprShallowCopyContext &ctx,
        const cg::OperatorNodeBase &opr_, const VarNodeArray &inputs,
        const OperatorNodeConfig &config) {
    auto &&opr = opr_.cast_final_safe<InputCallback>();
    auto* graph = ctx.owner_graph(opr, inputs);
    return graph->insert_opr(std::make_unique<InputCallback>(*graph, opr.m_callback, inputs, opr.m_output_shape, config));
}

MGB_REG_OPR_SHALLOW_COPY(InputCallback, InputCallback::shallow_copy);

/* ================ OutputCallback ================== */

MGB_DYN_TYPE_OBJ_FINAL_IMPL(OutputCallback);

OutputCallback::OutputCallback(Param param, const VarNodeArray& inputs,
                               const OperatorNodeConfig& config)
        : Super(inputs[0]->owner_graph(),
                setup_config_cn(config, inputs[0]->comp_node()),
                "output_callback", inputs),
          m_param(std::move(param)) {
    for (VarNode* i : inputs) {
        add_input({i});
    }
    if (!m_param.borrow) {
        input(0)->add_flag(VarNode::Flag::NO_SYS_STATIC_MEM_ALLOC);
    }
    add_output(None)
            ->add_flag(VarNode::Flag::ALLOW_EMPTY_SHAPE)
            .add_flag(VarNode::Flag::NO_SYS_MEM_ALLOC)
            .dtype(DType::from_enum(DTypeEnum::Byte));
}

SymbolVar OutputCallback::make(Param param, const SymbolVarArray& inputs) {
    mgb_assert(inputs.size() >= 1);
    auto vinputs = to_var_node_array(inputs);
    OperatorNodeConfig config;
    return inputs[0].insert_single_output_opr<OutputCallback>(std::move(param),
                                                              vinputs, config);
}

void OutputCallback::init_output_static_infer_desc() {}

cg::OperatorNodeBase::NodeProp* OutputCallback::do_make_node_prop() const {
    NodeProp* prop = Super::do_make_node_prop();
    prop->add_flag(NodeProp::Flag::NO_AUTOMATIC_DUP);
    SmallVector<NodeProp::DepType> dep_types(input().size(),
                                             NodeProp::DepType::DEV_COMP_ORDER);
    dep_types[0] = NodeProp::DepType::DEV_VALUE;
    prop->reset_dep_type(input(), dep_types);
    return prop;
}

void OutputCallback::scn_do_execute() {
    m_param.callback(input(0)->dev_tensor());
}

cg::OperatorNodeBase* OutputCallback::shallow_copy(
        const serialization::OprShallowCopyContext &ctx,
        const cg::OperatorNodeBase &opr_, const VarNodeArray &inputs,
        const OperatorNodeConfig &config) {
    auto &&opr = opr_.cast_final_safe<OutputCallback>();
    auto* graph = ctx.owner_graph(opr, inputs);
    return graph->insert_opr(std::make_unique<OutputCallback>(opr.m_param, inputs, config));
}

MGB_REG_OPR_SHALLOW_COPY(OutputCallback, OutputCallback::shallow_copy);

/* ================ NopCallback ================== */

MGB_DYN_TYPE_OBJ_FINAL_IMPL(NopCallback);

NopCallback::NopCallback(cg::ComputingGraph& graph, callback_t callback,
                         const VarNodeArray& inputs,
                         const OperatorNodeConfig& config)
        : Super(&graph, config, "nop_callback", inputs), m_callback(callback) {
    for (VarNode* i : inputs) {
        add_input({i});
    }
    add_output(None)
            ->add_flag(VarNode::Flag::ALLOW_EMPTY_SHAPE)
            .add_flag(VarNode::Flag::NO_SYS_MEM_ALLOC)
            .dtype(DType::from_enum(DTypeEnum::Byte));
}

SymbolVar NopCallback::make(cg::ComputingGraph& graph, callback_t callback,
                            CompNode comp_node, const SymbolVarArray& inputs) {
    mgb_assert(comp_node.valid());
    OperatorNodeConfig config;
    config.comp_node(comp_node);
    auto vinputs = to_var_node_array(inputs);
    auto opr = graph.insert_opr(
            std::make_unique<NopCallback>(graph, callback, vinputs, config));
    return opr->output(0);
}

void NopCallback::init_output_static_infer_desc() {}
void NopCallback::on_output_comp_node_stream_changed() {}

void NopCallback::init_output_comp_node() {
    auto cn = config().get_single_comp_node();
    mgb_assert(cn.valid());
    output(0)->comp_node(cn);
}

cg::OperatorNodeBase::NodeProp* NopCallback::do_make_node_prop() const {
    NodeProp* prop = Super::do_make_node_prop();
    SmallVector<NodeProp::DepType> dep_types(input().size(),
                                             NodeProp::DepType::DEV_COMP_ORDER);
    prop->reset_dep_type(input(), dep_types);
    prop->add_flag(
            cg::OperatorNodeBase::NodeProp::Flag::CROSS_COMP_NODE_MEMORY);
    return prop;
}

void NopCallback::do_execute(ExecEnv& env) {
    auto cn = output(0)->comp_node();
    auto runner = [this, cn] {
        owner_graph()->event().signal_inplace<cg::event::BeforeKernel>(this,
                                                                       cn);
        cn.activate();
        m_callback();
        owner_graph()->event().signal_inplace<cg::event::AfterKernel>(this, cn);
    };
    env.dispatch_on_comp_node(cn, runner);
}

}  // namespace opr
}  // namespace mgb

// vim: syntax=cpp.doxygen foldmethod=marker foldmarker=f{{{,f}}}