# dlc 规则检查器 - 自动检查 bits.c 是否符合实验要求
import re

RULES = {
    "bitAnd": {"legal": ["~", "|"], "max_ops": 8},
    "bitXor": {"legal": ["~", "&"], "max_ops": 14},
    "evenBits": {"legal": ["!", "~", "&", "^", "|", "+", "<<", ">>"], "max_ops": 8},
    "getByte": {"legal": ["!", "~", "&", "^", "|", "+", "<<", ">>"], "max_ops": 6},
    "bitMask": {"legal": ["!", "~", "&", "^", "|", "+", "<<", ">>"], "max_ops": 16},
    "reverseBytes": {"legal": ["!", "~", "&", "^", "|", "+", "<<", ">>"], "max_ops": 25},
    "leastBitPos": {"legal": ["!", "~", "&", "^", "|", "+", "<<", ">>"], "max_ops": 6},
    "logicalNeg": {"legal": ["~", "&", "^", "|", "+", "<<", ">>"], "max_ops": 12},
    "minusOne": {"legal": ["!", "~", "&", "^", "|", "+", "<<", ">>"], "max_ops": 2},
    "tmax": {"legal": ["!", "~", "&", "^", "|", "+", "<<", ">>"], "max_ops": 4},
    "negate": {"legal": ["!", "~", "&", "^", "|", "+", "<<", ">>"], "max_ops": 5},
    "isPositive": {"legal": ["!", "~", "&", "^", "|", "+", "<<", ">>"], "max_ops": 8},
    "isLess": {"legal": ["!", "~", "&", "^", "|", "+", "<<", ">>"], "max_ops": 24},
    "sm2tc": {"legal": ["!", "~", "&", "^", "|", "+", "<<", ">>"], "max_ops": 15},
}

FORBIDDEN = ["if", "while", "for", "switch", "printf", "/*", "*/"]

def check_code():

    with open(r"C:\Users\admin\Desktop\Computer_System\Exp1\code\bits.c", "r") as f:
        code = f.read()


    print("="*50)
    print("🧪 CS:APP Data Lab dlc 检查器")
    print("="*50)

    func_pattern = re.compile(r"int (\w+)\(.*?\)\s*\{(.*?)\}", re.DOTALL)
    funcs = func_pattern.findall(code)

    all_pass = True

    for name, body in funcs:
        if name not in RULES:
            continue

        print(f"\n📌 检查函数: {name}")

        # 检查禁止语句
        for fbd in FORBIDDEN:
            if fbd in body:
                print(f"   ❌ 违规: 使用了禁止语句 {fbd}")
                all_pass = False

        # 统计运算符
        ops = re.findall(r'([~!&^|+-]+)', body)
        op_list = []
        for o in ops:
            op_list += list(o)

        legal = RULES[name]["legal"]
        illegal_ops = [o for o in op_list if o not in legal]
        count = len(op_list)

        if illegal_ops:
            print(f"   ❌ 非法运算符: {set(illegal_ops)}")
            all_pass = False

        max_ops = RULES[name]["max_ops"]
        if count > max_ops:
            print(f"   ❌ 运算符超限: {count}/{max_ops}")
            all_pass = False

        if not illegal_ops and count <= max_ops:
            print(f"   ✅ 合规: 运算符 {count}/{max_ops}")

    print("\n" + "="*50)
    if all_pass:
        print("🎉 全部函数通过 dlc 检查！可以直接提交！")
    else:
        print("⚠️ 存在违规，请修改代码")
    print("="*50)

if __name__ == "__main__":
    check_code()