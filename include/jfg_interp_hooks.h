#ifndef JFG_INTERP_HOOKS_H
#define JFG_INTERP_HOOKS_H
/* Frame interpolation hooks. Included by generated code after recomp.h. */
#ifdef __cplusplus
extern "C" {
#endif
/* Entry of func_8004DA10 (setupOSTasks): hand the frame recorded since the last submit to the renderer. */
void jfg_interp_submit(uint8_t* rdram, recomp_context* ctx);
/* Entry of camSetProjMtx (0x80040AA4): the current camera's view x projection is about to be written at *$a1 and
   loaded into slot 0. */
void jfg_interp_camera(uint8_t* rdram, recomp_context* ctx);
/* Entry of camInit (0x8003F640): a level is loading; every camera starts afresh. */
void jfg_interp_level_start(uint8_t* rdram, recomp_context* ctx);
/* objSetupObject (0x80005F14) returning at 0x80006E04 with the new object in $v0. */
void jfg_interp_object_created(uint8_t* rdram, recomp_context* ctx);
/* Entry of func_80007C68 (objDoFrees / objFreeAll destroying the object in $a0). */
void jfg_interp_object_freed(uint8_t* rdram, recomp_context* ctx);
/* Entry of modFreeModel (0x8003C7C4): the model instance in $a0 is freed. */
void jfg_interp_instance_freed(uint8_t* rdram, recomp_context* ctx);
/* At the end of modGenAnimMatrices (0x8003DD9C): the instance's joint matrices are ready ($s4 instance, $s3 model). */
void jfg_interp_joints(uint8_t* rdram, recomp_context* ctx);
#ifdef __cplusplus
}
#endif
#endif
