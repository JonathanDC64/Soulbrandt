/* des_keybinds.h: the Keyboard page's rows that the port reads itself rather than cellPad (settings.cpp's keys_for_pad
 * takes them out of PAD_KEYS): the Kick key (DES_KICK_KEYS, des_kick in des_options.cpp) and the item slots'
 * keys (DES_ITEM_KEYS, des_item_hotkeys.cpp). Read as cellPad reads its keyboard, with cellPad_host_key_down.
 * des_keybinds.cpp. */
#pragma once

struct DesKeyBind {
    int in, mod;   /* virtual-key codes; mod 0: a plain input, not a chord */
};
struct DesKeys {
    DesKeyBind b[4];   /* cellPad's PAD_BIND_MAX */
    int n;
};

/* One input as the Keyboard page writes it (PAD_KEYS's names), n characters: its virtual-key code for
 * cellPad_host_key_down, the mouse buttons as VK_LBUTTON to VK_XBUTTON2. 0: not an input, or a wheel notch (never
 * held). */
int des_key_vk(const char* s, unsigned n);
/* The bindings from s to end (null: to the string's end), separated by '|', each an input or a modifier and an
 * input ("mouse4|shift+v"). At most 4; the ones that are not inputs are left out. */
DesKeys des_keys_parse(const char* s, const char* end);
/* One of them is held now (a chord: both of its inputs). */
bool des_keys_held(const DesKeys& k);
