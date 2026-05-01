# MiniLLVM IR 接口调用文档

本文档基于 `Johnny-son/compiler` 仓库  `src/ir` 代码整理。核心观点是：**IR 层可以被理解为一个库，`IRGenerator` 是这个库的主要调用方**。`IRGenerator` 负责遍历 AST，根据 AST 节点语义调用 `Module`、`Function`、`BasicBlock`、`IRBuilder`、`Value/User/Use`、`Type` 等接口，在内存中建立一棵 LLVM-like IR 对象图。

---

## 1. IR 的内存对象模型

这套 IR 不是直接拼字符串，也不是单纯线性三地址码，而是一套对象模型：

```text
Module
├── ConstInt 常量池
├── GlobalVariable 全局变量
├── Function 函数表
│   ├── FormalParam 形参
│   ├── BasicBlock 基本块列表
│   │   └── Instruction 指令列表
│   │       ├── AllocaInst
│   │       ├── LoadInst / StoreInst
│   │       ├── BinaryInst / ICmpInst / ZExtInst
│   │       ├── BranchInst / ReturnInst
│   │       ├── CallInst
│   │       ├── GetElementPtrInst
│   │       └── PhiInst
│   └── 局部 IR 名字分配器
└── ScopeStack 变量作用域栈
```

从值系统看：

```text
Value
├── ConstInt
├── GlobalVariable
├── FormalParam
├── BasicBlock
└── Instruction
    └── User
        └── operands: Use[]

Use
├── usee: Value*  // 被使用的值
└── user: User*   // 使用该值的对象
```

几个关键概念：

1. `Module` 是顶层容器，代表一个源文件或编译单元。
2. `Function` 保存函数签名、形参、基本块和局部命名状态。
3. `BasicBlock` 是 CFG 节点，里面顺序保存指令。
4. `Instruction` 同时是指令和 `Value`，所以一条 `add` 指令的结果可以直接作为下一条指令的操作数。
5. `Value/User/Use` 建立 def-use 链，为后续优化提供基础。
6. 当前变量 lowering 是 memory-based：变量名通常绑定到地址，读变量时 `load`，写变量时 `store`。
7. 源语言中的 `const` 属性保存在变量对应的地址 `Value` 上，IR 层在赋值前检查该属性并输出语义错误。

---

## 2. AST 如何一步步建立 IR

`IRGenerator` 持有：

```cpp
ast_node * root;
Module * module;
IRBuilder builder;
std::unordered_map<ast_operator_type, ast2ir_handler_t> ast2ir_handlers;
std::vector<std::pair<BasicBlock *, BasicBlock *>> loopTargets;
```

总体流程：

```text
IRGenerator::run()
└── ir_visit_ast_node(root)
    └── ir_compile_unit()
        ├── 第一阶段：predeclare_function()
        │   └── module->newFunction()
        ├── 第二阶段：翻译全局变量声明
        │   └── module->newVarValue()
        └── 第三阶段：翻译函数体
            ├── module->setCurrentFunction(func)
            ├── func->createBlock("entry")
            ├── builder.setInsertPoint(entry)
            ├── module->enterScope()
            ├── ir_function_formal_params()
            │   ├── createEntryAlloca()
            │   ├── module->bindValue()
            │   └── builder.createStore()
            ├── ir_block()
            └── module->leaveScope()
```

### 例子

源代码：

```c
int add(int a, int b) {
    int c;
    c = a + b;
    return c;
}
```

调用过程可以理解为：

```text
1. 预声明函数
   module->newFunction("add", i32, {FormalParam("a"), FormalParam("b")})

2. 建立函数体上下文
   func = module->findFunction("add")
   module->setCurrentFunction(func)
   entry = func->createBlock("entry")
   builder.setInsertPoint(entry)
   module->enterScope()

3. 形参进入局部槽位
   a.addr = createEntryAlloca(func, i32, "a")
   module->bindValue("a", a.addr)
   builder.createStore(paramA, a.addr)

   b.addr = createEntryAlloca(func, i32, "b")
   module->bindValue("b", b.addr)
   builder.createStore(paramB, b.addr)

4. 声明局部变量
   c.addr = createEntryAlloca(func, i32, "c")
   module->bindValue("c", c.addr)

5. 翻译 a + b
   a.addr = module->lookupValue("a")
   b.addr = module->lookupValue("b")
   lhs = emitRValue(a.addr)   // load
   rhs = emitRValue(b.addr)   // load
   sum = builder.createAdd(lhs, rhs, "addtmp")

6. 赋值 c = a + b
   c.addr = module->lookupValue("c")
   builder.createStore(sum, c.addr)

7. 返回 c
   ret = emitRValue(c.addr)   // load
   builder.createRet(ret)
```

---

# 3. Module 接口

`Module` 是 IRGenerator 的全局上下文，负责函数、全局变量、常量、作用域和当前函数状态。

## 3.1 `Module(std::string _name)`

**功能**：创建模块对象。

**输入**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `std::string` | `_name` | 模块名，通常对应源文件名 |

**输出**：构造出 `Module` 对象。

**实现逻辑**：初始化模块名、作用域栈、函数表、全局变量表、常量表，并注册内置函数，如 `putint`、`getint`。

---

## 3.2 `std::string toString() const` / `std::string toIRString()`

**功能**：把内存 IR 输出成文本 IR。

**输入**：无。

**输出**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `std::string` | 返回值 | 模块文本 IR |

**实现逻辑**：遍历全局变量和函数，调用对应对象的 `toString()` 拼接文本。

---

## 3.3 `void enterScope()`

**功能**：进入新作用域。

**输入**：无。

**输出**：无。

**实现逻辑**：转调 `ScopeStack::enterScope()`。函数体、语句块开始时调用。

---

## 3.4 `void leaveScope()`

**功能**：退出当前作用域。

**输入**：无。

**输出**：无。

**实现逻辑**：转调 `ScopeStack::leaveScope()`。语句块结束时调用。

---

## 3.5 `Function * getCurrentFunction()`

**功能**：获取当前正在翻译的函数。

**输入**：无。

**输出**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `Function *` | 返回值 | 当前函数；全局上下文为 `nullptr` |

**实现逻辑**：返回 `currentFunc`。

---

## 3.6 `void setCurrentFunction(Function * current)`

**功能**：设置当前函数上下文。

**输入**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `Function *` | `current` | 当前函数；全局上下文传 `nullptr` |

**输出**：无。

**实现逻辑**：更新 `currentFunc`。

---

## 3.7 `Function * newFunction(std::string name, Type * returnType, std::vector<FormalParam *> params = {}, bool builtin = false)`

**功能**：创建函数并注册到模块。

**输入**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `std::string` | `name` | 函数名 |
| `Type *` | `returnType` | 返回类型 |
| `std::vector<FormalParam *>` | `params` | 形参列表 |
| `bool` | `builtin` | 是否是内置函数 |

**输出**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `Function *` | 返回值 | 新建函数；失败返回 `nullptr` |

**实现逻辑**：检查函数重名和全局变量冲突；根据形参生成 `FunctionType`；创建 `Function`；给形参分配 IRName；插入函数映射表和函数列表。

---

## 3.8 `Function * createFunction(...)`

**功能**：`newFunction()` 的包装接口。

**输入/输出**：同 `newFunction()`。

**实现逻辑**：直接调用 `newFunction()`。

---

## 3.9 `Function * findFunction(std::string name)` / `Function * getFunction(const std::string & name)`

**功能**：按名字查找函数。

**输入**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `std::string` | `name` | 函数名 |

**输出**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `Function *` | 返回值 | 找到的函数；找不到为 `nullptr` |

**实现逻辑**：查 `funcMap`。

---

## 3.10 `std::vector<GlobalVariable *> & getGlobalVariables()`

**功能**：获取全局变量列表。

**输入**：无。

**输出**：全局变量顺序表引用。

**实现逻辑**：返回 `globalVariableVector`。

---

## 3.11 `std::vector<Function *> & getFunctionList()`

**功能**：获取函数列表。

**输入**：无。

**输出**：函数顺序表引用。

**实现逻辑**：返回 `funcVector`。

---

## 3.12 `ConstInt * newConstInt(int32_t intVal)`

**功能**：获取或创建整型常量。

**输入**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `int32_t` | `intVal` | 常量值 |

**输出**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `ConstInt *` | 返回值 | 常量对象 |

**实现逻辑**：先查常量池，存在则复用，不存在则创建并插入常量表。

---

## 3.13 `Value * newVarValue(Type * type, const std::string & name = "", int64_t lineno = -1)`

**功能**：创建变量对应的 IR 值。

**输入**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `Type *` | `type` | 变量类型 |
| `const std::string &` | `name` | 变量名 |
| `int64_t` | `lineno` | 定义行号，用于错误信息 |

**输出**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `Value *` | 返回值 | 变量值；失败为 `nullptr` |

**实现逻辑**：如果当前函数为空，创建全局变量；否则创建或绑定局部变量。当前新版函数体内更常见的做法是：`IRGenerator` 用 `createEntryAlloca()` 创建入口块 alloca，再用 `bindValue()` 绑定变量名。

---

## 3.14 `Value * findVarValue(std::string name)`

**功能**：查找变量。

**输入**：变量名。

**输出**：找到的 `Value *`，找不到为 `nullptr`。

**实现逻辑**：先查当前作用域栈，再查全局变量。

---

## 3.15 `bool bindValue(const std::string & name, Value * value)`

**功能**：把变量名绑定到某个 IR 值。

**输入**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `const std::string &` | `name` | 变量名 |
| `Value *` | `value` | 要绑定的值，通常是 alloca 地址 |

**输出**：是否绑定成功。

**实现逻辑**：向当前作用域登记名字和值的映射。

---

## 3.16 `Value * lookupValue(const std::string & name) const`

**功能**：查找当前可见的变量值。

**输入**：变量名。

**输出**：当前可见的 `Value *`。

**实现逻辑**：从当前作用域向外查找，再查全局变量。

---

## 3.17 `void outputIR(const std::string & filePath)`

**功能**：输出文本 IR 到文件。

**输入**：输出路径。

**输出**：无。

**实现逻辑**：生成文本 IR 后写入文件。

---

## 3.18 `void renameIR()`

**功能**：统一为 IR 值补名字。

**输入**：无。

**输出**：无。

**实现逻辑**：当前新版 MiniLLVM 路径中，命名主要已经在 `IRBuilder::insert()` 和 `Function::allocateLocalName()` 中完成。

---

# 4. Function 接口

`Function` 表示函数级 IR 容器。

## 4.1 `Function(std::string _name, FunctionType * _type, bool _builtin = false)`

**功能**：创建函数对象。

**输入**：函数名、函数类型、是否内置函数。

**输出**：构造出 `Function`。

**实现逻辑**：初始化 `GlobalValue`，缓存返回类型，保存内置标记。

---

## 4.2 `Type * getReturnType()`

**功能**：获取返回类型。

**输入**：无。

**输出**：`Type *`。

**实现逻辑**：返回 `returnType`。

---

## 4.3 `std::vector<FormalParam *> & getParams()`

**功能**：获取形参列表。

**输入**：无。

**输出**：形参列表引用。

**实现逻辑**：返回 `params`。`IRGenerator::ir_function_formal_params()` 会遍历它，为每个形参建立局部 alloca。

---

## 4.4 `bool isBuiltin()`

**功能**：判断是否内置函数。

**输入**：无。

**输出**：`bool`。

**实现逻辑**：返回 `builtIn`。

---

## 4.5 `void toString(std::string & str)`

**功能**：输出函数文本 IR。

**输入**：输出字符串引用。

**输出**：无显式返回值。

**实现逻辑**：如果是内置函数，不输出函数体；否则输出 `define ... { ... }`，并遍历基本块。

---

## 4.6 `BasicBlock * createBlock(const std::string & name = "")`

**功能**：创建基本块。

**输入**：基本块名字提示。

**输出**：新建的 `BasicBlock *`。

**实现逻辑**：名字为空则生成 `bbN`；清理非法字符；重名时追加后缀；创建 `BasicBlock` 并加入 `basicBlocks`。

---

## 4.7 `BasicBlock * getEntryBlock() const`

**功能**：获取入口块。

**输入**：无。

**输出**：第一个基本块；没有则为 `nullptr`。

**实现逻辑**：返回 `basicBlocks.front()`。

---

## 4.8 `const std::vector<BasicBlock *> & getBasicBlocks() const`

**功能**：获取基本块列表。

**输入**：无。

**输出**：基本块列表引用。

**实现逻辑**：返回 `basicBlocks`。

---

## 4.9 `std::string allocateLocalName(const std::string & hint = "")`

**功能**：分配函数内唯一局部 IR 名字。

**输入**：名字提示，例如 `addtmp`、`retval`、`a.addr`。

**输出**：唯一名字，例如 `%addtmp`、`%addtmp.1`、`%0`。

**实现逻辑**：如果 hint 为空，用计数器生成；否则补 `%` 前缀，清理非法字符，并通过 `usedLocalNames` 防止重名。

---

## 4.10 `int getMaxFuncCallArgCnt()` / `void setMaxFuncCallArgCnt(int count)`

**功能**：记录函数内最大调用实参数量。

**输入**：`set` 输入最大实参数量。

**输出**：`get` 返回当前记录值。

**实现逻辑**：读写 `maxFuncCallArgCnt`，主要服务后端调用约定和栈空间处理。

---

# 5. BasicBlock 接口

`BasicBlock` 是控制流图节点，也是指令序列容器。

## 5.1 `BasicBlock(Function * parent, std::string name)`

**功能**：创建基本块。

**输入**：所属函数、基本块名。

**输出**：构造出 `BasicBlock`。

**实现逻辑**：基本块本身是 `LabelType` 的 `Value`，保存父函数和名字。

---

## 5.2 `Function * getParent() const`

**功能**：获取所属函数。

**输入**：无。

**输出**：`Function *`。

**实现逻辑**：返回 `parent`。

---

## 5.3 `void appendInst(Instruction * inst)` / `void addInst(Instruction * inst)`

**功能**：追加指令。

**输入**：要追加的指令。

**输出**：无。

**实现逻辑**：如果指令为空或当前块已有 terminator，则不插入；否则加入 `instructions`。

---

## 5.4 `std::vector<Instruction *> & getInstructions()`

**功能**：获取可修改指令列表。

**输入**：无。

**输出**：指令列表引用。

**实现逻辑**：返回 `instructions`。`createEntryAlloca()` 直接使用这个接口把 alloca 插到入口块前部。

---

## 5.5 `Instruction * getTerminator() const`

**功能**：获取终结指令。

**输入**：无。

**输出**：最后一条指令如果是 terminator，则返回它；否则返回 `nullptr`。

**实现逻辑**：检查 `instructions.back()->isTerminator()`。

---

## 5.6 `bool hasTerminator() const`

**功能**：判断块是否已经结束。

**输入**：无。

**输出**：`bool`。

**实现逻辑**：返回 `getTerminator() != nullptr`。`IRBuilder::insert()` 依赖它防止在 `br` 或 `ret` 之后继续插指令。

---

## 5.7 `void toString(std::string & str) const`

**功能**：输出基本块文本 IR。

**输入**：输出字符串引用。

**输出**：无显式返回值。

**实现逻辑**：输出 `blockName:`，然后遍历指令并调用每条指令的 `toString()`。

---

# 6. IRBuilder 接口

`IRBuilder` 是 `IRGenerator` 创建指令的主要入口。它保存当前插入点 `currentBlock`。

## 6.1 `IRBuilder(Module * module)`

**功能**：创建 Builder。

**输入**：当前模块。

**输出**：构造出 `IRBuilder`。

**实现逻辑**：保存 `module`，当前插入块初始为空。

---

## 6.2 `void setInsertPoint(BasicBlock * block)`

**功能**：设置插入点。

**输入**：目标基本块。

**输出**：无。

**实现逻辑**：设置 `currentBlock = block`。

---

## 6.3 `BasicBlock * getInsertBlock() const`

**功能**：获取当前插入块。

**输入**：无。

**输出**：`BasicBlock *`。

**实现逻辑**：返回 `currentBlock`。

---

## 6.4 Builder 内部插入规则

所有 `createXXX()` 都走统一插入逻辑：

```text
insert(inst, name)
├── inst 为空 / currentBlock 为空 / currentBlock 已终结：删除 inst，返回 nullptr
├── 如果 inst 有结果值：调用 currentFunction()->allocateLocalName(name)
├── currentBlock->appendInst(inst)
└── 返回 inst
```

---

## 6.5 内存指令

### `AllocaInst * createAlloca(Type * type, const std::string & name = "")`

**功能**：创建 alloca。

**输入**：被分配类型、名字提示。

**输出**：`AllocaInst *`，结果是地址值。

**实现逻辑**：构造 `AllocaInst`，名字通常使用 `name + ".addr"`，插入当前块。

### `LoadInst * createLoad(Value * ptr, const std::string & name = "")`

**功能**：从地址读取值。

**输入**：指针值、结果名提示。

**输出**：`LoadInst *`，指令本身是读取结果。

**实现逻辑**：构造 `LoadInst(currentFunction(), ptr)` 并插入当前块。

### `StoreInst * createStore(Value * value, Value * ptr)`

**功能**：写内存。

**输入**：要写入的值、目标地址。

**输出**：`StoreInst *`，无结果值。

**实现逻辑**：构造 `StoreInst(currentFunction(), value, ptr)` 并插入当前块。

---

## 6.6 算术指令

### `createAdd` / `createSub` / `createMul` / `createSDiv` / `createSRem`

**功能**：创建整数加、减、乘、有符号除、有符号取余。

### `createFAdd` / `createFSub` / `createFMul` / `createFDiv`

**功能**：创建单精度浮点加、减、乘、除。

**输入**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `Value *` | `lhs` | 左右值 |
| `Value *` | `rhs` | 右右值 |
| `std::string` | `name` | 结果名提示 |

**输出**：`BinaryInst *`。

**实现逻辑**：用对应 `BinaryInst::Op` 构造二元指令并插入。

---

## 6.7 比较指令

### `createICmpEQ` / `createICmpNE` / `createICmpSLT` / `createICmpSLE` / `createICmpSGT` / `createICmpSGE`

**功能**：创建整数比较，结果通常是 `i1`。

**输入**：左右操作数、结果名提示。

**输出**：`ICmpInst *`。

**实现逻辑**：用对应 `ICmpInst::Predicate` 构造比较指令并插入。

### `createFCmpOEQ` / `createFCmpONE` / `createFCmpOLT` / `createFCmpOLE` / `createFCmpOGT` / `createFCmpOGE`

**功能**：创建浮点比较，结果同样是 `i1`。

**实现逻辑**：用有序浮点比较谓词构造 `FCmpInst` 并插入。

---

## 6.8 类型转换

### `ZExtInst * createZExt(Value * value, Type * targetType, const std::string & name = "")`

**功能**：零扩展，例如把 `i1` 扩展成 `i32`。

**输入**：原值、目标类型、结果名提示。

**输出**：`ZExtInst *`。

**实现逻辑**：构造 `ZExtInst` 并插入当前块。

### `createSIToFP` / `createFPToSI`

**功能**：创建 `int -> float` 和 `float -> int` 的隐式转换指令。

**实现逻辑**：构造 `CastInst`，分别输出 `sitofp` 和 `fptosi`。

---

## 6.9 地址计算

### `GetElementPtrInst * createGEP(Value * basePtr, const std::vector<Value *> & indices, const std::string & name = "")`

**功能**：根据基址和索引计算元素地址。

**输入**：基地址、索引列表、结果名提示。

**输出**：`GetElementPtrInst *`。

**实现逻辑**：构造 GEP 指令并插入当前块。

---

## 6.10 函数调用

### `CallInst * createCall(Function * callee, const std::vector<Value *> & args, const std::string & name = "")`

**功能**：创建函数调用。

**输入**：被调函数、实参右值列表、结果名提示。

**输出**：`CallInst *`；非 void 调用本身是返回值。

**实现逻辑**：用 `callee->getReturnType()` 作为结果类型，构造 call 指令并插入。

---

## 6.11 Phi

### `PhiInst * createPhi(Type * type, const std::string & name = "")`

**功能**：创建 phi 指令。

**输入**：结果类型、名字提示。

**输出**：`PhiInst *`。

**实现逻辑**：构造 `PhiInst` 并插入。当前变量生成主路径是 memory-based，phi 更多为后续 SSA 化或优化预留。

---

## 6.12 控制流

### `BranchInst * createBr(BasicBlock * target)`

**功能**：创建无条件跳转。

**输入**：目标基本块。

**输出**：`BranchInst *`，terminator。

**实现逻辑**：构造无条件 `BranchInst` 并插入当前块。

### `BranchInst * createCondBr(Value * cond, BasicBlock * trueBlock, BasicBlock * falseBlock)`

**功能**：创建条件跳转。

**输入**：`i1` 条件值、真分支块、假分支块。

**输出**：`BranchInst *`，terminator。

**实现逻辑**：构造条件 `BranchInst` 并插入当前块。

---

## 6.13 返回

### `ReturnInst * createRet(Value * value)`

**功能**：创建带返回值的 `ret`。

**输入**：返回值。

**输出**：`ReturnInst *`，terminator。

**实现逻辑**：构造 `ReturnInst(currentFunction(), value)` 并插入。

### `ReturnInst * createRetVoid()`

**功能**：创建 `ret void`。

**输入**：无。

**输出**：`ReturnInst *`，terminator。

**实现逻辑**：构造无返回值 ReturnInst 并插入。

---

# 7. Value / User / Use 接口

## 7.1 Value

### `getName()` / `setName()`

**功能**：读写源码级名字。

**输入**：`setName` 输入名字。

**输出**：`getName` 返回名字。

**实现逻辑**：读写 `name` 字段。

### `getIRName()` / `setIRName()`

**功能**：读写文本 IR 名字。

**输入**：`setIRName` 输入 IR 名字。

**输出**：`getIRName` 返回 IR 名字。

**实现逻辑**：读写 `IRName` 字段。

### `Type * getType()`

**功能**：获取值类型。

**输入**：无。

**输出**：`Type *`。

**实现逻辑**：返回 `type` 字段。

### `setConstValue(bool isConst = true)` / `isConstValue()`

**功能**：记录并查询该 `Value` 是否对应源语言中的 `const` 变量。

**输入**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `bool` | `isConst` | 是否为 const，默认标记为 true |

**输出**：

| 类型 | 名称 | 含义 |
|---|---|---|
| `bool` | `isConstValue()` 返回值 | 当前值是否是 const 变量对应的地址 |

**实现逻辑**：读写 `Value` 基类中的 `constValue` 字段。当前 memory-based lowering 中，变量名绑定到 `AllocaInst` 或 `GlobalVariable` 这类地址值，因此把 const 属性挂在该地址 `Value` 上，赋值时检查左值地址即可。

### `addUse()` / `removeUse()` / `removeUses()`

**功能**：维护被使用列表。

**输入**：`Use *`。

**输出**：无。

**实现逻辑**：增删 `uses` 列表。

### `replaceAllUseWith(Value * new_val)`

**功能**：把所有使用当前值的地方替换为新值。

**输入**：新值。

**输出**：无。

**实现逻辑**：遍历 use 列表，调用 use 的替换逻辑。优化阶段会非常依赖这个接口。

---

## 7.2 User

`User` 是使用其他 Value 的 Value，典型子类是 Instruction。

### `getOperands()`

**功能**：获取 `Use *` 操作数列表。

**输出**：`std::vector<Use *> &`。

### `getOperandsValue()`

**功能**：获取操作数对应的 `Value *` 列表。

**输出**：`std::vector<Value *>`。

### `getOperandsNum()`

**功能**：获取操作数数量。

**输出**：`int32_t`。

### `getOperand(int32_t pos)`

**功能**：获取指定位置操作数。

**输入**：下标。

**输出**：`Value *`。

### `setOperand(int32_t pos, Value * val)`

**功能**：替换指定位置操作数。

**输入**：下标、新值。

**输出**：无。

**实现逻辑**：通过 `Use::setUsee()` 维护旧值和新值的 use 列表。

### `addOperand(Value * val)`

**功能**：增加操作数。

**输入**：被使用的值。

**输出**：无。

**实现逻辑**：创建 `Use(val, this)`，加入 operands，并调用 `val->addUse(use)`。

### `removeOperand(...)` / `clearOperands()` / `replaceOperand(Value * val, Value * newVal)`

**功能**：删除、清空、替换操作数。

**实现逻辑**：通过 `Use::remove()` 和 `Use::setUsee()` 同步维护双向关系。

---

## 7.3 Use

### `Use(Value * _value, User * _user)`

**功能**：构造 def-use 边。

**输入**：被使用值、使用者。

**输出**：构造出 Use。

**注意**：构造函数不会自动插入 `Value::uses` 或 `User::operands`，通常应通过 `User::addOperand()` 使用。

### `User * getUser() const`

**功能**：获取使用者。

**输出**：`User *`。

### `Value * getUsee() const`

**功能**：获取被使用值。

**输出**：`Value *`。

### `void setUsee(Value * newVal)`

**功能**：把 Use 指向新值。

**输入**：新值。

**输出**：无。

**实现逻辑**：从旧值 use 列表删除，再加入新值 use 列表。

### `void remove()`

**功能**：断开 def-use 边。

**输出**：无。

**实现逻辑**：从 user 和 usee 两侧移除引用。

---

# 8. Type 接口

`Type` 是类型基类，支持 `FloatTyID`、`VoidTyID`、`LabelTyID`、`TokenTyID`、`IntegerTyID`、`FunctionTyID`、`PointerTyID`、`ArrayTyID`。

## 8.1 类型判断

### `isVoidType()` / `isLabelType()` / `isFunctionType()` / `isIntegerType()` / `isFloatType()` / `isPointerType()` / `isArrayType()`

**功能**：判断类型种类。

**输入**：无。

**输出**：`bool`。

**实现逻辑**：比较内部 `TypeID`。

## 8.2 `toString()`

**功能**：输出类型文本。

**输入**：无。

**输出**：例如 `i32`、`i1`、`float`、`void`、`label`、`i32*`。

**实现逻辑**：由具体子类实现。

## 8.3 `getSize()`

**功能**：获取类型大小。

**输入**：无。

**输出**：大小数值。

**实现逻辑**：由具体类型决定，后端分配空间时使用。

## 8.4 常见类型类

| 类型类 | 功能 | 用途 |
|---|---|---|
| `IntegerType` | 整数类型，如 `i32`、`i1` | 变量、表达式、条件 |
| `FloatType` | 单精度浮点类型，如 `float` | 浮点变量、表达式、函数参数 |
| `VoidType` | void | void 函数 |
| `LabelType` | label | 基本块 |
| `PointerType` | 指针 | alloca、global、load/store、GEP |
| `ArrayType` | 数组 | 数组变量和元素访问 |
| `FunctionType` | 函数签名 | Function 类型 |

---

# 9. Instruction 接口

`Instruction` 继承自 `User`，因此每条指令既能使用操作数，也能作为值被使用。

## 9.1 `Instruction(Function * _func, Type * _type)`

**功能**：创建指令基类部分。

**输入**：所属函数、结果类型。

**输出**：构造出指令。

**实现逻辑**：初始化 `User(_type)`，记录所属函数。

## 9.2 `virtual void toString(std::string & str)`

**功能**：输出指令文本。

**输入**：输出字符串引用。

**输出**：无显式返回值。

**实现逻辑**：具体指令类重写。

## 9.3 `bool hasResultValue()`

**功能**：判断指令是否产生结果值。

**输入**：无。

**输出**：`bool`。

**实现逻辑**：void 类型指令通常无结果；非 void 指令需要 IRName。

## 9.4 `virtual bool isTerminator() const`

**功能**：判断是否是基本块终结指令。

**输入**：无。

**输出**：`bool`。

**实现逻辑**：基类默认 false；`BranchInst`、`ReturnInst` 为 true。

## 9.5 `isDead()` / `setDead()`

**功能**：读写死指令标记。

**输入**：是否 dead。

**输出**：`isDead()` 返回 `bool`。

**实现逻辑**：维护 dead 标志，供优化或清理使用。

---

# 10. 具体指令类

这些类一般由 `IRBuilder` 间接创建。

| 指令类 | 功能 | 输入 | 输出 |
|---|---|---|---|
| `AllocaInst` | 分配局部槽位 | 函数、被分配类型、名字 | 地址值 |
| `LoadInst` | 从地址读取值 | 函数、指针 | 读取结果 |
| `StoreInst` | 写地址 | 函数、值、指针 | void |
| `BinaryInst` | 算术运算 | op、lhs、rhs | 运算结果 |
| `ICmpInst` | 整数比较 | predicate、lhs、rhs | i1 |
| `FCmpInst` | 浮点比较 | predicate、lhs、rhs | i1 |
| `ZExtInst` | 零扩展 | value、target type | 扩展结果 |
| `CastInst` | int/float 转换 | op、value、target type | 转换结果 |
| `GetElementPtrInst` | 地址计算 | basePtr、indices | 地址值 |
| `CallInst` | 函数调用 | callee、args | 返回值或 void |
| `PhiInst` | SSA 合流 | type、incoming | 合流结果 |
| `BranchInst` | 跳转 | target 或 cond+targets | terminator |
| `ReturnInst` | 返回 | 可选返回值 | terminator |

---

# 11. IRGenerator 内部辅助接口

这些不是 IR 库公共接口，但它们解释了 AST 与 IR 库之间的衔接。

## 11.1 `Value * emitRValue(Value * value, const std::string & name = "")`

**功能**：把可能是左值地址的值变成右值。

**输入**：待转换值、名字提示。

**输出**：右值。

**实现逻辑**：

```text
value == nullptr          -> nullptr
value 是 ConstInt/ConstFloat -> 原样返回
value 是数组对象地址      -> decayArrayToPointer(value)
value 是数组型 GlobalVariable -> decayArrayToPointer(value)
value 是普通 GlobalVariable -> builder.createLoad(value, name)
value 的类型是 PointerType -> builder.createLoad(value, name)
其他情况                 -> 原样返回
```

---

## 11.2 `Value * emitCondValue(Value * value)`

**功能**：把表达式值规范化为条件跳转可用的 `i1`。

**输入**：表达式值。

**输出**：`i1` 条件值。

**实现逻辑**：先 `emitRValue(value, "cond")`；如果已经是 `i1`，直接返回；如果是 `float`，生成 `fcmp one value, 0.0`；否则生成 `icmp ne value, 0`。

## 11.3 `Value * convertValueToType(Value * value, Type * targetType, const std::string & name = "")`

**功能**：按目标类型插入必要的隐式转换。

**实现逻辑**：

```text
目标类型相同        -> 原样返回
i1 -> i32          -> zext
i1/i32 -> float    -> zext(必要时) + sitofp
float -> i32       -> fptosi
其它不兼容类型      -> nullptr
```

---

## 11.4 `AllocaInst * createEntryAlloca(Function * func, Type * type, const std::string & name)`

**功能**：在函数入口块前部创建 alloca。

**输入**：函数、类型、变量名。

**输出**：`AllocaInst *`。

**实现逻辑**：

```text
entry = func->getEntryBlock()
如果 entry 不存在：func->createBlock("entry")
new AllocaInst(func, type)
设置 name 和 IRName
找到 entry 中连续 alloca 之后的位置
插入新 alloca
返回 alloca
```

**意义**：把所有局部变量槽位集中在 entry 块开头，便于后续 SSA 化或 mem2reg。

---

## 11.5 数组辅助接口

数组相关辅助接口负责把前端保留的维度和初始化列表落成 LLVM 风格 IR。

```text
getDeclDimsNode() / getDeclInitNode()
  -> 从声明节点中取可选 AST_OP_ARRAY_DIMS 和 initVal

buildArrayTypeFromDims()
  -> 对每个维度做常量表达式求值
  -> 由内向外构造 ArrayType

buildFormalParamType()
  -> 标量形参保持原基本类型
  -> int a[] 降成 i32*
  -> float a[] 降成 float*
  -> int/float a[][N] 降成 [N x elem]*

isArrayObjectAddress()
  -> 判断 Value 是否是数组对象地址

decayArrayToPointer()
  -> 对数组对象地址生成 getelementptr 0, 0
  -> 用于数组作为函数实参或子数组作为实参

fillArrayInitializer()
  -> 按数组类型展开嵌套初始化列表
  -> 缺省元素补 0

emitArrayInitializerStores()
  -> 局部数组初始化：逐元素 GEP + store

buildGlobalArrayInitializer()
  -> 全局数组初始化：生成 LLVM 聚合常量文本

buildScalarInitializerText()
  -> 全局标量初始化：按 int/float 类型生成常量文本
```

---

# 12. 典型 AST 节点翻译方式

## 12.1 编译单元

```text
ir_compile_unit()
├── module->setCurrentFunction(nullptr)
├── 预声明所有函数
├── 翻译全局变量
└── 翻译函数体
```

## 12.2 函数

```text
ir_function_define()
├── 检查不允许嵌套函数
├── 确保函数已预声明
└── ir_function_body()
```

函数体中创建 entry block，设置 builder 插入点，进入作用域，翻译形参和语句块。

## 12.3 形参

```text
预声明阶段：
    buildFormalParamType(paramNode)
    int a[]      -> i32*
    int a[][10]  -> [10 x i32]*

函数体阶段：
for param in currentFunc->getParams():
    local = createEntryAlloca(currentFunc, param->getType(), param->getName())
    module->bindValue(param->getName(), local)
    builder.createStore(param, local)
```

## 12.4 标识符

```text
val = module->lookupValue(name)
node->val = val
```

注意：这个 `val` 通常是地址，需要参与运算时再 `emitRValue()`。

## 12.5 字面量

```text
node->val = module->newConstInt(integer_val)
```

## 12.6 二元表达式

```text
left = ir_visit_ast_node(lhs)
right = ir_visit_ast_node(rhs)
lhsVal = emitRValue(left->val)
rhsVal = emitRValue(right->val)
node->val = builder.createAdd/Sub/Mul/SDiv/SRem(...)
```

关系运算一般是：

```text
icmp -> i1
zext -> i32 0/1
```

## 12.7 赋值

```text
left = ir_visit_ast_node(lhs)     // 地址
如果 left->val 是 constValue，报 E1303
right = ir_visit_ast_node(rhs)
value = emitRValue(right->val)
builder.createStore(value, left->val)
node->val = value
```

## 12.7.1 常量声明

```text
AST_OP_CONST_DECL -> ir_const_declaration()
ir_const_declaration() 先标记 node->isConst = true
再复用 ir_variable_declare()
```

局部常量仍然按变量槽位生成：

```text
addr = createEntryAlloca(type, name)
addr->setConstValue(true)
builder.createStore(initValue, addr)
```

全局常量当前也复用 `GlobalVariable` 的存储形式：

```text
global = module->newVarValue(type, name)
global->setConstValue(true)
global->setInitializer(initValue)
```

语义约束：

```text
const 声明没有初始化值 -> E1304
对 const 左值重新赋值 -> E1303
```

## 12.7.2 数组声明和初始化

数组声明节点仍然复用 `AST_OP_VAR_DECL` / `AST_OP_CONST_DECL`，但孩子中会额外带一个 `AST_OP_ARRAY_DIMS`：

```text
decl
├── type
├── id
├── arrayDims
└── initVal?
```

局部数组：

```text
type = buildArrayTypeFromDims(arrayDims, baseType)
addr = createEntryAlloca(func, type, name)
module->bindValue(name, addr)
如果有初始化：
    fillArrayInitializer()
    对每个元素：
        elemAddr = createGEP(addr, {0, i, j, ...})
        builder.createStore(valueOrZero, elemAddr)
```

全局数组：

```text
type = buildArrayTypeFromDims(arrayDims, baseType)
global = module->newVarValue(type, name)
如果有初始化：
    buildGlobalArrayInitializer() 生成聚合常量
否则：
    zeroinitializer
```

数组常量同样会给数组地址 `Value` 标记 `constValue`，数组元素访问生成的 GEP 会继承这个标记，因此 `const int a[2]; a[0] = 1;` 会在赋值阶段报 `E1303`。

## 12.7.3 数组访问

数组访问节点是 `AST_OP_ARRAY_ACCESS`，节点名保存数组名，孩子是各维下标表达式。

局部或全局数组：

```text
a[i][j]
base = lookupValue("a")
idx0 = emitRValue(i)
idx1 = emitRValue(j)
addr = createGEP(base, {0, idx0, idx1})
```

函数形参数组：

```text
int f(int a[][10]) { return a[i][j]; }
baseSlot = lookupValue("a")       // [10 x i32]** 的局部槽位
base = load baseSlot              // [10 x i32]*
addr = createGEP(base, {idx0, idx1})
```

读数组元素时，`emitRValue(addr)` 会继续生成 `load`。如果访问结果仍是子数组，例如 `buf[0]`，作为函数实参时会通过 `decayArrayToPointer()` 生成指向首元素的指针。

## 12.8 return

```text
如果有表达式：
    retVal = emitRValue(expr->val)
    builder.createRet(retVal)
否则：
    builder.createRetVoid()
```

## 12.9 if

```text
condbr cond, thenBlock, elseBlock/endBlock

thenBlock:
    then body
    br endBlock   // 如果没有 terminator

elseBlock:
    else body
    br endBlock   // 如果存在 else 且没有 terminator

endBlock:
    后续代码
```

关键接口：`createBlock()`、`emitCondValue()`、`createCondBr()`、`hasTerminator()`、`createBr()`。

## 12.10 while / break / continue

```text
br condBlock

condBlock:
    condbr cond, bodyBlock, endBlock

bodyBlock:
    body
    br condBlock

endBlock:
    后续代码
```

循环上下文：

```text
loopTargets.push_back({condBlock, endBlock})
continue -> br condBlock
break    -> br endBlock
loopTargets.pop_back()
```

## 12.11 逻辑与 / 逻辑或

短路逻辑不用普通二元指令，而是生成基本块和条件跳转。

`&&`：

```text
result = alloca i32
store 0, result
left false -> end
right false -> end
trueBlock: store 1, result
end: load result
```

`||`：

```text
result = alloca i32
store 1, result
left true -> end
right true -> end
falseBlock: store 0, result
end: load result
```

---

# 13. 扩展 IR 库时的建议

## 13.1 新表达式

```text
新增或复用 Instruction
在 IRBuilder 增加 createXXX()
在 IRGenerator handler 中调用 builder
```

## 13.2 新控制流

```text
Function::createBlock()
builder.setInsertPoint()
emitCondValue()
builder.createCondBr() / createBr()
BasicBlock::hasTerminator()
```

## 13.3 新变量形态，如数组

```text
扩展 Type
左值阶段生成地址，例如 createGEP()
右值阶段 emitRValue() 生成 load
赋值阶段 createStore()
```

## 13.4 优化

```text
遍历 Function / BasicBlock / Instruction
依赖 Value/User/Use 的 def-use 链
使用 replaceAllUseWith() 替换值
使用 dead 标记或删除接口清理指令
```

---

# 14. 总结

这套 IR 的调用主线是：

```text
AST
-> IRGenerator 分发 AST 节点
-> Module 管理全局上下文和符号
-> Function 创建函数与基本块
-> BasicBlock 容纳顺序指令
-> IRBuilder 创建并插入指令
-> Value/User/Use 串起数据流
-> Branch/Return 串起控制流
-> toString/toIRString 输出文本 IR
```

一句话概括：**IR 层是库，`IRGenerator` 是调用者；`IRBuilder` 是造指令的门面；`Module/Function/BasicBlock` 是组织结构；`Value/User/Use` 是数据流骨架；当前变量生成采用 memory-based lowering，表达式和控制流采用 LLVM-like CFG。**
