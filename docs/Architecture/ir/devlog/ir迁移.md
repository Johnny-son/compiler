# IR 迁移说明 (已完成)

## 背景

本次迁移的目标是把项目中的 IR 层从旧的 DragonIR/线性 IR 形态，迁到自实现的 MiniLLVM 形态。

迁移前的核心问题是：

- IR 语义不是 LLVM IR，而是自定义的 DragonIR。
- 函数内部使用 `InterCode` 保存一条线性指令序列。
- 控制流依赖 `LabelInstruction`、`GotoInstruction`、`CondBranchInstruction`。
- 赋值依赖 `MoveInstruction`，函数调用依赖 `ArgInstruction` 和旧 `FuncCallInstruction`。
- `.ll` 输出曾经需要集中式 emitter 把旧 IR 再翻译成 LLVM 文本。
- 后端也依赖旧 `InterCode`，导致前端即使生成了新 LLVM 风格 IR，ASM 路径仍然会断开。

迁移后的方向是：

- `src/ir` 作为一个自写的 MiniLLVM 库。
- `IRGenerator` 直接生成 LLVM 语义 IR。
- `Function` 管理 `BasicBlock`。
- `BasicBlock` 管理 LLVM 风格 `Instruction`。
- 每个 IR 类自己实现 `toString()`，直接输出合法 LLVM IR。
- 后端从 `BasicBlock` 和新 LLVM 指令中选择汇编。

## 总体架构变化

迁移后的主要结构是：

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

旧结构中最关键的 `InterCode` 已经删除。函数内部不再是一条线性 IR 指令列表，而是由多个基本块组成。控制流也不再用 label 指令伪造，而是由 `BasicBlock + BranchInst` 表达。

## 新增或保留的核心能力

### BasicBlock

新增 `BasicBlock`，作为 LLVM 风格基本块。

它负责：

- 保存所属 `Function`。
- 保存块名。
- 持有块内指令列表。
- 判断当前块是否已有 terminator。
- 输出块标签和块内指令文本。

这替代了旧的 `LabelInstruction`。标签不再是普通指令，而是基本块本身的名字。

### IRBuilder

新增 `IRBuilder`，让 `IRGenerator` 像调用 LLVM API 一样生成 IR。

它负责：

- 保存当前插入点 `BasicBlock *`。
- 创建指令并自动插入当前基本块。
- 为有结果的指令分配 SSA 名。
- 拦截 terminator 后继续插入普通指令的情况。

因此 `IRGenerator` 不再手动拼接 `blockInsts`，也不再直接管理线性指令顺序。

### LLVM 风格指令类

新增或启用的 LLVM 风格指令包括：

- `AllocaInst`：局部变量和临时内存分配。
- `LoadInst`：从地址读取值。
- `StoreInst`：把值写入地址。
- `BinaryInst`：整数二元运算。
- `ICmpInst`：整数比较，结果类型为 `i1`。
- `ZExtInst`：主要用于 `i1 -> i32`。
- `GetElementPtrInst`：数组和指针寻址预留。
- `CallInst`：函数调用。
- `PhiInst`：SSA phi 节点预留。
- `BranchInst`：条件或无条件跳转。
- `ReturnInst`：函数返回。

这些指令都继承 `Instruction`，并通过 `User/Use` 维护操作数关系。

## 删除的旧 IR 内容

本次删除了旧 DragonIR 相关内容：

```text
src/ir/IRCode.cpp
src/ir/include/IRCode.h
src/ir/include/IRConstant.h
src/ir/Instructions/BinaryInstruction.cpp
src/ir/Instructions/BinaryInstruction.h
src/ir/Instructions/EntryInstruction.cpp
src/ir/Instructions/EntryInstruction.h
src/ir/Instructions/LabelInstruction.cpp
src/ir/Instructions/LabelInstruction.h
src/ir/Values/LocalVariable.h
src/ir/Values/MemVariable.h
src/ir/Values/RegVariable.h
```

同时旧的 `ArgInstruction`、`MoveInstruction`、`GotoInstruction`、`CondBranchInstruction`、`FuncCallInstruction`、`ExitInstruction` 等旧路径也已经不再存在或不再被引用。

旧 opcode `IRInstOperator` 已从 `Instruction` 基类移除。现在判断指令类型不再依赖 `getOp()`，而是直接使用具体 LLVM 指令类。

## Function 的变化

迁移前：

- `Function` 持有 `InterCode code`。
- `Function::toString()` 会遍历旧线性 IR。
- `Function` 还维护旧的局部变量、内存变量、返回值变量、出口 label 等状态。

迁移后：

- `Function` 只持有 `std::vector<BasicBlock *> basicBlocks`。
- `Function::toString()` 只输出 `define ... { ... }` 和基本块。
- `Function::allocateLocalName()` 统一管理函数内 SSA 名。
- `Function::createBlock()` 统一管理基本块名去重。
- 旧的 `getInterCode()`、`LocalVariable`、`MemVariable`、`returnValue`、`exitLabel` 等路径已删除。

这样做的原因是：LLVM IR 的控制流单位是基本块，不是标签指令加线性跳转。把函数结构改成基本块列表后，IR 输出、后端遍历、后续优化都会更自然。

## Instruction 的变化

迁移前：

- `Instruction` 内部保存 `IRInstOperator op`。
- 后端和 emitter 通过 opcode 判断指令语义。

迁移后：

- `Instruction` 不再保存旧 opcode。
- 每个具体指令类表达自己的语义。
- `isTerminator()` 由 `BranchInst` 和 `ReturnInst` 覆盖。
- `toString()` 由每个指令类自己实现。

这样做的原因是：旧 opcode 是 DragonIR 的核心残留。如果继续保留，代码会变成“LLVM 类名 + DragonIR opcode”的混合形态，后续很容易重新写出旧式逻辑。

## IRGenerator 的变化

`IRGenerator` 已从旧线性 IR 生成器改成 MiniLLVM IR 生成器。

主要变化：

- 函数体开始时创建 `entry` 基本块。
- 局部变量声明使用 `alloca`。
- 常量声明复用变量声明路径，但会把变量地址 `Value` 标记为 const。
- 数组声明使用 `ArrayType`，局部数组使用 `alloca [N x ...]`，全局数组输出聚合初始化或 `zeroinitializer`。
- 数组元素访问使用 `GetElementPtrInst`，作为右值时再通过 `load` 读取。
- 数组作为函数形参时降成指针类型，例如 `int a[] -> i32*`、`int a[][N] -> [N x i32]*`。
- 变量右值读取使用 `load`。
- 赋值使用 `store`。
- 对 const 变量重新赋值会在生成 `store` 前报 `E1303`。
- 算术表达式使用 `BinaryInst`。
- 比较表达式使用 `ICmpInst`，需要作为 `int` 使用时接 `ZExtInst`。
- 函数调用使用 `CallInst`，实参直接作为 operands。
- `if/else`、`while`、`break/continue` 使用 `BasicBlock + BranchInst`。
- `return` 使用 `ReturnInst`。
- `&&` 和 `||` 使用短路控制流。
- 短路表达式作为右值时，先使用临时 `alloca + store + load` 合并结果。

旧写法：

```cpp
node->blockInsts.addInst(new MoveInstruction(...));
node->blockInsts.addInst(new BinaryInstruction(...));
node->blockInsts.addInst(new CondBranchInstruction(...));
```

新写法：

```cpp
Value *lhs = emitRValue(left->val, "lhs");
Value *rhs = emitRValue(right->val, "rhs");
node->val = builder.createAdd(lhs, rhs, "addtmp");
```

或者：

```cpp
Value *value = emitRValue(rhsNode->val, "storetmp");
builder.createStore(value, lhsAddress);
```

这样改的原因是：AST 到 IR 层应该表达语义，而不是关心字符串格式或线性 IR 拼接。指令插入、SSA 命名、terminator 检查都交给 `IRBuilder` 处理。

## Module 的变化

`Module::outputIR()` 现在直接写出 `Module::toString()`。

`Module::toString()` 负责输出：

- 内置函数声明。
- 全局变量定义。
- 普通函数定义。

之前集中式 `LLVMTextEmitter` 的翻译职责已经删除。输出 LLVM IR 不再需要把旧 DragonIR 翻译一遍。

## AST 的变化

`ast_node` 删除了旧的：

```cpp
InterCode blockInsts;
```

现在 AST 节点只保留：

```cpp
Value *val;
```

它表示该 AST 节点在 MiniLLVM IR 生成过程中绑定到的值。

这样做的原因是：AST 不应该携带一段线性 IR 指令。每个语句或表达式生成的指令已经被插入当前 `BasicBlock`，AST 只需要把表达式结果传给父节点。

同时 `ast_node::isConst` 会被保留到 IR 生成阶段：

- `AST_OP_CONST_DECL` 由 `IRGenerator::ir_const_declaration()` 处理。
- 常量声明节点最终仍调用 `ir_variable_declare()`。
- `ir_variable_declare()` 创建 `AllocaInst` 或 `GlobalVariable` 后调用 `setConstValue(true)`。
- `ir_assign()` 在 `builder.createStore()` 之前检查左值地址的 `isConstValue()`，如果为 true 则报 `E1303`。

这样做的原因是：LLVM 风格 IR 中的 `store` 本身不区分普通变量和源语言 `const` 变量，`const` 重新赋值必须在语义生成阶段拦截。

## 后端变化

### IRAdapter

迁移前：

- 后端通过 `Function::getInterCode().getInsts()` 获取指令。
- 指令类型靠旧 opcode 或旧指令类判断。

迁移后：

- 后端遍历 `Function::getBasicBlocks()`。
- `IRFunctionView::instructions()` 会按基本块顺序展开新指令。
- 新增 `IRBasicBlockView`。
- `IRInstKind` 改成 LLVM 风格：

```text
Alloca
Load
Store
Binary
ICmp
ZExt
GetElementPtr
Call
Phi
Branch
Return
```

### FrameLayout

栈帧布局现在不再为旧 `LocalVariable` 分配槽位。

现在会为这些值分配栈槽：

- 函数形参。
- `AllocaInst` 对应的对象。
- 有结果的指令值。
- 调用超过寄存器数量时的 outgoing arg area。

`AllocaInst` 的栈槽大小按它的 `allocatedType` 计算，而不是按指针结果类型计算。

### InstructionSelector

后端选择器现在按新 LLVM 指令生成 RISC-V 汇编：

- `AllocaInst`：栈帧布局阶段已分配空间，选择阶段不发指令。
- `LoadInst`：从指针所指地址读取。
- `StoreInst`：写入指针所指地址。
- `BinaryInst`：生成 `addw/subw/mulw/divw/remw`。
- `ICmpInst`：生成比较结果 0/1。
- `ZExtInst`：生成 `andi`，保留低 1 位。
- `GetElementPtrInst`：生成地址计算。
- `CallInst`：按调用约定放参数、调用函数、保存返回值。
- `BranchInst`：生成 `bnez` 和 `j`。
- `ReturnInst`：恢复栈帧并 `ret`。

现在 ASM 输出已经不是空函数，而是能从新 IR 直接生成汇编文本。

## 为什么要这样改

### 1. 彻底摆脱 DragonIR 语义

如果只在最后输出时翻译 LLVM 文本，内部仍然是 DragonIR，那么浮点、数组、指针、SSA、优化都会很难继续做。

这次把 IR 内部语义也改成 LLVM 风格，后续加功能时可以直接扩展 LLVM 指令类。

### 2. 让变量语义更接近 LLVM

局部变量统一用：

```llvm
%a.addr = alloca i32
store i32 %v, i32* %a.addr
%x = load i32, i32* %a.addr
```

这避免了手写 SSA 的复杂度，也和 LLVM 前端常见做法一致。后续需要优化时，可以再做 mem2reg。

### 3. 控制流必须以 BasicBlock 为核心

LLVM IR 中 `br` 和 `ret` 是 terminator，一个基本块只能以 terminator 结束。

改成 `BasicBlock` 后，可以自然处理：

- `if/else`
- `while`
- `break`
- `continue`
- 短路求值
- terminator 后死代码拦截

### 4. 输出责任下放到类本身

每个指令自己知道自己怎么输出：

```cpp
StoreInst::toString()
LoadInst::toString()
BranchInst::toString()
```

这样避免一个巨大的 emitter 到处 `dynamic_cast`，也让新增指令时改动更局部。

### 5. 后端不应该卡在旧 IR 上

如果后端还读 `InterCode`，旧 IR 就删不掉。

所以本次同时迁了后端适配层，让 ASM 也消费新 MiniLLVM IR。

## 当前验证情况

已执行：

```text
cmake --build build-local
```

结果：通过。

已执行：

```text
scripts/run_ci_tests.sh -ir --all --compiler ./build-local/compiler --show-failures
```

结果：

```text
all passed
OK number=43, NG number=0
```

数组功能接入后覆盖的新增用例包括数组定义、表达式维度、const 数组、数组初始化和数组形参。

已执行：

```text
scripts/run_ci_tests.sh -err --all --compiler ./build-local/compiler --show-failures
```

结果：

```text
all passed
OK number=10, NG number=0
```

已执行默认计划：

```text
scripts/run_ci_tests.sh --all --compiler ./build-local/compiler --show-failures
```

结果：`ast` 段为空时会正常跳过，`ir` 和 `err` 段通过；`asm` 段因为本机缺少 RISC-V assembler/linker 失败。

额外执行 ASM smoke test：

```text
./build-local/compiler -S -o /tmp/minic_param_new.s test/stage1_params/param_basic.c
./build-local/compiler -S -o /tmp/minic_if_new.s test/contesttestcases/2023_function/2023_func_24_if_test5.c
```

结果：均能生成非空 RISC-V 汇编。

未执行完整 ASM 链接运行测试，原因是当前环境缺少 RISC-V assembler/linker。

## 当前还没改的内容

### 1. 数组前端和 IR 主链路已接通

当前已经支持：

- 数组声明：`int a[10]`、`int a[4][2]`。
- 数组初始化：空初始化、平铺初始化、嵌套花括号初始化。
- 数组访问：`a[i]`、`a[i][j]`。
- 数组形参：`int a[]`、`int a[][N]`。
- 数组实参退化：局部/全局数组和子数组作为函数实参时生成指向首元素的 GEP。

仍未覆盖的数组相关内容：

- `void` 函数还没接入，因此部分 2023 后段数组用例仍卡在语法层。
- 浮点数组还没接入，因为浮点类型链路尚未完成。
- 大型无尾换行用例会触发当前测试脚本“程序输出和退出码粘连”的历史问题。

### 2. 浮点类型还没完整支持

当前迁移主要跑通整数子集。

后续要做：

- 增加或完善 `FloatType`。
- 增加浮点常量。
- 增加 `FCmpInst`。
- `BinaryInst` 的 `FAdd/FSub/FMul/FDiv` 接入 `IRGenerator`。
- 后端支持浮点寄存器和浮点指令，或者只输出 LLVM IR 由外部工具处理。

### 3. Phi 只是预留，尚未主动生成

`PhiInst` 已存在，但 `IRGenerator` 还没有使用它。

当前短路表达式结果合并使用：

```text
alloca + store + merge load
```

这很稳，但不是 SSA 最优形式。

后续可以做：

- mem2reg。
- 用 `PhiInst` 合并 `if/else` 或短路表达式结果。
- 删除不必要的临时栈槽。

### 4. ASM 后端只是基础迁移

后端已经能消费新 IR 并生成非空汇编，但当前实现仍然是比较直接的栈式翻译：

- 每个有结果的指令基本都会落栈。
- 寄存器分配还很粗糙。
- `GEP` 的数组路径需要等前端数组接入后继续测试。
- 没有完整运行 ASM 测试，因为本机缺少 RISC-V 工具链。

后续如果比赛需要 ASM 路径，需要在有 RISC-V 工具链的环境中跑：

```text
scripts/run_ci_tests.sh -asm --all --compiler ./build-local/compiler --show-failures
```

### 5. 错误测试脚本已兼容 macOS bash

`scripts/run_ci_tests.sh -err --all` 原先使用 `mapfile`。

macOS 默认 bash 版本较老，没有 `mapfile`，会导致脚本无法执行。

当前已经把读取错误期望的逻辑改成 `while read` 形式，并处理空计划段的数组展开，因此 `-err --all` 和默认 `--all` 计划都可以在当前环境直接运行到对应测试段。

## 后续推荐顺序

建议后续按这个顺序继续：

1. 在 Linux 或带 RISC-V 工具链的环境跑完整 ASM 测试。
2. 接 `void` 函数定义和返回语句。
3. 补浮点类型、浮点数组和浮点指令。
4. 做 mem2reg 或至少用 `PhiInst` 优化短路/分支表达式。
5. 清理 `FormalParam`、`GlobalVariable` 中旧后端寄存器字段，并评估 `Value::constValue` 是否需要迁到更专门的符号属性对象。
6. 根据比赛目标决定继续强化 ASM 后端，还是主线固定输出 LLVM IR。

## 当前结论

本次迁移已经完成核心目标：

- 旧 DragonIR 指令和 `InterCode` 已删除。
- `IRGenerator` 直接生成 MiniLLVM IR。
- 数组声明、数组访问和数组形参已经接入 MiniLLVM IR。
- `.ll` 输出直接由 IR 类 `toString()` 完成。
- 后端已经改为消费新 `BasicBlock` IR。
- IR 测试计划全部通过。

剩余工作主要集中在语言功能扩展和后端质量提升，而不是旧 IR 兼容层。
