#include <stdio.h>
#include <stdlib.h>

/*
 * Square Instruction Format (R-type)
 * +--------------+---------+--------+---------+---------+--------------+
 * |    func7     |   rs2   |  rs1   |  func3  |   rd    |   opcode    |
 * +--------------+---------+--------+---------+---------+--------------+
 * |    6         |   x0    |  src   |    6    |  dest   |   0x7b      |
 * |   [31:25]    | [24:20] |[19:15] | [14:12] | [11:7]  |   [6:0]     |
 * +--------------+---------+--------+---------+---------+--------------+
 *
 * Description:
 * - Operation: rd = rs1 * rs1 (Square calculation)
 * - Format: R-type instruction
 * - Encoding: Custom instruction using reserved opcode space
 * - Fields:
 *   > opcode (0x7b): Custom opcode in reserved space
 *   > rd: Destination register for result
 *   > func3 (6): Custom function select
 *   > rs1: Source register containing input value
 *   > rs2: Set to x0 (unused)
 *   > func7 (6): Custom function identifier
 */
static int square(int n)
{
    int rd;
    // volatile 让编译器不要优化这段代码
    __asm__ __volatile__ (
        /*
         * .insn - 直接指定指令编码
         * 格式: .insn r opcode, func3, func7, rd, rs1, rs2
         * r     - R类型指令格式
         * 0x7b  - 自定义opcode (等于二进制 1111011)
         * 6     - func3 字段值
         * 6     - func7 字段值
         * %0    - 输出操作数 rd (目标寄存器)
         * %1    - 输入操作数 rs1 (源寄存器1，存放输入值n)
         * x0    - 源寄存器2使用x0(值为0的寄存器)
         */
        ".insn r 0x7b, 6, 6, %0, %1, x0" 
        : "=r"(rd)    // "=r" 表示将结果写入通用寄存器，并映射到变量rd
        : "r"(n)      // "r" 表示将变量n的值放入通用寄存器作为输入
    );
    return rd;
}

int main(int argc, char *argv[])
{
    int n = 5;
    if (argc >= 2) {
        n = atoi(argv[1]);
    }
    int ret = square(n);
    printf("square(%d) = %d\n", n, ret);
    return 0;
}