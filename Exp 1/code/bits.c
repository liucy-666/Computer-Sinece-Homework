#include "btest.h"
#include <limits.h>


team_struct team =
{
   "彭睿壹",
   "202402720024"
};
#if 0
#endif

/*
 * bitAnd - x&y using only ~ and |
 *   Example: bitAnd(6, 5) = 4
 *   Legal ops: ~ |
 *   Max ops: 8
 *   Rating: 1
 */
int bitAnd(int x, int y) {
  return ~(~x | ~y);
}

/*
 * bitXor - x^y using only ~ and &
 *   Example: bitXor(4, 5) = 1
 *   Legal ops: ~ &
 *   Max ops: 14
 *   Rating: 2
 */
int bitXor(int x, int y) {
  return ~(~x & ~y) & ~(x & y);
}

/*
 * evenBits - return word with all even-numbered bits set to 1
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 8
 *   Rating: 2
 */
int evenBits(void) {
  int a = 0x55;
  int b = a | (a << 8);
  return b | (b << 16);
}

/*
 * getByte - Extract byte n from word x
 *   Bytes numbered from 0 (LSB) to 3 (MSB)
 *   Examples: getByte(0x12345678,1) = 0x56
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 6
 *   Rating: 2
 */
int getByte(int x, int n) {
  int shift = n << 3;
  return (x >> shift) & 0xFF;
}

/*
 * bitMask - Generate a mask consisting of all 1's
 *   between lowbit and highbit, inclusive.
 *   Examples: bitMask(5,3) = 0x38
 *   Assume 0 <= lowbit <= 31, and 0 <= highbit <= 31
 *   If lowbit > highbit, then mask should be 0
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 16
 *   Rating: 3
 */
int bitMask(int highbit, int lowbit) {
  int all1 = ~0;
  int mask_low = all1 << lowbit;
  //先左移 highbit 位，再加 1
  int mask_high = ((all1 << highbit) << 1);
  int mask = mask_low & ~mask_high;
  // 处理 low > high 的情况
  int cmp = (highbit + ~lowbit + 1) >> 31;
  return mask & ~cmp;
}

/*
 * reverseBytes - reverse the bytes of x
 *   Example: reverseBytes(0x01020304) = 0x04030201
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 25
 *   Rating: 3
 */
int reverseBytes(int x) {
  int b0 = x & 0xFF;
  int b1 = (x >> 8) & 0xFF;
  int b2 = (x >> 16) & 0xFF;
  int b3 = (x >> 24) & 0xFF;
  return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

/*
 * leastBitPos - return a mask that marks the position of the
 *               least significant 1 bit. If x == 0, return 0
 *   Example: leastBitPos(96) = 0x20
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 6
 *   Rating: 4
 */
int leastBitPos(int x) {
  return x & (~x + 1);
}

/*
 * logicalNeg - implement the ! operator, using all of
 *              the legal operators except !
 *   Examples: logicalNeg(3) = 0, logicalNeg(0) = 1
 *   Legal ops: ~ & ^ | + << >>
 *   Max ops: 12
 *   Rating: 4
 */
int logicalNeg(int x) {
  int neg = ~x + 1;
  int or_val = x | neg;
  return (or_val >> 31) + 1;
}

/*
 * minusOne - return a value of -1
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 2
 *   Rating: 1
 */
int minusOne(void) {
  return ~0;
}

/*
 * TMax - return maximum two's complement integer
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 4
 *   Rating: 1
 */
int tmax(void) {
  return ~(1 << 31);
}

/*
 * negate - return -x
 *   Example: negate(1) = -1.
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 5
 *   Rating: 2
 */
int negate(int x) {
  return ~x + 1;
}

/*
 * isPositive - return 1 if x > 0, return 0 otherwise
 *   Example: isPositive(-1) = 0.
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 8
 *   Rating: 3
 */
int isPositive(int x) {
  int sign = x >> 31;
  int zero = !x;
  return !(sign | zero);
}

/*
 * isLess - if x < y then return 1, else return 0
 *   Example: isLess(4,5) = 1.
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 24
 *   Rating: 3
 */
int isLess(int x, int y) {
  int sign_x = x >> 31;
  int sign_y = y >> 31;
  /*processing diff*/
  int diff = x + (~y + 1);
  int sign_diff = diff >> 31;
  /* different sign */
  int case1 = sign_x & ~sign_y;
  /*same sign but the diff: x < y */
  int case2 = !(sign_x ^ sign_y) & sign_diff;
  /*把-1变为1*/
  return !!(case1 | case2);
}

/*
 * sm2tc - Convert from sign-magnitude to two's complement
 *   where the MSB is the sign bit
 *   Example: sm2tc(0x80000005) = -5.
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 15
 *   Rating: 4
 */
int sm2tc(int x) {
  int sign = x >> 31;
  int mag = x & ~(1 << 31);
  int neg = ~mag + 1;
  return (mag & ~sign) | (neg & sign);
}