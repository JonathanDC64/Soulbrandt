/* BCAS20071, the Asian disc with its 1.04 update: its address for every BLUS30443 address the port uses
 * (des_region.h). Sorted by the US address; one line per address. 0xFFFFFFFF: this program has no such thing
 * (only code that runs on the other discs uses it).
 * tools/region/region_table.py checks that every address in the port is here (CI). */
DES_REGION_ADDR(0x000112E8, 0x000112E8)  /* hook des_body_sample */
DES_REGION_ADDR(0x00011768, 0x00011768)  /* hook des_kf_target */
DES_REGION_ADDR(0x00013874, 0xFFFFFFFF)  /* data kOnlineTagCases */
DES_REGION_ADDR(0x00023750, 0x000219C0)  /* call */
DES_REGION_ADDR(0x000B14C8, 0x000AEE60)  /* call kSpacingPlay */
DES_REGION_ADDR(0x000B15C8, 0x000AEF60)  /* call kFontPlay */
DES_REGION_ADDR(0x000D5B24, 0x000D34BC)  /* hook des_cam_own */
DES_REGION_ADDR(0x00110608, 0x0010DFA0)  /* hook des_mouse_look */
DES_REGION_ADDR(0x001107C0, 0x0010E158)  /* hook des_cam_step */
DES_REGION_ADDR(0x00119670, 0x00117748)  /* hook des_mouse_look */
DES_REGION_ADDR(0x00119818, 0x001178F0)  /* hook des_cam_rate */
DES_REGION_ADDR(0x001198D8, 0x001179B0)  /* hook des_cam_rate */
DES_REGION_ADDR(0x00119FAC, 0x00118084)  /* hook des_cam_rate */
DES_REGION_ADDR(0x0011A018, 0x001180F0)  /* hook des_cam_rate */
DES_REGION_ADDR(0x0011A100, 0x001181D8)  /* hook des_cam_rate */
DES_REGION_ADDR(0x0011B830, 0x00119908)  /* hook des_cam_rate */
DES_REGION_ADDR(0x0011C0E8, 0x0011A1C0)  /* hook des_cam_rate */
DES_REGION_ADDR(0x0011C15C, 0x0011A234)  /* hook des_cam_keep */
DES_REGION_ADDR(0x0011C2BC, 0x0011A394)  /* hook des_cam_rate */
DES_REGION_ADDR(0x0011C758, 0x0011A830)  /* hook des_mouse_look */
DES_REGION_ADDR(0x0011C910, 0x0011A9E8)  /* hook des_mouse_look */
DES_REGION_ADDR(0x0011CF84, 0x0011B05C)  /* hook des_cam_rate */
DES_REGION_ADDR(0x0011CF98, 0x0011B070)  /* hook des_cam_rate */
DES_REGION_ADDR(0x0011D2D4, 0x0011B3AC)  /* hook des_cam_rate */
DES_REGION_ADDR(0x0011D6E4, 0x0011B7BC)  /* hook des_cam_rate */
DES_REGION_ADDR(0x0011D820, 0x0011B8F8)  /* hook des_cam_rate */
DES_REGION_ADDR(0x00123100, 0x001211D8)  /* hook des_cam_rate */
DES_REGION_ADDR(0x00123164, 0x0012123C)  /* hook des_cam_rate */
DES_REGION_ADDR(0x00124188, 0x00122260)  /* hook des_cam_rate */
DES_REGION_ADDR(0x0012554C, 0x00123624)  /* hook des_cam_rate */
DES_REGION_ADDR(0x001256D4, 0x001237AC)  /* hook des_cam_rate */
DES_REGION_ADDR(0x0012EE08, 0x0012CEE0)  /* call, wrapped (des_aspect) */
DES_REGION_ADDR(0x0012F074, 0x0012D14C)  /* hook des_cam_ctor */
DES_REGION_ADDR(0x0012F0DC, 0x0012D1B4)  /* hook des_cam_ctor */
DES_REGION_ADDR(0x0012F19C, 0x0012D274)  /* hook des_frustum_tall */
DES_REGION_ADDR(0x0015AE10, 0x00158EE8)  /* call kLoadSouls */
DES_REGION_ADDR(0x00162D0C, 0x00160DE4)  /* data kGameParamCases */
DES_REGION_ADDR(0x001D17A0, 0x001CF878)  /* call */
DES_REGION_ADDR(0x001D1E64, 0x001CFF3C)  /* hook des_phys_rate */
DES_REGION_ADDR(0x001D35D8, 0x001D16B0)  /* call */
DES_REGION_ADDR(0x001DF83C, 0x001DD914)  /* hook des_phys_kf */
DES_REGION_ADDR(0x001EF6D0, 0x001ED7A8)  /* hook des_phys_kf */
DES_REGION_ADDR(0x001FEAF4, 0x001FCBCC)  /* hook des_slide_rate */
DES_REGION_ADDR(0x001FED50, 0x001FCE28)  /* hook des_slide_rate */
DES_REGION_ADDR(0x00217E70, 0x00215F48)  /* hook des_motion_blur */
DES_REGION_ADDR(0x002184DC, 0x002165B4)  /* hook des_dof */
DES_REGION_ADDR(0x00226778, 0x00224850)  /* call */
DES_REGION_ADDR(0x0025EB88, 0x0025CC60)  /* hook des_fade_rate */
DES_REGION_ADDR(0x0025EC14, 0x0025CCEC)  /* hook des_fade_rate */
DES_REGION_ADDR(0x0026558C, 0x00263664)  /* hook des_fade_rate */
DES_REGION_ADDR(0x00265C88, 0x00263D60)  /* hook des_fade_rate */
DES_REGION_ADDR(0x0026FB20, 0x0026DBF8)  /* hook des_dev_hook */
DES_REGION_ADDR(0x0026FC48, 0x0026DD20)  /* hook des_dev_hook */
DES_REGION_ADDR(0x0026FC98, 0x0026DD70)  /* hook des_dev_hook */
DES_REGION_ADDR(0x00273620, 0x002716F8)  /* call */
DES_REGION_ADDR(0x002737A0, 0x00271878)  /* call */
DES_REGION_ADDR(0x00273BE0, 0x00271CB8)  /* call */
DES_REGION_ADDR(0x00273D38, 0x00271E10)  /* hook des_move_post */
DES_REGION_ADDR(0x00273D4C, 0x00271E24)  /* return address in 0x00273D38 */
DES_REGION_ADDR(0x0027882C, 0x00276904)  /* hook des_dev_hook */
DES_REGION_ADDR(0x002802E0, 0x0027E460)  /* call */
DES_REGION_ADDR(0x00280724, 0x0027E8A4)  /* hook des_dev_hook */
DES_REGION_ADDR(0x00280C78, 0x0027EDF8)  /* hook des_dev_hook */
DES_REGION_ADDR(0x00280E64, 0x0027EFE4)  /* hook des_dev_hook */
DES_REGION_ADDR(0x00280F88, 0x0027F108)  /* hook des_dev_hook */
DES_REGION_ADDR(0x002818D0, 0x0027FA50)  /* hook des_dev_hook */
DES_REGION_ADDR(0x00286614, 0x002846C4)  /* hook des_dev_hook */
DES_REGION_ADDR(0x0028B224, 0x002892D4)  /* hook des_dev_hook */
DES_REGION_ADDR(0x002986B8, 0x00296768)  /* call */
DES_REGION_ADDR(0x002A6AA8, 0x002A4B58)  /* wrapped kChrInsThread */
DES_REGION_ADDR(0x002A6AF4, 0x002A4BA4)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6B14, 0x002A4BC4)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6B24, 0x002A4BD4)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6B34, 0x002A4BE4)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6B50, 0x002A4C00)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6B60, 0x002A4C10)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6B7C, 0x002A4C2C)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6B98, 0x002A4C48)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6BA0, 0x002A4C50)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6BB0, 0x002A4C60)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6BD0, 0x002A4C80)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6BEC, 0x002A4C9C)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6BF8, 0x002A4CA8)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6C34, 0x002A4CE4)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6C58, 0x002A4D08)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6C60, 0x002A4D10)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6C70, 0x002A4D20)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002A6CA8, 0x002A4D58)  /* return address in 0x002A6AA8 */
DES_REGION_ADDR(0x002D8978, 0x002D71B8)  /* call */
DES_REGION_ADDR(0x002DD368, 0x002DC0D0)  /* call kUseGoods */
DES_REGION_ADDR(0x002EC2B8, 0x002EA328)  /* call */
DES_REGION_ADDR(0x002EDEB0, 0x002EBF20)  /* hook des_dev_hook */
DES_REGION_ADDR(0x002F5B14, 0x002F3B84)  /* hook des_ik_rate */
DES_REGION_ADDR(0x002F5CCC, 0x002F3D3C)  /* hook des_ik_rate */
DES_REGION_ADDR(0x002F5DB8, 0x002F3E28)  /* hook des_ik_rate */
DES_REGION_ADDR(0x002FF8F0, 0x002FD960)  /* hook des_roll_dir */
DES_REGION_ADDR(0x00301C24, 0x002FFC94)  /* hook des_roll_dir */
DES_REGION_ADDR(0x0030C330, 0x0030A3A0)  /* hook des_net_sync */
DES_REGION_ADDR(0x0030C378, 0x0030A3E8)  /* hook des_net_sync */
DES_REGION_ADDR(0x0030C46C, 0x0030A4DC)  /* hook des_net_sync */
DES_REGION_ADDR(0x0030F2BC, 0x0030D32C)  /* hook des_roll_tie */
DES_REGION_ADDR(0x0030F734, 0x0030D7A4)  /* hook des_roll_dir */
DES_REGION_ADDR(0x00313B24, 0x00311B94)  /* hook des_net_sync */
DES_REGION_ADDR(0x003147EC, 0x0031285C)  /* hook des_ghost_rate */
DES_REGION_ADDR(0x00314ABC, 0x00312B2C)  /* hook des_ghost_rate */
DES_REGION_ADDR(0x00315030, 0x003130A0)  /* hook des_ghost_rate */
DES_REGION_ADDR(0x00391818, 0x0038FB80)  /* hook des_net_replay */
DES_REGION_ADDR(0x003957B8, 0x00393B20)  /* hook des_net_replay */
DES_REGION_ADDR(0x003A596C, 0x003A3CD4)  /* hook des_net_replay */
DES_REGION_ADDR(0x003A5CA8, 0x003A4010)  /* hook des_net_show */
DES_REGION_ADDR(0x003A5EA0, 0x003A4208)  /* hook des_net_replay */
DES_REGION_ADDR(0x003D42BC, 0x003D2624)  /* hook des_twist_rate */
DES_REGION_ADDR(0x003DBA18, 0x003D9D80)  /* call */
DES_REGION_ADDR(0x003DBBB0, 0x003D9F18)  /* call */
DES_REGION_ADDR(0x003E2DF0, 0x003E1158)  /* hook des_sun_gate */
DES_REGION_ADDR(0x003E6330, 0x003E4698)  /* hook des_sun_gate */
DES_REGION_ADDR(0x003E6518, 0x003E4880)  /* hook des_sun_gate */
DES_REGION_ADDR(0x004418B8, 0x0043FC20)  /* call */
DES_REGION_ADDR(0x0054E8C0, 0x00550870)  /* hook des_net_show */
DES_REGION_ADDR(0x0054E8F8, 0x005508A8)  /* hook des_net_show */
DES_REGION_ADDR(0x0054E934, 0x005508E4)  /* hook des_net_grade_poll */
DES_REGION_ADDR(0x00558884, 0x0055961C)  /* hook des_net_show */
DES_REGION_ADDR(0x00560904, 0x0056169C)  /* hook des_net_show */
DES_REGION_ADDR(0x00562BC4, 0x0056395C)  /* hook des_net_show */
DES_REGION_ADDR(0x00567E9C, 0x00568C34)  /* hook des_qwc_local */
DES_REGION_ADDR(0x00595B80, 0x00596E18)  /* call */
DES_REGION_ADDR(0x00597108, 0x005983A0)  /* call */
DES_REGION_ADDR(0x00597388, 0x00598620)  /* call */
DES_REGION_ADDR(0x0059E3A8, 0x0059F640)  /* call */
DES_REGION_ADDR(0x005A5920, 0x005A6BB8)  /* call */
DES_REGION_ADDR(0x005C2810, 0x005C3AA8)  /* call */
DES_REGION_ADDR(0x005C2848, 0x005C3AE0)  /* call */
DES_REGION_ADDR(0x005F7254, 0x005F84EC)  /* hook des_cut_cam */
DES_REGION_ADDR(0x005F72EC, 0x005F8584)  /* hook des_cut_cam */
DES_REGION_ADDR(0x005F7394, 0x005F862C)  /* hook des_cut_cam */
DES_REGION_ADDR(0x005F743C, 0x005F86D4)  /* hook des_cut_cam */
DES_REGION_ADDR(0x006A2E8C, 0x006A3FEC)  /* hook des_noise_rate */
DES_REGION_ADDR(0x006A3990, 0x006A4AF0)  /* hook des_noise_rate */
DES_REGION_ADDR(0x00718D84, 0x00719EE4)  /* hook des_hud_canvas */
DES_REGION_ADDR(0x0071A6B8, 0x0071B818)  /* call kDlgBaseUpdate */
DES_REGION_ADDR(0x0071B5A0, 0x0071C700)  /* call kFontSpacing */
DES_REGION_ADDR(0x0071B728, 0x0071C888)  /* call kFontBegin */
DES_REGION_ADDR(0x00720F50, 0x007220B0)  /* call */
DES_REGION_ADDR(0x00720F80, 0x007220E0)  /* call */
DES_REGION_ADDR(0x007212A8, 0x00722408)  /* call */
DES_REGION_ADDR(0x00721490, 0x007225F0)  /* call */
DES_REGION_ADDR(0x007216D0, 0x00722830)  /* call */
DES_REGION_ADDR(0x007216F0, 0x00722850)  /* call */
DES_REGION_ADDR(0x00723450, 0x007245B0)  /* call */
DES_REGION_ADDR(0x00723498, 0x007245F8)  /* call */
DES_REGION_ADDR(0x00724AD0, 0x00725C30)  /* call */
DES_REGION_ADDR(0x00724E40, 0x00725FA0)  /* hook des_menu_tick */
DES_REGION_ADDR(0x0072D970, 0x0072EAD0)  /* call */
DES_REGION_ADDR(0x007731F0, 0x00774350)  /* call */
DES_REGION_ADDR(0x00789BC0, 0x0078AD48)  /* call kGaugeUpdate */
DES_REGION_ADDR(0x0078E2E8, 0x0078F470)  /* call kSmoothGaugeUpdate */
DES_REGION_ADDR(0x0078EB60, 0x0078FCE8)  /* call kGaugeDraw */
DES_REGION_ADDR(0x0078EDC8, 0x0078FF50)  /* call kGaugeDraw */
DES_REGION_ADDR(0x0078F020, 0x007901A8)  /* call kGaugeDraw */
DES_REGION_ADDR(0x00798CD8, 0x00799E60)  /* call kScrollMsgUpdate */
DES_REGION_ADDR(0x0079EF7C, 0x007A0104)  /* hook des_hud_w2s */
DES_REGION_ADDR(0x007A6264, 0x007A73EC)  /* hook des_hud_onscreen */
DES_REGION_ADDR(0x007A8C60, 0x007A9DE8)  /* call kGetSoulUpdate */
DES_REGION_ADDR(0x007B5AE8, 0x007B6C70)  /* hook des_hud_onscreen */
DES_REGION_ADDR(0x007B61D4, 0x007B735C)  /* hook des_hud_onscreen */
DES_REGION_ADDR(0x007DC7A0, 0x007DD928)  /* call */
DES_REGION_ADDR(0x007E4248, 0x007E53D0)  /* call */
DES_REGION_ADDR(0x0081E110, 0x0081F298)  /* call kCmdChoice */
DES_REGION_ADDR(0x008219E0, 0x00822B68)  /* call kInvMessage */
DES_REGION_ADDR(0x008AEB68, 0x008AFCF0)  /* hook des_hud_onscreen */
DES_REGION_ADDR(0x008CD078, 0x008CE200)  /* call kNowLoadingUpdate */
DES_REGION_ADDR(0x008DA0E0, 0x008DB268)  /* hook des_viewer_rate */
DES_REGION_ADDR(0x008DB170, 0x008DC2F8)  /* hook des_viewer_rate */
DES_REGION_ADDR(0x008EF430, 0x008F0380)  /* call kLogoUpdate */
DES_REGION_ADDR(0x008F2A58, 0x008F3F20)  /* call kBackPlateUpdate */
DES_REGION_ADDR(0x008FB4F0, 0x008FC9B8)  /* call */
DES_REGION_ADDR(0x008FD2A8, 0x008FE770)  /* call */
DES_REGION_ADDR(0x0090FCD0, 0x00911198)  /* call */
DES_REGION_ADDR(0x009E0148, 0x009E1610)  /* call */
DES_REGION_ADDR(0x00A8CA48, 0x00A8DF10)  /* call */
DES_REGION_ADDR(0x00A8CBF0, 0x00A8E0B8)  /* call */
DES_REGION_ADDR(0x00A9DD58, 0x00A9F220)  /* hook des_game_error */
DES_REGION_ADDR(0x00AC8078, 0x00AC9540)  /* boot patch (apply_boot_patches.py) */
DES_REGION_ADDR(0x00B495B8, 0x00B4AA80)  /* call */
DES_REGION_ADDR(0x00B5CFA0, 0x00B5E468)  /* call */
DES_REGION_ADDR(0x00B5D2F8, 0x00B5E7C0)  /* call */
DES_REGION_ADDR(0x00B5D548, 0x00B5EA10)  /* call */
DES_REGION_ADDR(0x00CD4854, 0x00CD5D1C)  /* hook des_hud_ctx */
DES_REGION_ADDR(0x00D229A4, 0x00D23E6C)  /* hook des_viewport_room */
DES_REGION_ADDR(0x00D67F80, 0x00D69448)  /* call */
DES_REGION_ADDR(0x00D68218, 0x00D696E0)  /* call */
DES_REGION_ADDR(0x00F60BD0, 0x00F62008)  /* call kDialogDraw */
DES_REGION_ADDR(0x00F62E68, 0x00F642A0)  /* call */
DES_REGION_ADDR(0x00FF54D4, 0x00FF690C)  /* hook des_ik_rate */
DES_REGION_ADDR(0x00FF5680, 0x00FF6AB8)  /* hook des_ik_rate */
DES_REGION_ADDR(0x00FF58CC, 0x00FF6D04)  /* hook des_ik_rate */
DES_REGION_ADDR(0x0109F2C4, 0x010A06FC)  /* hook des_body_xform */
DES_REGION_ADDR(0x011B1D6C, 0x011B31A4)  /* hook des_body_sampled */
DES_REGION_ADDR(0x012D86C4, 0x012D9AFC)  /* hook des_rag_pose_a */
DES_REGION_ADDR(0x012D9354, 0x012DA78C)  /* hook des_rag_pose_b */
DES_REGION_ADDR(0x013B75A8, 0x013B89E0)  /* hook des_fx_state */
DES_REGION_ADDR(0x013B75D4, 0x013B8A0C)  /* hook des_fx_state */
DES_REGION_ADDR(0x013B7940, 0x013B8D78)  /* hook des_fx_state */
DES_REGION_ADDR(0x013B7B9C, 0x013B8FD4)  /* hook des_fx_state */
DES_REGION_ADDR(0x013B7CA4, 0x013B90DC)  /* hook des_fx_state */
DES_REGION_ADDR(0x013B7CCC, 0x013B9104)  /* hook des_fx_state */
DES_REGION_ADDR(0x0167AC58, 0x0167C090)  /* hook ds_free */
DES_REGION_ADDR(0x0167AC88, 0x0167C0C0)  /* hook ds_malloc */
DES_REGION_ADDR(0x016EBB98, 0x016ED0A0)  /* data kNoName */
DES_REGION_ADDR(0x0185AF98, 0x0185AF98)  /* vtable */
DES_REGION_ADDR(0x01867F50, 0x01867F50)  /* data kCursorVt */
DES_REGION_ADDR(0x018698A0, 0x018698A0)  /* data kHudDialogVt */
DES_REGION_ADDR(0x0186A968, 0x0186A968)  /* data kTitleVt */
DES_REGION_ADDR(0x0186E1E0, 0x0186E1E0)  /* data kFadePlateVt */
DES_REGION_ADDR(0x0186FBC0, 0x0186FBC0)  /* data kLoadingVt */
DES_REGION_ADDR(0x01870C30, 0x01870C18)  /* data kBackPlateVt */
DES_REGION_ADDR(0x018B4810, 0x018B47F8)  /* vtable */
DES_REGION_ADDR(0x018B4F98, 0x018B4F80)  /* vtable */
DES_REGION_ADDR(0x018C8DB4, 0x018C8BF8)  /* data kFrameDt */
DES_REGION_ADDR(0x018CEFB4, 0x018CEE0C)  /* data kCameraFov */
DES_REGION_ADDR(0x018CEFBC, 0x018CEE14)  /* data kCameraAspect */
DES_REGION_ADDR(0x018D3268, 0x018D30C0)  /* data kStickZoneRadius */
DES_REGION_ADDR(0x018D3274, 0x018D30CC)  /* data kStickZoneScale */
DES_REGION_ADDR(0x0190433C, 0x0190430C)  /* data kContextAspect */
DES_REGION_ADDR(0x019AD3B0, 0x019AD3C4)  /* data kSoulsConsts, a slot of the TOC */
DES_REGION_ADDR(0x019AD560, 0x019AD574)  /* data kChrInsConsts, a slot of the TOC */
DES_REGION_ADDR(0x019AD7BC, 0x019AD7D0)  /* data kDevConsts, a slot of the TOC */
DES_REGION_ADDR(0x019ADD50, 0x019ADD64)  /* data kMenusConsts, a slot of the TOC */
DES_REGION_ADDR(0x019ADE38, 0x019ADE4C)  /* data kScrollConsts, a slot of the TOC */
DES_REGION_ADDR(0x019ADF0C, 0x019ADF20)  /* data kMenuManConsts, a slot of the TOC */
DES_REGION_ADDR(0x019B5040, 0x019B5058)  /* data kToc, the TOC base (r2) */
DES_REGION_ADDR(0x019D8C88, 0x019D8D08)  /* data kLanguageIndex */
DES_REGION_ADDR(0x01AD6298, 0x01AD62C8)  /* data kFlipModeObj */
DES_REGION_ADDR(0x01B3A28C, 0x01B3A2BC)  /* data kRemoMan */
DES_REGION_ADDR(0x01B3D6E0, 0x01B3D710)  /* data kChrMan */
DES_REGION_ADDR(0x01B4B150, 0x01B4B1D8)  /* data kMenuSeMgr */
DES_REGION_ADDR(0x01B4EF9C, 0x01B4F02C)  /* data kPgdRoot */
