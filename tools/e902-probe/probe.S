	.option norvc
	.text
	.globl _start
_start:
	auipc	a0, 0              /* a0 = own address as the E902 sees it */
	sw	a0, 0x100(a0)      /* marker 1: E902 view of _start */
	lui	a1, 0x12345
	addi	a1, a1, 0x678
	sw	a1, 0x104(a0)      /* marker 2: 0x12345678 */
	li	a1, 0
1:	addi	a1, a1, 1
	sw	a1, 0x108(a0)      /* counter: alive */
	j	1b
