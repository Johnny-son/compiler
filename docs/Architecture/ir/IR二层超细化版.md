# IR 二层超细化版

本文档覆盖 `src/ir` 下全部文件。每个文件都包含：

- 关键函数调用链图（谁调谁）
- 修改建议
- 常见坑位

---

## A. `src/ir/include` 头文件层

### 1. `src/ir/include/Type.h`

文件职责：类型系统基类与类型分类接口。

关键函数调用链图：

```text
AST.cpp::typeAttr2Type
  -> IntegerType::getTypeInt / VoidType::getType
IRGenerator / Module / Backend
  -> Type::is* / toString / getSize
```

修改建议：

- 新类型优先扩这里的 TypeID 与查询接口，再在 `Types/` 落地子类。

常见坑位：

- 忘实现 `toString()` 会导致 IR/ASM 输出不可读或崩溃。

### 2. `src/ir/include/Value.h`

文件职责：值系统基类声明。

关键函数调用链图：

```text
User/Instruction/LocalVariable/GlobalVariable...
  -> 继承 Value
优化/替换逻辑
  -> Value::replaceAllUseWith
```

修改建议：

- 若新增“值属性”（如 constness、SSA id），从这里加通用接口最稳。

常见坑位：

- `name` 与 `IRName` 语义不同，混用会导致符号输出错乱。

### 3. `src/ir/include/User.h`

文件职责：带操作数的值基类。

关键函数调用链图：

```text
Instruction/Constant
  -> User::addOperand/removeOperand
    -> Use 边维护
```

修改建议：

- 新增批量替换操作时建议在 `User` 层集中实现，避免每类指令重复写。

常见坑位：

- 直接改 `operands` 容器而不经接口会破坏 def-use 一致性。

### 4. `src/ir/include/Use.h`

文件职责：定义 def-use 边。

关键函数调用链图：

```text
User::addOperand
  -> new Use(value, user)
Use::setUsee/remove
  -> Value 与 User 双端同步更新
```

修改建议：

- 做 IR 优化 pass 时尽量复用 `setUsee/remove`，别手写双端同步逻辑。

常见坑位：

- 仅删一端引用会造成悬挂 use，后续遍历崩溃。

### 5. `src/ir/include/Instruction.h`

文件职责：IR 指令基类和操作码定义。

关键函数调用链图：

```text
IRGenerator
  -> new XxxInstruction(...)
Module::outputIR / Backend IRAdapter
  -> Instruction::getOp / hasResultValue
```

修改建议：

- 新指令先扩 `IRInstOperator`，再补 `IRAdapter` 分类和后端分发。

常见坑位：

- 将有结果值的指令误设为 void type，会导致后续 result 槽位丢失。

### 6. `src/ir/include/IRCode.h`

文件职责：函数级线性指令容器声明。

关键函数调用链图：

```text
IRGenerator::ir_*
  -> ast_node.blockInsts.addInst(...)
Function::getInterCode
  -> Module::outputIR / Backend
```

修改建议：

- 如果要引入 basic block 层，可先在 `InterCode` 增 block 切片视图。

常见坑位：

- 拼接后未清空源 block 会触发双重释放。

### 7. `src/ir/include/Constant.h`

文件职责：常量值基类（继承 User）。

关键函数调用链图：

```text
ConstInt / GlobalValue
  -> Constant
```

修改建议：

- 若引入复合常量表达式，可直接在 Constant 体系扩展。

常见坑位：

- 误把“可变全局值”当常量值语义处理，会混淆地址常量与内容可变性。

### 8. `src/ir/include/GlobalValue.h`

文件职责：全局实体基类（函数、全局变量）。

关键函数调用链图：

```text
Module::newFunction/newGlobalVariable
  -> Function / GlobalVariable : GlobalValue
Module::outputIR
  -> getIRName/alignment
```

修改建议：

- 需要链接可见性策略时，在这里加 linkage/visibility 控制接口。

常见坑位：

- 全局符号名必须稳定，rename 流程不要重命名 GlobalValue。

### 9. `src/ir/include/Function.h`

文件职责：函数对象声明（参数、局部变量、IR 指令、栈统计）。

关键函数调用链图：

```text
Module::newFunction
  -> Function ctor
IRGenerator::ir_function_define
  -> setExitLabel/setReturnValue/getInterCode
Backend FrameLayout
  -> 读取 maxFuncCallArgCnt 等统计
```

修改建议：

- 新增函数级元信息（inline hint、pure 等）可直接扩这里。

常见坑位：

- `realArgCount` 统计依赖 `ArgInstruction`/调用输出流程，改动时需联调。

### 10. `src/ir/include/Module.h`

文件职责：模块容器声明（函数表、全局表、常量池、作用域）。

关键函数调用链图：

```text
main.cpp::compile
  -> Module module
IRGenerator
  -> newVarValue/findVarValue/newFunction
Module::outputIR
  -> emit LLVM-style text
BackendDriver
  -> 消费 Module
```

修改建议：

- 若要拆分职责，优先把 IR 文本输出从 Module 分离为独立 emitter。

常见坑位：

- 在 `currentFunc==nullptr` 场景创建匿名变量会触发错误。

### 11. `src/ir/include/IRGenerator.h`

文件职责：AST -> IR 翻译器声明与 handler 映射定义。

关键函数调用链图：

```text
main.cpp::compile
  -> IRGenerator(root, module).run()
     -> ir_visit_ast_node
        -> ir_xxx handlers
```

修改建议：

- 新 AST 节点支持时，同步在这里声明 `ir_xxx` 并加入映射。

常见坑位：

- 声明了 handler 但构造函数未注册，会落入 `ir_default` 静默跳过。

### 12. `src/ir/include/IRConstant.h`

文件职责：IR 名称前缀与关键字常量。

关键函数调用链图：

```text
Function::renameIR / GlobalValue ctor
  -> 使用 IR_*_PREFIX
文本输出
  -> 使用关键字常量
```

修改建议：

- 全量命名前缀改动时先更新这里，再跑全链路输出回归。

常见坑位：

- 改前缀会影响测试基线和后端 label 匹配，需同步更新。

---

## B. 核心实现文件（根目录 cpp）

### 13. `src/ir/Value.cpp`

文件职责：`Value` 通用行为实现。

关键函数调用链图：

```text
User::addOperand
  -> Value::addUse
Use::setUsee/remove
  -> Value::removeUse/addUse
优化替换
  -> Value::replaceAllUseWith
     -> User::setOperand
```

修改建议：

- 若要做 use-list 性能优化，可将 `vector` 改为侵入式链表，但需全链路改。
- `replaceAllUseWith` 可加类型校验钩子防误替换。

常见坑位：

- 遍历 `uses` 同时修改容器，必须先拷贝（当前实现已处理）。

### 14. `src/ir/User.cpp`

文件职责：`User` 操作数管理实现。

关键函数调用链图：

```text
Instruction/Constant 构造
  -> User::addOperand
     -> new Use + Value::addUse
删除路径
  -> removeOperand/clearOperands
     -> Use::remove
```

修改建议：

- 可增加 `replaceOperandAt(pos,newVal)` 显式 API，减少外部遍历替换。

常见坑位：

- `removeOperand(int pos)` 中 use 删除后容器会收缩，循环写法要特别小心。

### 15. `src/ir/Use.cpp`

文件职责：`Use` 边变更实现。

关键函数调用链图：

```text
User::setOperand/replaceOperand
  -> Use::setUsee
User/Value 清理
  -> Use::remove
```

修改建议：

- 可增加 debug assert：`newVal != nullptr`、`user/usee` 有效性。

常见坑位：

- `remove()` 不 delete 自己；调用方忘释放会泄漏。

### 16. `src/ir/Instruction.cpp`

文件职责：`Instruction` 基类实现。

关键函数调用链图：

```text
各指令子类 ctor
  -> Instruction ctor
输出与后端
  -> getOp / hasResultValue / getFunction
```

修改建议：

- 可在 `toString` 默认实现里输出 op code，便于排查 unknown 指令。

常见坑位：

- 子类若未覆写 `toString`，输出会退化为 unknown。

### 17. `src/ir/IRCode.cpp`

文件职责：线性指令序列管理实现。

关键函数调用链图：

```text
IRGenerator::ir_*
  -> InterCode::addInst(inst)
父节点合并子节点
  -> InterCode::addInst(block)
Function::Delete
  -> InterCode::Delete
```

修改建议：

- 如果后续要支持指令插入/删除优化，可增加双向迭代器接口。

常见坑位：

- 先删指令再清 operands 会遗留 use 边悬挂。

### 18. `src/ir/Function.cpp`

文件职责：函数对象完整实现。

关键函数调用链图：

```text
Module::newFunction
  -> Function::Function
IRGenerator::ir_function_define
  -> getInterCode/setExitLabel/setReturnValue
文本输出
  -> Function::toString
命名阶段
  -> Function::renameIR
```

修改建议：

- `toString` 与 `Module::outputIR` 目前并存两种输出路径，可逐步统一。
- `renameIR` 可按类别分命名域（param/local/temp/label）提升可读性。

常见坑位：

- `paramsType` 构造逻辑若改错会影响函数签名类型。
- `Delete()` 会删 code 与 vars，外部不可重复删。

### 19. `src/ir/Module.cpp`

文件职责：模块级符号管理 + LLVM 风格文本输出实现。

关键函数调用链图：

```text
Module::Module
  -> scopeStack init
  -> newFunction(putint/getint)
IRGenerator
  -> newFunction/newVarValue/findVarValue/newConstInt
输出阶段
  -> renameIR
  -> outputIR
     -> emitLLVMFunctionDeclaration/Definition
```

修改建议：

- 建议拆出 `LLVMIREmitter`，减少 Module 职责过重。
- `newFunction` 里的参数类型收集有可读性优化空间（当前向量构造方式可精简）。
- 全局变量初始化策略可扩展为常量表达式树 evaluator 独立模块。

常见坑位：

- 作用域栈全局层必须先 `enterScope`，否则全局变量插入失败。
- `newVarValue` 在全局场景要求 name 非空，匿名全局会报错。
- 输出 LLVM 文本时 label 命名 sanitize 规则改动会影响分支目标一致性。

### 20. `src/ir/IRGenerator.cpp`

文件职责：AST 到 IR 的核心翻译实现。

关键函数调用链图：

```text
IRGenerator::run
  -> ir_visit_ast_node(root)
     -> ir_compile_unit
        -> ir_function_define
           -> EntryInstruction
           -> ir_function_formal_params
           -> ir_block
           -> LabelInstruction(exit)
           -> ExitInstruction
        -> ir_declare_statment / ir_variable_declare
        -> ir_add/sub/mul/div/mod
        -> ir_assign
        -> ir_return
        -> ir_function_call
```

修改建议：

- 先补全 `ir_function_formal_params`（这是当前语义短板之一）。
- 可引入统一错误上报对象，把 TODO 处语义错误补齐。
- 大表达式翻译可抽共用 `translateBinaryOp` 减少重复代码。
- `eval_global_const_expr` 可独立成 utility，便于测试。

常见坑位：

- `block_node->needScope=false` 是函数体特殊逻辑，改动需谨慎。
- 标识符查找失败时 `node->val` 为空，后续直接用会崩。
- 赋值左右求值顺序与副作用语义相关，改顺序会破坏行为。

---

## C. `src/ir/Instructions` 指令文件

### 21. `src/ir/Instructions/EntryInstruction.h`

文件职责：入口指令声明。

关键函数调用链图：

```text
IRGenerator::ir_function_define
  -> new EntryInstruction(func)
```

修改建议：

- 如需 prologue 元信息（例如栈大小注释），可在类上加辅助字段。

常见坑位：

- 入口指令必须是函数 IR 的首条语义指令之一。

### 22. `src/ir/Instructions/EntryInstruction.cpp`

文件职责：入口指令实现与文本输出。

关键函数调用链图：

```text
EntryInstruction ctor
  -> Instruction(IRINST_OP_ENTRY, void)
toString
  -> "entry"
```

修改建议：

- 若引入多目标 IR 方言，可让 `toString` 走统一 formatter。

常见坑位：

- `void` 类型改错会被误判成有结果值指令。

### 23. `src/ir/Instructions/ExitInstruction.h`

文件职责：出口指令声明。

关键函数调用链图：

```text
IRGenerator::ir_function_define
  -> new ExitInstruction(func, ret)
```

修改建议：

- 如果支持多返回值，可在这里扩展 operand 语义。

常见坑位：

- 返回值 operand 可为空，调用端要判 `getOperandsNum()`。

### 24. `src/ir/Instructions/ExitInstruction.cpp`

文件职责：出口指令实现。

关键函数调用链图：

```text
ExitInstruction ctor
  -> addOperand(ret?)
Module::outputIR
  -> 读取 EXIT operand -> emit ret
```

修改建议：

- 输出字符串可以补类型信息，便于 debug。

常见坑位：

- 对 void 函数错误附加返回值会产生后端语义冲突。

### 25. `src/ir/Instructions/LabelInstruction.h`

文件职责：标签指令声明。

关键函数调用链图：

```text
IRGenerator::ir_function_define / control flow
  -> new LabelInstruction
GotoInstruction
  -> 引用 LabelInstruction*
```

修改建议：

- 可增加 basic-block id 字段，便于 CFG 建图。

常见坑位：

- label 重命名策略要与 goto 目标一致。

### 26. `src/ir/Instructions/LabelInstruction.cpp`

文件职责：标签指令实现。

关键函数调用链图：

```text
toString
  -> IRName + ':'
```

修改建议：

- 输出可附注原块信息（调试期）。

常见坑位：

- `IRName` 为空时输出非法标签，需确保 `renameIR` 在输出前执行。

### 27. `src/ir/Instructions/GotoInstruction.h`

文件职责：无条件跳转指令声明。

关键函数调用链图：

```text
IRGenerator::ir_return / future branch
  -> new GotoInstruction(func, targetLabel)
Backend IRAdapter
  -> IRInstView::targetLabelRaw
```

修改建议：

- 若支持条件跳转，新增 `BrInstruction` 而非复用 goto。

常见坑位：

- target 必须是 `LabelInstruction*`，传错指令类型会 UB。

### 28. `src/ir/Instructions/GotoInstruction.cpp`

文件职责：goto 指令实现。

关键函数调用链图：

```text
GotoInstruction ctor
  -> target = static_cast<LabelInstruction*>
toString
  -> "br label <target>"
```

修改建议：

- constructor 可加 `dynamic_cast` 断言提高安全性。

常见坑位：

- static_cast 在非法输入下无保护，调试阶段建议加 assert。

### 29. `src/ir/Instructions/MoveInstruction.h`

文件职责：赋值指令声明。

关键函数调用链图：

```text
IRGenerator::ir_assign / variable init / return store
  -> new MoveInstruction(dst, src)
```

修改建议：

- 如扩展类型系统，建议在 ctor 内加类型兼容校验。

常见坑位：

- dst/src operand 顺序不可颠倒。

### 30. `src/ir/Instructions/MoveInstruction.cpp`

文件职责：赋值指令实现。

关键函数调用链图：

```text
MoveInstruction ctor
  -> addOperand(dst)
  -> addOperand(src)
toString
  -> "dst = src"
```

修改建议：

- 可在 debug 模式输出类型 `dst:type <- src:type`。

常见坑位：

- 忘记先计算 src 再写 dst 会在有副作用场景引发问题（上层生成顺序要稳）。

### 31. `src/ir/Instructions/BinaryInstruction.h`

文件职责：二元算术指令声明。

关键函数调用链图：

```text
IRGenerator::ir_add/sub/mul/div/mod
  -> new BinaryInstruction(op, lhs, rhs, type)
```

修改建议：

- 可增加 `isCommutative()` 辅助接口给优化器用。

常见坑位：

- 操作码与语义不一致会让后端发错指令模板。

### 32. `src/ir/Instructions/BinaryInstruction.cpp`

文件职责：二元算术指令实现。

关键函数调用链图：

```text
BinaryInstruction ctor
  -> addOperand(lhs/rhs)
toString
  -> add/sub/mul/div/mod 文本
```

修改建议：

- 重复分支可抽成 op->keyword 映射表，降低维护成本。

常见坑位：

- 新增操作码忘记更新 `toString` 会回落 unknown。

### 33. `src/ir/Instructions/FuncCallInstruction.h`

文件职责：函数调用指令声明。

关键函数调用链图：

```text
IRGenerator::ir_function_call
  -> new FuncCallInstruction(current, called, args, retType)
Backend IRAdapter
  -> calledFunction / getCalledName
```

修改建议：

- 可补充“调用约定元信息”字段，为后端 ABI 扩展做准备。

常见坑位：

- calledFunction 为空会导致输出和后端阶段崩溃。

### 34. `src/ir/Instructions/FuncCallInstruction.cpp`

文件职责：函数调用指令实现与文本输出。

关键函数调用链图：

```text
ctor
  -> addOperand(args...)
toString
  -> call void/i32 @func(args)
  -> func->realArgCountReset()
```

修改建议：

- 当前参数类型输出假定较强，建议改为直接读取 calledFunction 签名类型。
- 可以把 ARG 计数校验提到 IRGenerator，降低 toString 副作用。

常见坑位：

- `toString` 内重置 arg 计数属于副作用行为，调试/多次输出时要注意。

### 35. `src/ir/Instructions/ArgInstruction.h`

文件职责：实参 ARG 指令声明。

关键函数调用链图：

```text
(可选调用约定路径)
  -> new ArgInstruction(func, argVal)
```

修改建议：

- 若最终仅保留 FuncCall operand 方案，可考虑弃用 ARG 指令减少双轨逻辑。

常见坑位：

- ARG 与 FuncCall 参数数量统计不一致会报错。

### 36. `src/ir/Instructions/ArgInstruction.cpp`

文件职责：ARG 指令实现。

关键函数调用链图：

```text
toString
  -> "arg <val>"
  -> func->realArgCountInc()
```

修改建议：

- 若保留 ARG，建议在 IRGenerator 显式生成以避免统计语义漂移。

常见坑位：

- toString 带计数副作用，重复打印会重复累加。

---

## D. `src/ir/Types` 类型文件

### 37. `src/ir/Types/IntegerType.h`

文件职责：整数类型类声明（bool/int 单例）。

关键函数调用链图：

```text
AST/IRGenerator/Module
  -> IntegerType::getTypeInt/getTypeBool
  -> Type query / toString / getSize
```

修改建议：

- 若支持 i8/i16/i64，可扩位宽工厂并配套缓存策略。

常见坑位：

- `getSize()` 目前固定 4，扩位宽时必须同步改这里。

### 38. `src/ir/Types/IntegerType.cpp`

文件职责：整数类型单例实现。

关键函数调用链图：

```text
getTypeBool/getTypeInt
  -> static singleton instance
```

修改建议：

- 若多线程初始化场景严格要求，可评估静态初始化顺序与线程安全。

常见坑位：

- 返回裸指针单例，禁止外部 delete。

### 39. `src/ir/Types/VoidType.h`

文件职责：void 类型声明。

关键函数调用链图：

```text
Instruction/Function/AST
  -> VoidType::getType
```

修改建议：

- 保持 void 语义单一，不要混入 label/none 语义。

常见坑位：

- 用 void 作为本应有结果值指令类型会破坏数据流。

### 40. `src/ir/Types/VoidType.cpp`

文件职责：void 单例实现。

关键函数调用链图：

```text
VoidType::getType
  -> static oneInstance
```

修改建议：

- 无特殊，保持稳定。

常见坑位：

- 同上，单例不可手动释放。

### 41. `src/ir/Types/LabelType.h`

文件职责：标签类型声明。

关键函数调用链图：

```text
Label 相关语义/扩展
  -> LabelType::getType
```

修改建议：

- 未来 CFG 显式建模时可把 LabelType 用作基本块值类型。

常见坑位：

- 与 VoidType 文本同为 "void"，调试输出上可能混淆。

### 42. `src/ir/Types/LabelType.cpp`

文件职责：标签类型单例实现。

关键函数调用链图：

```text
LabelType::getType
  -> static oneInstance
```

修改建议：

- 保持简洁即可。

常见坑位：

- 单例生命周期同上。

### 43. `src/ir/Types/FunctionType.h`

文件职责：函数类型声明与字符串化。

关键函数调用链图：

```text
Module::newFunction
  -> new FunctionType(ret, argTypes)
Function::getReturnType/getParams
  -> 输出签名
```

修改建议：

- 可增加可变参数/调用约定字段，便于后续 ABI 扩展。

常见坑位：

- 参数类型向量构造错误会导致签名和调用检查错配。

### 44. `src/ir/Types/PointerType.h`

文件职责：指针类型声明、深度与根类型推导。

关键函数调用链图：

```text
PointerType::get(pointee)
  -> StorageSet 缓存唯一化
  -> getRootType/getDepth
```

修改建议：

- 若引入数组/函数指针丰富语义，可在这里加强 pretty-print 与比较逻辑。

常见坑位：

- 哈希/相等策略基于 pointee 指针身份，不同实例语义等价时需注意。

---

## E. `src/ir/Values` 值对象文件

### 45. `src/ir/Values/ConstInt.h`

文件职责：整型常量值定义。

关键函数调用链图：

```text
IRGenerator::ir_leaf_node_uint
  -> Module::newConstInt
     -> ConstInt(val)
Module::outputIR
  -> 直接使用 getIRName() 字面值
```

修改建议：

- 可增加更宽整型支持（int64）并扩展常量池键类型。

常见坑位：

- `getIRName()` 返回字面量字符串，不带 `%/@` 前缀是预期行为。

### 46. `src/ir/Values/GlobalVariable.h`

文件职责：全局变量值定义。

关键函数调用链图：

```text
Module::newGlobalVariable
  -> GlobalVariable(type,name)
IRGenerator::ir_variable_declare (global init)
  -> setInitializer
BackendDriver
  -> isInBSSSection / getInitializerInt
```

修改建议：

- 若支持数组/复合初始化，可扩展 initializer 表达能力。

常见坑位：

- `setInitializer(0)` 会进入 BSS 语义，别误判为“已初始化 data 段”。

### 47. `src/ir/Values/LocalVariable.h`

文件职责：局部变量值定义。

关键函数调用链图：

```text
Function::newLocalVarValue
  -> LocalVariable
FrameLayoutBuilder
  -> slotOf(local)
InstructionSelector
  -> load/store local by fp+offset
```

修改建议：

- 增加 debug 名称与作用域打印接口，便于遮蔽变量排查。

常见坑位：

- `scope_level` 与名字遮蔽逻辑要配合 ScopeStack 使用。

### 48. `src/ir/Values/FormalParam.h`

文件职责：函数形参值定义。

关键函数调用链图：

```text
Module::newFunction(params)
  -> Function::params
Backend entry
  -> a0-a7 -> storeValue(param)
```

修改建议：

- 形参完整语义补全后，可增加“来源寄存器/栈位”元信息。

常见坑位：

- 形参目前 IR 生成未全接通，修改时要与 `ir_function_formal_params` 联动。

### 49. `src/ir/Values/MemVariable.h`

文件职责：内存型值定义。

关键函数调用链图：

```text
Function::newMemVariable
  -> MemVariable
后端/优化
  -> getMemoryAddr
```

修改建议：

- 若 spill 机制上线，可把 MemVariable 作为 spill 抽象桥梁。

常见坑位：

- base reg/offset 未设置就使用会读到默认值。

### 50. `src/ir/Values/RegVariable.h`

文件职责：寄存器值定义。

关键函数调用链图：

```text
(潜在寄存器分配阶段)
  -> RegVariable(type,name,regId)
  -> getRegId/getIRName
```

修改建议：

- 若推进 RA，建议统一寄存器编号映射表，避免 magic number 漫延。

常见坑位：

- 当前主流程未深度使用该类，扩展时要避免与 Instruction::regId 双轨冲突。

