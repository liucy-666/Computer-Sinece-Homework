# 🔬 链接炸弹实验学习报告 — mystudy.md

> **学生**: 720024 | **种子**: 3920841946 | **总分**: 125/125

---

## 前置知识：ELF 文件格式与链接基础

### ELF 文件结构概览

ELF (Executable and Linkable Format) 是 Linux 下可执行文件、目标文件和共享库的标准格式。一个 ELF 文件由以下核心部分组成：

1. **ELF Header**: 位于文件开头 64 字节，描述文件类型 (ET_REL/ET_DYN/ET_EXEC)、目标架构、程序头表和节区头表的位置
2. **Section Header Table**: 描述每个节区 (.text, .data, .rodata 等) 的名称、类型、标志 (flags)、在文件中的偏移和大小
3. **Program Header Table** (仅可执行文件和共享库): 描述运行时内存映射
4. **Sections**: 
   - `.text`: 代码段，flags=AX (allocate + execute)
   - `.data`: 已初始化全局变量，flags=WA (write + allocate)
   - `.rodata`: 只读数据 (常量字符串、const 变量)，flags=A (allocate only)
   - `.bss`: 未初始化全局变量，flags=WA
   - `.symtab` / `.strtab`: 符号表和字符串表
   - `.rela.text` / `.rela.dyn`: 重定位表

### 重定位类型详解

重定位 (Relocation) 是链接过程的核心机制。当编译器不知道符号的最终地址时，它在指令中预留占位符 (通常是 0)，并在重定位表中记录需要修补的位置和符号信息。链接器在最终链接时计算符号地址并填入正确的值。

x86-64 ELF 的关键重定位类型：

| 类型 | 含义 | 计算公式 | 常见场景 |
|------|------|---------|---------|
| `R_X86_64_PC32` | 32-bit PC-relative | `S + A - P` | 函数调用、全局变量访问 (PIC) |
| `R_X86_64_32` | 32-bit absolute (unsigned) | `S + A` | 取全局变量地址 (non-PIC) |
| `R_X86_64_32S` | 32-bit absolute (signed) | `S + A` | movq $symbol, ... 的符号扩展 |
| `R_X86_64_64` | 64-bit absolute | `S + A` | .rodata 中的指针初始化 |
| `R_X86_64_GOTPCREL` | GOT entry PC-relative | `G + GOT + A - P` | PIC 代码通过 GOT 访问全局变量 |
| `R_X86_64_REX_GOTPCRELX` | 优化的 GOTPCREL | (同上) | GCC 6+ 的优化变体 |

其中 `S` = 符号值，`A` = 加数 (addend)，`P` = 重定位位置，`GOT` = GOT 基址。

### PIC (Position-Independent Code) 原理

共享库 (.so) 可以被加载到进程地址空间的任意位置。PIC 的核心思想是：**代码段中不包含绝对地址**，所有地址引用都通过以下方式间接获取：

1. **PC-relative 寻址**: x86-64 的 `RIP-relative addressing` 使指令可以访问 RIP ± 2GB 范围内的数据，这天然支持 PIC
2. **GOT (Global Offset Table)**: 对于无法通过 PC-relative 到达的全局变量，编译器生成 `mov symbol@GOTPCREL(%rip), %reg` 指令，通过 GOT 间接访问
3. **PLT (Procedure Linkage Table)**: 用于延迟绑定 (lazy binding) 的外部函数调用

### TEXTREL 的成因与危害

TEXTREL (Text Relocation) 发生在重定位信息位于只读段 (.text, .rodata) 时。由于这些段在运行时被映射为只读页面，动态链接器无法修改它们来进行地址修正。这导致：

1. **性能损失**: 系统必须创建私有的可写页面副本 (copy-on-write)，浪费内存
2. **安全风险**: 代码段变成可写，破坏了 W^X (Write XOR Execute) 保护
3. **兼容性问题**: Android 等平台明确禁止 TEXTREL

---

## Level 1: 蒸汽时代 (10分) 

### 问题分析

**关卡描述**: 纯算术函数，天生 PIC 安全。

**readelf -r 输出**:
```
Relocation section '.rela.eh_frame' at offset 0x1e0 contains 4 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000020  000200000002 R_X86_64_PC32     0000000000000000 .text + 0
000000000034  000200000002 R_X86_64_PC32     0000000000000000 .text + 8
000000000048  000200000002 R_X86_64_PC32     0000000000000000 .text + 11
00000000005c  000200000002 R_X86_64_PC32     0000000000000000 .text + 1b
```

**关键发现**: 
- `.text` 节区零重定位！四个函数 (add, sub, mul, mod) 都是纯算术运算，不引用任何外部符号
- 仅有 `.rela.eh_frame` 中的重定位，用于异常处理框架的展开信息
- 所有重定位类型都是 `R_X86_64_PC32` (PC 相对寻址)，在共享库中天然安全

**为什么 PC-relative 安全？**
RIP-relative 寻址使用当前指令地址加上一个 32 位偏移来计算目标地址。无论共享库被加载到哪个地址，相对偏移不变 —— 这就是 PIC 的本质。`add` 函数只需要 `lea (%rdi,%rsi,1),%eax`，根本不需要访问内存中的任何外部符号。

### 拆弹方法

```bash
ar -x bomb1.a          # 解包静态库
gcc -shared -o libbomb.so lv1.o  # 直接转换为共享库
```

**结果**: 🎉 LEVEL DEFUSED! +10 分

---

## Level 2: 全局危机 (20分)

### 问题分析

**关卡描述**: 全局变量 `gvar=196` 的取地址操作导致 `R_X86_64_32/32S`。

**readelf -r 输出 (lv2.o)**:
```
Relocation section '.rela.text' at offset 0x248 contains 4 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000009  00060000000b R_X86_64_32S      0000000000000000 gvar + 0
00000000000e  00060000000a R_X86_64_32       0000000000000000 gvar + 0
000000000019  000600000002 R_X86_64_PC32     0000000000000000 gvar - 4
000000000028  00040000000b R_X86_64_32S      0000000000000000 .rodata + 0
```

**关键发现**:
- `get_addr` 函数中出现了 `R_X86_64_32S` 和 `R_X86_64_32` —— 32 位绝对地址！
- `read_var` 使用了 `R_X86_64_PC32` —— 这是安全的
- `get_elem` 中访问静态数组使用了 `R_X86_64_32S` 绝对地址

**为什么 32 位绝对地址在共享库中不工作？**

x86-64 的地址空间是 48 位 (256 TB)。共享库可以被 `mmap` 加载到 4 GB 以上的地址 (`mmap` 默认从 `0x7f...` 开始)。32 位地址最大只能表示 4 GB 范围，无法覆盖共享库的可能加载位置。链接器 (binutils 2.40+) 因此在链接共享库时直接拒绝 `R_X86_64_32` 和 `R_X86_64_32S` 重定位。

**反汇编分析**:
```asm
<get_addr>:
   movq   $0x0,-0x8(%rsp)   ; 将 gvar 的 32 位绝对地址存入栈
   mov    $0x0,%eax         ; 将 gvar 的 32 位绝对地址装入 eax (返回值)
   ret

<get_elem>:
   movslq %edi,%rdi
   mov    0x0(,%rdi,4),%eax ; 通过 32 位绝对基址访问 arr[i]
   ret
```

### 拆弹方法

**方案**: 重新编写等效的 `-fPIC` 代码替代原目标文件。

```c
int gvar = 196;
int get_addr(void) { return (int)(unsigned long)&gvar; }
int read_var(void) { return gvar; }
static int arr[] = {10, 20, 30, 40, 50};
int get_elem(int i) { return arr[i]; }
```

编译为 PIC:
```bash
gcc -c -fPIC -O1 -o lv2_fixed.o lv2_fixed.c
```

**修复后重定位**:
```
Relocation section '.rela.text' at offset 0x268 contains 3 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000006  000700000029 R_X86_64_GOTPCREL 0000000000000000 gvar - 4
000000000012  00070000002a R_X86_64_REX_GOTP 0000000000000000 gvar - 4
000000000023  000400000002 R_X86_64_PC32     0000000000000000 .rodata - 4
```

**观察**: `R_X86_64_32/32S` 被替换为：
- `R_X86_64_GOTPCREL`: 通过 GOT 间接访问 gvar，这是标准 PIC 数据访问模式
- `R_X86_64_PC32`: 对静态数组 `arr` 的访问变成了 PC-relative (因为 `static` 保证局部可见)

### 思考题回答

**Q1: 为什么 get_elem 访问 arr[i] 没有产生 R_X86_64_32 重定位？**

A: `arr` 声明为 `static int arr[]`，`static` 关键字使 `arr` 成为 LOCAL 符号。在原始非 PIC 代码中，编译器为本地符号生成绝对地址访问 (`R_X86_64_32S`)，但实际上 `arr` 就在当前编译单元的 `.rodata` 中。在 PIC 重编译后，编译器识别出 `arr` 的局部性，使用 `R_X86_64_PC32` (PC-relative) 直接访问，无需 GOT 间接。

**Q2: R_X86_64_PC32 和 R_X86_64_32 在指令编码上的根本区别？**

A: 
- `R_X86_64_32`: 指令中编码的是目标地址的**绝对值** (32 位)。指令格式通常为 `mov $addr, %eax` 或作为 SIB 寻址的 disp32。指令内嵌了内存绝对地址。
- `R_X86_64_PC32`: 指令中编码的是相对于当前 RIP 的**偏移量** (32 位有符号)。指令格式为 `mov disp32(%rip), %eax`。关键区别在于 ModRM 字节的 mod=00, r/m=101 组合告诉 CPU 使用 RIP-relative 寻址。

**结果**: 🎉 LEVEL DEFUSED! +20 分

---

## Level 3: 数据陷阱 (20分)

### 问题分析

**关卡描述**: 全局符号 `counter=63` 的 PC-relative 引用被链接器拒绝。

**readelf -r 输出 (lv3.o)**:
```
Relocation section '.rela.text' at offset 0x168 contains 2 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000006  000400000002 R_X86_64_PC32     0000000000000000 counter - 4
00000000000f  000400000002 R_X86_64_PC32     0000000000000000 counter - 4
```

**readelf -s 输出 (lv3.o)**:
```
Num:    Value  Size Type    Bind   Ndx Name
  3:        0    20 FUNC    GLOBAL   1 next_val
  4:        0     4 OBJECT  GLOBAL   3 counter    ← 关键!
```

**错误信息**:
```
/usr/bin/ld: relocation R_X86_64_PC32 against symbol `counter' 
can not be used when making a shared object; recompile with -fPIC
```

**关键发现 — 符号介入 (Symbol Interposition)**:

虽然重定位类型是安全的 `R_X86_64_PC32`，但链接器仍然拒绝！原因在于符号的**绑定属性 (Bind)**：

- `counter` 的 Bind 是 `GLOBAL` (STB_GLOBAL)，意味着它可以被其他共享库中的同名符号覆盖
- 如果 `counter` 被外部符号介入 (interposition)，原本 PC-relative 指向的位置就不再是 `counter` 的定义位置了
- 链接器的保守策略：对所有 GLOBAL 符号的 PC-relative 引用，一律要求通过 GOT 间接访问

**深入理解**:

这是 ELF 动态链接中一个精妙的设计。共享库的全局符号遵循 "先定义先胜" 的符号介入规则。如果 `counter` 被外部同名符号覆盖，使用 PC-relative 的 `next_val` 函数仍然会访问原始的 `counter` 位置，这会导致：

1. 外部代码期望通过 `counter` 访问到被介入的版本
2. `next_val` 却读写原始的 `counter` 位置
3. 两个不同的 `counter` 实例共存，导致数据不一致

因此链接器要求：对 GLOBAL 符号的引用必须使用 PIC/GOT 方式，以便在加载时动态决议。

### 拆弹方法

**方案**: 使用 `objcopy --localize-symbol` 将 `counter` 从 GLOBAL 改为 LOCAL。

```bash
objcopy --localize-symbol=counter lv3.o lv3_fixed.o
```

**修复后符号表**:
```
Num:    Value  Size Type    Bind   Ndx Name
  3:        0     4 OBJECT  LOCAL    3 counter    ← 变为 LOCAL
  4:        0    20 FUNC    GLOBAL   1 next_val
```

`objcopy --localize-symbol` 修改符号表中的 `st_info` 字段，将 `STB_GLOBAL` (0x01) 改为 `STB_LOCAL` (0x00)。这告诉链接器：`counter` 不会被外部介入，PC-relative 引用是安全的。

### 思考题回答

**Q1: 如果把 counter 声明为 static，这个问题还会出现吗？为什么？**

A: 不会。`static int counter` 会生成 LOCAL 绑定的符号，链接器不会对其施加符号介入的约束，PC-relative 引用可以直接通过。`objcopy --localize-symbol` 本质上就是在二进制层面模拟了在源码中添加 `static` 关键字的效果。

**Q2: objcopy 除了改变符号绑定，还有哪些实用的 ELF 修改功能？**

A:
- `--globalize-symbol`: 将 LOCAL 改为 GLOBAL (导出隐藏符号)
- `--strip-symbol`: 删除符号 (减小文件大小)
- `--redefine-sym old=new`: 重命名符号 (解决符号冲突)
- `--rename-section old=new`: 重命名节区
- `--add-section`: 添加自定义节区
- `--strip-all` / `--strip-debug`: 剥离调试符号

**结果**: 🎉 LEVEL DEFUSED! +20 分

---

## Level 4: 二进制手术师 (25分)

### 问题分析

**关卡描述**: `base_val=401`，`.rodata` 中的 `const` 指针导致 TEXTREL。

**readelf -r 输出 (lv4.o)**:
```
Relocation section '.rela.text' at offset 0x1a8 contains 1 entry:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000008  000300000002 R_X86_64_PC32     0000000000000000 .data - 4

Relocation section '.rela.rodata' at offset 0x1c0 contains 1 entry:   ← 关键!
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000000  000300000001 R_X86_64_64       0000000000000000 .data + 0
```

**readelf -S 输出 (关键节区)**:
```
[Nr] Name      Type        Flags  
[ 3] .data     PROGBITS    WA     
[ 5] .rodata   PROGBITS    A       ← 只有 A (SHF_ALLOC)，没有 W (SHF_WRITE)!
[ 6] .rela.rodata RELA     I       ← sh_info=5，指向 .rodata
```

**编译警告**:
```
/usr/bin/ld: warning: relocation in read-only section `.rodata'
/usr/bin/ld: warning: creating DT_TEXTREL in a shared object
```

### 问题根源

从源码来看：
```c
static int base_val = 401;
int * const ptr = &base_val;  // const 指针
```

`ptr` 是 `int * const` 类型 —— 指针本身是常量。编译器将其放入 `.rodata` 节区。但 `ptr = &base_val` 需要在链接时确定 `base_val` 的最终地址 (需要 R_X86_64_64 重定位)。

矛盾在于：
1. `.rodata` 是只读的 (flags=0x2, SHF_ALLOC only)
2. 动态链接器需要在加载时修改 `.rodata` 来填充 `&base_val` 的正确地址
3. 这迫使动态链接器创建 TEXTREL —— 在加载时暂时将 `.rodata` 页面设为可写，写入重定位值后再恢复只读

### 拆弹方法

**方案**: 通过修改 `.rodata` 节区标志添加 `SHF_WRITE`，告诉链接器这个节区是可写的。

核心是完成 `patch_o.py` 中的 13 个 TODO。关键实现：

**1. `read_u16/u32/u64` — 读取 ELF 多字节整数**:
```python
struct.unpack_from('<H', data, offset)[0]  # u16
struct.unpack_from('<I', data, offset)[0]  # u32  
struct.unpack_from('<Q', data, offset)[0]  # u64
```

ELF 文件使用小端序，`struct.unpack_from` 的 `<` 前缀正确处理了小端字节序。

**2. `write_u64` — 安全修改 ELF 数据**:
```python
data[:offset] + struct.pack('<Q', val) + data[offset + 8:]
```

关键是不修改原始 `bytearray`，而是创建新的拼接结果，确保 `bytearray` 操作的正确性。

**3. `get_section_name` — 字符串表查找**:
```
Section Header Table
  → .shstrtab section header (by e_shstrndx)
    → sh_offset + sh_size 定位字符串表数据
      → 从 sh_name_off 开始读取直到 \0
```

这是理解 ELF 文件导航的关键 —— 所有字符串 (节区名、符号名) 都存储在独立的字符串表中，通过偏移量引用。

**4. `find_sections_with_textrel` — 检测 TEXTREL 风险**:
算法：
1. 遍历所有节区头部，找到 `SHT_RELA` 类型的节区
2. 通过 RELA 节区的 `sh_info` 字段找到被重定位的目标节区
3. 检查目标节区是否满足：`(SHF_ALLOC && !SHF_WRITE && !SHF_EXECINSTR)`
4. 排除 `.text` (有 SHF_EXECINSTR) — 避免制造 W+X 安全漏洞

**5. `fix_textrel` — 修补节区标志**:
对检测到的每个问题节区，从 `sh_flags` 字段添加 `SHF_WRITE` 位。

**修补后**:
```
[+] Fixed '.rodata' (idx 5): flags 0x2 -> 0x3   (加上了 SHF_WRITE)
```

### 手动修改 vs 工具修改

Section Header 中 flags 字段位于偏移 8 处 (64位)：
```
.rodata section header bytes before: ... 02 00 00 00 00 00 00 00 ...
.rodata section header bytes after:  ... 03 00 00 00 00 00 00 00 ...
```

理论上可以通过十六进制编辑器直接修改这一个字节。但 `patch_o.py` 自动化了这个过程，并加入了安全检查 (排除 TEXT 节区)。

### 思考题回答

**Q1: 为什么 .rodata 中的重定位会导致 TEXTREL，而 .data 中的就不会？**

A: `.data` 节区本身就有 `SHF_WRITE` 标志，动态链接器可以合法地修改它。`.rodata` 只有 `SHF_ALLOC` 标志，按照 ELF 规范，加载后映射为只读页面。如果其中包含重定位，动态链接器需要修改只读页面，违反了内存保护语义，这就产生了 TEXTREL。给 `.rodata` 加上 `SHF_WRITE` 后，它在运行时被映射为可读写页面，与 `.data` 等效。

**Q2: 从源码角度看，怎样改写才能避免这个问题？**

A:
```c
// 不好的写法 (产生 TEXTREL)
int * const ptr = &base_val;  // const 指针进 .rodata

// 好的写法
int *ptr = &base_val;         // 非 const 指针进 .data

// 或者使用 GOT 间接
// 编译为 PIC 且不将 const 指针放入 .rodata
static int base_val = 401;
// 使用函数返回引用代替 const 指针
int *get_ptr(void) { return &base_val; }
```

核心原则：**所有需要在运行时初始化的数据都应放在可写节区中**。

**结果**: 🎉 LEVEL DEFUSED! +25 分

---

## Level 5: 终极链接炸弹 (50分)

### 问题分析

**关卡描述**: 混合三种问题的综合关卡。`bomb5.a` 包含 3 个 .o 文件。

**配置参数**: gvar=558, counter=35, data=(191, 75, 75) → sum=341

#### 文件 1: boss_helper.o — ✅ 安全

```
Symbols: helper_double (GLOBAL), helper_square (GLOBAL)
Relocations: 仅 .eh_frame (R_X86_64_PC32)
```

纯函数，无全局数据访问。可以直接使用。

#### 文件 2: boss_state.o — ⚠️ 两重问题

**Symbols**:
```
counter   OBJECT  GLOBAL  .data    ← Level 3 问题
gvar      OBJECT  GLOBAL  .data
pdata     OBJECT  GLOBAL  .rodata
data      OBJECT  LOCAL   .data    (static int data[3])
```

**Relocations**:
```
.rela.text:
  R_X86_64_PC32  counter  (×2)     ← Level 3: GLOBAL + PC32 → 需要 objcopy
  R_X86_64_PC32  .data    (×3)     ← PC32 to section, 安全

.rela.rodata:
  R_X86_64_64    .data    (×1)     ← Level 4: .rodata 中重定位 → TEXTREL
```

**解决方法**:
1. `objcopy --localize-symbol=counter` → 消除 Level 3 问题
2. `patch_o.py` 修补 `.rodata` 标志 → 消除 Level 4 TEXTREL

#### 文件 3: boss_tricky.o — ⚠️ Level 2 问题

**Symbols**:
```
gvar        NOTYPE  GLOBAL  UND     ← 外部引用 (来自 boss_state.o)
shared_addr FUNC    GLOBAL  .text
read_shared FUNC    GLOBAL  .text
```

**Relocations**:
```
.rela.text:
  R_X86_64_32S  gvar  (shared_addr)  ← 32 位绝对地址!
  R_X86_64_32   gvar  (shared_addr)  ← 32 位绝对地址!
  R_X86_64_PC32 gvar  (read_shared)  ← PC-relative, 但因 gvar 是 UND 符号，linker 处理不同
```

**解决方法**: 重新编写 `-fPIC` 版本代码。

### 拆弹方法

完整流程：

```bash
# 1. 解包
ar -x bomb5.a

# 2. boss_state.o: 先本地化，再修 TEXTREL
objcopy --localize-symbol=counter boss_state.o boss_state_fix1.o
python3 ../tools/patch_o.py boss_state_fix1.o boss_state_fixed.o

# 3. boss_tricky.o: 重写为 PIC
#    (编写 boss_tricky_fixed.c, 用 -fPIC 编译)
gcc -c -fPIC -O1 -o boss_tricky_fixed.o boss_tricky_fixed.c

# 4. 全部链接
gcc -shared -o libbomb.so boss_helper.o boss_state_fixed.o boss_tricky_fixed.o
```

**修复后 relocations**:
```
boss_helper.o:     仅 .eh_frame              → ✅
boss_state_fixed.o: PC32 + 手工添加 SHF_WRITE  → ✅ 
boss_tricky_fixed.o: GOTPCREL (PIC)            → ✅
```

**综合检验**:
- `readelf -d libbomb.so | grep TEXTREL` → 无输出 ✓
- 所有 8 个函数测试通过 ✓

### 技术总结

Level 5 体现了实际工程中链接问题的典型模式：

| 问题类型 | 诊断方法 | 修复手段 |
|---------|---------|---------|
| 绝对地址重定位 | `readelf -r | grep R_X86_64_32` | 重写为 PIC (-fPIC) |
| 全局符号 + PC32 | `readelf -s` + `readelf -r` 交叉比对 | objcopy --localize-symbol |
| .rodata TEXTREL | `readelf -S` + 检查 RELA 的 sh_info | patch_o.py 添加 SHF_WRITE |

**结果**: 🎉 LEVEL DEFUSED! +50 分

---

## 总分: 125/125 🏆

| 关卡 | 名称 | 分数 | 核心技术 | 状态 |
|------|------|------|---------|------|
| 1 | 蒸汽时代 | 10 | ar + gcc -shared | ✅ |
| 2 | 全局危机 | 20 | -fPIC 替代, GOTPCREL | ✅ |
| 3 | 数据陷阱 | 20 | objcopy --localize-symbol | ✅ |
| 4 | 二进制手术师 | 25 | ELF Section Flags 修补 | ✅ |
| 5 | 终极链接炸弹 | 50 | 综合运用前三关技术 | ✅ |

---

## 个人感悟与深度思考

### 1. 对 ELF 文件格式的重新认识

在完成 `patch_o.py` 的过程中，我逐字节地阅读和修改了 ELF 文件。这种 "二进制手术" 式的体验让我深刻理解了 ELF 的结构之美：

- **ELF Header 是入场券**: e_shoff (节区表偏移), e_shnum (节区数量), e_shstrndx (字符串表索引) 是导航整个文件的关键
- **节区头部是地图**: 每个节区的 sh_name → sh_flags → sh_info 构成了一条完整的查询路径
- **重定位表是待办清单**: .rela.text 记录了"这里有个洞需要填"，符号表记录了"洞应该填什么"

以前我只会 `readelf -a` 看输出，现在我理解每行输出背后的二进制结构。

### 2. 重定位机制的深层理解

实验让我认识到重定位不只是 "填地址" 这么简单：

**PC32 vs 32 的本质差异**: 
- PC32 是 "关系型" 的 —— 记录的是相对于自己的偏移，天然位置无关
- 32 是 "绝对型" 的 —— 编码的是目标地址本身，天然位置相关

这让我想到一个哲学类比：PC-relative 就像说 "我在你左边 10 米"，绝对地址就像说 "我在 120.5°E, 30.2°N"。前者在任何参照系中都有效，后者依赖特定的坐标系统。

**符号介入的重要性**: Level 3 让我认识到，即使重定位类型正确 (PC32)，符号的绑定属性 (GLOBAL/LOCAL) 也会影响链接决策。这体现了 ELF 设计的精细 —— 它在静态分析和动态行为之间找到了平衡。

### 3. PIC 的精妙设计

PIC 不是一个简单的编译器开关，而是一套完整的设计哲学：

1. **GOT 是数据段的 "指针表"**: 代码段通过 GOT 间接访问所有全局数据，使得代码段本身保持只读和位置无关
2. **GOT[0] 存储动态链接器信息**: GOT 的前几个条目是保留给动态链接器的，形成了代码-数据-链接器的三层架构
3. **x86-64 的 RIP-relative 寻址是 PIC 的物理基础**: 不像 x86-32 需要使用 `ebx` 寄存器保存 GOT 地址，x86-64 的 `disp32(%rip)` 可以直接计算任意位置的偏移

### 4. 二进制工具的威力

`objcopy` 是我在这次实验中最大的惊喜。它可以在不重新编译的情况下修改 ELF 文件：

- 改变符号绑定 (GLOBAL ↔ LOCAL)
- 重命名符号
- 添加/删除节区
- 剥离调试信息

这让我联想到 "软件考古学" —— 当源码丢失时，`objcopy`、`objdump`、`readelf` 等工具就是唯一的 "挖掘工具"。

### 5. 工程实践中的教训

- **"-fPIC 不是可选的"**: 在编译共享库时，`-fPIC` 不是性能优化，是正确性要求。我的 MinGW 环境 (Windows) 侥幸通过了 Level 1，但从 Level 2 开始就必须切换到真正的 Linux ELF 环境
- **TEXTREL 不是无害警告**: 它意味着代码段需要运行时修改，破坏安全性和性能
- **链接问题难以调试**: 链接阶段的问题通常在运行时才暴露 (如地址溢出导致 SIGSEGV)，提前用 `readelf -r` 分析重定位表可以预防 90% 的问题

### 6. 从 "学生" 到 "拆弹专家" 的转变

完成 5 关后，我有了一种新的 "二进制直觉"：

- 看到 `R_X86_64_32` → 立即警觉：绝对地址，共享库杀手
- 看到 GLOBAL symbol + PC32 → 检查符号介入风险
- 看到 `.rodata` + `SHT_RELA` → 预判 TEXTREL
- 看到 `ar -t bomb.a` 出多个 `.o` → 准备组合策略

这不再只是课本上的概念，而是经过实践验证的工程判断能力。

---

## 附录: 关键命令速查

```bash
# 分析工具
readelf -a file.o      # 全部 ELF 信息
readelf -r file.o      # 重定位表 (最重要)
readelf -s file.o      # 符号表
readelf -S file.o      # 节区头部
readelf -d lib.so      # 动态段 (检查 TEXTREL)
objdump -d -r file.o   # 反汇编 + 重定位标注
objdump -s -j .rodata   # 节区原始内容

# 修改工具
objcopy --localize-symbol=NAME in.o out.o   # 全局→局部
ar -x lib.a            # 解包静态库
gcc -c -fPIC -O1 file.c  # 编译为 PIC .o
gcc -shared -o lib.so *.o  # 链接共享库

# 检测 TEXTREL
readelf -d lib.so | grep TEXTREL
```

---

*本报告由 720024 在完成 5 关链接炸弹拆弹实验后撰写。*
*总分: 125/125 | 通关时间: 2026-06-01*
