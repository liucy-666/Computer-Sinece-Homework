## 快速开始

```
python3 init_bomb.py 202401

cd level1
ar -x bomb1.a          # 解包炸弹
gcc -shared -o libbomb.so lv1.o   # 尝试直接转换
python3 ../bomb_tester.py . libbomb.so    # 测试是否通过
```

## 五关详解

### Level 1: 蒸汽时代 (Steam Age) — 10 分

**炸弹描述**

最简单的关卡。炸弹中的函数是纯算术运算，没有全局变量、没有静态数据、没有函数指针。所有重定位都是 `R_X86_64_PC32`（PC 相对寻址），在共享库中天然安全。

**拆弹方法**

```bash
ar -x bomb1.a              # 解包
readelf -r lv1.o           # 查看重定位（只有 PC-relative）
gcc -shared -o libbomb.so lv1.o   # 直接转换
python3 ../bomb_tester.py . libbomb.so    # 验证
```

**关键观察**

`readelf -r` 看到的只有 `.eh_frame` 节区的 PC-relative 重定位，`.text` 节区中没有重定位——纯函数不引用任何外部符号。

---

### Level 2: 全局危机 (Global Crisis) — 20 分

**炸弹描述**

炸弹中有代码访问全局变量并取其地址。未使用 `-fPIC` 编译时，取地址操作生成 **`R_X86_64_32`** 或 **`R_X86_64_32S`** 重定位——它们在指令中嵌入了一个 **32 位绝对地址**。

共享库可能被加载到 4GB 以上的地址空间，32 位绝对地址无法覆盖这个范围。因此链接器拒绝将这类 `.o` 链接为共享库。
`objdump -d lv2.o` 反汇编，对照重定位表找到对应指令
-
- **解决思路**： 链接器拒绝的原因是：共享库加载地址可能超过 4GB，32 位地址放不下

1. 解包炸弹，确认链接器拒绝
2. 用 `readelf -r` 定位问题重定位
3. 分析原函数逻辑（反汇编或看 test 期望值推理）
4. 新建 `.c` 文件，用 `-fPIC` 编译，实现相同行为，用 `-fPIC` 重新编译。PIC 代码会改用 `R_X86_64_GOTPCREL` 通过 GOT 间接访问，不再嵌入绝对地址
5. 重新链接为 `.so`，用 `bomb_tester.py` 验证


### Level 3: 数据陷阱 (Data Trap) — 20 分

**炸弹描述**

炸弹中的代码访问一个 **全局变量**。即使编译器使用了 PC 相对寻址（`R_X86_64_PC32`），链接器仍然拒绝将其链接为共享库。

问题在于 **符号介入（symbol interposition）**：共享库中的全局符号可能被其他同名符号覆盖，PC 相对引用无法应对这种情况。

**解决思路**
- 链接器拒绝的原因是**符号介入（symbol interposition）**：共享库中的全局符号可能被其他同名符号覆盖。PC 相对引用无法处理这种情况，但如果是 `LOCAL` 符号（等价于源码中的 `static`），链接器就不会拒绝

**拆弹思路**

1. 解包，确认链接器拒绝
2. 用 `readelf -s` 查看符号绑定，`readelf -r` 查看重定位类型和符号
3. 搜索 `objcopy` 的手册，找到能改变符号绑定的选项
4. 用该选项将问题符号从 `GLOBAL` 改为 `LOCAL`
5. 链接为 `.so`，用 `bomb_tester.py` 验证


### Level 4: 二进制手术师 (Binary Surgeon) — 25 分

**炸弹描述**

炸弹中的代码定义了一个 `const` 指针指向静态变量。`const` 指针被放入了 **`.rodata`**（只读数据节区），但初始化需要在运行时确定地址——这就产生了位于**只读节区**的重定位。

链接为共享库时，动态链接器需要修改这个值，但 `.rodata` 是只读的，冲突产生了 **TEXTREL**（代码段重定位）。

**拆弹步骤**

```bash
# 第一步：解包，确认问题
ar -x bomb4.a
gcc -shared -o libbomb.so lv4.o    # 会有 TEXTREL 警告
readelf -d libbomb.so | grep TEXTREL

# 第二步：分析节区结构
readelf -S lv4.o                    # 查看节区标志
readelf -r lv4.o                    # 找到 .rodata 中的重定位

# 第三步：完成 tools/patch_o.py 中的 TODO
# 实现：
#   1. read_u16 / read_u32 / read_u64 — 从 ELF 二进制读取数据
#   2. get_section_name — 从字符串表查找节区名
#   3. find_sections_with_textrel — 检测 TEXTREL 风险
#   4. fix_textrel — 对只读数据节区添加 SHF_WRITE 标志

# 第四步：用完成的修补工具修复
python3 ../tools/patch_o.py lv4.o lv4_fixed.o
gcc -shared -o libbomb.so lv4_fixed.o
python3 ../bomb_tester.py . libbomb.so
```

> ps：不要给 `.text` 节区添加可写标志，这会造成 W+X 安全风险！


### Level 5: 终极链接炸弹 (Ultimate Link Bomb) — 50 分

**炸弹描述**

Boss 关。`bomb5.a` 包含 **3 个 `.o` 文件**，每个有不同类型的问题。

**线索**

- 用 `ar -x bomb5.a` 解包，得到三个 `.o` 文件
- 对每个文件运行 `readelf -r`，判断它们各自的问题类型：
  - 重定位数量为 0 或只有 `.eh_frame`,安全，可直接使用
  - 包含 `R_X86_64_PC32` 到全局符号,需要 Level 3 的技术
  - 包含 `R_X86_64_32/32S`,需要 Level 2 的技术
  - `.rodata` 节区包含重定位,需要 Level 4 的技术

**拆弹思路**

1. 解包，分析每个文件的重定位和符号表
2. 对安全文件——直接保留
3. 对有符号绑定问题的文件——用 `objcopy` 在二进制层面修复
4. 对有 TEXTREL 风险的文件——用 `patch_o.py` 修补节区标志
5. 对包含绝对地址的文件——重写为 fPIC 代码重新编译
6. 将修复后的所有 `.o` 链接为 `.so`，用 `bomb_tester.py` 验证

---

## 实验过程中用到的相关命令和工具

### readelf — 读取 ELF 信息

```bash
readelf -a file.o       # 全部信息
readelf -h file.o       # ELF 头部
readelf -S file.o       # 节区头部表
readelf -r file.o       # 重定位表
readelf -s file.o       # 符号表
readelf -d lib.so       # 动态段
```

### objdump — 反汇编

```bash
objdump -d file.o              # 反汇编
objdump -d -r file.o           # 反汇编 + 显示重定位
objdump -s -j .rodata file.o   # 显示节区原始字节
```

### objcopy — 修改 ELF 文件

```bash
objcopy --localize-symbol=<name> in.o out.o     # 全局→局部
objcopy --globalize-symbol=<name> in.o out.o    # 局部→全局
objcopy --strip-symbol=<name> in.o out.o        # 删除符号
```

### ar — 静态库操作

```bash
ar -t lib.a             # 列出包含的文件
ar -x lib.a             # 解包
```

### 链接共享库

```bash
gcc -shared -o lib.so *.o             # 基本链接
gcc -shared -fPIC -o lib.so *.o       # 所有 PIC
```