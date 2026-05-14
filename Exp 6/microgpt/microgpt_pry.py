"""
The fast, vectorized, autograd-enabled version of microGPT.
Built from scratch using NumPy to simulate a modern Tensor engine.

@karpathy (original algorithm)
Optimized with custom Tensor Matrix Engine
"""

import os
import math
import random
import numpy as np

random.seed(42)
np.random.seed(42)

# -----------------------------------------------------------------------------
# 1. Dataset & Tokenizer
# -----------------------------------------------------------------------------
if not os.path.exists('input.txt'):
    import urllib.request
    names_url = 'https://raw.githubusercontent.com/karpathy/makemore/988aa59/names.txt'
    urllib.request.urlretrieve(names_url, 'input.txt')
docs = [line.strip() for line in open('input.txt') if line.strip()]
random.shuffle(docs)
print(f"num docs: {len(docs)}")

uchars = sorted(set(''.join(docs))) 
BOS = len(uchars) 
vocab_size = len(uchars) + 1 
print(f"vocab size: {vocab_size}")

# -----------------------------------------------------------------------------
# 2. Autograd Tensor Engine (The core upgrade)
# -----------------------------------------------------------------------------
class Tensor:
    __slots__ = ('data', 'grad', '_children', '_backward')

    def __init__(self, data, children=()):
        self.data = np.asarray(data, dtype=np.float32)
        self.grad = np.zeros_like(self.data)
        self._backward = lambda: None
        self._children = set(children)

    def backward(self):
        topo, visited = [], set()
        def build_topo(v):
            if v not in visited:
                visited.add(v)
                for child in v._children:
                    build_topo(child)
                topo.append(v)
        build_topo(self)
        self.grad = np.ones_like(self.data)
        for v in reversed(topo):
            v._backward()

    # --- Basic Operations ---
    def __add__(self, other):
            other = other if isinstance(other, Tensor) else Tensor(other)
            out = Tensor(self.data + other.data, (self, other))
            def _backward():
                grad_self = out.grad
                grad_other = out.grad
                # 兼容标量与向量相加时的梯度聚合
                if grad_self.shape != self.data.shape:
                    grad_self = np.sum(grad_self) if self.data.ndim == 0 else grad_self
                if grad_other.shape != other.data.shape:
                    grad_other = np.sum(grad_other) if other.data.ndim == 0 else grad_other
                self.grad += grad_self
                other.grad += grad_other
            out._backward = _backward
            return out

    def __radd__(self, other): 
        return self + other

    def __matmul__(self, other):
            other = other if isinstance(other, Tensor) else Tensor(other)
            out = Tensor(self.data @ other.data, (self, other))
            def _backward():
                # 2D @ 1D: 矩阵乘向量 (计算全连接层)
                if self.data.ndim == 2 and other.data.ndim == 1:
                    self.grad += np.outer(out.grad, other.data) # 核心修复：使用外积
                    other.grad += self.data.T @ out.grad
                # 1D @ 1D: 向量点积 (计算注意力分数)
                elif self.data.ndim == 1 and other.data.ndim == 1:
                    self.grad += out.grad * other.data
                    other.grad += out.grad * self.data
                # 2D @ 2D: 标准矩阵乘法
                else:
                    self.grad += out.grad @ other.data.T
                    other.grad += self.data.T @ out.grad
            out._backward = _backward
            return out
        
    def __mul__(self, other):
        other = other if isinstance(other, Tensor) else Tensor(other)
        out = Tensor(self.data * other.data, (self, other))
        def _backward():
            grad_self = out.grad * other.data
            grad_other = out.grad * self.data
            # 兼容标量乘以向量时的梯度聚合
            if grad_self.shape != self.data.shape:
                grad_self = np.sum(grad_self) if self.data.ndim == 0 else grad_self
            if grad_other.shape != other.data.shape:
                grad_other = np.sum(grad_other) if other.data.ndim == 0 else grad_other
            self.grad += grad_self
            other.grad += grad_other
        out._backward = _backward
        return out

    def __rmul__(self, other): 
        return self * other
    
    def __neg__(self): 
        return self * Tensor(-1.0)

    # --- Activations & Math ---
    def relu(self):
        out = Tensor(np.maximum(0, self.data), (self,))
        def _backward():
            self.grad += (self.data > 0) * out.grad
        out._backward = _backward
        return out

    def log(self):
        out = Tensor(np.log(self.data + 1e-8), (self,)) 
        def _backward():
            self.grad += out.grad / (self.data + 1e-8)
        out._backward = _backward
        return out

    # --- Array Operations ---
    def __getitem__(self, item): 
        out = Tensor(self.data[item], (self,))
        def _backward():
            if isinstance(item, int) or isinstance(item, slice):
                self.grad[item] += out.grad
            else:
                np.add.at(self.grad, item, out.grad)
        out._backward = _backward
        return out

    @staticmethod
    def stack(tensors): 
        data = np.stack([t.data for t in tensors])
        out = Tensor(data, tuple(tensors))
        def _backward():
            for i, t in enumerate(tensors):
                t.grad += out.grad[i]
        out._backward = _backward
        return out

    @staticmethod
    def concat(tensors): 
        data = np.concatenate([t.data for t in tensors])
        out = Tensor(data, tuple(tensors))
        def _backward():
            idx = 0
            for t in tensors:
                size = t.data.size
                t.grad += out.grad[idx:idx+size].reshape(t.data.shape)
                idx += size
        out._backward = _backward
        return out

# -----------------------------------------------------------------------------
# 3. Model Parameters Initialization
# -----------------------------------------------------------------------------
n_layer = 1     
n_embd = 16     
block_size = 16 
n_head = 4      
head_dim = n_embd // n_head 

matrix = lambda nout, nin, std=0.08: Tensor(np.random.randn(nout, nin) * std)
state_dict = {'wte': matrix(vocab_size, n_embd), 'wpe': matrix(block_size, n_embd), 'lm_head': matrix(vocab_size, n_embd)}

for i in range(n_layer):
    state_dict[f'layer{i}.attn_wq'] = matrix(n_embd, n_embd)
    state_dict[f'layer{i}.attn_wk'] = matrix(n_embd, n_embd)
    state_dict[f'layer{i}.attn_wv'] = matrix(n_embd, n_embd)
    state_dict[f'layer{i}.attn_wo'] = matrix(n_embd, n_embd)
    state_dict[f'layer{i}.mlp_fc1'] = matrix(4 * n_embd, n_embd)
    state_dict[f'layer{i}.mlp_fc2'] = matrix(n_embd, 4 * n_embd)

params = list(state_dict.values()) 
print(f"num params: {sum(p.data.size for p in params)}") # 计算真正的参数总量

# -----------------------------------------------------------------------------
# 4. Neural Network Architecture (Vectorized)
# -----------------------------------------------------------------------------
def softmax(logits):
    x_data = logits.data
    max_val = np.max(x_data, axis=-1, keepdims=True)
    exps = np.exp(x_data - max_val)
    probs = exps / np.sum(exps, axis=-1, keepdims=True)
    
    out = Tensor(probs, (logits,))
    
    def _backward():
        dOut = out.grad
        sum_out_dOut = np.sum(probs * dOut, axis=-1, keepdims=True)
        logits.grad += probs * (dOut - sum_out_dOut)
        
    out._backward = _backward
    return out

def rmsnorm(x):
    ms = np.mean(x.data * x.data)
    scale = (ms + 1e-5) ** -0.5
    return x * Tensor(scale) 

def gpt(token_id, pos_id, keys, values):
    tok_emb = state_dict['wte'][token_id] 
    pos_emb = state_dict['wpe'][pos_id] 
    
    x = tok_emb + pos_emb 
    x = rmsnorm(x) 

    for li in range(n_layer):
        # 1) Multi-head Attention block
        x_residual = x
        x = rmsnorm(x)
        
        # 核心修复：改用 W @ x，直接使用参数 Tensor 保证计算图不断裂
        q = state_dict[f'layer{li}.attn_wq'] @ x
        k = state_dict[f'layer{li}.attn_wk'] @ x
        v = state_dict[f'layer{li}.attn_wv'] @ x
        
        keys[li].append(k)
        values[li].append(v)
        
        x_attn_heads = []
        for h in range(n_head):
            hs = h * head_dim
            q_h = q[hs:hs+head_dim]
            
            attn_logits_list = []
            for t in range(len(keys[li])):
                k_t = keys[li][t][hs:hs+head_dim]
                score = (q_h @ k_t) * Tensor(1.0 / head_dim**0.5)
                attn_logits_list.append(score)
                
            attn_logits = Tensor.stack(attn_logits_list)
            attn_weights = softmax(attn_logits)
            
            head_out = Tensor(np.zeros(head_dim, dtype=np.float32))
            for t in range(len(values[li])):
                v_t = values[li][t][hs:hs+head_dim]
                w = attn_weights[t]
                head_out = head_out + (v_t * w) 
                
            x_attn_heads.append(head_out)
        
        x_attn = Tensor.concat(x_attn_heads)
        
        # 也是 W @ x
        x = state_dict[f'layer{li}.attn_wo'] @ x_attn
        x = x + x_residual
        
        # 2) MLP block
        x_residual = x
        x = rmsnorm(x)
        
        # 同样替换为 W @ x
        x = state_dict[f'layer{li}.mlp_fc1'] @ x
        x = x.relu()
        x = state_dict[f'layer{li}.mlp_fc2'] @ x
        x = x + x_residual

    # 输出映射
    logits = state_dict['lm_head'] @ x
    return logits

# -----------------------------------------------------------------------------
# 5. Training Loop
# -----------------------------------------------------------------------------
learning_rate, beta1, beta2, eps_adam = 0.01, 0.85, 0.99, 1e-8
# Adam 优化器的缓存也需要变成 numpy 数组的结构
m = [np.zeros_like(p.data) for p in params] 
v = [np.zeros_like(p.data) for p in params] 

num_steps = 1000 
for step in range(num_steps):

    doc = docs[step % len(docs)]
    tokens = [BOS] + [uchars.index(ch) for ch in doc] + [BOS]
    n = min(block_size, len(tokens) - 1)

    keys, values = [[] for _ in range(n_layer)], [[] for _ in range(n_layer)]
    losses = []
    
    for pos_id in range(n):
        token_id, target_id = tokens[pos_id], tokens[pos_id + 1]
        logits = gpt(token_id, pos_id, keys, values)
        probs = softmax(logits)
        loss_t = -probs[target_id].log()
        losses.append(loss_t)
        
    loss = (1 / n) * sum(losses) 

    loss.backward()

    lr_t = learning_rate * (1 - step / num_steps) 
    for i, p in enumerate(params):
        m[i] = beta1 * m[i] + (1 - beta1) * p.grad
        v[i] = beta2 * v[i] + (1 - beta2) * (p.grad ** 2)
        m_hat = m[i] / (1 - beta1 ** (step + 1))
        v_hat = v[i] / (1 - beta2 ** (step + 1))
        
        p.data -= lr_t * m_hat / (np.sqrt(v_hat) + eps_adam)
        p.grad = np.zeros_like(p.data) # 梯度清零

    # 强制将 loss.data 转为 float 防止底层格式化报错
    print(f"step {step+1:4d} / {num_steps:4d} | loss {float(loss.data):.4f}", end='\r')

# -----------------------------------------------------------------------------
# 6. Inference
# -----------------------------------------------------------------------------
temperature = 0.5 
print("\n--- inference (new, hallucinated names) ---")
for sample_idx in range(20):
    keys, values = [[] for _ in range(n_layer)], [[] for _ in range(n_layer)]
    token_id = BOS
    sample = []
    for pos_id in range(block_size):
        logits = gpt(token_id, pos_id, keys, values)
        logits_temp = logits * Tensor(1.0 / temperature)
        probs = softmax(logits_temp)
        
        token_id = random.choices(range(vocab_size), weights=probs.data)[0]
        if token_id == BOS:
            break
        sample.append(uchars[token_id])
    print(f"sample {sample_idx+1:2d}: {''.join(sample)}")