/* des_game_calls.h: the lifted functions the port calls by name
 * (DES_GAME_CALLS, des_game_title.h). Include after ps3emu/ppu_lift.h.
 *
 * In a single exe they are the lift's own functions, declared here as the
 * lift's ppu_recomp.h declares them: the port's sources include only the
 * fixed part of that header (ps3emu/ppu_lift.h), which declares no lifted
 * function.
 *
 * In the split build's exe (DES_SPLIT, docs/57) they are in the game DLL.
 * Each name becomes a forwarder in the exe that calls the DLL's function
 * through a pointer, resolved by guest address when the DLL loads
 * (des_game_host.cpp). Calls, function pointers, template arguments
 * (gauge_fill_rate<func_...>) and trampoline values all work through it. */
#pragma once
#include "des_game_title.h"

#ifndef PS3_GAME_SPLIT
#define DES_GAME_CALL_DECL_(a) void func_##a(ppu_context* ctx);
DES_GAME_CALLS(DES_GAME_CALL_DECL_)
#undef DES_GAME_CALL_DECL_
#else
namespace des_game_calls {
#define DES_GAME_CALL_DECL_(a) void f_##a(ppu_context* ctx);
DES_GAME_CALLS(DES_GAME_CALL_DECL_)
#undef DES_GAME_CALL_DECL_
}

#define func_00023750 des_game_calls::f_00023750
#define func_00039210 des_game_calls::f_00039210
#define func_00039540 des_game_calls::f_00039540
#define func_00039790 des_game_calls::f_00039790
#define func_00039870 des_game_calls::f_00039870
#define func_00039940 des_game_calls::f_00039940
#define func_00039A10 des_game_calls::f_00039A10
#define func_00039AE0 des_game_calls::f_00039AE0
#define func_00039BB0 des_game_calls::f_00039BB0
#define func_00039D50 des_game_calls::f_00039D50
#define func_00039E30 des_game_calls::f_00039E30
#define func_00039F20 des_game_calls::f_00039F20
#define func_0003A0D8 des_game_calls::f_0003A0D8
#define func_0003C758 des_game_calls::f_0003C758
#define func_0003C9A8 des_game_calls::f_0003C9A8
#define func_0003CA98 des_game_calls::f_0003CA98
#define func_0003CB78 des_game_calls::f_0003CB78
#define func_0003CC48 des_game_calls::f_0003CC48
#define func_0003CDF8 des_game_calls::f_0003CDF8
#define func_0003CEE8 des_game_calls::f_0003CEE8
#define func_0003D208 des_game_calls::f_0003D208
#define func_0003D2E8 des_game_calls::f_0003D2E8
#define func_0003D478 des_game_calls::f_0003D478
#define func_0003D548 des_game_calls::f_0003D548
#define func_0003D618 des_game_calls::f_0003D618
#define func_000B14C8 des_game_calls::f_000B14C8
#define func_000B15C8 des_game_calls::f_000B15C8
#define func_0012EE08 des_game_calls::f_0012EE08
#define func_001381E8 des_game_calls::f_001381E8
#define func_0015AE10 des_game_calls::f_0015AE10
#define func_0017B258 des_game_calls::f_0017B258
#define func_00196158 des_game_calls::f_00196158
#define func_001D17A0 des_game_calls::f_001D17A0
#define func_001D35D8 des_game_calls::f_001D35D8
#define func_00226778 des_game_calls::f_00226778
#define func_00273620 des_game_calls::f_00273620
#define func_002737A0 des_game_calls::f_002737A0
#define func_00273BE0 des_game_calls::f_00273BE0
#define func_002802E0 des_game_calls::f_002802E0
#define func_002986B8 des_game_calls::f_002986B8
#define func_002D8978 des_game_calls::f_002D8978
#define func_002DD368 des_game_calls::f_002DD368
#define func_002EC2B8 des_game_calls::f_002EC2B8
#define func_003DBA18 des_game_calls::f_003DBA18
#define func_003DBBB0 des_game_calls::f_003DBBB0
#define func_004418B8 des_game_calls::f_004418B8
#define func_00443838 des_game_calls::f_00443838
#define func_00451430 des_game_calls::f_00451430
#define func_00595B80 des_game_calls::f_00595B80
#define func_00597108 des_game_calls::f_00597108
#define func_00597388 des_game_calls::f_00597388
#define func_0059E3A8 des_game_calls::f_0059E3A8
#define func_005A5920 des_game_calls::f_005A5920
#define func_005C2810 des_game_calls::f_005C2810
#define func_005C2848 des_game_calls::f_005C2848
#define func_0071A6B8 des_game_calls::f_0071A6B8
#define func_0071B5A0 des_game_calls::f_0071B5A0
#define func_0071B728 des_game_calls::f_0071B728
#define func_00720F50 des_game_calls::f_00720F50
#define func_00720F80 des_game_calls::f_00720F80
#define func_007212A8 des_game_calls::f_007212A8
#define func_00721490 des_game_calls::f_00721490
#define func_007216D0 des_game_calls::f_007216D0
#define func_007216F0 des_game_calls::f_007216F0
#define func_00723450 des_game_calls::f_00723450
#define func_00723498 des_game_calls::f_00723498
#define func_00724AD0 des_game_calls::f_00724AD0
#define func_0072D970 des_game_calls::f_0072D970
#define func_007731F0 des_game_calls::f_007731F0
#define func_00789BC0 des_game_calls::f_00789BC0
#define func_0078E2E8 des_game_calls::f_0078E2E8
#define func_0078EB60 des_game_calls::f_0078EB60
#define func_0078EDC8 des_game_calls::f_0078EDC8
#define func_0078F020 des_game_calls::f_0078F020
#define func_00798CD8 des_game_calls::f_00798CD8
#define func_007A8C60 des_game_calls::f_007A8C60
#define func_007DC7A0 des_game_calls::f_007DC7A0
#define func_007E4248 des_game_calls::f_007E4248
#define func_0081E110 des_game_calls::f_0081E110
#define func_008219E0 des_game_calls::f_008219E0
#define func_008CD078 des_game_calls::f_008CD078
#define func_008EF430 des_game_calls::f_008EF430
#define func_008F2A58 des_game_calls::f_008F2A58
#define func_008FB4F0 des_game_calls::f_008FB4F0
#define func_008FD2A8 des_game_calls::f_008FD2A8
#define func_0090FCD0 des_game_calls::f_0090FCD0
#define func_009E0148 des_game_calls::f_009E0148
#define func_00A36220 des_game_calls::f_00A36220
#define func_00A7CC60 des_game_calls::f_00A7CC60
#define func_00A8CA48 des_game_calls::f_00A8CA48
#define func_00A8CBF0 des_game_calls::f_00A8CBF0
#define func_00A941F8 des_game_calls::f_00A941F8
#define func_00B495B8 des_game_calls::f_00B495B8
#define func_00B5CFA0 des_game_calls::f_00B5CFA0
#define func_00B5D2F8 des_game_calls::f_00B5D2F8
#define func_00B5D548 des_game_calls::f_00B5D548
#define func_00D67F80 des_game_calls::f_00D67F80
#define func_00D68218 des_game_calls::f_00D68218
#define func_00F60BD0 des_game_calls::f_00F60BD0
#define func_00F62E68 des_game_calls::f_00F62E68
#define func_01610188 des_game_calls::f_01610188
#endif
