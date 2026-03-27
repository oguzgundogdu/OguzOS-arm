.section .rodata
.global _calculator_cs_start
.global _calculator_cs_end
_calculator_cs_start:
.incbin "apps/calculator.cs"
_calculator_cs_end:
.byte 0
