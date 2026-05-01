# IR 改造成迷你 LLVM 的设计方案 (已完成)

## 目标

后续 IR 不再继续沿用 DragonIR 的语义和文本格式，而是把 `src/ir` 改造成一个手写的“迷你 LLVM IR 库”。

目标不是链接 LLVM 官方 C++ 库，而是在现有代码上实现一套能生成标准 `.ll` 文本的内部 IR：

- 保留 `Value` / `User` / `Use` / `Type` / `Module` / `Function` 这类核心基础设施。
- 把指令集从 DragonIR 指令改成 LLVM 语义指令。
- 让 AST 到 IR 的生成过程直接创建 LLVM 风格的 `Instruction`。
- 最终每个 IR 类自己实现 `toString()`，`Module::outputIR()` 只负责按顺序写出合法 LLVM IR。

这条路适合比赛环境，因为代码完全自实现，不依赖 LLVM 官方库，也能保留 Def-Use 链，为后续优化留下空间。

## 设计定稿

后续代码按下面几个原则实现，不再摇摆：

- `src/ir` 的定位是自写 MiniLLVM 库，不是 DragonIR 到 LLVM IR 的字符串翻译器。
- `IRGenerator` 只调用 `Module`、`Function`、`BasicBlock`、`IRBuilder` 等 API，不直接 `new MoveInstruction` / `new BinaryInstruction` / 手动拼 `blockInsts`。
- `Function` 拥有 `BasicBlock` 列表，`BasicBlock` 拥有 `Instruction` 列表。
- `LabelInstruction` 升级为 `BasicBlock`，`GotoInstruction` / `CondBranchInstruction` 升级为 `BranchInst`。
- `MoveInstruction` 废弃，赋值和变量读写统一由 `store` / `load` 表示。
- 输出方案确定采用“每个类实现 `toString()`”。不再保留集中式 `LLVMTextEmitter` 翻译器。
- 第一版局部变量全部走 `alloca/load/store`，先保证 LLVM IR 正确；后续再考虑 mem2reg 或 SSA 优化。

## 当前落地状态（2026-04-29）

本轮已经把 `src/ir` 的主 IR 生成链路切到 MiniLLVM 风格：

- 已新增 `BasicBlock`、`IRBuilder`、`ArrayType`。
- 已新增 LLVM 风格指令类：`AllocaInst`、`LoadInst`、`StoreInst`、`BinaryInst`、`ICmpInst`、`ZExtInst`、`GetElementPtrInst`、`CallInst`、`PhiInst`、`BranchInst`、`ReturnInst`。
- `Function` 已拥有 `BasicBlock` 列表，并负责函数内 SSA 名和基本块名去重。
- `Module::toString()` 已能直接输出 `declare`、全局变量和函数定义；`Module::outputIR()` 已直接调用 `Module::toString()`，不再依赖 DragonIR 到 LLVM 的集中式翻译。
- `IRGenerator` 已改为用 `IRBuilder` 生成标量变量、函数参数、赋值、算术、比较、函数调用、全局变量、`if/while/break/continue/return` 和 `&&/||/!`。
- `AST_OP_CONST_DECL` 已接入 `IRGenerator`，常量声明会标记对应地址 `Value` 的 const 属性。
- `ir_assign()` 已在生成 `store` 前检查 const 左值，常量重新赋值会报 `E1303`。
- 数组声明、数组访问、数组初始化和数组形参已经接入：前端生成 `AST_OP_ARRAY_DIMS` / `AST_OP_ARRAY_ACCESS` / `AST_OP_INIT_LIST`，IR 层生成 `ArrayType` 和 `GetElementPtrInst`。
- 局部变量和短路表达式的临时结果都走 entry block `alloca`，读写用 `load/store`。
- `IRBuilder` 和 `IRGenerator::ir_block()` 已拦截 terminator 后继续插入普通指令，避免生成 `ret/br` 后的非法死代码。
- ASM 后端的 `IRAdapter`、`FrameLayout`、`InstructionSelector` 已改为消费 `Function::getBasicBlocks()` 和新的 LLVM 指令类，不再读取旧 `InterCode`。
- 旧 `InterCode`、旧 DragonIR opcode、旧 DragonIR 指令文件、旧 `LocalVariable/MemVariable/RegVariable` 文件已经删除。

当前仍是过渡状态：

- 数组主链路已经接通，但 `void` 函数和浮点数组还没有接入，因此 2023 后段部分数组/矩阵用例仍不会进入 IR 阶段。
- `PhiInst` 已预留基础类和输出能力，第一版逻辑表达式仍使用 alloca 合并值，不主动生成 phi。
- ASM 后端已经能从新 IR 生成非空汇编；当前环境缺少 RISC-V assembler/linker，因此只能验证汇编文本生成和本地构建，不能运行完整 ASM 用例。

已验证：

- `cmake --build build-local` 通过。
- `scripts/run_ci_tests.sh -ir --all --compiler ./build-local/compiler --show-failures` 通过，数组用例接入后结果 `OK number=43, NG number=0`。
- `scripts/run_ci_tests.sh -err --all --compiler ./build-local/compiler --show-failures` 通过，结果 `OK number=10, NG number=0`。
- `scripts/run_ci_tests.sh --all --compiler ./build-local/compiler --show-failures` 已能正常读取空 AST 段并继续执行；当前 IR/ERR 通过，ASM 段因本机缺少 RISC-V assembler/linker 失败。
- `./build-local/compiler -S -o /tmp/minic_main_new.s test/contesttestcases/2023_function/2023_func_00_main.c` 可生成包含 prologue/return 的非空 RISC-V 汇编。
- 额外检查短路用例 `2023_func_50_short_circuit.c`：生成的 `.ll` 可被 `clang` 编译并运行，程序语义输出为 `11111210`；该用例不在当前 IR 计划中，脚本 mismatch 来自程序无尾部换行时测试脚本追加退出码会和最后一个数字粘连。
- `scripts/run_ci_tests.sh` 已去掉 `mapfile` 依赖，并修复空计划段的数组展开，当前 macOS 自带 bash 可以运行 `--all` 计划。
- `scripts/run_ci_tests.sh -asm ...` 当前环境缺少 RISC-V assembler/linker，暂不能完成链接运行。

## 当前代码的保留价值

现有 `src/ir` 最有价值的是 Value 体系：

- `Value`：所有可被使用的值，包含名字、类型、use 链，也暂存源语言 const 属性。
- `User`：本身也是 `Value`，同时可以使用其他 `Value`，适合做 `Instruction` 基类。
- `Use`：维护 define-use 边，后续做死代码删除、替换操作数很重要。
- `Type`：目前已有 `IntegerType`、`VoidType`、`PointerType`、`FunctionType` 等基础类型。
- `Instruction`：可以继续作为所有 LLVM 指令的公共基类。

这些不需要推倒重写，只需要把语义从 DragonIR 调整到 LLVM IR。

## 总体架构

最终 IR 层变成下面这个结构：

```text
Module
  ├── GlobalVariable
  └── Function
        └── BasicBlock
              └── Instruction
                    ├── AllocaInst
                    ├── LoadInst
                    ├── StoreInst
                    ├── BinaryInst
                    ├── ICmpInst
                    ├── ZExtInst
                    ├── GetElementPtrInst
                    ├── CallInst
                    ├── PhiInst
                    ├── BranchInst
                    └── ReturnInst
```

核心变化：

- `Function` 不再只维护一条线性的 `InterCode`，而是维护 `BasicBlock` 列表。
- `LabelInstruction` 不再作为普通指令存在，标签升级为 `BasicBlock`。
- `GotoInstruction` 和 `CondBranchInstruction` 合并到 LLVM 风格的 `BranchInst`。
- `MoveInstruction` 废弃，赋值语义由 `store` 完成，取值语义由 `load` 完成。
- `ArgInstruction` 废弃，函数调用参数直接作为 `CallInst` 的 operands。

## 目录与命名定稿

MiniLLVM 类按 LLVM 风格命名，旧 DragonIR 指令已经从代码中移除。

新增基础结构：

```text
src/ir/include/BasicBlock.h
src/ir/BasicBlock.cpp
src/ir/include/IRBuilder.h
src/ir/IRBuilder.cpp
```

新增 LLVM 指令类：

```text
src/ir/Instructions/AllocaInst.h
src/ir/Instructions/AllocaInst.cpp
src/ir/Instructions/LoadInst.h
src/ir/Instructions/LoadInst.cpp
src/ir/Instructions/StoreInst.h
src/ir/Instructions/StoreInst.cpp
src/ir/Instructions/BinaryInst.h
src/ir/Instructions/BinaryInst.cpp
src/ir/Instructions/ICmpInst.h
src/ir/Instructions/ICmpInst.cpp
src/ir/Instructions/ZExtInst.h
src/ir/Instructions/ZExtInst.cpp
src/ir/Instructions/GetElementPtrInst.h
src/ir/Instructions/GetElementPtrInst.cpp
src/ir/Instructions/CallInst.h
src/ir/Instructions/CallInst.cpp
src/ir/Instructions/PhiInst.h
src/ir/Instructions/PhiInst.cpp
src/ir/Instructions/BranchInst.h
src/ir/Instructions/BranchInst.cpp
src/ir/Instructions/ReturnInst.h
src/ir/Instructions/ReturnInst.cpp
```

迁移规则：

- 新代码只使用 `*Inst` 命名的 LLVM 指令类。
- 旧的 `MoveInstruction`、`ArgInstruction`、`LabelInstruction`、`GotoInstruction`、`CondBranchInstruction`、`BinaryInstruction` 已删除。
- 所有新指令都继承现有 `Instruction` 基类，并通过 `User` / `Use` 维护 operands。
- `PhiInst` 第一版可以只预留类和接口，不参与 `IRGenerator`；后续做 mem2reg 或 SSA 优化时再启用。

## MiniLLVM API 层定位

这次改造的核心不是“让现有 DragonIR 最后翻译成 LLVM IR”，而是把 `src/ir` 本身做成一个类似 LLVM C++ API 的小型库。`IRGenerator` 只负责遍历 AST 和语义决策，不直接拼指令、不直接管理指令列表、不知道某条 LLVM 指令怎么打印。

理想调用关系：

```text
AST
  └── IRGenerator
        └── MiniLLVM API
              ├── Module
              ├── Function
              ├── BasicBlock
              ├── IRBuilder
              ├── Type
              └── Value / Instruction
                    └── output .ll
```

也就是说，`IRGenerator` 中应该逐步消失这些写法：

```cpp
node->blockInsts.addInst(new MoveInstruction(...));
node->blockInsts.addInst(new BinaryInstruction(...));
node->blockInsts.addInst(new CondBranchInstruction(...));
```

替换成类似 LLVM 官方 API 的写法：

```cpp
Value *lhs = emitRValue(node->sons[0]);
Value *rhs = emitRValue(node->sons[1]);
node->val = builder.createAdd(lhs, rhs);
```

或者：

```cpp
Value *addr = emitLValue(node->sons[0]);
Value *value = emitRValue(node->sons[1]);
builder.createStore(value, addr);
```

最终 `IRGenerator` 不直接关心 `Instruction` 插到哪里，`IRBuilder` 会根据当前插入点自动放进当前 `BasicBlock`。

## 期望的 IRGenerator 使用方式

### 函数定义

目标风格接近 LLVM：

```cpp
Function *func = module->getOrCreateFunction(name, functionType);
BasicBlock *entry = BasicBlock::Create(func, "entry");
builder.setInsertPoint(entry);
```

翻译函数体时，`IRGenerator` 只移动插入点：

```cpp
builder.setInsertPoint(currentBlock);
emitStmt(blockNode);
```

### 局部变量声明

局部变量定义不再创建 `LocalVariable + MoveInstruction`，而是：

```cpp
AllocaInst *addr = builder.createAlloca(type, varName);
module->addLocalValue(varName, addr);

if (hasInit) {
    Value *initValue = emitRValue(initExpr);
    builder.createStore(initValue, addr);
}
```

这和 LLVM 官方常见写法一致：变量符号表里保存的是地址，读变量时再 `load`。

### 变量读取

变量节点本身先查到地址：

```cpp
Value *addr = module->findVarValue(name);
```

如果表达式需要右值，则通过 Builder 读出来：

```cpp
Value *value = builder.createLoad(addr, "loadtmp");
```

所以需要拆两个辅助接口：

```cpp
Value *emitLValue(ast_node *node); // 返回地址
Value *emitRValue(ast_node *node); // 返回值
```

变量出现在赋值左边时调用 `emitLValue`，出现在表达式里时调用 `emitRValue`。

### 赋值

目标写法：

```cpp
Value *addr = emitLValue(lhsNode);
Value *value = emitRValue(rhsNode);
builder.createStore(value, addr);
node->val = value;
```

当前实现还会在 `createStore()` 之前检查：

```cpp
if (addr->isConstValue()) {
    report_ir_error("E1303", ...);
    return false;
}
```

原因是 LLVM IR 的 `store` 不知道源语言 const 语义，必须在 IRGenerator 阶段拦截。

这里不再存在 `MoveInstruction`。

### 算术表达式

目标写法：

```cpp
Value *lhs = emitRValue(node->sons[0]);
Value *rhs = emitRValue(node->sons[1]);
node->val = builder.createAdd(lhs, rhs, "addtmp");
```

减法、乘法、除法类似：

```cpp
builder.createSub(lhs, rhs, "subtmp");
builder.createMul(lhs, rhs, "multmp");
builder.createSDiv(lhs, rhs, "divtmp");
builder.createSRem(lhs, rhs, "modtmp");
```

### 比较表达式

LLVM 的比较结果是 `i1`：

```cpp
Value *cmp = builder.createICmpSLT(lhs, rhs, "cmptmp");
```

如果这个比较结果要作为 `int` 表达式返回给上层，可以再转成 `i32`：

```cpp
node->val = builder.createZExt(cmp, IntegerType::getTypeInt(), "booltoint");
```

如果这个比较结果直接用于 `if` / `while` 条件，就不需要 `zext`。

### return

目标写法：

```cpp
if (returnType->isVoidType()) {
    builder.createRetVoid();
} else {
    Value *retValue = emitRValue(node->sons[0]);
    builder.createRet(retValue);
}
```

第一版可以先不做“统一出口块”，直接在每个 `return` 处生成 `ret`。如果后端或优化需要统一出口，再由后续 pass 规范化。

### if / while

控制流也应该接近 LLVM 官方 API：

```cpp
BasicBlock *thenBB = function->createBlock("then");
BasicBlock *elseBB = function->createBlock("else");
BasicBlock *mergeBB = function->createBlock("if.end");

Value *cond = emitCondValue(node->sons[0]);
builder.createCondBr(cond, thenBB, elseBB);

builder.setInsertPoint(thenBB);
emitStmt(thenNode);
builder.createBr(mergeBB);

builder.setInsertPoint(elseBB);
emitStmt(elseNode);
builder.createBr(mergeBB);

builder.setInsertPoint(mergeBB);
```

`while` 同理：

```cpp
BasicBlock *condBB = function->createBlock("while.cond");
BasicBlock *bodyBB = function->createBlock("while.body");
BasicBlock *endBB = function->createBlock("while.end");

builder.createBr(condBB);

builder.setInsertPoint(condBB);
Value *cond = emitCondValue(condNode);
builder.createCondBr(cond, bodyBB, endBB);

builder.setInsertPoint(bodyBB);
emitStmt(bodyNode);
builder.createBr(condBB);

builder.setInsertPoint(endBB);
```

这样 `break/continue` 也不用保存 `Instruction *`，而是保存目标 `BasicBlock *`：

```cpp
std::vector<LoopContext> loopStack;

struct LoopContext {
    BasicBlock *continueBlock;
    BasicBlock *breakBlock;
};
```

## MiniLLVM 库的 public API 定稿

为了让 `IRGenerator` 调用方式稳定，后续先以这些 public API 为准，再逐步填内部实现。

### Module API

```cpp
class Module {
public:
    explicit Module(std::string name);

    Function *createFunction(const std::string &name, FunctionType *type, bool builtin = false);
    Function *getFunction(const std::string &name) const;

    GlobalVariable *createGlobalVariable(Type *type, const std::string &name);
    ConstInt *getConstInt(int32_t value);

    void enterScope();
    void leaveScope();
    bool bindValue(const std::string &name, Value *value);
    Value *lookupValue(const std::string &name) const;

    std::string toString() const;
    void outputIR(const std::string &filePath);
};
```

### Function API

```cpp
class Function : public GlobalValue {
public:
    BasicBlock *createBlock(const std::string &name = "");
    BasicBlock *getEntryBlock() const;
    const std::vector<BasicBlock *> &getBasicBlocks() const;

    std::string allocateLocalName(const std::string &hint = "");
    const std::vector<FormalParam *> &getParams() const;
    Type *getReturnType() const;
    void toString(std::string &str) const;
};
```

命名规则：

- `Function` 内维护统一的 `nameCounter`。
- 有返回值但没有显式名字的指令，通过 `allocateLocalName()` 分配 `%0`、`%1`、`%2`。
- 显式命名的局部地址推荐使用 `%a.addr`、`%tmp.addr` 这类不和数字名冲突的名字。
- 同一个函数内所有 SSA 名和 alloca 名都由 `Function` 统一分配或校验，避免 `IRBuilder` 和 `IRGenerator` 各自编号导致冲突。

### BasicBlock API

```cpp
class BasicBlock : public Value {
public:
    void appendInst(Instruction *inst);
    Instruction *getTerminator() const;
    bool hasTerminator() const;
    void toString(std::string &str) const;
};
```

### Instruction API

所有 LLVM 指令类继承 `Instruction`，并各自实现 `toString()`。

```cpp
class Instruction : public User {
public:
    virtual void toString(std::string &str) = 0;
    bool hasResultValue() const;
    bool isTerminator() const;
};
```

约定：

- `alloca/load/binary/icmp/zext/call(非void)` 有结果名，输出格式以 `%name = ...` 开头。
- `store/br/ret/call(void)` 没有结果名，直接输出指令文本。
- 指令的 `toString()` 只输出当前指令一行，不负责缩进和换行；缩进由 `BasicBlock::toString()` 统一加。

### IRBuilder API

```cpp
class IRBuilder {
public:
    explicit IRBuilder(Module *module);

    void setInsertPoint(BasicBlock *block);
    BasicBlock *getInsertBlock() const;

    AllocaInst *createAlloca(Type *type, const std::string &name = "");
    LoadInst *createLoad(Value *ptr, const std::string &name = "");
    StoreInst *createStore(Value *value, Value *ptr);

    BinaryInst *createAdd(Value *lhs, Value *rhs, const std::string &name = "");
    BinaryInst *createSub(Value *lhs, Value *rhs, const std::string &name = "");
    BinaryInst *createMul(Value *lhs, Value *rhs, const std::string &name = "");
    BinaryInst *createSDiv(Value *lhs, Value *rhs, const std::string &name = "");
    BinaryInst *createSRem(Value *lhs, Value *rhs, const std::string &name = "");

    ICmpInst *createICmpEQ(Value *lhs, Value *rhs, const std::string &name = "");
    ICmpInst *createICmpNE(Value *lhs, Value *rhs, const std::string &name = "");
    ICmpInst *createICmpSLT(Value *lhs, Value *rhs, const std::string &name = "");
    ICmpInst *createICmpSLE(Value *lhs, Value *rhs, const std::string &name = "");
    ICmpInst *createICmpSGT(Value *lhs, Value *rhs, const std::string &name = "");
    ICmpInst *createICmpSGE(Value *lhs, Value *rhs, const std::string &name = "");

    ZExtInst *createZExt(Value *value, Type *targetType, const std::string &name = "");
    GetElementPtrInst *createGEP(Value *basePtr, const std::vector<Value *> &indices, const std::string &name = "");
    CallInst *createCall(Function *callee, const std::vector<Value *> &args, const std::string &name = "");
    PhiInst *createPhi(Type *type, const std::string &name = "");
    BranchInst *createBr(BasicBlock *target);
    BranchInst *createCondBr(Value *cond, BasicBlock *trueBlock, BasicBlock *falseBlock);
    ReturnInst *createRet(Value *value);
    ReturnInst *createRetVoid();
};
```

这个 API 层就是我们自写的“类似 LLVM 的库”。`IRGenerator` 只依赖这些接口，后续如果内部指令类怎么组织、怎么打印、怎么维护 use 链，都不会污染 AST 翻译层。

## 类型系统改造

LLVM IR 对类型更严格，类型系统要能输出 LLVM 语法。

当前可保留：

- `IntegerType`：输出 `i1`、`i32`。
- `VoidType`：输出 `void`。
- `PointerType`：输出 `<element-type>*`。
- `FunctionType`：描述函数返回值和参数类型。

建议新增或补强：

- `FloatType`：如果语言支持 `float`，输出 `float`。
- `ArrayType`：数组必须支持，输出形如 `[10 x i32]`。
- `Type::toString()` 必须直接返回 LLVM 类型文本。

注意：布尔比较在 LLVM 中通常产生 `i1`，如果 MiniC 语义需要把条件结果当 `int` 使用，就用 `zext i1 to i32`。

## LLVM 文本方言定稿

第一版固定输出 LLVM 旧版强类型指针语法，方便兼容常见 SysY / 编译原理实验环境中的 LLVM 10/12：

```llvm
%p = alloca i32
%v = load i32, i32* %p
store i32 %v, i32* %p
```

因此：

- `PointerType` 必须保存 pointee type。
- `PointerType::toString()` 输出 `<pointee>*`，例如 `i32*`、`[10 x i32]*`。
- `LoadInst` / `StoreInst` / `GetElementPtrInst` 根据 pointee type 输出完整类型。
- 暂不采用 LLVM 15+ 的 opaque pointer 语法 `ptr`。如果后续验证工具链统一升级，再单独切换这一层。

## 指令集设计

### AllocaInst

表示局部变量或临时内存分配。

示例输出：

```llvm
%a = alloca i32, align 4
```

设计要点：

- 指令自身类型应是 `PointerType(allocatedType)`。
- 需要保存 `allocatedType`。
- 变量符号表里保存的局部变量应指向这个 alloca 结果。

### LoadInst

表示从地址中读值。

示例输出：

```llvm
%1 = load i32, i32* %a
```

设计要点：

- 操作数是指针。
- 指令自身类型是指针指向的元素类型。
- `IRGenerator` 做右值读取时创建 `LoadInst`。

### StoreInst

表示把值写入地址。

示例输出：

```llvm
store i32 %v, i32* %a
```

设计要点：

- `store` 没有返回值，类型是 `void`。
- operand 0 是 value，operand 1 是 pointer。
- 构造时可以检查 `ptr` 的元素类型和 `val` 的类型一致。

### BinaryInst

表示整数或浮点二元运算。

示例输出：

```llvm
%3 = add i32 %1, %2
%4 = fadd float %1, %2
```

设计要点：

- 可以保留一个统一的 `BinaryInst`，内部 opcode 用 LLVM 风格：`add/sub/mul/sdiv/srem/fadd/fsub/fmul/fdiv`。
- 指令自身类型通常等于左右操作数类型。
- 后续如果支持隐式类型转换，需要在 IRGenerator 中先插入转换指令。

### ICmpInst / FCmpInst

表示比较。

示例输出：

```llvm
%cmp = icmp slt i32 %a, %b
```

设计要点：

- `icmp` 结果类型是 `i1`。
- 整数比较用 `icmp eq/ne/slt/sle/sgt/sge`。
- 如果支持浮点，浮点比较用 `fcmp oeq/one/olt/ole/ogt/oge`。

### ZExtInst

表示零扩展，主要用于 `i1 -> i32`。

示例输出：

```llvm
%5 = zext i1 %cmp to i32
```

设计要点：

- 条件跳转可以直接使用 `i1`。
- 如果表达式结果要作为 `int` 保存或参与整数表达式，再插入 `zext`。

### GetElementPtrInst

表示 LLVM 的 `getelementptr`，用于数组元素、指针偏移、数组作为参数后的下标寻址。

示例输出：

```llvm
%arrayidx = getelementptr inbounds [10 x i32], [10 x i32]* %a, i32 0, i32 %i
```

设计要点：

- `GEP` 返回的是地址，不是元素值；如果要读元素，还需要接一个 `LoadInst`。
- 全局或局部数组 `a[i]` 的常见索引是 `0, i`：第一个 `0` 进入数组对象，第二个 `i` 进入数组元素。
- 指令需要保存 base pointer 和 indices。
- 指令自身类型是目标元素地址的 `PointerType`，例如上例结果类型为 `i32*`。
- `IRGenerator::emitLValue()` 翻译数组元素时应生成 `GEP` 并返回它。

### BranchInst

同时表示无条件跳转和条件跳转。

示例输出：

```llvm
br label %next
br i1 %cond, label %then, label %else
```

设计要点：

- 无条件分支保存一个目标 `BasicBlock`。
- 条件分支保存 condition、trueBlock、falseBlock。
- `BasicBlock` 末尾必须有 terminator，不能在 `br` / `ret` 后继续插普通指令。

### ReturnInst

表示函数返回。

示例输出：

```llvm
ret i32 %v
ret void
```

设计要点：

- `ret` 是 terminator。
- 返回值类型必须和 `FunctionType` 的返回类型一致。

### CallInst

表示函数调用。

示例输出：

```llvm
%r = call i32 @foo(i32 %a, i32 %b)
call void @putint(i32 %x)
```

设计要点：

- 如果返回类型是 `void`，指令没有结果名。
- 参数直接作为 operands，不再需要 `ArgInstruction`。
- 构造时检查实参数量和类型。

### PhiInst

表示 LLVM 的 `phi` 节点。第一版不要求 `IRGenerator` 主动生成，但必须在指令体系里预留，方便后续 mem2reg 或 SSA 优化。

示例输出：

```llvm
%x = phi i32 [ %a, %then ], [ %b, %else ]
```

设计要点：

- `PhiInst` 的 incoming 不是普通单个 operand，而是一组 `(Value *, BasicBlock *)`。
- 可以内部维护 `std::vector<std::pair<Value *, BasicBlock *>> incomingValues`。
- 插入位置必须在 BasicBlock 的普通指令最前面，即 terminator 之前、非 phi 指令之前。
- 后续做 mem2reg 时，用 `PhiInst` 消除 `alloca/load/store` 形成真正 SSA。

## BasicBlock 设计

新增 `BasicBlock` 类，建议放在：

```text
src/ir/include/BasicBlock.h
src/ir/BasicBlock.cpp
```

基本接口：

```cpp
class BasicBlock : public Value {
public:
    explicit BasicBlock(Function *parent, std::string name);

    Function *getParent() const;
    void addInst(Instruction *inst);
    std::vector<Instruction *> &getInstructions();
    bool hasTerminator() const;
    void toString(std::string &str);
};
```

命名规则：

- BasicBlock 名字不带 `%` 存储，输出时统一加 `%`。
- `entry` 是每个函数第一个基本块。
- 自动生成块名可以使用 `bb0`、`bb1`、`then0`、`else0`、`end0`。

## IRBuilder 设计

新增 `IRBuilder`，让 `IRGenerator` 不直接手动管理插入细节。

建议放在：

```text
src/ir/include/IRBuilder.h
src/ir/IRBuilder.cpp
```

核心职责：

- 保存当前插入点 `BasicBlock *currentBlock`。
- 创建指令并自动插入当前块。
- 创建基本块。
- 自动生成临时 SSA 名字。

示例接口：

```cpp
class IRBuilder {
public:
    explicit IRBuilder(Module *module);

    void setInsertPoint(BasicBlock *block);
    BasicBlock *getInsertBlock() const;

    AllocaInst *createAlloca(Type *type, const std::string &name);
    LoadInst *createLoad(Value *ptr, const std::string &name = "");
    StoreInst *createStore(Value *value, Value *ptr);

    BinaryInst *createAdd(Value *lhs, Value *rhs, const std::string &name = "");
    BinaryInst *createSub(Value *lhs, Value *rhs, const std::string &name = "");
    BinaryInst *createMul(Value *lhs, Value *rhs, const std::string &name = "");
    BinaryInst *createSDiv(Value *lhs, Value *rhs, const std::string &name = "");
    BinaryInst *createSRem(Value *lhs, Value *rhs, const std::string &name = "");

    ICmpInst *createICmpEQ(Value *lhs, Value *rhs, const std::string &name = "");
    ICmpInst *createICmpNE(Value *lhs, Value *rhs, const std::string &name = "");
    ICmpInst *createICmpSLT(Value *lhs, Value *rhs, const std::string &name = "");
    ICmpInst *createICmpSLE(Value *lhs, Value *rhs, const std::string &name = "");
    ICmpInst *createICmpSGT(Value *lhs, Value *rhs, const std::string &name = "");
    ICmpInst *createICmpSGE(Value *lhs, Value *rhs, const std::string &name = "");

    ZExtInst *createZExt(Value *value, Type *targetType, const std::string &name = "");
    GetElementPtrInst *createGEP(Value *basePtr, const std::vector<Value *> &indices, const std::string &name = "");

    BranchInst *createBr(BasicBlock *target);
    BranchInst *createCondBr(Value *cond, BasicBlock *trueBlock, BasicBlock *falseBlock);
    ReturnInst *createRet(Value *value);
    ReturnInst *createRetVoid();
    CallInst *createCall(Function *callee, const std::vector<Value *> &args, const std::string &name = "");
    PhiInst *createPhi(Type *type, const std::string &name = "");
};
```

重要约定：

- 只有有返回值的指令才向当前 `Function` 申请 `%0`、`%1` 这种 SSA 名。
- `store`、`br`、`ret` 这类 `void` 指令不分配结果名。
- 如果当前块已经有 terminator，继续插入普通指令应拒绝插入并返回 `nullptr`；`IRGenerator` 可以继续遍历 AST，但不会产生非法 LLVM IR。
- 可以保留一个 `deadInsertion` 标志或 dummy dead block 用于调试，但最终输出不能包含 terminator 后的指令。

## IRGenerator 改造思路

### 左值和右值分离

这是从 DragonIR 迁移到 LLVM IR 最关键的一点。

建议在 AST 生成时明确区分：

- 左值地址：变量、数组元素等可被赋值的位置，返回 pointer。
- 右值结果：表达式计算出来的值，返回普通 SSA value。

例如：

```text
a = b + 1
```

生成逻辑应是：

```text
addr(a)          -> %a.addr
value(b)         -> load i32, i32* %b.addr
const 1          -> i32 1
add              -> %tmp = add i32 %b, 1
store            -> store i32 %tmp, i32* %a.addr
```

不要再用 `MoveInstruction` 表示赋值。

### 局部变量定义

局部变量定义时：

1. 在函数 entry block 插入 `alloca`。
2. 把变量名绑定到 alloca 指令。
3. 如果有初始化表达式，计算右值后 `store` 到 alloca。

这样先保证正确性。后续如果要优化，可以再写 mem2reg。

### 表达式生成

表达式统一返回 `Value *`：

- 常量直接返回 `ConstInt`。
- 变量右值创建 `LoadInst`。
- 数组元素左值通过 `GetElementPtrInst` 计算地址，右值再接 `LoadInst`。
- 二元表达式递归生成左右值，再创建 `BinaryInst` 或 `ICmpInst`。
- 函数调用创建 `CallInst`。

### 条件和短路

条件表达式最终需要能给 `br i1` 使用。

建议提供一个辅助函数：

```cpp
Value *emitCondValue(ast_node *node);
```

规则：

- 如果表达式已经是 `i1`，直接返回。
- 如果表达式是 `i32`，生成 `icmp ne i32 %x, 0`。
- `&&` 和 `||` 直接生成 BasicBlock 实现短路，不要先算成普通二元表达式。

### 短路表达式的返回值

`&&` / `||` 既要短路，又可能作为普通表达式返回值使用，例如：

```c
int x = a && b;
```

第一版不使用 `PhiInst` 合并值，而是继续沿用 alloca 逃课策略：

1. 在当前函数的 entry block 创建一个临时 `i32` 栈槽，例如 `%logic.tmp = alloca i32`。
2. 根据短路控制流，在各个结果路径中 `store i32 0/1, i32* %logic.tmp`。
3. 所有路径跳到 merge block。
4. 在 merge block 中 `load i32, i32* %logic.tmp`，这个 load 的结果就是整个逻辑表达式的 `Value *`。

这样 `int x = (a && b);`、`return a || b;`、`foo(a && b);` 都能统一使用右值结果。

未来启用 `PhiInst` 后，可以把这个临时 alloca 替换成：

```llvm
%logic = phi i32 [ 0, %false_path ], [ 1, %true_path ]
```

### 数组元素寻址

数组访问必须通过 `GetElementPtrInst`，不能用普通整数 `add/mul` 拼地址。

局部数组：

```llvm
%a = alloca [10 x i32], align 4
%idx = getelementptr inbounds [10 x i32], [10 x i32]* %a, i32 0, i32 %i
store i32 1, i32* %idx
```

全局数组同理，base pointer 是 `@a`。

`emitLValue(a[i])` 返回 `%idx`，`emitRValue(a[i])` 则在 `%idx` 后面追加：

```llvm
%v = load i32, i32* %idx
```

## 输出设计

确定采用方案 A：每个类实现 `toString()`。

每个指令负责输出自己的 LLVM 文本：

```cpp
void StoreInst::toString(std::string &str)
{
    Value *val = getOperand(0);
    Value *ptr = getOperand(1);
    str = "store " + val->getType()->toString() + " " + val->getIRName()
        + ", " + ptr->getType()->toString() + " " + ptr->getIRName();
}
```

输出职责分层固定如下：

- `Type::toString()`：输出 LLVM 类型文本，例如 `i32`、`i1`、`void`、`i32*`。
- `Value::getIRName()`：输出 LLVM 值名，例如 `%0`、`%a`、`@g`、`42`。
- `Instruction::toString()`：输出单条 LLVM 指令，不带前导缩进，不带结尾换行。
- `BasicBlock::toString()`：输出块标签和块内指令，负责给普通指令加两个空格缩进。
- `Function::toString()`：输出 `define ... { ... }`，遍历自己的 `BasicBlock`。
- `Module::toString()`：输出内置函数声明、全局变量、函数定义。
- `Module::outputIR()`：只负责打开文件、调用 `Module::toString()`、写入文件。

也就是说，不要再写一个集中式的大型 `LLVMTextEmitter` 去 `dynamic_cast` 所有旧指令并翻译；LLVM 文本输出由各个 IR 类自己的 `toString()` 分层完成。

每个新 LLVM 指令类新增后，必须同时实现：

- 构造函数：建立 operands 和类型。
- `toString()`：输出 LLVM 文本。
- 必要的类型检查：例如 `StoreInst` 检查 value 类型和 pointer 元素类型一致。

## 文件改造计划

建议按阶段改，避免一次性推倒导致无法编译。

### 阶段 1：新增基础结构

- 新增 `BasicBlock`。
- 新增 `IRBuilder`。
- 新增第一批 LLVM 指令类：`AllocaInst`、`LoadInst`、`StoreInst`、`BinaryInst`、`ReturnInst`。
- 同时预留 `GetElementPtrInst`、`PhiInst` 的类声明和 API 位置，但可以稍后实现完整生成逻辑。
- 让 `Type`、`Instruction`、`BasicBlock`、`Function`、`Module` 都能通过 `toString()` 输出合法 LLVM 文本。

验收标准：

- 可以手写构造一个简单函数：

```llvm
define i32 @main() {
entry:
  ret i32 0
}
```

### 阶段 2：让 Function 支持 BasicBlock

- `Function` 增加 `std::vector<BasicBlock *> basicBlocks`。
- 已删除旧 `InterCode`，后端也改为遍历 BasicBlock。
- `Function::toString()` 只输出新的 BasicBlock 路径。

验收标准：

- 原有测试仍能编译。
- 新 IR 路径可以输出基本块结构。

### 阶段 3：改 IRGenerator 的变量和表达式

- 局部变量定义改为 `alloca/store`。
- 变量右值改为 `load`。
- 赋值改为 `store`。
- 算术表达式改为 `BinaryInst`。
- 比较表达式改为 `ICmpInst`，必要时 `ZExtInst`。
- 统一 SSA 命名由 `Function::allocateLocalName()` 负责。

验收标准：

- 简单赋值、加减乘除、return 能生成合法 `.ll`。

### 阶段 4：改控制流

- `if/else` 改为创建 `then`、`else`、`merge` BasicBlock。
- `while` 改为创建 `cond`、`body`、`end` BasicBlock。
- `break/continue` 用 block 栈管理目标。
- `&&` / `||` 改为短路控制流。
- 短路表达式作为右值时，用临时 alloca + store + merge load 返回结果。
- `IRBuilder` 拦截 terminator 后的普通指令，避免生成非法死代码。

验收标准：

- 条件、循环、短路测试能生成合法 `.ll`。

### 阶段 5：改数组、函数调用和全局变量

- 新增 `ArrayType` 和 `GetElementPtrInst` 的完整实现。
- 数组元素左值统一通过 `GEP` 生成地址。
- 删除 `ArgInstruction` 的使用。
- `CallInst` 直接保存 callee 和实参 operands。
- 全局变量按 LLVM 格式输出。
- 内置函数输出 `declare`。

验收标准：

- 函数调用、递归、全局变量测试通过。

### 阶段 6：预留 Phi 并删除 DragonIR 兼容层

- `PhiInst` 完成基础 `toString()` 和 incoming 管理，先不要求 IRGenerator 使用。
- 已删除 `MoveInstruction`、`ArgInstruction`、`LabelInstruction`、`GotoInstruction`、`CondBranchInstruction`、`BinaryInstruction`、`EntryInstruction`。
- 已删除旧的 DragonIR opcode 和 `InterCode`。
- 已删除 `LLVMTextEmitter` 中从 DragonIR 翻译到 LLVM IR 的逻辑，`Module::outputIR()` 直接调用 `Module::toString()`。

验收标准：

- `src/ir` 内部语义完全是 LLVM 风格。
- `.ll` 输出不依赖额外翻译器。

## 迁移时的注意点

1. 不要先删旧指令，先加新指令并跑通新路径。
2. 局部变量先全部走 `alloca/load/store`，这是最稳的 LLVM 生成方式。
3. 表达式返回右值，赋值左边返回地址，这条规则必须固定。
4. 比较结果是 `i1`，参与整数运算时再 `zext` 到 `i32`。
5. BasicBlock 必须以 terminator 结束，`br` / `ret` 后不能再插普通指令。
6. 数组和指针寻址必须用 `GetElementPtrInst`，不要在 IR 层手搓地址整数运算。
7. 逻辑短路表达式作为右值时，第一版用临时 alloca 合并结果；未来再用 `PhiInst` 优化。
8. 输出 `.ll` 后可以用 `clang` 或 `lli` 检查语法，但比赛环境最终不依赖 LLVM 库。

## 推荐优先落地的最小闭环

第一版先只支持：

- `Module`
- `Function`
- `BasicBlock`
- `ConstInt`
- `AllocaInst`
- `LoadInst`
- `StoreInst`
- `BinaryInst`
- `ReturnInst`

只要能生成下面这种 IR，就说明架构通了：

```llvm
define i32 @main() {
entry:
  %a = alloca i32, align 4
  store i32 1, i32* %a
  %0 = load i32, i32* %a
  %1 = add i32 %0, 2
  ret i32 %1
}
```

之后再补 `icmp`、`br`、`call`、数组和浮点。
