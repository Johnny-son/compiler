# IR 架构详解

本文档聚焦 `src/ir`，并按“统一对象模型 + 符号管理 + 翻译器 + 指令系统”展开。你可以把这一层理解为整个编译器的中枢层。

二层超细化入口：

- [IR 二层超细化版](./IR二层超细化版.md)

## 1. IR 层定位

IR 层承担两条主线：

1. 把前端 AST 翻译为线性、可执行的中间表示（指令序列）。
2. 维护符号、作用域、类型、值依赖关系，并给后端提供稳定输入。

IR 层上下游关系：

- 上游输入：`frontend` 的 `ast_node`
- 下游输出：
  - LLVM 风格文本 IR（`Module::outputIR`）
  - 结构化 `Module/Function/Instruction/Value`（供 backend 读取）

## 1.1 统一错误输出规范（2026-04 更新）

IR 层报错统一采用以下模板：

```text
IR错误[Exxxx] 第N行 类别: 详细原因
IR错误[Exxxx] 未知行 类别: 详细原因
```

说明：

- `Exxxx` 为 IR 错误码。
- 行号未知时固定输出“未知行”，不再输出负数行号。
- 主流程在 IR 失败后仅输出简短汇总，避免重复细节报错。

当前错误码分段约定：

- `E10xx`：节点分发与通用翻译错误
- `E11xx`：函数定义、形参与 return 语义错误
- `E12xx`：函数调用、控制流与逻辑翻译错误
- `E13xx`：变量声明与初始化错误
- `E14xx`：模块符号检查错误
- `E15xx`：调用约定与 ARG 计数一致性错误

## 2. 总体对象模型

IR 中最核心的抽象关系是：

- `Type`：类型
- `Value`：值
- `User`：使用值的值（拥有 operands）
- `Use`：`Value <-> User` 之间的一条 def-use 边
- `Instruction`：一种 `User`
- `Function`：全局值，同时持有 `InterCode`
- `Module`：源文件级容器，持有函数、全局变量、常量池、作用域栈

这套设计使得数据流依赖可以追踪（例如 `replaceAllUseWith`）。

## 3. 文件级详解

---

## 3.1 `include/` 协议与核心抽象

### `src/ir/include/Type.h`

主要作用：

- 类型系统根类 `Type`。
- 定义类型 ID 与公共查询接口（`isVoidType/isIntegerType/isPointerType/...`）。

关键点：

- `toString()` 为纯虚函数，具体子类负责字符串表示。
- `getSize()` 默认返回 `-1`，由具体类型覆盖。

### `src/ir/include/Value.h`

主要作用：

- 值系统根类 `Value`。

关键字段：

- `name`：源级名字
- `IRName`：IR 输出名字
- `type`：类型
- `uses`：被使用边列表

关键方法：

- `addUse/removeUse/removeUses`
- `replaceAllUseWith`
- `getScopeLevel/getRegId/getMemoryAddr/getLoadRegId`（可被子类覆写）

### `src/ir/include/User.h`

主要作用：

- `User` 继承 `Value`，表示“带操作数”的值。

关键字段与能力：

- `operands`（`Use*` 列表）
- `addOperand/setOperand/removeOperand/clearOperands`
- `getOperand/getOperandsNum/getOperandsValue`

### `src/ir/include/Use.h`

主要作用：

- 定义 `Use` 边：一端是被使用 `Value`，另一端是使用者 `User`。

关键方法：

- `setUsee(newVal)`：把边重定向到新值。
- `remove()`：从两端解绑，但不销毁 `Use` 对象本身。

### `src/ir/include/Constant.h`

主要作用：

- 定义 `Constant : User`。

语义定位：

- 表示不可变值；可被复合成常量表达式。
- `GlobalValue` 也通过该层继承链获得“值 + 操作数”能力。

### `src/ir/include/GlobalValue.h`

主要作用：

- 定义全局实体基类 `GlobalValue : Constant`。

关键语义：

- 全局变量/函数都属于全局值。
- IR 名字默认是 `@name`。
- 包含对齐、linkage、visibility 元信息。

### `src/ir/include/Function.h`

主要作用：

- 定义函数对象 `Function : GlobalValue`。

关键字段：

- 返回类型、形参列表
- `InterCode code`
- 局部变量列表
- 函数出口标签、返回值槽
- 栈帧统计信息（最大深度、最大实参数、是否存在调用等）

关键方法：

- `getInterCode()` 获取函数 IR
- `newLocalVarValue()` 创建局部变量
- `renameIR()` 完成形参/局部/指令重命名

### `src/ir/include/Instruction.h`

主要作用：

- 定义 IR 指令基类 `Instruction : User` 及操作码 `IRInstOperator`。

操作码覆盖：

- `ENTRY/EXIT/LABEL/GOTO/COND_BR`
- `ADD/SUB/MUL/DIV/MOD`
- `CMP_EQ/CMP_NE/CMP_LT/CMP_LE/CMP_GT/CMP_GE`
- `ASSIGN/FUNC_CALL/ARG`

关键能力：

- `getOp()`、`hasResultValue()`、`toString()`
- 死代码标记 `isDead/setDead`
- 函数归属 `getFunction()`

### `src/ir/include/IRCode.h`

主要作用：

- 定义 `InterCode`：指令序列容器。

关键能力：

- `addInst(Instruction*)`
- `addInst(InterCode&)`（拼接并转移所有权）
- `Delete()` 统一清理指令与操作数边

### `src/ir/include/Module.h`

主要作用：

- 定义模块容器 `Module`（一个源文件一个模块）。

关键职责：

- 作用域控制 `enterScope/leaveScope`
- 当前函数状态 `getCurrentFunction/setCurrentFunction`
- 函数表、全局变量表、常量池管理
- `newVarValue/findVarValue` 语义分析期间的变量创建和查询
- `renameIR/outputIR/Delete`

### `src/ir/include/IRGenerator.h`

主要作用：

- 声明 AST -> IR 翻译器 `IRGenerator`。

关键设计：

- `ast_operator_type -> handler` 映射表分发
- 为每类 AST 节点定义 `ir_xxx` 处理函数
- 通过 `predeclare_function + ir_function_body` 两阶段支持前向调用与递归
- 通过 `loopTargets` 维护 `break/continue` 所需的循环跳转目标

### `src/ir/include/IRConstant.h`

主要作用：

- 汇总 IR 命名与关键字常量。

关键常量：

- 名字前缀：`@`、`%l`、`%t`、`%m`、`.L`
- 关键字：`declare/define/add/sub/mul/div/mod`

---

## 3.2 核心实现文件

### `src/ir/Value.cpp`

主要作用：

- 实现 `Value` 通用行为。

关键实现点：

- `addUse/removeUse/removeUses` 维护 def-use 列表。
- `replaceAllUseWith` 在所有用户中替换旧值。
- 默认寄存器/内存接口返回无效状态，等待子类覆盖。

### `src/ir/User.cpp`

主要作用：

- 实现 `User` 操作数管理。

关键实现点：

- `addOperand` 会创建 `Use` 并把边注册到两端。
- `setOperand`、`replaceOperand` 支持操作数替换。
- `clearOperands` 逐条解绑并释放边。

### `src/ir/Use.cpp`

主要作用：

- 实现 `Use` 边的重定向与解绑。

关键实现点：

- `setUsee`：旧 value 去边，新 value 加边。
- `remove`：同时通知 `usee` 与 `user` 删除此边。

### `src/ir/Instruction.cpp`

主要作用：

- 实现 `Instruction` 基类行为。

关键实现点：

- `hasResultValue()` 用类型是否 `void` 判定“有无结果值”。
- 默认 `toString()` 仅输出 unknown，具体子类覆写。

### `src/ir/IRCode.cpp`

主要作用：

- 实现 `InterCode` 的拼接与销毁。

关键实现点：

- `addInst(InterCode&)` 采用“插入后清空源容器”避免双重释放。
- `Delete()` 先 `clearOperands` 再 delete 指令。

### `src/ir/Function.cpp`

主要作用：

- 实现函数对象行为。

关键实现点：

- `toString` 输出 DragonIR 风格函数文本（含 declare 行和指令行）。
- `newLocalVarValue/newMemVariable` 创建函数内值对象。
- `renameIR` 统一重命名形参/局部/label/结果值。
- 维护调用统计：`realArgCount`、`maxFuncCallArgCnt` 等。

### `src/ir/Module.cpp`

主要作用：

- 实现模块管理、符号管理和 LLVM 风格文本输出。

关键实现点：

- 构造时注入内置函数 `putint/getint`。
- `newFunction/newVarValue/findVarValue` 负责语义构建阶段的对象管理。
- `newConstInt` 做整型常量池去重。
- `outputIR` 输出：内置声明 -> 全局定义 -> 用户函数定义。
- 文件顶部一组 `emitLLVM...` 辅助函数实现 IR 指令到 LLVM 文本映射，其中比较指令会落成 `icmp + zext`，条件跳转会落成 `icmp ne i32 cond, 0 + br i1`。
- `newFunction/newVarValue` 会阻止函数与全局变量同名；符号冲突/非法全局名错误已统一为 `E1400/E1401` 风格输出。

### `src/ir/IRGenerator.cpp`

主要作用：

- 实现 AST 到 IR 的核心翻译。

关键实现点：

- 构造函数中注册全部节点 handler。
- `ir_compile_unit` 先预声明函数签名，再翻译全局变量和函数体，支持先调用后定义与递归。
- `ir_function_define` 串联 `predeclare_function + ir_function_body`；函数体内统一建立 entry、返回槽、exit label 与 exit 指令。
- `ir_function_formal_params` 把形式参数映射到当前函数作用域变量，并用 `MoveInstruction` 落地，后续按普通局部变量处理。
- `ir_block` 负责按 `needScope` 控制作用域入栈/出栈。
- `ir_return`：return 值写入函数返回槽，再 goto 统一出口。
- `ir_add/sub/mul/div/mod/eq/ne/lt/le/gt/ge`：生成算术或比较 `BinaryInstruction`，比较结果统一落成 `i32 0/1`。
- `ir_logical_not` 把逻辑非翻译成“与 0 比较”；`ir_logical_and/or` 通过标签与条件跳转实现短路求值。
- `ir_if/ir_while` 负责分支与循环标签编排，`block_ends_with_terminator` 避免重复补跳转。
- `loopTargets` 维护 `(continueTarget, breakTarget)` 栈，用于 `ir_break/ir_continue`。
- `ir_assign`：生成 `MoveInstruction`。
- `ir_variable_declare`：局部/全局变量声明与初始化。
- `eval_global_const_expr`：全局初始化常量折叠，已覆盖整数、比较和逻辑表达式。
- `ir_function_call`：实参求值 + 参数个数校验 + 调用指令生成。
- `report_ir_error`：IR 统一诊断入口，负责错误码、位置和类别格式化输出。

已统一的典型错误：

- 函数形参节点非法（`E1101`）
- 函数形参数量不一致（`E1111`）
- `void` 函数返回表达式 / 非 `void` 函数缺返回值 / 函数外 return（`E1120/E1121/E1122`）
- `break/continue` 不在循环内（`E1210/E1211`）
- 全局变量初始化表达式非法（`E1301`）
- 全局变量初始化落地失败（`E1302`）

---

## 3.3 `Types/` 类型子系统文件

### `src/ir/Types/IntegerType.h`

主要作用：

- 定义整数类型类，支持 bool(1 bit) 和 int(32 bit) 单例。

### `src/ir/Types/IntegerType.cpp`

主要作用：

- 实现 `getTypeBool/getTypeInt` 单例获取。

### `src/ir/Types/VoidType.h`

主要作用：

- 定义 void 类型类和 `getType()` 单例接口。

### `src/ir/Types/VoidType.cpp`

主要作用：

- 实现 void 单例创建。

### `src/ir/Types/LabelType.h`

主要作用：

- 定义标签类型（用于 label 相关语义）。

### `src/ir/Types/LabelType.cpp`

主要作用：

- 实现标签类型单例。

### `src/ir/Types/FunctionType.h`

主要作用：

- 定义函数类型（返回类型 + 参数类型列表）。

关键能力：

- `toString` 生成函数类型文本
- `getReturnType/getArgTypes`

### `src/ir/Types/PointerType.h`

主要作用：

- 定义指针类型及其层级信息。

关键能力：

- `getPointeeType/getRootType/getDepth`
- 静态 `get(pointee)` 通过 `StorageSet` 去重缓存

---

## 3.4 `Values/` 值对象文件

### `src/ir/Values/ConstInt.h`

主要作用：

- 整型常量值对象。

关键点：

- `getIRName()` 直接返回字面值文本。
- 保存 `intVal`，支持 load 寄存器编号缓存。

### `src/ir/Values/GlobalVariable.h`

主要作用：

- 全局变量值对象。

关键点：

- 持有 BSS/初始化状态（`inBSSSection`、`hasInitializer`、`initializerInt`）。
- `setInitializer` 会更新是否进入 BSS。

### `src/ir/Values/LocalVariable.h`

主要作用：

- 局部变量值对象。

关键点：

- 记录 `scope_level`。
- 支持寄存器/内存地址接口覆盖。

### `src/ir/Values/FormalParam.h`

主要作用：

- 函数形参值对象。

关键点：

- 保存寄存器号、内存地址、load 寄存器号。
- 供调用约定与后端参数处理使用。

### `src/ir/Values/MemVariable.h`

主要作用：

- 必在内存中的值对象（后端临时内存抽象）。

关键点：

- `getMemoryAddr` 总是可用。

### `src/ir/Values/RegVariable.h`

主要作用：

- 寄存器值对象。

关键点：

- 通过 `regId` 标识物理或虚拟寄存器编号。
- `getIRName()` 返回寄存器名。

---

## 3.5 `Instructions/` 指令子系统文件

### `src/ir/Instructions/EntryInstruction.h`

主要作用：

- 声明函数入口指令类。

### `src/ir/Instructions/EntryInstruction.cpp`

主要作用：

- 实现入口指令构造和 `toString("entry")`。

### `src/ir/Instructions/ExitInstruction.h`

主要作用：

- 声明函数出口指令类。

### `src/ir/Instructions/ExitInstruction.cpp`

主要作用：

- 实现出口指令，可选带返回值操作数。
- `toString` 输出 `exit` 或 `exit <val>`。

### `src/ir/Instructions/LabelInstruction.h`

主要作用：

- 声明标签指令类。

### `src/ir/Instructions/LabelInstruction.cpp`

主要作用：

- `toString` 输出 `<IRName>:`。

### `src/ir/Instructions/GotoInstruction.h`

主要作用：

- 声明无条件跳转指令，保存目标 `LabelInstruction*`。

### `src/ir/Instructions/GotoInstruction.cpp`

主要作用：

- 实现 `br label target` 形式输出。
- 提供 `getTarget()` 给后端读取。

### `src/ir/Instructions/CondBranchInstruction.h`

主要作用：

- 声明条件跳转指令类，保存条件值和真假分支目标。

### `src/ir/Instructions/CondBranchInstruction.cpp`

主要作用：

- 实现 `br cond, label trueTarget, label falseTarget` 形式输出。
- 供 `if/while` 与短路逻辑翻译共享。

### `src/ir/Instructions/MoveInstruction.h`

主要作用：

- 声明赋值指令类（assign/move）。

### `src/ir/Instructions/MoveInstruction.cpp`

主要作用：

- 实现 `dst = src` 指令构造与输出。

### `src/ir/Instructions/BinaryInstruction.h`

主要作用：

- 声明二元算术/比较指令类。

### `src/ir/Instructions/BinaryInstruction.cpp`

主要作用：

- 根据操作码输出 `add/sub/mul/div/mod/cmp_*` 指令文本。

### `src/ir/Instructions/FuncCallInstruction.h`

主要作用：

- 声明函数调用指令类，保存 `calledFunction`。

### `src/ir/Instructions/FuncCallInstruction.cpp`

主要作用：

- 实现调用指令构造、参数拼接输出、返回值输出。
- 校验 ARG 指令计数与调用参数数量关系。
- 当 ARG 计数与调用参数不一致且计数非零时，输出统一错误 `E1500`。

### `src/ir/Instructions/ArgInstruction.h`

主要作用：

- 声明实参传递指令类。

### `src/ir/Instructions/ArgInstruction.cpp`

主要作用：

- 实现 `arg <value>` 输出。
- 顺带递增函数的 `realArgCount` 统计。

---

## 4. IR 层内部协作关系

### 4.1 翻译协作

- `IRGenerator` 遍历 AST。
- `Module` 提供符号与作用域服务。
- `Function` 承接函数级指令与变量。
- 指令对象进入 `InterCode`。

### 4.2 命名协作

- `Module::renameIR` 驱动函数级重命名。
- `Function::renameIR` 给形参、局部、label、结果值统一分配 IR 名字。

### 4.3 输出协作

- `Module::outputIR` 从 `Function/Instruction/Value` 读取结构信息。
- 辅助函数按对象类型发射 LLVM 风格文本。

## 5. 当前边界与扩展点

当前边界：

- 函数定义/调用当前支持 `int` 返回值与 `int` 标量形参，尚未扩到数组形参和更完整类型系统。
- 全局初始化表达式仍限定为可常量折叠的整数/比较/逻辑表达式。
- LLVM 文本输出仍集中在 `Module` 内部，尚未拆成独立 emitter 或 basic block/CFG 层。

建议优先扩展：

1. 扩数组、指针、`void` 函数等类型与签名，前中后端一起打通。
2. 引入 basic block / CFG 抽象，让分支与循环不再只靠线性指令序列表达。
3. 分离 LLVM 文本输出器与 `Module`，降低职责耦合。
4. 再叠加基础优化 pass（常量传播、死代码删除等）。
