/**************************************************************************
	行/列求和函数。按下面的要求编辑此文件：
	1. 将你的学号、姓名，以注释的方式写到下面；
	2. 实现不同版本的行列求和函数；
	3. 编辑rc_fun_rec rc_fun_tab数组，将你的最好的答案
		（最好的行和列求和、最好的列求和）作为数组的前两项
***************************************************************************/
   
/*
	学号：201209054233
	姓名：彭睿壹
*/


#include  <stdio.h>
#include  <stdlib.h>
#include  "rowcol.h"
#include  <math.h>

/* 参考的列求和函数实现 */
/* 计算矩阵中的每一列的和。请注意对于行和列求和来说，调用参数是
	一样的，只是第2个参数不会用到而已
*/

void c_sum(matrix_t M, vector_t rowsum, vector_t colsum)
{
    int i,j;
    for (j = 0; j < N; j++) {
	colsum[j] = 0;
	for (i = 0; i < N; i++)
	    colsum[j] += M[i][j];
    }
}


/* 参考的列和行求和函数实现 */
/* 计算矩阵中的每一行、每一列的和。 */

void rc_sum(matrix_t M, vector_t rowsum, vector_t colsum)
{
    int i,j;
    for (i = 0; i < N; i++) {
	rowsum[i] = colsum[i] = 0;
	for (j = 0; j < N; j++) {
	    rowsum[i] += M[i][j];
	    colsum[i] += M[j][i];
	}
    }
}

/* 最好的列求和：交换循环次序，保证步长为 1 的内存访问，并进行8路展开 */
#include <immintrin.h>

/* 使用 AVX2 指令集优化的列求和 */
void my_c_sum (matrix_t M, vector_t rowsum, vector_t colsum) {
    int i, j;
    // 初始化 colsum
    for (j = 0; j < N; j++) colsum[j] = 0;

    for (i = 0; i < N; i++) {
        // 每次处理 8 个整数 (256位 / 32位 = 8)
        for (j = 0; j <= N - 8; j += 8) {
            // 加载 colsum 的当前值
            __m256i v_col = _mm256_loadu_si256((__m256i*)&colsum[j]);
            // 加载矩阵 M 的当前行数据
            __m256i v_mat = _mm256_loadu_si256((__m256i*)&M[i][j]);
            // 向量加法
            v_col = _mm256_add_epi32(v_col, v_mat);
            // 写回内存
            _mm256_storeu_si256((__m256i*)&colsum[j], v_col);
        }
        // 处理不满足 8 个的余数部分
        for (; j < N; j++) {
            colsum[j] += M[i][j];
        }
    }
}

/* 使用 AVX2 指令集优化的行列同时求和 */
void my_rc_sum (matrix_t M, vector_t rowsum, vector_t colsum) {
    int i, j;
    for (j = 0; j < N; j++) colsum[j] = 0;

    for (i = 0; i < N; i++) {
        __m256i v_rsum = _mm256_setzero_si256(); // 行累加向量
        
        for (j = 0; j <= N - 8; j += 8) {
            __m256i v_mat = _mm256_loadu_si256((__m256i*)&M[i][j]);
            
            // 更新列和
            __m256i v_col = _mm256_loadu_si256((__m256i*)&colsum[j]);
            v_col = _mm256_add_epi32(v_col, v_mat);
            _mm256_storeu_si256((__m256i*)&colsum[j], v_col);
            
            // 更新行和 (将当前 8 个元素累加到行向量中)
            v_rsum = _mm256_add_epi32(v_rsum, v_mat);
        }
        
        // 垂直求和：将 v_rsum 中的 8 个元素加在一起
        int temp[8];
        _mm256_storeu_si256((__m256i*)temp, v_rsum);
        int rsum = temp[0] + temp[1] + temp[2] + temp[3] + temp[4] + temp[5] + temp[6] + temp[7];
        
        // 处理余数并完成行求和
        for (; j < N; j++) {
            rsum += M[i][j];
            colsum[j] += M[i][j];
        }
        rowsum[i] = rsum;
    }
}

/* 
	这个表格包含多个数组元素，每一组元素（函数名字, COL/ROWCOL, "描述字符串"）
	COL表示该函数仅仅计算每一列的和
	ROWCOL表示该函数计算每一行、每一列的和
	将你认为最好的两个实现，放在最前面。
	比如：
	{my_c_sum1, "超级垃圾列求和实现"},
	{my_rc_sum2, "好一点的行列求和实现"},
*/

rc_fun_rec rc_fun_tab[] = 
{

  /* 第一项，应当是你写的最好列求和的函数实现 */
    {my_c_sum, COL, "彭睿壹的 SIMD 列求和"},
    {my_rc_sum, ROWCOL, "彭睿壹的 SIMD 行列求和"},

    {c_sum, COL, "Column sum, reference implementation"},
    {rc_sum, ROWCOL, "Row and column sum, reference implementation"},

 /* 下面的代码不能修改或者删除！！表明数组列表结束 */
    {NULL,ROWCOL,NULL}
};
