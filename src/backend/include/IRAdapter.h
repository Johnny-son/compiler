#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Module;
class Function;
class BasicBlock;
class Instruction;
class Value;
class Type;

enum class IRValueKind : std::int8_t {
	Invalid,
	ConstInt,
	GlobalVariable,
	Function,
	FormalParam,
	InstructionResult,
	Unknown
};

enum class IRInstKind : std::int8_t {
	Invalid,
	Alloca,
	Load,
	Store,
	Binary,
	ICmp,
	ZExt,
	GetElementPtr,
	Call,
	Phi,
	Branch,
	Return,
	Unknown
};

class IRValueView {

public:
	IRValueView() = default;
	explicit IRValueView(Value * value);

	[[nodiscard]] bool valid() const;
	[[nodiscard]] Value * raw() const;
	[[nodiscard]] Type * type() const;
	[[nodiscard]] IRValueKind kind() const;
	[[nodiscard]] bool isConstantInt() const;
	[[nodiscard]] bool isGlobalVariable() const;
	[[nodiscard]] bool isFunction() const;
	[[nodiscard]] bool isFormalParam() const;
	[[nodiscard]] bool isInstructionResult() const;
	[[nodiscard]] std::string name() const;
	[[nodiscard]] std::string irName() const;
	[[nodiscard]] int32_t intValue(int32_t fallback = 0) const;

private:
	Value * value = nullptr;
};

class IRInstView {

public:
	IRInstView() = default;
	explicit IRInstView(Instruction * inst);

	[[nodiscard]] bool valid() const;
	[[nodiscard]] Instruction * raw() const;
	[[nodiscard]] IRInstKind kind() const;
	[[nodiscard]] std::string irName() const;
	[[nodiscard]] Type * type() const;
	[[nodiscard]] bool hasResult() const;
	[[nodiscard]] IRValueView result() const;
	[[nodiscard]] std::size_t operandCount() const;
	[[nodiscard]] IRValueView operand(std::size_t index) const;
	[[nodiscard]] std::vector<IRValueView> operands() const;
	[[nodiscard]] Function * calledFunctionRaw() const;
	[[nodiscard]] std::string calledFunctionName() const;
	[[nodiscard]] bool isConditionalBranch() const;
	[[nodiscard]] BasicBlock * targetBlockRaw() const;
	[[nodiscard]] BasicBlock * trueBlockRaw() const;
	[[nodiscard]] BasicBlock * falseBlockRaw() const;

private:
	Instruction * inst = nullptr;
};

class IRBasicBlockView {

public:
	IRBasicBlockView() = default;
	explicit IRBasicBlockView(BasicBlock * block);

	[[nodiscard]] bool valid() const;
	[[nodiscard]] BasicBlock * raw() const;
	[[nodiscard]] std::string name() const;
	[[nodiscard]] std::string irName() const;
	[[nodiscard]] std::vector<IRInstView> instructions() const;

private:
	BasicBlock * block = nullptr;
};

class IRFunctionView {

public:
	IRFunctionView() = default;
	explicit IRFunctionView(Function * func);

	[[nodiscard]] bool valid() const;
	[[nodiscard]] Function * raw() const;
	[[nodiscard]] std::string name() const;
	[[nodiscard]] std::string irName() const;
	[[nodiscard]] Type * returnType() const;
	[[nodiscard]] bool isBuiltin() const;
	[[nodiscard]] std::vector<IRValueView> params() const;
	[[nodiscard]] std::vector<IRBasicBlockView> blocks() const;
	[[nodiscard]] std::vector<IRInstView> instructions() const;

private:
	Function * func = nullptr;
};

class IRModuleView {

public:
	IRModuleView() = default;
	explicit IRModuleView(Module * module);

	[[nodiscard]] bool valid() const;
	[[nodiscard]] Module * raw() const;
	[[nodiscard]] std::string name() const;
	[[nodiscard]] std::vector<IRValueView> globals() const;
	[[nodiscard]] std::vector<IRFunctionView> functions() const;

private:
	Module * module = nullptr;
};

class IRAdapter {

public:
	static IRModuleView adapt(Module * module);
};
