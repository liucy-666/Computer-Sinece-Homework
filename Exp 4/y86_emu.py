import sys

# Y86-64 寄存器映射
REG_NAMES = {
    0: '%rax', 1: '%rcx', 2: '%rdx', 3: '%rbx',
    4: '%rsp', 5: '%rbp', 6: '%rsi', 7: '%rdi',
    8: '%r8',  9: '%r9',  10: '%r10', 11: '%r11',
    12: '%r12', 13: '%r13', 14: '%r14'
}

class Y86Emulator:
    def __init__(self, mem_size=8192):
        self.mem = bytearray(mem_size) # 内存
        self.regs = [0] * 15           # 15个通用寄存器
        self.pc = 0                    # 程序计数器
        self.zf = True                 # 零标志位
        self.sf = False                # 符号标志位
        self.of = False                # 溢出标志位
        self.status = "AOK"            # 处理器状态: AOK, HLT, ADR, INS

    def load_coe(self, filename):
        """解析 Xilinx COE 文件并加载到内存"""
        try:
            with open(filename, 'r') as f:
                content = f.read()
            
            # 定位数据段
            if 'memory_initialization_vector=' in content:
                vector_str = content.split('memory_initialization_vector=')[1]
            else:
                vector_str = content
                
            # 清洗字符串（去掉分号，逗号替换为空格）
            vector_str = vector_str.replace(';', ' ').replace(',', ' ')
            tokens = vector_str.split()
            
            # 写入内存
            for i, token in enumerate(tokens):
                if token.strip():
                    self.mem[i] = int(token, 16)
            print(f"[*] 成功加载 {len(tokens)} 字节的机器码。")
        except Exception as e:
            print(f"[-] 加载 COE 文件失败: {e}")
            sys.exit(1)

    # --- 辅助方法 ---
    def read_mem64(self, addr):
        """从内存读取 64 位小端数据"""
        if addr < 0 or addr + 8 > len(self.mem):
            self.status = "ADR"
            return 0
        return int.from_bytes(self.mem[addr:addr+8], byteorder='little', signed=False)

    def write_mem64(self, addr, val):
        """向内存写入 64 位小端数据"""
        if addr < 0 or addr + 8 > len(self.mem):
            self.status = "ADR"
            return
        val_bytes = val.to_bytes(8, byteorder='little', signed=False)
        self.mem[addr:addr+8] = val_bytes

    def to_signed(self, val):
        return val - (1 << 64) if (val & (1 << 63)) else val

    def to_unsigned(self, val):
        return val & 0xFFFFFFFFFFFFFFFF

    def check_cond(self, ifun):
        """判断跳转/传送条件"""
        if ifun == 0: return True                     # 无条件
        if ifun == 1: return (self.sf ^ self.of) or self.zf # le
        if ifun == 2: return self.sf ^ self.of        # l
        if ifun == 3: return self.zf                  # e
        if ifun == 4: return not self.zf              # ne
        if ifun == 5: return not (self.sf ^ self.of)  # ge
        if ifun == 6: return not (self.sf ^ self.of) and not self.zf # g
        return False

    def alu(self, op, valA, valB):
        """算术逻辑单元并更新条件码"""
        sa = self.to_signed(valA)
        sb = self.to_signed(valB)
        
        if op == 0:   # ADD
            res = sb + sa
            self.of = ((sa < 0) == (sb < 0)) and ((res < 0) != (sa < 0))
        elif op == 1: # SUB (rB - rA)
            res = sb - sa
            self.of = ((sa < 0) != (sb < 0)) and ((res < 0) != (sb < 0))
        elif op == 2: # AND
            res = sb & sa
            self.of = False
        elif op == 3: # XOR
            res = sb ^ sa
            self.of = False
            
        res_u = self.to_unsigned(res)
        self.zf = (res_u == 0)
        self.sf = (res_u & (1 << 63)) != 0
        return res_u

    def step(self):
        """执行单条指令的取指、译码、执行、访存、写回及 PC 更新"""
        if self.pc >= len(self.mem):
            self.status = "ADR"
            return
            
        # [取指阶段 Fetch]
        icode = self.mem[self.pc] >> 4
        ifun = self.mem[self.pc] & 0x0F
        
        valP = self.pc + 1

        if icode == 0x0: # halt
            self.status = "HLT"
            return
            
        elif icode == 0x1: # nop
            self.pc = valP
            
        elif icode == 0x2: # cmovXX / rrmovq
            rA = self.mem[valP] >> 4
            rB = self.mem[valP] & 0x0F
            valP += 1
            if self.check_cond(ifun):
                self.regs[rB] = self.regs[rA]
            self.pc = valP
            
        elif icode == 0x3: # irmovq
            rB = self.mem[valP] & 0x0F
            valP += 1
            valC = self.read_mem64(valP)
            valP += 8
            self.regs[rB] = valC
            self.pc = valP
            
        elif icode == 0x4: # rmmovq
            rA = self.mem[valP] >> 4
            rB = self.mem[valP] & 0x0F
            valP += 1
            valC = self.read_mem64(valP)
            valP += 8
            valB = self.regs[rB] if rB != 0xF else 0
            self.write_mem64(valB + valC, self.regs[rA])
            self.pc = valP
            
        elif icode == 0x5: # mrmovq
            rA = self.mem[valP] >> 4
            rB = self.mem[valP] & 0x0F
            valP += 1
            valC = self.read_mem64(valP)
            valP += 8
            valB = self.regs[rB] if rB != 0xF else 0
            valM = self.read_mem64(valB + valC)
            self.regs[rA] = valM
            self.pc = valP
            
        elif icode == 0x6: # OPq
            rA = self.mem[valP] >> 4
            rB = self.mem[valP] & 0x0F
            valP += 1
            self.regs[rB] = self.alu(ifun, self.regs[rA], self.regs[rB])
            self.pc = valP
            
        elif icode == 0x7: # jXX
            valC = self.read_mem64(valP)
            valP += 8
            if self.check_cond(ifun):
                self.pc = valC
            else:
                self.pc = valP
                
        elif icode == 0x8: # call
            valC = self.read_mem64(valP)
            valP += 8
            self.regs[4] = self.to_unsigned(self.regs[4] - 8)
            self.write_mem64(self.regs[4], valP)
            self.pc = valC
            
        elif icode == 0x9: # ret
            valM = self.read_mem64(self.regs[4])
            self.regs[4] = self.to_unsigned(self.regs[4] + 8)
            self.pc = valM
            
        elif icode == 0xA: # pushq
            rA = self.mem[valP] >> 4
            valP += 1
            self.regs[4] = self.to_unsigned(self.regs[4] - 8)
            self.write_mem64(self.regs[4], self.regs[rA])
            self.pc = valP
            
        elif icode == 0xB: # popq
            rA = self.mem[valP] >> 4
            valP += 1
            valM = self.read_mem64(self.regs[4])
            self.regs[4] = self.to_unsigned(self.regs[4] + 8)
            self.regs[rA] = valM
            self.pc = valP
            
        else:
            self.status = "INS" # 非法指令

    def run(self):
        """开始执行代码直至遇到 HLT 等停止条件"""
        print("[*] 处理器开始执行...")
        while self.status == "AOK":
            self.step()
        print(f"[*] 处理器已停止，退出状态: {self.status}\n")

    def dump_state(self):
        """打印寄存器及目标内存值"""
        print("=== 寄存器状态 ===")
        for i in range(15):
            print(f"{REG_NAMES[i]:<5}: 0x{self.regs[i]:016X}")
        print("\n=== 条件码 ===")
        print(f"ZF: {int(self.zf)} | SF: {int(self.sf)} | OF: {int(self.of)}")
        print("\n=== 内存检查 ===")
        # 针对本次测试任务，检查内存 0x200 的值
        target_val = self.read_mem64(0x200)
        print(f"内存 [0x200] = 0x{target_val:016X} (十进制: {target_val})")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("使用方法: python y86_emu.py <your_coe_file.coe>")
        sys.exit(1)
        
    emu = Y86Emulator()
    emu.load_coe(sys.argv[1])
    emu.run()
    emu.dump_state()