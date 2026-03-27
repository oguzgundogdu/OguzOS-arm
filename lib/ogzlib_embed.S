.section .rodata
.global _ogzlib_ui_start
.global _ogzlib_ui_end
_ogzlib_ui_start:
.incbin "lib/OguzOS.UI.ogzl"
_ogzlib_ui_end:
.byte 0
