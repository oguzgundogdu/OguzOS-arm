.section .rodata
.global _calculator_csg_start
.global _calculator_csg_end
_calculator_csg_start:
.incbin "apps/calculator.csg"
_calculator_csg_end:
.byte 0
