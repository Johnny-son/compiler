# docs

存放开发中的设计文档  

## 参考文档
[llvm参考文档](https://evian-zhang.github.io/llvm-ir-tutorial/)  

## 设计思路

### log信息
位置: common/Status.cpp/h  
使用方法:  
```cpp
Status::Error();
```

### 模块
frontend -> ir -> backend。
frontend: inputFile -> ASTGenerator -> CSTVisitor -> AST  
ir: astRoot -> IRGenerator -> module  
backend: module -> CodeGenerator -> asm  

目前基本是照搬exp4, 然后删了没用的东西, 前端改了下位置,可读性好一点  
ir那里应该是改成llvm了, 但是应该比较史, 不过能过测试
后端搬过来但还没改成riscv64, 所以没交
ci的话等明天看看吧, ai用完了