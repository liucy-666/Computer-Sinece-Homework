/**************************************************************************
	多项式计算函数。按下面的要求编辑此文件：
	1. 将你的学号、姓名，以注释的方式写到下面；
	2. 实现不同版本的多项式计算函数；
	3. 编辑peval_fun_rec peval_fun_tab数组，将你的最好的答案
		（最小CPE、最小C10）作为数组的前两项
***************************************************************************/
   
/*
	学号：201209054233
	姓名：夜66
*/



#include  <stdio.h>
#include  <stdlib.h>
#include <immintrin.h>
typedef int (*peval_fun)(int*, int, int);

typedef struct {
  peval_fun f;
  char *descr;
} peval_fun_rec, *peval_fun_ptr;


/**************************************************************************
 Edit this comment to indicate your name and Andrew ID
#ifdef ASSIGN
   Submission by Harry Q. Bovik, bovik@andrew.cmu.edu
#else
   Instructor's version.
   Created by Randal E. Bryant, Randy.Bryant@cs.cmu.edu, 10/07/02
#endif
***************************************************************************/

/*
	实现一个指定的常系数多项式计算
	第一次，请直接运行程序，以便获知你需要实现的常系数是啥
*/
int const_poly_eval(int *not_use, int not_use2, int x)
{
    int result = 0;
    int x64, x32, x16, x8, x2;
	
    // 预先计算 x 的移位。CPU 会将这些互相独立的指令并行执行（超标量流水线）
    x64 = x << 6;
    x32 = x << 5;
    x16 = x << 4;
    x8  = x << 3;
    x2  = x << 1;
	
    // 依据霍纳法则组装：40 + 57x + (17x + 94x * x) * x
    result = 40 + (x64 - x8 + x) + ((x16 + x) + (x64 + x32 - x2) * x) * x;
    
    return result;
}



/* 多项式计算函数。注意：这个只是一个参考实现，你需要实现自己的版本 */

/*
	友情提示：lcc支持ATT格式的嵌入式汇编，例如
	
	_asm("movl %eax,%ebx");
	_asm("pushl %edx");
	
	可以在lcc中project->configuration->Compiler->Code Generation->Generate .asm，
	将其选中后，可以在lcc目录下面生成对应程序的汇编代码实现。通过查看汇编文件，
	你可以了解编译器是如何实现你的代码的。有些实现可能非常低效。
	你可以在适当的地方加入嵌入式汇编，来大幅度提高计算性能。
*/

int poly_eval(int *a, int degree, int x)
{
    int result = 0;
    int i;
    int xpwr = 1; /* x的幂次 */
//    printf("阶=%d\n",degree);
    for (i = 0; i <= degree; i++) {
	result += a[i]*xpwr;
	xpwr   *= x;
    }
    return result;
}


/* 第一项：4路循环展开 + 多累加器，打破数据依赖，针对极高阶数优化 CPE */


/*双路 SIMD 展开 + 减少向量更新开销 */
int my_poly_eval(int *a, int degree, int x) {
    int i;
    __m128i sum0 = _mm_setzero_si128();
    __m128i sum1 = _mm_setzero_si128();
    
    // 预计算幂次向量
    int x2 = x * x, x3 = x2 * x, x4 = x3 * x;
    int x8 = x4 * x4;
    
    // sum0 处理 a[0, 1, 2, 3], sum1 处理 a[4, 5, 6, 7]
    __m128i v_xpwr0 = _mm_set_epi32(x3, x2, x, 1);
    __m128i v_xpwr1 = _mm_mullo_epi32(v_xpwr0, _mm_set1_epi32(x4));
    __m128i v_x8 = _mm_set1_epi32(x8);

    // 8路展开 (双向量并行)
    for (i = 0; i <= degree - 7; i += 8) {
        __m128i v_a0 = _mm_loadu_si128((__m128i*)&a[i]);
        __m128i v_a1 = _mm_loadu_si128((__m128i*)&a[i+4]);
        
        sum0 = _mm_add_epi32(sum0, _mm_mullo_epi32(v_a0, v_xpwr0));
        sum1 = _mm_add_epi32(sum1, _mm_mullo_epi32(v_a1, v_xpwr1));
        
        // 这里的更新是瓶颈，但双路展开能显著降低 CPE
        v_xpwr0 = _mm_mullo_epi32(v_xpwr0, v_x8);
        v_xpwr1 = _mm_mullo_epi32(v_xpwr1, v_x8);
    }

    // 水平累加
    __m128i final_v = _mm_add_epi32(sum0, sum1);
    int temp[4];
    _mm_storeu_si128((__m128i*)temp, final_v);
    int result = temp[0] + temp[1] + temp[2] + temp[3];

    // 处理余项
    int xpwr = _mm_extract_epi32(v_xpwr0, 0);
    for (; i <= degree; i++) {
        result += a[i] * xpwr;
        xpwr *= x;
    }
    return result;
}

/* 第二项：10阶特化版。完全展开，消除所有循环控制开销，指令高度并行化 */
/* 最快10阶版：利用 3 个 XMM 寄存器完全并行化 */
int my_poly_eval_10(int *a, int degree, int x) {
    if (degree != 10) return my_poly_eval(a, degree, x);

    int x2 = x * x, x3 = x2 * x, x4 = x3 * x;
    int x8 = x4 * x4;

    // 向量化幂次预设
    __m128i v_xpwr_0_3 = _mm_set_epi32(x3, x2, x, 1);
    __m128i v_xpwr_4_7 = _mm_mullo_epi32(v_xpwr_0_3, _mm_set1_epi32(x4));
    // 8, 9, 10 项的幂次，第 4 位填充 0 避免干扰加法
    __m128i v_xpwr_8_10 = _mm_set_epi32(0, x8 * x2, x8 * x, x8);

    // 一次性从内存读入所有系数
    __m128i v_a_0_3 = _mm_loadu_si128((__m128i*)&a[0]);
    __m128i v_a_4_7 = _mm_loadu_si128((__m128i*)&a[4]);
    // 最后一组只读 3 个，为安全起见，分别提取
    __m128i v_a_8_10 = _mm_set_epi32(0, a[10], a[9], a[8]);

    // 核心计算：三组乘法并行
    __m128i m0 = _mm_mullo_epi32(v_a_0_3, v_xpwr_0_3);
    __m128i m1 = _mm_mullo_epi32(v_a_4_7, v_xpwr_4_7);
    __m128i m2 = _mm_mullo_epi32(v_a_8_10, v_xpwr_8_10);

    // 结果汇聚
    __m128i res = _mm_add_epi32(_mm_add_epi32(m0, m1), m2);
    
    int t[4];
    _mm_storeu_si128((__m128i*)t, res);
    return t[0] + t[1] + t[2] + t[3];
}
/*
	这个表格包含多个数组元素，每一组元素（函数名字, "描述字符串"）
	将你认为最好的两个实现，放在最前面。
	比如：
	{my_poly_eval1, "超级垃圾实现"},
	{my_poly_eval2, "好一点的实现"},
*/
   
peval_fun_rec peval_fun_tab[] = 
{

  /* 第一项，应当是你写的最好CPE的函数实现 */
 {my_poly_eval, "彭睿壹的CPE"},
  /* 第二项，应当是你写的在10阶时具有最好性能的实现 */
 {my_poly_eval_10, "彭睿壹的10阶实现"},

 {poly_eval, "poly_eval: 参考实现"},

 /* 下面的代码不能修改或者删除！！表明数组列表结束 */
 {NULL, ""}
};







