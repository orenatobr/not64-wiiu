; Functional equivalent of dborth's Texture2DShader vertex shader (offset + scale, texcoord passthrough).
; Only the register words are compared with the compiler-generated ones (tools/validate-assembler.sh).
; $MODE = "UniformRegister"
; $ATTRIB_VARS[0].name = "attr_position"
; $ATTRIB_VARS[0].type = "vec3"
; $ATTRIB_VARS[0].location = 0
; $ATTRIB_VARS[1].name = "attr_texture_coord"
; $ATTRIB_VARS[1].type = "vec2"
; $ATTRIB_VARS[1].location = 1
; $UNIFORM_VARS[0].name = "unf_angle"
; $UNIFORM_VARS[0].type = "float"
; $UNIFORM_VARS[0].count = 1
; $UNIFORM_VARS[0].offset = 0
; $UNIFORM_VARS[1].name = "unf_offset"
; $UNIFORM_VARS[1].type = "vec3"
; $UNIFORM_VARS[1].count = 1
; $UNIFORM_VARS[1].offset = 4
; $UNIFORM_VARS[2].name = "unf_scale"
; $UNIFORM_VARS[2].type = "vec3"
; $UNIFORM_VARS[2].count = 1
; $UNIFORM_VARS[2].offset = 8
; $SQ_PGM_RESOURCES_VS.NUM_GPRS = 3
; $SQ_PGM_RESOURCES_VS.STACK_SIZE = 1
; $NUM_SPI_VS_OUT_ID = 1
; $SPI_VS_OUT_ID[0].SEMANTIC_0 = 0
; $SQ_VTX_SEMANTIC_CLEAR = 0xFFFFFFFC
; $NUM_SQ_VTX_SEMANTIC = 2
; $SQ_VTX_SEMANTIC[0].SEMANTIC_ID = 0
; $SQ_VTX_SEMANTIC[1].SEMANTIC_ID = 1
; $VGT_VERTEX_REUSE_BLOCK_CNTL.VTX_REUSE_DEPTH = 14
; $VGT_HOS_REUSE_DEPTH.REUSE_DEPTH = 16

00 CALL_FS NO_BARRIER
01 ALU:
0 x: MUL R0.x, R1.x, C2.x
  y: MUL R0.y, R1.y, C2.y
  z: MUL R0.z, R1.z, C2.z
1 x: ADD R0.x, R0.x, C1.x
  y: ADD R0.y, R0.y, C1.y
  z: ADD R0.z, R0.z, C1.z
  w: MOV R0.w, 1.0f
02 EXP_DONE: POS0, R0.xyzw
03 EXP_DONE: PARAM0, R2.xyzw NO_BARRIER
END_OF_PROGRAM
