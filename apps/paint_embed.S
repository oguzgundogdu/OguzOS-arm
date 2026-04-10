.section .rodata
.global _paint_csg_start
.global _paint_csg_end
_paint_csg_start:
.incbin "apps/paint.csg"
_paint_csg_end:
.byte 0
