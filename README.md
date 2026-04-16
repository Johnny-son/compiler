# a simple compiler

contesttestcases 默认放 test 目录下, 方便测试。

## 运行指令
构建:  
```sh
cmake -S . -B build
cmake --build build -j
```

排除后端构建:  
```sh
cmake -S . -B build-nobackend -DMINIC_ENABLE_BACKEND=OFF
cmake --build build-nobackend -j
```

生成 AST 图片:  
```sh
cd build && ./compiler -S -T -A -o ./output.png ../test/contesttestcases/2023_function/2023_func_00_main.c
```

生成 IR 代码:  
```sh
cd build && ./compiler -S -I -A -o ./output.ll ../test/contesttestcases/2023_function/2023_func_00_main.c
```

生成 RISCV64 代码:  
```sh
cd build && ./compiler -S -A -o ./output.s ../test/contesttestcases/2023_function/2023_func_00_main.c
```

脚本测试:  
可以指定目录或文件, 选择 ast(检查能否生图) / ir / asm 测试, 显示失败测例  
```sh
bash scripts/run_ci_tests.sh -ast 2023_function
bash scripts/run_ci_tests.sh -ir 2023_func_00_main.c
bash scripts/run_ci_tests.sh -asm 2023_func_01_var_defn2.c 2023_func_02_var_defn3.c
bash scripts/run_ci_tests.sh -asm 2023_function --show-failures
```

或者 --all 选项, 这会读取 ci_test_plan.txt (测试前带 # 是跳过):  
```sh
./scripts/run_ci_tests.sh --all
```