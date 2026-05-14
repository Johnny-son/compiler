// Check basic structural invariants of the LLVM-style IR.

#include "VerifyPass.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "IRCFG.h"
#include "BranchInst.h"
#include "PhiInst.h"
#include "ReturnInst.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"
#include "Type.h"
#include "Use.h"
#include "User.h"
#include "Value.h"

namespace {

std::string functionName(Function * function)
{
	return function != nullptr ? function->getIRName() : "<null-function>";
}

std::string blockName(BasicBlock * block)
{
	return block != nullptr ? block->getIRName() : "<null-block>";
}

std::string valueName(Value * value)
{
	if (value == nullptr) {
		return "<null-value>";
	}
	std::string name = value->getIRName();
	return name.empty() ? "<unnamed-value>" : name;
}

bool sameType(Type * lhs, Type * rhs)
{
	if (lhs == rhs) {
		return true;
	}
	if (lhs == nullptr || rhs == nullptr) {
		return false;
	}
	return lhs->toString() == rhs->toString();
}

bool containsUse(const std::vector<Use *> & uses, Use * expected)
{
	return std::find(uses.begin(), uses.end(), expected) != uses.end();
}

bool containsBlock(const std::unordered_set<BasicBlock *> & blocks, BasicBlock * block)
{
	return block != nullptr && blocks.find(block) != blocks.end();
}

bool containsInstruction(const std::unordered_set<Instruction *> & instructions, Instruction * inst)
{
	return inst != nullptr && instructions.find(inst) != instructions.end();
}

void addError(std::vector<std::string> & errors, Function * function, BasicBlock * block, const std::string & message)
{
	errors.push_back(
		"[VerifyPass] function " + functionName(function) + ", block " + blockName(block) + ": " + message);
}

void verifyUseDefLinks(
	Function * function,
	BasicBlock * block,
	Instruction * inst,
	const std::unordered_set<Instruction *> & functionInstructions,
	std::vector<std::string> & errors)
{
	auto & operands = inst->getOperands();
	for (auto * use: operands) {
		if (use == nullptr) {
			addError(errors, function, block, "instruction has a null operand use");
			continue;
		}
		if (use->getUser() != inst) {
			addError(errors, function, block, "operand use has a mismatched user");
		}
		auto * usee = use->getUsee();
		if (usee == nullptr) {
			addError(errors, function, block, "operand use points to a null value");
			continue;
		}
		if (!containsUse(usee->getUseList(), use)) {
			addError(
				errors,
				function,
				block,
				"operand of " + valueName(inst) + " is missing from " + valueName(usee) + "'s use-list");
		}

		if (auto * definingInst = dynamic_cast<Instruction *>(usee);
			definingInst != nullptr && !containsInstruction(functionInstructions, definingInst)) {
			addError(
				errors,
				function,
				block,
				"operand " + valueName(usee) + " is an instruction outside the current function");
		}
	}
}

void verifyReverseUseLinks(
	Function * function,
	BasicBlock * block,
	Instruction * def,
	const std::unordered_set<Instruction *> & functionInstructions,
	std::vector<std::string> & errors)
{
	for (auto * use: def->getUseList()) {
		if (use == nullptr) {
			addError(errors, function, block, "definition " + valueName(def) + " has a null use-list entry");
			continue;
		}
		auto * user = dynamic_cast<Instruction *>(use->getUser());
		if (user == nullptr) {
			addError(errors, function, block, "definition " + valueName(def) + " is used by a non-instruction user");
			continue;
		}
		if (!containsInstruction(functionInstructions, user)) {
			addError(errors, function, block, "definition " + valueName(def) + " is used outside its function");
			continue;
		}
		if (!containsUse(user->getOperands(), use)) {
			addError(
				errors,
				function,
				block,
				"definition " + valueName(def) + " has a use that is missing from its user's operands");
		}
	}
}

void verifyBranchTargets(
	Function * function,
	BasicBlock * block,
	BranchInst * branch,
	const std::unordered_set<BasicBlock *> & functionBlocks,
	std::vector<std::string> & errors)
{
	if (branch->isConditional()) {
		if (branch->getOperandsNum() != 1 || branch->getOperand(0) == nullptr) {
			addError(errors, function, block, "conditional branch must have one condition operand");
		}
		if (!containsBlock(functionBlocks, branch->getTrueTarget())) {
			addError(errors, function, block, "conditional branch true target is not in the current function");
		}
		if (!containsBlock(functionBlocks, branch->getFalseTarget())) {
			addError(errors, function, block, "conditional branch false target is not in the current function");
		}
		return;
	}

	if (branch->getOperandsNum() != 0) {
		addError(errors, function, block, "unconditional branch must not have operands");
	}
	if (!containsBlock(functionBlocks, branch->getTarget())) {
		addError(errors, function, block, "unconditional branch target is not in the current function");
	}
}

void verifyReturn(
	Function * function,
	BasicBlock * block,
	ReturnInst * ret,
	std::vector<std::string> & errors)
{
	Type * returnType = function->getReturnType();
	if (returnType != nullptr && returnType->isVoidType()) {
		if (ret->getOperandsNum() != 0) {
			addError(errors, function, block, "void function returns a value");
		}
		return;
	}

	if (ret->getOperandsNum() != 1) {
		addError(errors, function, block, "non-void function return must have exactly one value");
		return;
	}
	if (!sameType(returnType, ret->getOperand(0)->getType())) {
		addError(errors, function, block, "return value type does not match function return type");
	}
}

void verifyPhi(
	Function * function,
	BasicBlock * block,
	PhiInst * phi,
	const IRCFG & cfg,
	const std::unordered_set<BasicBlock *> & functionBlocks,
	std::vector<std::string> & errors)
{
	const auto predIter = cfg.predecessors.find(block);
	const std::vector<BasicBlock *> predecessors =
		predIter == cfg.predecessors.end() ? std::vector<BasicBlock *>{} : predIter->second;
	const auto & incomingValues = phi->getIncomingValues();

	if (incomingValues.size() != static_cast<std::size_t>(phi->getOperandsNum())) {
		addError(errors, function, block, "phi incoming list and operand list have different sizes");
	}
	if (incomingValues.size() != predecessors.size()) {
		addError(errors, function, block, "phi incoming count does not match CFG predecessor count");
	}

	std::unordered_set<BasicBlock *> seenIncomingBlocks;
	for (std::size_t index = 0; index < incomingValues.size(); ++index) {
		auto * value = incomingValues[index].first;
		auto * incomingBlock = incomingValues[index].second;
		if (value == nullptr) {
			addError(errors, function, block, "phi has a null incoming value");
			continue;
		}
		if (!sameType(phi->getType(), value->getType())) {
			addError(errors, function, block, "phi incoming value type does not match phi type");
		}
		if (!containsBlock(functionBlocks, incomingBlock)) {
			addError(errors, function, block, "phi incoming block is not in the current function");
			continue;
		}
		if (!seenIncomingBlocks.insert(incomingBlock).second) {
			addError(errors, function, block, "phi has duplicate incoming blocks");
		}
		if (std::find(predecessors.begin(), predecessors.end(), incomingBlock) == predecessors.end()) {
			addError(errors, function, block, "phi incoming block is not a CFG predecessor");
		}
		if (index < static_cast<std::size_t>(phi->getOperandsNum()) && phi->getOperand(static_cast<int32_t>(index)) != value) {
			addError(errors, function, block, "phi incoming list is out of sync with operand list");
		}
	}

	for (auto * pred: predecessors) {
		if (seenIncomingBlocks.find(pred) == seenIncomingBlocks.end()) {
			addError(errors, function, block, "phi is missing an incoming value for predecessor " + blockName(pred));
		}
	}
}

void verifyFunction(Function * function, std::vector<std::string> & errors)
{
	if (function == nullptr || function->isBuiltin()) {
		return;
	}

	const auto & blocks = function->getBasicBlocks();
	if (blocks.empty()) {
		errors.push_back("[VerifyPass] function " + functionName(function) + " has no basic blocks");
		return;
	}

	std::unordered_set<BasicBlock *> functionBlocks;
	std::unordered_set<Instruction *> functionInstructions;
	for (auto * block: blocks) {
		if (block != nullptr) {
			functionBlocks.insert(block);
			for (auto * inst: block->getInstructions()) {
				if (inst != nullptr) {
					functionInstructions.insert(inst);
				}
			}
		}
	}

	IRCFG cfg = IRCFGBuilder::build(function);
	for (auto * block: blocks) {
		if (block == nullptr) {
			errors.push_back("[VerifyPass] function " + functionName(function) + " contains a null basic block");
			continue;
		}
		if (block->getParent() != function) {
			addError(errors, function, block, "basic block parent does not match function");
		}

		const auto & instructions = block->getInstructions();
		bool seenNonPhi = false;
		bool seenTerminator = false;
		for (std::size_t index = 0; index < instructions.size(); ++index) {
			auto * inst = instructions[index];
			if (inst == nullptr) {
				addError(errors, function, block, "contains a null instruction");
				continue;
			}
			if (inst->getFunction() != function) {
				addError(errors, function, block, "instruction parent function does not match containing function");
			}

			auto * phi = dynamic_cast<PhiInst *>(inst);
			if (phi != nullptr) {
				if (seenNonPhi) {
					addError(errors, function, block, "phi instruction appears after a non-phi instruction");
				}
				verifyPhi(function, block, phi, cfg, functionBlocks, errors);
			} else {
				seenNonPhi = true;
			}

			if (inst->isTerminator()) {
				if (seenTerminator) {
					addError(errors, function, block, "contains more than one terminator");
				}
				seenTerminator = true;
				if (index + 1 != instructions.size()) {
					addError(errors, function, block, "terminator is not the last instruction");
				}
			}

			if (auto * branch = dynamic_cast<BranchInst *>(inst); branch != nullptr) {
				verifyBranchTargets(function, block, branch, functionBlocks, errors);
			}
			if (auto * ret = dynamic_cast<ReturnInst *>(inst); ret != nullptr) {
				verifyReturn(function, block, ret, errors);
			}

			verifyUseDefLinks(function, block, inst, functionInstructions, errors);
			verifyReverseUseLinks(function, block, inst, functionInstructions, errors);
		}

		if (!seenTerminator) {
			addError(errors, function, block, "basic block has no terminator");
		}
	}
}

} // namespace

bool VerifyPass::run(Module & module)
{
	std::vector<std::string> errors;
	for (auto * function: module.getFunctionList()) {
		verifyFunction(function, errors);
	}

	for (const auto & error: errors) {
		std::cerr << error << '\n';
	}
	return errors.empty();
}
