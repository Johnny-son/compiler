# Backend 架构详解

本文档只聚焦 `src/backend`，目标是把后端从“输入 IR 到输出 RISCV64 汇编”的每层职责讲清楚，并逐文件说明。

二层超细化入口：

- [Backend 二层超细化版](./Backend二层超细化版.md)

## 1. 模块定位

后端承担三件事：

1. 把中端 `Module/Function/Instruction/Value` 变成后端可消费的视图。
2. 为每个函数计算栈帧布局。
3. 按 RV64 指令模板进行指令选择并输出 `.s` 文本。

后端不负责前端语法，不负责 AST，也不负责中端语义检查。

## 2. 后端总流程

入口函数：`BackendDriver::run(Module * module, const std::string & outputFile)`

流程顺序：

1. 输出文件打开。
2. `IRAdapter::adapt(module)` 构建 `IRModuleView`。
3. 发射全局变量段：`.bss` / `.data`。
4. 遍历每个非 builtin 函数：
   - `FrameLayoutBuilder::build(function)`
   - `InstructionSelector selector(function, layout)`
   - `AsmFunction asm = selector.run()`
   - `AsmPrinter::printFunction(fp, asm)`
5. 关闭文件并返回。

## 3. 文件级详解

### 3.1 后端总调度

#### `src/backend/include/BackendDriver.h`

主要作用：

- 声明后端门面类 `BackendDriver`。
- 对外只暴露一个方法 `run`，屏蔽内部流水线细节。

#### `src/backend/BackendDriver.cpp`

主要作用：

- 组织整个后端执行阶段。

关键职责：

- 检查 `module` 与输出文件有效性。
- 通过 `IRAdapter` 统一读取 IR 内容。
- 处理全局变量段发射策略：
  - BSS 场景发 `.bss + .zero`
  - 非零初始化发 `.data + .word`
- 逐函数执行布局与指令选择。

设计价值：

- 把“多步后端流程”集中在一个清晰入口，便于替换子模块实现。

---

### 3.2 IR 适配层

#### `src/backend/include/IRAdapter.h`

主要作用：

- 定义后端专用的只读视图类型，避免后端深耦合中端实体。

核心类型：

- `IRValueKind`：值分类（常量、全局、局部、形参、指令结果等）。
- `IRInstKind`：指令分类（Entry/Exit/Add/Sub/Call...）。
- `IRValueView`：值访问包装。
- `IRInstView`：指令访问包装。
- `IRFunctionView`：函数访问包装。
- `IRModuleView`：模块访问包装。
- `IRAdapter`：`adapt()` 入口。

#### `src/backend/IRAdapter.cpp`

主要作用：

- 实现所有 View 的分类、属性读取、容器遍历。

关键实现点：

- `classifyValue`：通过 `dynamic_cast` 映射到 `IRValueKind`。
- `classifyInstruction`：按 `IRInstOperator` 映射到 `IRInstKind`。
- `IRInstView::calledFunctionRaw/Name`：处理调用指令特有字段。
- `IRInstView::targetLabelRaw`：处理 `goto` 目标标签。
- `IRFunctionView::params/locals/instructions`：构建轻量视图数组。
- `IRModuleView::globals/functions`：统一输出给后端驱动。

设计价值：

- 后端逻辑可以只认 View，不直接触碰中端内部容器结构。

---

### 3.3 栈帧布局

#### `src/backend/include/FrameLayout.h`

主要作用：

- 声明栈对象类型、栈槽信息、函数栈帧模型和构建器。

核心定义：

- `StackObjectKind`：`SavedReturnAddress / SavedFramePointer / FormalParam / LocalVariable / InstructionResult / OutgoingArgArea`
- `StackSlotInfo`：某个对象的偏移、大小、对齐、名字等
- `FunctionFrameLayout`：封装一个函数的完整栈帧布局
- `FrameLayoutBuilder::build()`：布局构建入口

关键常量策略：

- `stackSlotSize = 8`
- `stackAlign = 16`
- `argRegCount = 8`
- 固定保存区 `savedAreaSize = 16`（`ra` + `fp`）

#### `src/backend/FrameLayout.cpp`

主要作用：

- 实现栈帧构建算法。

关键实现点：

- `slotSizeForType`：按类型大小/指针特性推导槽位大小（最小 8 字节）。
- `appendValueSlot`：参数、局部变量、指令结果统一入槽并计算负偏移。
- `build()`：
  - 先放保存区槽位
  - 再放参数、局部、指令结果槽位
  - 若调用实参数量超过 8，增加 `OutgoingArgArea`
  - 最后帧大小 16 字节对齐

与 `InstructionSelector` 关系：

- `InstructionSelector` 通过 `slotOf(value)` 直接取偏移发射 `lw/sw/ld/sd`。

---

### 3.4 指令选择

#### `src/backend/include/InstructionSelector.h`

主要作用：

- 声明 IR -> RV64 的翻译器类 `InstructionSelector`。

核心接口：

- `run()`：遍历函数 IR 指令并生成 `AsmFunction`。
- `translateEntry/Exit/Assign/Binary/Call/Label/Goto`：按指令类别分派翻译。
- `loadValue/storeValue`：值读写抽象，统一处理常量/全局/栈槽。

#### `src/backend/InstructionSelector.cpp`

主要作用：

- 实现具体 RV64 指令模板翻译。

寄存器约定（当前实现）：

- 固定工作寄存器：`t0/t1/t2/t3`
- 参数寄存器：`a0-a7`
- 栈/帧/返回：`sp/fp/ra`

关键翻译策略：

- `translateEntry`：
  - `addi sp, sp, -frameSize`
  - 保存 `ra/fp`
  - 设新 `fp`
  - 前 8 个形参从 `a0-a7` 落栈
- `translateExit`：
  - 若有返回值，加载到 `a0`
  - 恢复 `ra` 与旧 `fp`
  - `sp <- fp`，`ret`
- `translateAssign`：`load(src)` + `store(dst)`
- `translateBinary`：`load lhs/rhs` -> `addw/subw/mulw/divw/remw` -> `store result`
- `translateCall`：
  - 前 8 实参写 `a0-a7`
  - 超出实参写到调用区栈空间
  - `call symbol`
  - 有返回值则 `a0` 写回结果槽
- `translateLabel/Goto`：映射为标签和 `j`

类型相关策略：

- `isEightByteType` 决定使用 `ld/sd` 还是 `lw/sw`。

当前边界：

- 使用固定寄存器模板，不是完整图着色寄存器分配器。

---

### 3.5 汇编中间表示与打印

#### `src/backend/include/Asm.h`

主要作用：

- 声明汇编抽象对象层。

核心结构：

- `AsmOperandKind`：寄存器、立即数、内存、标签、符号
- `AsmOperand`：操作数实体与工厂方法
- `AsmInstruction`：标签行或 opcode 行
- `AsmFunction`：函数级汇编容器
- `AsmPrinter`：文本输出器

#### `src/backend/Asm.cpp`

主要作用：

- 实现汇编对象工厂和打印逻辑。

关键实现点：

- `AsmOperand::toString()` 统一各类型操作数字符串化。
- `AsmFunction` 提供 `emitLabel/emitOp` 累积指令。
- `AsmPrinter::printFunction()` 输出 ELF 风格函数骨架：
  - `.text`
  - `.globl`
  - `.type`
  - 函数体
  - `.size`

工程价值：

- 把“语义翻译”与“文本拼接”分离，便于后续加注释或格式策略。

## 4. 典型控制与数据路径

### 4.1 全局变量路径

- 中端 `GlobalVariable` -> `IRValueView` -> `BackendDriver`
- `isInBSSSection()` 决定写 `.bss` 还是 `.data`
- `InstructionSelector` 访问全局值时先 `la` 地址，再 `lw/sw` 或 `ld/sd`

### 4.2 函数内临时结果路径

- IR 指令若 `hasResult()`，在 `FrameLayoutBuilder` 分配槽位
- 翻译算术时结果先进 `t2`，再 `store` 到该槽位
- 后续用户读取时 `load` 出来参与新运算

### 4.3 函数调用参数路径

- 前 8 参数：caller 计算后放 `a0-a7`
- 超出参数：caller 写到以 `sp` 为基址的调用区
- callee entry：把寄存器参数写入本函数参数槽位

## 5. 当前后端的边界与演进方向

当前边界：

- 无独立寄存器分配模块。
- 指令模板偏“最小可用”，未覆盖复杂控制流和高级优化。
- 当前主要针对整数路径。

建议演进：

1. 将工作寄存器策略抽象成寄存器分配层。
2. 在 `InstructionSelector` 中补更多 IR 指令映射（比较、条件跳转等）。
3. 引入 peephole 优化与冗余 load/store 折叠。
4. 扩展 ABI 细节（保存寄存器、调用约定边界场景）。
