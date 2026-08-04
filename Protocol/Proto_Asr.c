#include "Proto_Asr.h"

/* 语音命令 ID (6..16) → 播报 ID (27..31 / 36..41) 映射。
 * 索引 = cmd_id - ASR_CMD_FORWARD (6)。宏值由 v0.7 协议 §三/§四 决定；
 * 若协议再调 ID，此表须与 Proto_Asr.h 宏同步核对（踩坑记录：唯一权威映射）。 */
static const uint8_t cmd_to_voice[11] = {
    ASR_VOICE_FORWARD,   /* cmd=6  → play=27 */
    ASR_VOICE_BACKWARD,  /* cmd=7  → play=28 */
    ASR_VOICE_LEFT,      /* cmd=8  → play=29 */
    ASR_VOICE_RIGHT,     /* cmd=9  → play=30 */
    ASR_VOICE_STOP,      /* cmd=10 → play=31 */
    ASR_VOICE_L_FWD,     /* cmd=11 → play=36 */
    ASR_VOICE_L_REV,     /* cmd=12 → play=37 */
    ASR_VOICE_L_STOP,    /* cmd=13 → play=38 */
    ASR_VOICE_R_FWD,     /* cmd=14 → play=39 */
    ASR_VOICE_R_REV,     /* cmd=15 → play=40 */
    ASR_VOICE_R_STOP,    /* cmd=16 → play=41 */
};

uint8_t Proto_Asr_CmdToVoice(uint8_t cmd)
{
    if (cmd < ASR_CMD_FORWARD || cmd > ASR_CMD_R_STOP) return 0;
    return cmd_to_voice[cmd - ASR_CMD_FORWARD];
}
