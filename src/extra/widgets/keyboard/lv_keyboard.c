/**
 * @file lv_keyboard.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_keyboard.h"
#include "lv_ime_dict.h"
#include "lv_ime_phrase_dict.h"
#if LV_USE_KEYBOARD

#include "../../../misc/lv_assert.h"
#include "../../../widgets/lv_textarea.h"

#include <ctype.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS &lv_keyboard_class
#define LV_KB_BTN(width) (LV_BTNMATRIX_CTRL_POPOVER | (width))

#define LV_KEYBOARD_MAX_CANDIDATES 100
#define LV_KEYBOARD_IME_PAGE_SIZE 8
#define LV_KEYBOARD_IME_NAV_PREV "<"
#define LV_KEYBOARD_IME_NAV_NEXT ">"
#define LV_KEYBOARD_BUILD_VISIT_BUDGET 512
#define LV_KEYBOARD_CHAR_MIX_MAX_PINYIN_LEN 6

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    const char * ptr;
    uint8_t len;
} lv_utf8_span_t;

typedef struct {
    const char * phrase;
    uint32_t score;
} lv_phrase_hit_t;

typedef struct {
    char ch;
    int32_t child_idx;
    int32_t sibling_idx;
    int32_t entry_head;
} lv_keyboard_trie_node_t;

typedef struct {
    uint32_t entry_idx;
    int32_t next_idx;
} lv_keyboard_trie_link_t;

typedef struct {
    lv_keyboard_trie_node_t * nodes;
    lv_keyboard_trie_link_t * links;
    int32_t node_cnt;
    int32_t node_cap;
    int32_t link_cnt;
    int32_t link_cap;
    int8_t status;
} lv_keyboard_trie_t;

typedef struct {
    int32_t node_idx;
    size_t key_len;
} lv_keyboard_trie_match_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void lv_keyboard_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_keyboard_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);

static void lv_keyboard_update_map(lv_obj_t * obj);
static void lv_keyboard_update_ctrl_map(lv_obj_t * obj);
static void lv_keyboard_target_event_cb(lv_event_t * e);

static bool lv_keyboard_commit_cancel(lv_obj_t * obj, lv_keyboard_t * keyboard);
static bool lv_keyboard_commit_ready(lv_obj_t * obj, lv_keyboard_t * keyboard);
static void lv_keyboard_switch_mode(lv_obj_t * obj, lv_keyboard_mode_t mode);
static bool lv_keyboard_handle_common_key(lv_obj_t * obj, lv_keyboard_t * keyboard, const char * txt);
static bool lv_keyboard_handle_textarea_key(lv_keyboard_t * keyboard, const char * txt);

static void lv_keyboard_handle_ime_chn(lv_keyboard_t * keyboard, const char * txt);
static bool lv_keyboard_ime_has_composition(const lv_keyboard_t * keyboard);
static void lv_keyboard_ime_reset(lv_keyboard_t * keyboard, bool keep_text);
static void lv_keyboard_ime_backspace(lv_keyboard_t * keyboard);
static void lv_keyboard_ime_append_letter(lv_keyboard_t * keyboard, char ch);
static bool lv_keyboard_ime_commit_first_candidate(lv_keyboard_t * keyboard);
static void lv_keyboard_ime_commit_text(lv_keyboard_t * keyboard, const char * txt);
static bool lv_keyboard_ime_page_step(lv_keyboard_t * keyboard, int dir);
static void lv_keyboard_ime_release_map(lv_keyboard_t * keyboard);
static void lv_keyboard_ime_release_candidates(lv_keyboard_t * keyboard);
static bool lv_keyboard_ime_ensure_candidate_btnm(lv_keyboard_t * keyboard);
static uint16_t lv_keyboard_candidate_width_units(const char * candidate);
static uint8_t lv_keyboard_candidate_page_limit(const char * candidate);
static uint8_t lv_keyboard_candidate_visible_count(const lv_keyboard_t * keyboard, int start_idx);
static void lv_keyboard_show_candidates(lv_keyboard_t * keyboard);
static void lv_keyboard_update_candidate_page(lv_keyboard_t * keyboard);
static void lv_keyboard_clear_candidates(lv_keyboard_t * keyboard);
static void lv_keyboard_select_candidate(lv_keyboard_t * keyboard, int idx);
static void lv_keyboard_candidate_event_cb(lv_event_t * e);

static bool lv_keyboard_is_ascii_letter_key(const char * txt);
static bool lv_keyboard_is_digit_key(const char * txt);
static bool lv_keyboard_is_mode_label_key(const char * txt);
static bool lv_keyboard_is_punctuation_key(const char * txt);
static bool lv_keyboard_should_collect_phrase_candidates(const char * pinyin);
static bool lv_keyboard_is_multi_syllable_input(const char * pinyin);
static uint8_t lv_keyboard_utf8_char_size(const char * str);
static void lv_keyboard_collect_entry_candidates(const char * chinese, char * tmp, size_t tmp_len, size_t tmp_cap,
                                                 char ** out_list, int * out_cnt, int out_max,
                                                 const char * suffix, bool append_suffix);

static bool lv_keyboard_ensure_pinyin_trie(void);
static bool lv_keyboard_ensure_phrase_tries(void);
static bool lv_keyboard_trie_init(lv_keyboard_trie_t * trie, int32_t node_cap, int32_t link_cap);
static void lv_keyboard_trie_deinit(lv_keyboard_trie_t * trie);
static int32_t lv_keyboard_trie_find_child(const lv_keyboard_trie_t * trie, int32_t parent_idx, char ch);
static int32_t lv_keyboard_trie_get_or_add_child(lv_keyboard_trie_t * trie, int32_t parent_idx, char ch);
static bool lv_keyboard_trie_insert(lv_keyboard_trie_t * trie, const char * key, uint32_t entry_idx);
static int32_t lv_keyboard_trie_follow(const lv_keyboard_trie_t * trie, const char * key);
static bool lv_keyboard_trie_has_exact_match(const lv_keyboard_trie_t * trie, const char * key);
static void lv_keyboard_trie_collect_entries(const lv_keyboard_trie_t * trie, int32_t node_idx,
                                             uint32_t * out_list, int * out_cnt, int out_max,
                                             bool include_self, bool recursive);
static void lv_keyboard_phrase_collect_hits(const lv_keyboard_trie_t * trie, int32_t node_idx, const char * input,
                                            lv_phrase_hit_t * hits, int * hit_cnt, int out_max,
                                            bool include_self, bool recursive);
static int lv_keyboard_pinyin_trie_find_matches(const char * pinyin, size_t pos,
                                                lv_keyboard_trie_match_t * matches, int max_matches,
                                                int32_t * prefix_node_idx, size_t * prefix_len);

static void lv_keyboard_build_candidates(const char * pinyin, size_t pos,
                                         char * tmp, size_t tmp_len, size_t tmp_cap,
                                         char ** out_list, int * out_cnt, int out_max,
                                         bool allow_partial_tail, uint16_t * visit_budget);
static int lv_keyboard_collect_phrase_candidates(const char * pinyin, char *** out_list);
static int lv_keyboard_collect_rime_candidates(const char * pinyin, char ** out_list, int out_max);
static int lv_keyboard_collect_char_candidates(const char * pinyin, char ** out_list, int out_max);
static uint32_t lv_keyboard_phrase_score(const char * input, const lv_ime_phrase_entry_t * entry);
static void lv_keyboard_phrase_try_insert(lv_phrase_hit_t * hits, int * hit_cnt, int max,
                                          const char * phrase, uint32_t score);
static bool lv_keyboard_is_single_syllable(const char * pinyin);
static bool lv_keyboard_is_two_syllables(const char * pinyin);
static int lv_keyboard_utf8_char_count(const char * str);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t lv_keyboard_class = {
    .constructor_cb = lv_keyboard_constructor,
    .destructor_cb = lv_keyboard_destructor,
    .width_def = LV_PCT(100),
    .height_def = LV_PCT(50),
    .instance_size = sizeof(lv_keyboard_t),
    .editable = 1,
    .base_class = &lv_btnmatrix_class
};

static lv_keyboard_trie_t pinyin_trie;
static lv_keyboard_trie_t phrase_pinyin_trie;
static lv_keyboard_trie_t phrase_initials_trie;

static const char * const ime_chn_kb_map[] = {
    "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", "。", "，", "：", "\n",
    LV_SYMBOL_KEYBOARD, "中", LV_SYMBOL_LEFT, "拼音", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t ime_kb_ctrl_chn_map[] = {
    LV_KEYBOARD_CTRL_BTN_FLAGS | 5, LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4),
    LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 6, LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3),
    LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1),
    LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1),
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6,
    LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

static const char * const default_kb_map_lc[] = {
    "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, "英", LV_SYMBOL_LEFT, "QWERTY", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t default_kb_ctrl_lc_map[] = {
    LV_KEYBOARD_CTRL_BTN_FLAGS | 5, LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4),
    LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 6, LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3),
    LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1),
    LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1),
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6,
    LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

static const char * const default_kb_map_uc[] = {
    "1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, "英", LV_SYMBOL_LEFT, "QWERTY", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t default_kb_ctrl_uc_map[] = {
    LV_KEYBOARD_CTRL_BTN_FLAGS | 5, LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4),
    LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 6, LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3),
    LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1),
    LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1),
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6,
    LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

static const char * const default_kb_map_spec[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
    "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, "符号", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t default_kb_ctrl_spec_map[] = {
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | 2,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6, LV_BTNMATRIX_CTRL_CHECKED | 2,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

static const char * const default_kb_map_num[] = {
    "1", "2", "3", LV_SYMBOL_KEYBOARD, "\n",
    "4", "5", "6", LV_SYMBOL_OK, "\n",
    "7", "8", "9", LV_SYMBOL_BACKSPACE, "\n",
    "+/-", "0", ".", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, ""
};

static const lv_btnmatrix_ctrl_t default_kb_ctrl_num_map[] = {
    1, 1, 1, LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
    1, 1, 1, LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
    1, 1, 1, 2,
    1, 1, 1, 1, 1
};

static const char ** kb_map[10] = {
    (const char **)default_kb_map_lc,
    (const char **)default_kb_map_uc,
    (const char **)default_kb_map_spec,
    (const char **)default_kb_map_num,
    (const char **)default_kb_map_lc,
    (const char **)default_kb_map_lc,
    (const char **)default_kb_map_lc,
    (const char **)default_kb_map_lc,
    (const char **)ime_chn_kb_map,
    NULL,
};

static const lv_btnmatrix_ctrl_t * kb_ctrl[10] = {
    default_kb_ctrl_lc_map,
    default_kb_ctrl_uc_map,
    default_kb_ctrl_spec_map,
    default_kb_ctrl_num_map,
    default_kb_ctrl_lc_map,
    default_kb_ctrl_lc_map,
    default_kb_ctrl_lc_map,
    default_kb_ctrl_lc_map,
    ime_kb_ctrl_chn_map,
    NULL,
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * lv_keyboard_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(&lv_keyboard_class, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

void lv_keyboard_reset_ime(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN) {
        lv_keyboard_ime_reset(keyboard, true);
    }
}

void lv_keyboard_set_textarea(lv_obj_t * obj, lv_obj_t * ta)
{
    if(ta) {
        LV_ASSERT_OBJ(ta, &lv_textarea_class);
    }

    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;

    if(keyboard->ta == ta) {
        return;
    }

    if(keyboard->ta && lv_obj_is_valid(keyboard->ta)) {
        lv_obj_remove_event_cb_with_user_data(keyboard->ta, lv_keyboard_target_event_cb, keyboard);
        lv_obj_clear_state(keyboard->ta, LV_STATE_FOCUSED);
    }

    keyboard->ta = ta;

    if(keyboard->ta) {
        lv_obj_add_event_cb(keyboard->ta, lv_keyboard_target_event_cb, LV_EVENT_DELETE, keyboard);
        lv_obj_add_state(keyboard->ta, LV_STATE_FOCUSED);
    }

    if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN) {
        lv_keyboard_ime_reset(keyboard, true);
    }
}

void lv_keyboard_set_mode(lv_obj_t * obj, lv_keyboard_mode_t mode)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_keyboard_switch_mode(obj, mode);
}

void lv_keyboard_set_candidate_list(lv_obj_t * obj, lv_obj_t * list)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;

    if(keyboard->candidate_list == list) {
        return;
    }

    if(keyboard->candidate_list && lv_obj_is_valid(keyboard->candidate_list)) {
        lv_obj_remove_event_cb_with_user_data(keyboard->candidate_list, lv_keyboard_target_event_cb, keyboard);
    }

    lv_keyboard_ime_release_candidates(keyboard);

    if(keyboard->candidate_btnm && lv_obj_is_valid(keyboard->candidate_btnm)) {
        lv_obj_del(keyboard->candidate_btnm);
    }
    keyboard->candidate_btnm = NULL;
    keyboard->candidate_list = list;

    if(keyboard->candidate_list) {
        lv_obj_add_event_cb(keyboard->candidate_list, lv_keyboard_target_event_cb, LV_EVENT_DELETE, keyboard);
    }
}

void lv_keyboard_set_popovers(lv_obj_t * obj, bool en)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    if(keyboard->popovers == en) {
        return;
    }

    keyboard->popovers = en;
    lv_keyboard_update_ctrl_map(obj);
}

void lv_keyboard_set_map(lv_obj_t * obj, lv_keyboard_mode_t mode, const char * map[],
                         const lv_btnmatrix_ctrl_t ctrl_map[])
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    kb_map[mode] = map;
    kb_ctrl[mode] = ctrl_map;
    lv_keyboard_update_map(obj);
}

lv_obj_t * lv_keyboard_get_textarea(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    return ((lv_keyboard_t *)obj)->ta;
}

lv_keyboard_mode_t lv_keyboard_get_mode(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    return ((lv_keyboard_t *)obj)->mode;
}

bool lv_btnmatrix_get_popovers(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    return ((lv_keyboard_t *)obj)->popovers;
}

void lv_keyboard_def_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    uint16_t btn_id = lv_btnmatrix_get_selected_btn(obj);
    if(btn_id == LV_BTNMATRIX_BTN_NONE) {
        return;
    }

    const char * txt = lv_btnmatrix_get_btn_text(obj, btn_id);
    if(txt == NULL) {
        return;
    }

    if(lv_keyboard_handle_common_key(obj, keyboard, txt)) {
        return;
    }

    if(keyboard->ta == NULL || !lv_obj_is_valid(keyboard->ta)) {
        keyboard->ta = NULL;
        return;
    }

    if(lv_keyboard_handle_textarea_key(keyboard, txt)) {
        return;
    }

    if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN) {
        lv_keyboard_handle_ime_chn(keyboard, txt);
    }
    else {
        lv_textarea_add_text(keyboard->ta, txt);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_keyboard_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    keyboard->ta = NULL;
    keyboard->mode = LV_KEYBOARD_MODE_TEXT_LOWER;
    keyboard->popovers = 0;
    keyboard->pinyin_buf[0] = '\0';
    keyboard->candidate_list = NULL;
    keyboard->candidate_btnm = NULL;
    keyboard->candidate_cnt = 0;
    keyboard->candidate_map = NULL;
    keyboard->pinyin_start_pos = 0;
    keyboard->candidate_page = 0;
    keyboard->total_candidates = 0;
    keyboard->candidates_list = NULL;

    lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(obj, lv_keyboard_def_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_base_dir(obj, LV_BASE_DIR_LTR, 0);

    lv_keyboard_update_map(obj);
}

static void lv_keyboard_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;

    if(keyboard->ta && lv_obj_is_valid(keyboard->ta)) {
        lv_obj_remove_event_cb_with_user_data(keyboard->ta, lv_keyboard_target_event_cb, keyboard);
    }

    if(keyboard->candidate_list && lv_obj_is_valid(keyboard->candidate_list)) {
        lv_obj_remove_event_cb_with_user_data(keyboard->candidate_list, lv_keyboard_target_event_cb, keyboard);
    }

    lv_keyboard_ime_release_candidates(keyboard);

    if(keyboard->candidate_btnm && lv_obj_is_valid(keyboard->candidate_btnm)) {
        lv_obj_del(keyboard->candidate_btnm);
    }
}

static void lv_keyboard_target_event_cb(lv_event_t * e)
{
    lv_keyboard_t * keyboard = (lv_keyboard_t *)lv_event_get_user_data(e);
    if(keyboard == NULL) {
        return;
    }

    lv_obj_t * target = lv_event_get_target(e);
    if(target == keyboard->ta) {
        keyboard->ta = NULL;
        return;
    }

    if(target == keyboard->candidate_list) {
        keyboard->candidate_list = NULL;
        keyboard->candidate_btnm = NULL;
        lv_keyboard_ime_release_candidates(keyboard);
    }
}

static void lv_keyboard_update_map(lv_obj_t * obj)
{
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    lv_btnmatrix_set_map(obj, kb_map[keyboard->mode]);
    lv_keyboard_update_ctrl_map(obj);
}

static void lv_keyboard_update_ctrl_map(lv_obj_t * obj)
{
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    const lv_btnmatrix_ctrl_t * src = kb_ctrl[keyboard->mode];
    if(src == NULL) {
        return;
    }

    if(keyboard->popovers) {
        lv_btnmatrix_set_ctrl_map(obj, src);
        return;
    }

    lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;
    lv_btnmatrix_ctrl_t * ctrl_map = lv_mem_alloc(sizeof(lv_btnmatrix_ctrl_t) * btnm->btn_cnt);
    if(ctrl_map == NULL) {
        return;
    }

    lv_memcpy(ctrl_map, src, sizeof(lv_btnmatrix_ctrl_t) * btnm->btn_cnt);
    for(uint16_t i = 0; i < btnm->btn_cnt; i++) {
        ctrl_map[i] &= (lv_btnmatrix_ctrl_t)(~LV_BTNMATRIX_CTRL_POPOVER);
    }

    lv_btnmatrix_set_ctrl_map(obj, ctrl_map);
    lv_mem_free(ctrl_map);
}

static bool lv_keyboard_commit_cancel(lv_obj_t * obj, lv_keyboard_t * keyboard)
{
    if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN) {
        lv_keyboard_ime_reset(keyboard, true);
    }

    lv_res_t res = lv_event_send(obj, LV_EVENT_CANCEL, NULL);
    if(res != LV_RES_OK) {
        return true;
    }

    if(keyboard->ta) {
        res = lv_event_send(keyboard->ta, LV_EVENT_CANCEL, NULL);
        if(res != LV_RES_OK) {
            return true;
        }
    }

    return true;
}

static bool lv_keyboard_commit_ready(lv_obj_t * obj, lv_keyboard_t * keyboard)
{
    if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN && lv_keyboard_ime_has_composition(keyboard)) {
        if(!lv_keyboard_ime_commit_first_candidate(keyboard)) {
            lv_keyboard_ime_reset(keyboard, true);
        }
    }

    lv_res_t res = lv_event_send(obj, LV_EVENT_READY, NULL);
    if(res != LV_RES_OK) {
        return true;
    }

    if(keyboard->ta) {
        res = lv_event_send(keyboard->ta, LV_EVENT_READY, NULL);
        if(res != LV_RES_OK) {
            return true;
        }
    }

    return true;
}

static void lv_keyboard_switch_mode(lv_obj_t * obj, lv_keyboard_mode_t mode)
{
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    if(keyboard->mode == mode) {
        return;
    }

    if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN && lv_keyboard_ime_has_composition(keyboard)) {
        if(!lv_keyboard_ime_commit_first_candidate(keyboard)) {
            lv_keyboard_ime_reset(keyboard, true);
        }
    }

    keyboard->mode = mode;
    lv_keyboard_update_map(obj);
}

static bool lv_keyboard_handle_common_key(lv_obj_t * obj, lv_keyboard_t * keyboard, const char * txt)
{
    if(strcmp(txt, "abc") == 0) {
        lv_keyboard_switch_mode(obj, LV_KEYBOARD_MODE_TEXT_LOWER);
        return true;
    }

    if(strcmp(txt, "ABC") == 0) {
        lv_keyboard_switch_mode(obj, LV_KEYBOARD_MODE_TEXT_UPPER);
        return true;
    }

    if(strcmp(txt, "1#") == 0) {
        lv_keyboard_switch_mode(obj, LV_KEYBOARD_MODE_SPECIAL);
        return true;
    }

    if(strcmp(txt, "英") == 0) {
        lv_keyboard_switch_mode(obj, LV_KEYBOARD_MODE_IME_CHN);
        return true;
    }

    if(strcmp(txt, "中") == 0) {
        lv_keyboard_switch_mode(obj, LV_KEYBOARD_MODE_TEXT_LOWER);
        return true;
    }

    if(strcmp(txt, LV_SYMBOL_CLOSE) == 0 || strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) {
        return lv_keyboard_commit_cancel(obj, keyboard);
    }

    if(strcmp(txt, LV_SYMBOL_OK) == 0) {
        return lv_keyboard_commit_ready(obj, keyboard);
    }

    return false;
}

static bool lv_keyboard_handle_textarea_key(lv_keyboard_t * keyboard, const char * txt)
{
    if(strcmp(txt, "Enter") == 0 || strcmp(txt, LV_SYMBOL_NEW_LINE) == 0) {
        if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN && lv_keyboard_ime_has_composition(keyboard)) {
            lv_keyboard_ime_reset(keyboard, true);
            return true;
        }

        lv_textarea_add_char(keyboard->ta, '\n');
        if(lv_textarea_get_one_line(keyboard->ta)) {
            lv_res_t res = lv_event_send(keyboard->ta, LV_EVENT_READY, NULL);
            if(res != LV_RES_OK) {
                return true;
            }
        }
        return true;
    }

    if(strcmp(txt, LV_SYMBOL_LEFT) == 0) {
        if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN && lv_keyboard_ime_page_step(keyboard, -1)) {
            return true;
        }
        lv_textarea_cursor_left(keyboard->ta);
        return true;
    }

    if(strcmp(txt, LV_SYMBOL_RIGHT) == 0) {
        if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN && lv_keyboard_ime_page_step(keyboard, 1)) {
            return true;
        }
        lv_textarea_cursor_right(keyboard->ta);
        return true;
    }

    if(strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN) {
            lv_keyboard_handle_ime_chn(keyboard, txt);
        }
        else {
            lv_keyboard_clear_candidates(keyboard);
            lv_textarea_del_char(keyboard->ta);
        }
        return true;
    }

    if(strcmp(txt, "+/-") == 0) {
        uint16_t cur = lv_textarea_get_cursor_pos(keyboard->ta);
        const char * ta_txt = lv_textarea_get_text(keyboard->ta);
        if(ta_txt[0] == '-') {
            lv_textarea_set_cursor_pos(keyboard->ta, 1);
            lv_textarea_del_char(keyboard->ta);
            lv_textarea_add_char(keyboard->ta, '+');
            lv_textarea_set_cursor_pos(keyboard->ta, cur);
        }
        else if(ta_txt[0] == '+') {
            lv_textarea_set_cursor_pos(keyboard->ta, 1);
            lv_textarea_del_char(keyboard->ta);
            lv_textarea_add_char(keyboard->ta, '-');
            lv_textarea_set_cursor_pos(keyboard->ta, cur);
        }
        else {
            lv_textarea_set_cursor_pos(keyboard->ta, 0);
            lv_textarea_add_char(keyboard->ta, '-');
            lv_textarea_set_cursor_pos(keyboard->ta, cur + 1);
        }
        return true;
    }

    if(lv_keyboard_is_mode_label_key(txt)) {
        return true;
    }

    return false;
}

static void lv_keyboard_handle_ime_chn(lv_keyboard_t * keyboard, const char * txt)
{
    if(keyboard->ta == NULL || !lv_obj_is_valid(keyboard->ta)) {
        keyboard->ta = NULL;
        return;
    }

    if(lv_keyboard_is_ascii_letter_key(txt)) {
        lv_keyboard_ime_append_letter(keyboard, (char)tolower((unsigned char)txt[0]));
        return;
    }

    if(strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        lv_keyboard_ime_backspace(keyboard);
        return;
    }

    if(lv_keyboard_is_digit_key(txt) && lv_keyboard_ime_has_composition(keyboard)) {
        int local_idx = txt[0] - '1';
        int absolute_idx = keyboard->candidate_page * LV_KEYBOARD_IME_PAGE_SIZE + local_idx;
        lv_keyboard_select_candidate(keyboard, absolute_idx);
        return;
    }

    if(strcmp(txt, " ") == 0) {
        if(!lv_keyboard_ime_commit_first_candidate(keyboard)) {
            lv_keyboard_ime_reset(keyboard, true);
            lv_textarea_add_char(keyboard->ta, ' ');
        }
        return;
    }

    if(lv_keyboard_is_mode_label_key(txt)) {
        return;
    }

    if(lv_keyboard_is_punctuation_key(txt)) {
        if(!lv_keyboard_ime_commit_first_candidate(keyboard)) {
            lv_keyboard_ime_reset(keyboard, true);
        }
        lv_textarea_add_text(keyboard->ta, txt);
        return;
    }

    if(lv_keyboard_ime_has_composition(keyboard)) {
        if(!lv_keyboard_ime_commit_first_candidate(keyboard)) {
            lv_keyboard_ime_reset(keyboard, true);
        }
    }

    lv_textarea_add_text(keyboard->ta, txt);
}

static bool lv_keyboard_ime_has_composition(const lv_keyboard_t * keyboard)
{
    return keyboard && keyboard->pinyin_buf[0] != '\0';
}

static void lv_keyboard_ime_reset(lv_keyboard_t * keyboard, bool keep_text)
{
    if(keyboard == NULL) {
        return;
    }

    if(!keep_text && keyboard->ta && lv_obj_is_valid(keyboard->ta) && keyboard->pinyin_buf[0] != '\0') {
        size_t len = strlen(keyboard->pinyin_buf);
        lv_textarea_set_cursor_pos(keyboard->ta, keyboard->pinyin_start_pos + (uint32_t)len);
        for(size_t i = 0; i < len; i++) {
            lv_textarea_del_char(keyboard->ta);
        }
    }

    keyboard->pinyin_buf[0] = '\0';
    keyboard->pinyin_start_pos = 0;
    lv_keyboard_clear_candidates(keyboard);
}

static void lv_keyboard_ime_backspace(lv_keyboard_t * keyboard)
{
    if(keyboard == NULL || keyboard->ta == NULL || !lv_obj_is_valid(keyboard->ta)) {
        if(keyboard) {
            keyboard->ta = NULL;
        }
        return;
    }

    if(!lv_keyboard_ime_has_composition(keyboard)) {
        lv_keyboard_clear_candidates(keyboard);
        lv_textarea_del_char(keyboard->ta);
        return;
    }

    size_t len = strlen(keyboard->pinyin_buf);
    if(len == 0) {
        lv_keyboard_clear_candidates(keyboard);
        return;
    }

    lv_textarea_set_cursor_pos(keyboard->ta, keyboard->pinyin_start_pos + (uint32_t)len);
    lv_textarea_del_char(keyboard->ta);
    keyboard->pinyin_buf[len - 1] = '\0';

    if(keyboard->pinyin_buf[0] == '\0') {
        keyboard->pinyin_start_pos = 0;
        lv_keyboard_clear_candidates(keyboard);
        return;
    }

    lv_keyboard_show_candidates(keyboard);
}

static void lv_keyboard_ime_append_letter(lv_keyboard_t * keyboard, char ch)
{
    if(keyboard == NULL || keyboard->ta == NULL || !lv_obj_is_valid(keyboard->ta)) {
        if(keyboard) {
            keyboard->ta = NULL;
        }
        return;
    }

    size_t len = strlen(keyboard->pinyin_buf);
    if(len >= sizeof(keyboard->pinyin_buf) - 1) {
        return;
    }

    if(len == 0) {
        keyboard->pinyin_start_pos = lv_textarea_get_cursor_pos(keyboard->ta);
    }

    lv_textarea_add_char(keyboard->ta, (uint32_t)ch);
    keyboard->pinyin_buf[len] = ch;
    keyboard->pinyin_buf[len + 1] = '\0';
    lv_keyboard_show_candidates(keyboard);
}

static bool lv_keyboard_ime_commit_first_candidate(lv_keyboard_t * keyboard)
{
    if(keyboard == NULL) {
        return false;
    }

    if(keyboard->total_candidates > 0 && keyboard->candidates_list != NULL) {
        lv_keyboard_select_candidate(keyboard, 0);
        return true;
    }

    return false;
}

static void lv_keyboard_ime_commit_text(lv_keyboard_t * keyboard, const char * txt)
{
    if(keyboard == NULL || txt == NULL || keyboard->ta == NULL || !lv_obj_is_valid(keyboard->ta)) {
        if(keyboard) {
            keyboard->ta = NULL;
        }
        return;
    }

    size_t len = strlen(keyboard->pinyin_buf);
    if(len > 0) {
        lv_textarea_set_cursor_pos(keyboard->ta, keyboard->pinyin_start_pos + (uint32_t)len);
        for(size_t i = 0; i < len; i++) {
            lv_textarea_del_char(keyboard->ta);
        }
    }

    lv_textarea_add_text(keyboard->ta, txt);
    lv_keyboard_ime_reset(keyboard, true);
}

static bool lv_keyboard_ime_page_step(lv_keyboard_t * keyboard, int dir)
{
    if(keyboard == NULL || keyboard->candidate_cnt == 0) {
        return false;
    }

    if(dir > 0) {
        int next_start = keyboard->candidate_page + keyboard->candidate_cnt;
        if(next_start >= keyboard->total_candidates) {
            return false;
        }

        keyboard->candidate_page = next_start;
        lv_keyboard_update_candidate_page(keyboard);
        return true;
    }

    if(dir < 0) {
        if(keyboard->candidate_page <= 0) {
            return false;
        }

        int prev_start = 0;
        int scan_start = 0;
        while(scan_start < keyboard->candidate_page) {
            uint8_t visible = lv_keyboard_candidate_visible_count(keyboard, scan_start);
            if(visible == 0) {
                break;
            }

            int next_start = scan_start + visible;
            if(next_start >= keyboard->candidate_page) {
                break;
            }

            prev_start = scan_start;
            scan_start = next_start;
        }

        keyboard->candidate_page = prev_start;
        lv_keyboard_update_candidate_page(keyboard);
        return true;
    }

    return false;
}

static void lv_keyboard_ime_release_map(lv_keyboard_t * keyboard)
{
    if(keyboard->candidate_map) {
        lv_mem_free((void *)keyboard->candidate_map);
        keyboard->candidate_map = NULL;
    }
}

static void lv_keyboard_ime_release_candidates(lv_keyboard_t * keyboard)
{
    lv_keyboard_ime_release_map(keyboard);

    if(keyboard->candidates_list) {
        for(int i = 0; i < keyboard->total_candidates; i++) {
            lv_mem_free((void *)keyboard->candidates_list[i]);
        }
        lv_mem_free((void *)keyboard->candidates_list);
        keyboard->candidates_list = NULL;
    }

    keyboard->candidate_cnt = 0;
    keyboard->candidate_page = 0;
    keyboard->total_candidates = 0;
}

static bool lv_keyboard_ime_ensure_candidate_btnm(lv_keyboard_t * keyboard)
{
    if(keyboard == NULL || keyboard->candidate_list == NULL || !lv_obj_is_valid(keyboard->candidate_list)) {
        keyboard->candidate_list = NULL;
        return false;
    }

    if(keyboard->candidate_btnm && !lv_obj_is_valid(keyboard->candidate_btnm)) {
        keyboard->candidate_btnm = NULL;
    }

    if(keyboard->candidate_btnm == NULL) {
        keyboard->candidate_btnm = lv_btnmatrix_create(keyboard->candidate_list);
        if(keyboard->candidate_btnm == NULL) {
            return false;
        }

        lv_obj_clear_flag(keyboard->candidate_btnm, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_add_flag(keyboard->candidate_btnm, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(keyboard->candidate_btnm, lv_keyboard_candidate_event_cb, LV_EVENT_VALUE_CHANGED, keyboard);
        lv_obj_set_style_base_dir(keyboard->candidate_btnm, LV_BASE_DIR_LTR, 0);
    }

    return true;
}

static uint16_t lv_keyboard_candidate_width_units(const char * candidate)
{
    int char_cnt = lv_keyboard_utf8_char_count(candidate);
    if(char_cnt <= 1) {
        return 1;
    }
    if(char_cnt == 2) {
        return 2;
    }
    if(char_cnt == 3) {
        return 3;
    }
    return 4;
}

static uint8_t lv_keyboard_candidate_page_limit(const char * candidate)
{
    int char_cnt = lv_keyboard_utf8_char_count(candidate);
    if(char_cnt <= 1) {
        return 8;
    }
    if(char_cnt == 2) {
        return 5;
    }
    if(char_cnt == 3) {
        return 4;
    }
    return 3;
}

static uint8_t lv_keyboard_candidate_visible_count(const lv_keyboard_t * keyboard, int start_idx)
{
    if(keyboard == NULL || keyboard->candidates_list == NULL || start_idx < 0 || start_idx >= keyboard->total_candidates) {
        return 0;
    }

    uint8_t limit = lv_keyboard_candidate_page_limit(keyboard->candidates_list[start_idx]);
    if(limit > LV_KEYBOARD_IME_PAGE_SIZE) {
        limit = LV_KEYBOARD_IME_PAGE_SIZE;
    }

    int remaining = keyboard->total_candidates - start_idx;
    if(remaining <= 0) {
        return 0;
    }

    return (uint8_t)((remaining < limit) ? remaining : limit);
}

static void lv_keyboard_show_candidates(lv_keyboard_t * keyboard)
{
    if(keyboard == NULL || !lv_keyboard_ime_has_composition(keyboard)) {
        lv_keyboard_clear_candidates(keyboard);
        return;
    }

    char ** candidates = NULL;
    int count = lv_keyboard_collect_phrase_candidates(keyboard->pinyin_buf, &candidates);

    lv_keyboard_ime_release_candidates(keyboard);

    if(count <= 0 || candidates == NULL) {
        return;
    }

    keyboard->candidates_list = (const char **)candidates;
    keyboard->total_candidates = count;
    keyboard->candidate_page = 0;
    lv_keyboard_update_candidate_page(keyboard);
}

static void lv_keyboard_update_candidate_page(lv_keyboard_t * keyboard)
{
    if(keyboard == NULL || keyboard->total_candidates <= 0 || keyboard->candidates_list == NULL) {
        if(keyboard && keyboard->candidate_btnm && lv_obj_is_valid(keyboard->candidate_btnm)) {
            lv_obj_add_flag(keyboard->candidate_btnm, LV_OBJ_FLAG_HIDDEN);
        }
        if(keyboard) {
            keyboard->candidate_cnt = 0;
            keyboard->candidate_page = 0;
        }
        return;
    }

    if(!lv_keyboard_ime_ensure_candidate_btnm(keyboard)) {
        keyboard->candidate_cnt = 0;
        return;
    }

    if(keyboard->candidate_page < 0) {
        keyboard->candidate_page = 0;
    }
    if(keyboard->candidate_page >= keyboard->total_candidates) {
        keyboard->candidate_page = keyboard->total_candidates - 1;
    }

    int start = keyboard->candidate_page;
    uint8_t visible = lv_keyboard_candidate_visible_count(keyboard, start);
    bool has_prev = keyboard->candidate_page > 0;
    bool has_next = (start + visible) < keyboard->total_candidates;
    uint16_t item_cnt = (uint16_t)(visible + (has_prev ? 1 : 0) + (has_next ? 1 : 0));

    const char ** old_map = keyboard->candidate_map;
    const char ** map = lv_mem_alloc(sizeof(char *) * (item_cnt + 1));
    lv_btnmatrix_ctrl_t * ctrl_map = lv_mem_alloc(sizeof(lv_btnmatrix_ctrl_t) * item_cnt);
    if(map == NULL || ctrl_map == NULL) {
        if(map) {
            lv_mem_free((void *)map);
        }
        if(ctrl_map) {
            lv_mem_free(ctrl_map);
        }
        keyboard->candidate_cnt = 0;
        lv_obj_add_flag(keyboard->candidate_btnm, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    uint16_t map_idx = 0;
    uint16_t ctrl_idx = 0;

    if(has_prev) {
        map[map_idx++] = LV_KEYBOARD_IME_NAV_PREV;
        ctrl_map[ctrl_idx++] = LV_BTNMATRIX_CTRL_CLICK_TRIG | 1;
    }

    for(int i = 0; i < visible; i++) {
        const char * candidate = keyboard->candidates_list[start + i];
        uint16_t width = lv_keyboard_candidate_width_units(candidate);
        map[map_idx++] = candidate;
        ctrl_map[ctrl_idx++] = LV_BTNMATRIX_CTRL_CLICK_TRIG | width;
    }

    if(has_next) {
        map[map_idx++] = LV_KEYBOARD_IME_NAV_NEXT;
        ctrl_map[ctrl_idx++] = LV_BTNMATRIX_CTRL_CLICK_TRIG | 1;
    }

    map[map_idx] = "";

    lv_btnmatrix_set_map(keyboard->candidate_btnm, map);
    lv_btnmatrix_set_ctrl_map(keyboard->candidate_btnm, ctrl_map);
    lv_obj_set_size(keyboard->candidate_btnm, lv_obj_get_width(keyboard->candidate_list), lv_obj_get_height(keyboard->candidate_list));
    lv_obj_center(keyboard->candidate_btnm);
    lv_obj_clear_flag(keyboard->candidate_btnm, LV_OBJ_FLAG_HIDDEN);

    keyboard->candidate_map = map;
    keyboard->candidate_cnt = (uint8_t)visible;

    if(old_map) {
        lv_mem_free((void *)old_map);
    }
    lv_mem_free(ctrl_map);
}

static void lv_keyboard_clear_candidates(lv_keyboard_t * keyboard)
{
    if(keyboard == NULL) {
        return;
    }

    lv_keyboard_ime_release_candidates(keyboard);

    if(keyboard->candidate_btnm && lv_obj_is_valid(keyboard->candidate_btnm)) {
        lv_obj_add_flag(keyboard->candidate_btnm, LV_OBJ_FLAG_HIDDEN);
        lv_btnmatrix_set_selected_btn(keyboard->candidate_btnm, LV_BTNMATRIX_BTN_NONE);
    }
}

static void lv_keyboard_select_candidate(lv_keyboard_t * keyboard, int idx)
{
    if(keyboard == NULL || idx < 0 || idx >= keyboard->total_candidates || keyboard->candidates_list == NULL) {
        return;
    }

    const char * txt = keyboard->candidates_list[idx];
    if(txt == NULL || txt[0] == '\0') {
        return;
    }

    lv_keyboard_ime_commit_text(keyboard, txt);
}

static void lv_keyboard_candidate_event_cb(lv_event_t * e)
{
    lv_obj_t * btnm = lv_event_get_target(e);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)lv_event_get_user_data(e);
    if(keyboard == NULL) {
        return;
    }

    uint16_t btn_id = lv_btnmatrix_get_selected_btn(btnm);
    if(btn_id == LV_BTNMATRIX_BTN_NONE) {
        return;
    }

    const char * txt = lv_btnmatrix_get_btn_text(btnm, btn_id);
    if(txt == NULL) {
        return;
    }

    if(strcmp(txt, LV_KEYBOARD_IME_NAV_PREV) == 0) {
        lv_keyboard_ime_page_step(keyboard, -1);
        return;
    }

    if(strcmp(txt, LV_KEYBOARD_IME_NAV_NEXT) == 0) {
        lv_keyboard_ime_page_step(keyboard, 1);
        return;
    }

    int start = keyboard->candidate_page;
    if(keyboard->candidate_page > 0) {
        btn_id--;
    }

    lv_keyboard_select_candidate(keyboard, start + (int)btn_id);
}

static bool lv_keyboard_is_ascii_letter_key(const char * txt)
{
    return txt && txt[0] != '\0' && txt[1] == '\0' && isalpha((unsigned char)txt[0]) != 0;
}

static bool lv_keyboard_is_digit_key(const char * txt)
{
    return txt && txt[0] >= '1' && txt[0] <= '9' && txt[1] == '\0';
}

static bool lv_keyboard_is_mode_label_key(const char * txt)
{
    if(txt == NULL) {
        return false;
    }

    return strcmp(txt, "QWERTY") == 0 || strcmp(txt, "拼音") == 0 || strcmp(txt, "符号") == 0;
}

static bool lv_keyboard_is_punctuation_key(const char * txt)
{
    static const char * const punctuation_keys[] = {
        ".", ",", ":", ";", "!", "?", "。", "，", "：",
        "_", "-", "+", "&", "/", "*", "=", "%", "#",
        "<", ">", "\\", "@", "$", "(", ")", "{", "}",
        "[", "]", "\"", "'", NULL
    };

    if(txt == NULL) {
        return false;
    }

    for(int i = 0; punctuation_keys[i] != NULL; i++) {
        if(strcmp(txt, punctuation_keys[i]) == 0) {
            return true;
        }
    }

    return false;
}

static uint8_t lv_keyboard_utf8_char_size(const char * str)
{
    unsigned char c = (unsigned char)str[0];
    if((c & 0xF8) == 0xF0) {
        return 4;
    }
    if((c & 0xF0) == 0xE0) {
        return 3;
    }
    if((c & 0xE0) == 0xC0) {
        return 2;
    }
    return 1;
}

static void lv_keyboard_collect_entry_candidates(const char * chinese, char * tmp, size_t tmp_len, size_t tmp_cap,
                                                 char ** out_list, int * out_cnt, int out_max,
                                                 const char * suffix, bool append_suffix)
{
    if(chinese == NULL || out_list == NULL || out_cnt == NULL) {
        return;
    }

    size_t suffix_len = 0;
    if(append_suffix && suffix) {
        suffix_len = strlen(suffix);
    }

    const char * p = chinese;
    while(*p != '\0' && *out_cnt < out_max) {
        uint8_t char_len = lv_keyboard_utf8_char_size(p);
        if(tmp_len + char_len + suffix_len > tmp_cap) {
            p += char_len;
            continue;
        }

        size_t total_len = tmp_len + char_len + suffix_len;
        char * out = lv_mem_alloc(total_len + 1);
        if(out == NULL) {
            p += char_len;
            continue;
        }

        if(tmp_len > 0) {
            lv_memcpy(out, tmp, tmp_len);
        }
        lv_memcpy(out + tmp_len, p, char_len);
        if(suffix_len > 0) {
            lv_memcpy(out + tmp_len + char_len, suffix, suffix_len);
        }
        out[total_len] = '\0';
        out_list[*out_cnt] = out;
        (*out_cnt)++;

        p += char_len;
    }
}

static bool lv_keyboard_ensure_pinyin_trie(void)
{
    if(pinyin_trie.status == 1) {
        return true;
    }
    if(pinyin_trie.status == -1) {
        return false;
    }

    int32_t node_cap = 1;
    int32_t link_cap = 0;
    for(int i = 0; pinyin_dict[i].pinyin != NULL; i++) {
        node_cap += (int32_t)strlen(pinyin_dict[i].pinyin);
        link_cap++;
    }

    if(!lv_keyboard_trie_init(&pinyin_trie, node_cap, link_cap)) {
        pinyin_trie.status = -1;
        return false;
    }

    for(uint32_t i = 0; pinyin_dict[i].pinyin != NULL; i++) {
        if(!lv_keyboard_trie_insert(&pinyin_trie, pinyin_dict[i].pinyin, i)) {
            lv_keyboard_trie_deinit(&pinyin_trie);
            pinyin_trie.status = -1;
            return false;
        }
    }

    pinyin_trie.status = 1;
    return true;
}

static bool lv_keyboard_ensure_phrase_tries(void)
{
    if(phrase_pinyin_trie.status == 1 && phrase_initials_trie.status == 1) {
        return true;
    }
    if(phrase_pinyin_trie.status == -1 || phrase_initials_trie.status == -1) {
        return false;
    }

    int32_t pinyin_node_cap = 1;
    int32_t initials_node_cap = 1;
    int32_t pinyin_link_cap = 0;
    int32_t initials_link_cap = 0;

    for(int i = 0; ime_phrase_dict[i].pinyin != NULL; i++) {
        pinyin_node_cap += (int32_t)strlen(ime_phrase_dict[i].pinyin);
        pinyin_link_cap++;
        if(ime_phrase_dict[i].initials) {
            initials_node_cap += (int32_t)strlen(ime_phrase_dict[i].initials);
            initials_link_cap++;
        }
    }

    if(!lv_keyboard_trie_init(&phrase_pinyin_trie, pinyin_node_cap, pinyin_link_cap) ||
       !lv_keyboard_trie_init(&phrase_initials_trie, initials_node_cap, initials_link_cap)) {
        lv_keyboard_trie_deinit(&phrase_pinyin_trie);
        lv_keyboard_trie_deinit(&phrase_initials_trie);
        phrase_pinyin_trie.status = -1;
        phrase_initials_trie.status = -1;
        return false;
    }

    for(uint32_t i = 0; ime_phrase_dict[i].pinyin != NULL; i++) {
        if(!lv_keyboard_trie_insert(&phrase_pinyin_trie, ime_phrase_dict[i].pinyin, i)) {
            lv_keyboard_trie_deinit(&phrase_pinyin_trie);
            lv_keyboard_trie_deinit(&phrase_initials_trie);
            phrase_pinyin_trie.status = -1;
            phrase_initials_trie.status = -1;
            return false;
        }

        if(ime_phrase_dict[i].initials &&
           !lv_keyboard_trie_insert(&phrase_initials_trie, ime_phrase_dict[i].initials, i)) {
            lv_keyboard_trie_deinit(&phrase_pinyin_trie);
            lv_keyboard_trie_deinit(&phrase_initials_trie);
            phrase_pinyin_trie.status = -1;
            phrase_initials_trie.status = -1;
            return false;
        }
    }

    phrase_pinyin_trie.status = 1;
    phrase_initials_trie.status = 1;
    return true;
}

static bool lv_keyboard_trie_init(lv_keyboard_trie_t * trie, int32_t node_cap, int32_t link_cap)
{
    lv_keyboard_trie_deinit(trie);

    trie->nodes = lv_mem_alloc(sizeof(lv_keyboard_trie_node_t) * node_cap);
    trie->links = lv_mem_alloc(sizeof(lv_keyboard_trie_link_t) * link_cap);
    if(trie->nodes == NULL || (link_cap > 0 && trie->links == NULL)) {
        lv_keyboard_trie_deinit(trie);
        return false;
    }

    trie->node_cap = node_cap;
    trie->link_cap = link_cap;
    trie->node_cnt = 1;
    trie->link_cnt = 0;
    trie->status = 0;

    trie->nodes[0].ch = '\0';
    trie->nodes[0].child_idx = -1;
    trie->nodes[0].sibling_idx = -1;
    trie->nodes[0].entry_head = -1;
    return true;
}

static void lv_keyboard_trie_deinit(lv_keyboard_trie_t * trie)
{
    if(trie->nodes) {
        lv_mem_free(trie->nodes);
    }
    if(trie->links) {
        lv_mem_free(trie->links);
    }

    memset(trie, 0, sizeof(*trie));
}

static int32_t lv_keyboard_trie_find_child(const lv_keyboard_trie_t * trie, int32_t parent_idx, char ch)
{
    if(trie == NULL || trie->nodes == NULL || parent_idx < 0) {
        return -1;
    }

    int32_t child_idx = trie->nodes[parent_idx].child_idx;
    while(child_idx >= 0) {
        if(trie->nodes[child_idx].ch == ch) {
            return child_idx;
        }
        child_idx = trie->nodes[child_idx].sibling_idx;
    }

    return -1;
}

static int32_t lv_keyboard_trie_get_or_add_child(lv_keyboard_trie_t * trie, int32_t parent_idx, char ch)
{
    int32_t child_idx = lv_keyboard_trie_find_child(trie, parent_idx, ch);
    if(child_idx >= 0) {
        return child_idx;
    }

    if(trie->node_cnt >= trie->node_cap) {
        return -1;
    }

    child_idx = trie->node_cnt++;
    trie->nodes[child_idx].ch = ch;
    trie->nodes[child_idx].child_idx = -1;
    trie->nodes[child_idx].entry_head = -1;
    trie->nodes[child_idx].sibling_idx = trie->nodes[parent_idx].child_idx;
    trie->nodes[parent_idx].child_idx = child_idx;
    return child_idx;
}

static bool lv_keyboard_trie_insert(lv_keyboard_trie_t * trie, const char * key, uint32_t entry_idx)
{
    if(trie == NULL || trie->nodes == NULL || key == NULL || key[0] == '\0') {
        return false;
    }

    int32_t node_idx = 0;
    for(const char * p = key; *p != '\0'; p++) {
        node_idx = lv_keyboard_trie_get_or_add_child(trie, node_idx, *p);
        if(node_idx < 0) {
            return false;
        }
    }

    if(trie->link_cnt >= trie->link_cap) {
        return false;
    }

    trie->links[trie->link_cnt].entry_idx = entry_idx;
    trie->links[trie->link_cnt].next_idx = trie->nodes[node_idx].entry_head;
    trie->nodes[node_idx].entry_head = trie->link_cnt;
    trie->link_cnt++;
    return true;
}

static int32_t lv_keyboard_trie_follow(const lv_keyboard_trie_t * trie, const char * key)
{
    if(trie == NULL || trie->nodes == NULL || key == NULL || key[0] == '\0') {
        return -1;
    }

    int32_t node_idx = 0;
    for(const char * p = key; *p != '\0'; p++) {
        node_idx = lv_keyboard_trie_find_child(trie, node_idx, *p);
        if(node_idx < 0) {
            return -1;
        }
    }

    return node_idx;
}

static bool lv_keyboard_trie_has_exact_match(const lv_keyboard_trie_t * trie, const char * key)
{
    int32_t node_idx = lv_keyboard_trie_follow(trie, key);
    return node_idx >= 0 && trie->nodes[node_idx].entry_head >= 0;
}

static void lv_keyboard_trie_collect_entries(const lv_keyboard_trie_t * trie, int32_t node_idx,
                                             uint32_t * out_list, int * out_cnt, int out_max,
                                             bool include_self, bool recursive)
{
    if(trie == NULL || trie->nodes == NULL || node_idx < 0 || out_list == NULL || out_cnt == NULL) {
        return;
    }

    if(include_self) {
        int32_t link_idx = trie->nodes[node_idx].entry_head;
        while(link_idx >= 0 && *out_cnt < out_max) {
            out_list[*out_cnt] = trie->links[link_idx].entry_idx;
            (*out_cnt)++;
            link_idx = trie->links[link_idx].next_idx;
        }
    }

    if(!recursive || *out_cnt >= out_max) {
        return;
    }

    int32_t child_idx = trie->nodes[node_idx].child_idx;
    while(child_idx >= 0 && *out_cnt < out_max) {
        lv_keyboard_trie_collect_entries(trie, child_idx, out_list, out_cnt, out_max, true, true);
        child_idx = trie->nodes[child_idx].sibling_idx;
    }
}

static void lv_keyboard_phrase_collect_hits(const lv_keyboard_trie_t * trie, int32_t node_idx, const char * input,
                                            lv_phrase_hit_t * hits, int * hit_cnt, int out_max,
                                            bool include_self, bool recursive)
{
    if(trie == NULL || trie->nodes == NULL || node_idx < 0 || input == NULL || hits == NULL || hit_cnt == NULL) {
        return;
    }

    if(include_self) {
        int32_t link_idx = trie->nodes[node_idx].entry_head;
        while(link_idx >= 0) {
            const lv_ime_phrase_entry_t * entry = &ime_phrase_dict[trie->links[link_idx].entry_idx];
            uint32_t score = lv_keyboard_phrase_score(input, entry);
            lv_keyboard_phrase_try_insert(hits, hit_cnt, out_max, entry->phrase, score);
            link_idx = trie->links[link_idx].next_idx;
        }
    }

    if(!recursive) {
        return;
    }

    int32_t child_idx = trie->nodes[node_idx].child_idx;
    while(child_idx >= 0) {
        lv_keyboard_phrase_collect_hits(trie, child_idx, input, hits, hit_cnt, out_max, true, true);
        child_idx = trie->nodes[child_idx].sibling_idx;
    }
}

static int lv_keyboard_pinyin_trie_find_matches(const char * pinyin, size_t pos,
                                                lv_keyboard_trie_match_t * matches, int max_matches,
                                                int32_t * prefix_node_idx, size_t * prefix_len)
{
    if(!lv_keyboard_ensure_pinyin_trie()) {
        if(prefix_node_idx) {
            *prefix_node_idx = -1;
        }
        if(prefix_len) {
            *prefix_len = 0;
        }
        return 0;
    }

    int32_t node_idx = 0;
    size_t matched_len = 0;
    int match_cnt = 0;

    for(const char * p = &pinyin[pos]; *p != '\0'; p++) {
        node_idx = lv_keyboard_trie_find_child(&pinyin_trie, node_idx, *p);
        if(node_idx < 0) {
            break;
        }

        matched_len++;
        if(pinyin_trie.nodes[node_idx].entry_head >= 0 && match_cnt < max_matches) {
            matches[match_cnt].node_idx = node_idx;
            matches[match_cnt].key_len = matched_len;
            match_cnt++;
        }
    }

    if(prefix_node_idx) {
        *prefix_node_idx = (matched_len > 0) ? node_idx : -1;
    }
    if(prefix_len) {
        *prefix_len = matched_len;
    }

    return match_cnt;
}

static void lv_keyboard_build_candidates(const char * pinyin, size_t pos,
                                         char * tmp, size_t tmp_len, size_t tmp_cap,
                                         char ** out_list, int * out_cnt, int out_max,
                                         bool allow_partial_tail, uint16_t * visit_budget)
{
    if(visit_budget && *visit_budget == 0) {
        return;
    }

    if(visit_budget) {
        (*visit_budget)--;
    }

    if(*out_cnt >= out_max) {
        return;
    }

    size_t total_len = strlen(pinyin);
    if(pos >= total_len) {
        char * out = lv_mem_alloc(tmp_len + 1);
        if(out == NULL) {
            return;
        }

        lv_memcpy(out, tmp, tmp_len);
        out[tmp_len] = '\0';
        out_list[*out_cnt] = out;
        (*out_cnt)++;
        return;
    }

    if(lv_keyboard_ensure_pinyin_trie()) {
        lv_keyboard_trie_match_t matches[16];
        int32_t prefix_node_idx = -1;
        size_t prefix_len = 0;
        int match_cnt = lv_keyboard_pinyin_trie_find_matches(pinyin, pos, matches, 16, &prefix_node_idx, &prefix_len);

        for(int i = match_cnt - 1; i >= 0 && *out_cnt < out_max; i--) {
            uint32_t entry_ids[8];
            int entry_cnt = 0;
            lv_keyboard_trie_collect_entries(&pinyin_trie, matches[i].node_idx, entry_ids, &entry_cnt,
                                             (int)(sizeof(entry_ids) / sizeof(entry_ids[0])), true, false);

            for(int entry_i = 0; entry_i < entry_cnt && *out_cnt < out_max; entry_i++) {
                const char * chinese = pinyin_dict[entry_ids[entry_i]].chinese;
                const char * p = chinese;
                while(*p != '\0' && *out_cnt < out_max) {
                    uint8_t char_len = lv_keyboard_utf8_char_size(p);
                    if(tmp_len + char_len <= tmp_cap) {
                        lv_memcpy(&tmp[tmp_len], p, char_len);
                        lv_keyboard_build_candidates(pinyin, pos + matches[i].key_len, tmp, tmp_len + char_len, tmp_cap,
                                                     out_list, out_cnt, out_max, allow_partial_tail, visit_budget);
                    }
                    p += char_len;
                }
            }
        }

        if(!allow_partial_tail || *out_cnt >= out_max) {
            return;
        }

        size_t remain_len = total_len - pos;
        if(remain_len == 0 || prefix_node_idx < 0 || prefix_len != remain_len) {
            return;
        }

        uint32_t partial_ids[LV_KEYBOARD_MAX_CANDIDATES];
        int partial_cnt = 0;
        lv_keyboard_trie_collect_entries(&pinyin_trie, prefix_node_idx, partial_ids, &partial_cnt,
                                         LV_KEYBOARD_MAX_CANDIDATES, false, true);

        for(int i = 0; i < partial_cnt && *out_cnt < out_max; i++) {
            lv_keyboard_collect_entry_candidates(pinyin_dict[partial_ids[i]].chinese, tmp, tmp_len, tmp_cap,
                                                 out_list, out_cnt, out_max, NULL, false);
        }
        return;
    }

    int max_match_len = 0;
    for(int i = 0; pinyin_dict[i].pinyin != NULL; i++) {
        size_t plen = strlen(pinyin_dict[i].pinyin);
        if(plen == 0) {
            continue;
        }

        if(strncmp(&pinyin[pos], pinyin_dict[i].pinyin, plen) == 0 && (int)plen > max_match_len) {
            max_match_len = (int)plen;
        }
    }

    for(int cur_len = max_match_len; cur_len >= 1 && *out_cnt < out_max; cur_len--) {
        for(int i = 0; pinyin_dict[i].pinyin != NULL; i++) {
            size_t plen = strlen(pinyin_dict[i].pinyin);
            if(plen == 0 || (int)plen != cur_len) {
                continue;
            }
            if(strncmp(&pinyin[pos], pinyin_dict[i].pinyin, plen) != 0) {
                continue;
            }

            const char * chinese = pinyin_dict[i].chinese;
            const char * p = chinese;
            while(*p != '\0' && *out_cnt < out_max) {
                uint8_t char_len = lv_keyboard_utf8_char_size(p);
                if(tmp_len + char_len <= tmp_cap) {
                    lv_memcpy(&tmp[tmp_len], p, char_len);
                    lv_keyboard_build_candidates(pinyin, pos + plen, tmp, tmp_len + char_len, tmp_cap,
                                                 out_list, out_cnt, out_max, allow_partial_tail, visit_budget);
                }
                p += char_len;
            }
        }
    }

    if(!allow_partial_tail || *out_cnt >= out_max) {
        return;
    }

    size_t remain_len = total_len - pos;
    if(remain_len == 0) {
        return;
    }

    for(int i = 0; pinyin_dict[i].pinyin != NULL && *out_cnt < out_max; i++) {
        size_t plen = strlen(pinyin_dict[i].pinyin);
        if(plen <= remain_len) {
            continue;
        }
        if(strncmp(pinyin_dict[i].pinyin, &pinyin[pos], remain_len) != 0) {
            continue;
        }

        lv_keyboard_collect_entry_candidates(pinyin_dict[i].chinese, tmp, tmp_len, tmp_cap,
                                             out_list, out_cnt, out_max, NULL, false);
    }
}

static int lv_keyboard_collect_phrase_candidates(const char * pinyin, char *** out_list)
{
    if(pinyin == NULL || out_list == NULL) {
        return 0;
    }

    char ** list = lv_mem_alloc(LV_KEYBOARD_MAX_CANDIDATES * sizeof(char *));
    if(list == NULL) {
        return 0;
    }

    int count = 0;
    if(lv_keyboard_should_collect_phrase_candidates(pinyin)) {
        count = lv_keyboard_collect_rime_candidates(pinyin, list, LV_KEYBOARD_MAX_CANDIDATES);
    }

    size_t pinyin_len = strlen(pinyin);
    bool mix_char_candidates = (count < LV_KEYBOARD_MAX_CANDIDATES);
    if(count > 0 && pinyin_len > LV_KEYBOARD_CHAR_MIX_MAX_PINYIN_LEN) {
        mix_char_candidates = false;
    }

    if(mix_char_candidates) {
        char ** extra = lv_mem_alloc(LV_KEYBOARD_MAX_CANDIDATES * sizeof(char *));
        if(extra) {
            int extra_cnt = lv_keyboard_collect_char_candidates(pinyin, extra, LV_KEYBOARD_MAX_CANDIDATES);
            int i = 0;
            for(; i < extra_cnt && count < LV_KEYBOARD_MAX_CANDIDATES; i++) {
                bool dup = false;
                for(int j = 0; j < count; j++) {
                    if(strcmp(list[j], extra[i]) == 0) {
                        dup = true;
                        break;
                    }
                }

                if(dup) {
                    lv_mem_free(extra[i]);
                }
                else {
                    list[count++] = extra[i];
                    extra[i] = NULL;
                }
            }

            for(; i < extra_cnt; i++) {
                if(extra[i]) {
                    lv_mem_free(extra[i]);
                }
            }
            lv_mem_free(extra);
        }
    }

    if(count == 0) {
        lv_mem_free(list);
        return 0;
    }

    *out_list = list;
    return count;
}

static uint32_t lv_keyboard_phrase_score(const char * input, const lv_ime_phrase_entry_t * entry)
{
    if(input == NULL || entry == NULL || entry->pinyin == NULL || entry->phrase == NULL) {
        return 0;
    }

    size_t in_len = strlen(input);
    if(in_len == 0) {
        return 0;
    }

    if(strcmp(input, entry->pinyin) == 0) {
        return 3000000000u + entry->freq;
    }

    if(strncmp(entry->pinyin, input, in_len) == 0) {
        return 2000000000u + entry->freq;
    }

    if(entry->initials && strncmp(entry->initials, input, in_len) == 0) {
        return 1000000000u + entry->freq;
    }

    return 0;
}

static void lv_keyboard_phrase_try_insert(lv_phrase_hit_t * hits, int * hit_cnt, int max,
                                          const char * phrase, uint32_t score)
{
    if(hits == NULL || hit_cnt == NULL || phrase == NULL || score == 0) {
        return;
    }

    for(int i = 0; i < *hit_cnt; i++) {
        if(strcmp(hits[i].phrase, phrase) == 0) {
            if(score > hits[i].score) {
                hits[i].score = score;
            }
            return;
        }
    }

    if(*hit_cnt < max) {
        hits[*hit_cnt].phrase = phrase;
        hits[*hit_cnt].score = score;
        (*hit_cnt)++;
        return;
    }

    int min_idx = 0;
    for(int i = 1; i < *hit_cnt; i++) {
        if(hits[i].score < hits[min_idx].score) {
            min_idx = i;
        }
    }

    if(score > hits[min_idx].score) {
        hits[min_idx].phrase = phrase;
        hits[min_idx].score = score;
    }
}

static int lv_keyboard_collect_rime_candidates(const char * pinyin, char ** out_list, int out_max)
{
    if(pinyin == NULL || out_list == NULL || out_max <= 0) {
        return 0;
    }

    lv_phrase_hit_t hits[LV_KEYBOARD_MAX_CANDIDATES];
    int hit_cnt = 0;

    if(lv_keyboard_ensure_phrase_tries()) {
        int32_t pinyin_node_idx = lv_keyboard_trie_follow(&phrase_pinyin_trie, pinyin);
        if(pinyin_node_idx >= 0) {
            lv_keyboard_phrase_collect_hits(&phrase_pinyin_trie, pinyin_node_idx, pinyin, hits, &hit_cnt, out_max, true, true);
        }

        int32_t initials_node_idx = lv_keyboard_trie_follow(&phrase_initials_trie, pinyin);
        if(initials_node_idx >= 0) {
            lv_keyboard_phrase_collect_hits(&phrase_initials_trie, initials_node_idx, pinyin, hits, &hit_cnt, out_max, true, true);
        }
    }
    else {
        for(int i = 0; ime_phrase_dict[i].pinyin != NULL; i++) {
            uint32_t score = lv_keyboard_phrase_score(pinyin, &ime_phrase_dict[i]);
            if(score == 0) {
                continue;
            }

            lv_keyboard_phrase_try_insert(hits, &hit_cnt, out_max, ime_phrase_dict[i].phrase, score);
        }
    }

    if(hit_cnt == 0) {
        return 0;
    }

    for(int i = 0; i < hit_cnt - 1; i++) {
        for(int j = i + 1; j < hit_cnt; j++) {
            if(hits[j].score > hits[i].score) {
                lv_phrase_hit_t tmp = hits[i];
                hits[i] = hits[j];
                hits[j] = tmp;
            }
        }
    }

    int out_cnt = 0;
    for(int i = 0; i < hit_cnt && out_cnt < out_max; i++) {
        size_t len = strlen(hits[i].phrase);
        char * out = lv_mem_alloc(len + 1);
        if(out == NULL) {
            continue;
        }

        lv_memcpy(out, hits[i].phrase, len);
        out[len] = '\0';
        out_list[out_cnt++] = out;
    }

    return out_cnt;
}

static int lv_keyboard_collect_char_candidates(const char * pinyin, char ** out_list, int out_max)
{
    if(pinyin == NULL || out_list == NULL || out_max <= 0) {
        return 0;
    }

    int count = 0;
    char tmp[64];
    uint16_t exact_budget = LV_KEYBOARD_BUILD_VISIT_BUDGET;
    lv_keyboard_build_candidates(pinyin, 0, tmp, 0, sizeof(tmp) - 1, out_list, &count, out_max, false, &exact_budget);

    if(count < out_max) {
        char ** partial = lv_mem_alloc(sizeof(char *) * out_max);
        if(partial) {
            int partial_cnt = 0;
            uint16_t partial_budget = LV_KEYBOARD_BUILD_VISIT_BUDGET;
            lv_keyboard_build_candidates(pinyin, 0, tmp, 0, sizeof(tmp) - 1, partial, &partial_cnt, out_max, true,
                                         &partial_budget);

            for(int i = 0; i < partial_cnt && count < out_max; i++) {
                bool dup = false;
                for(int j = 0; j < count; j++) {
                    if(strcmp(out_list[j], partial[i]) == 0) {
                        dup = true;
                        break;
                    }
                }

                if(dup) {
                    lv_mem_free(partial[i]);
                    partial[i] = NULL;
                }
                else {
                    out_list[count++] = partial[i];
                    partial[i] = NULL;
                }
            }

            for(int i = 0; i < partial_cnt; i++) {
                if(partial[i]) {
                    lv_mem_free(partial[i]);
                }
            }
            lv_mem_free(partial);
        }
    }

    return count;
}

static bool lv_keyboard_should_collect_phrase_candidates(const char * pinyin)
{
    if(pinyin == NULL || pinyin[0] == '\0') {
        return false;
    }

    size_t len = strlen(pinyin);
    if(len <= 1) {
        return false;
    }

    if(lv_keyboard_is_single_syllable(pinyin)) {
        return false;
    }

    if(lv_keyboard_is_multi_syllable_input(pinyin)) {
        return true;
    }

    if(lv_keyboard_ensure_phrase_tries()) {
        return lv_keyboard_trie_follow(&phrase_initials_trie, pinyin) >= 0;
    }

    return lv_keyboard_is_two_syllables(pinyin);
}

static bool lv_keyboard_is_multi_syllable_input(const char * pinyin)
{
    if(pinyin == NULL || pinyin[0] == '\0') {
        return false;
    }

    size_t total_len = strlen(pinyin);
    if(total_len < 2 || !lv_keyboard_ensure_pinyin_trie()) {
        return false;
    }

    int8_t exact_cnt[16];
    for(size_t i = 0; i <= total_len; i++) {
        exact_cnt[i] = -1;
    }
    exact_cnt[0] = 0;

    for(size_t pos = 0; pos < total_len; pos++) {
        if(exact_cnt[pos] < 0) {
            continue;
        }

        lv_keyboard_trie_match_t matches[16];
        int32_t prefix_node_idx = -1;
        size_t prefix_len = 0;
        int match_cnt = lv_keyboard_pinyin_trie_find_matches(pinyin, pos, matches, 16, &prefix_node_idx, &prefix_len);

        for(int i = 0; i < match_cnt; i++) {
            size_t next_pos = pos + matches[i].key_len;
            int8_t next_cnt = (int8_t)(exact_cnt[pos] + 1);
            if(next_cnt > exact_cnt[next_pos]) {
                exact_cnt[next_pos] = next_cnt;
            }
        }

        if(exact_cnt[pos] >= 1 && prefix_node_idx >= 0 && prefix_len == (total_len - pos)) {
            return true;
        }
    }

    if(exact_cnt[total_len] >= 2) {
        return true;
    }

    for(size_t pos = 1; pos < total_len; pos++) {
        if(exact_cnt[pos] < 1) {
            continue;
        }

        lv_keyboard_trie_match_t matches[16];
        int32_t prefix_node_idx = -1;
        size_t prefix_len = 0;
        lv_keyboard_pinyin_trie_find_matches(pinyin, pos, matches, 16, &prefix_node_idx, &prefix_len);
        if(prefix_node_idx >= 0 && prefix_len == (total_len - pos)) {
                return true;
        }
    }

    return false;
}

static bool lv_keyboard_is_single_syllable(const char * pinyin)
{
    if(pinyin == NULL || pinyin[0] == '\0') {
        return false;
    }

    if(lv_keyboard_ensure_pinyin_trie()) {
        return lv_keyboard_trie_has_exact_match(&pinyin_trie, pinyin);
    }

    for(int i = 0; pinyin_dict[i].pinyin != NULL; i++) {
        if(strcmp(pinyin_dict[i].pinyin, pinyin) == 0) {
            return true;
        }
    }

    return false;
}

static bool lv_keyboard_is_two_syllables(const char * pinyin)
{
    if(pinyin == NULL || pinyin[0] == '\0') {
        return false;
    }

    size_t total_len = strlen(pinyin);
    if(total_len < 2) {
        return false;
    }

    if(lv_keyboard_ensure_pinyin_trie()) {
        for(size_t split = 1; split < total_len; split++) {
            char first[16];
            size_t first_len = split;
            if(first_len >= sizeof(first)) {
                break;
            }

            lv_memcpy(first, pinyin, first_len);
            first[first_len] = '\0';
            if(lv_keyboard_trie_has_exact_match(&pinyin_trie, first) &&
               lv_keyboard_trie_has_exact_match(&pinyin_trie, pinyin + split)) {
                return true;
            }
        }
        return false;
    }

    for(int i = 0; pinyin_dict[i].pinyin != NULL; i++) {
        const char * first = pinyin_dict[i].pinyin;
        size_t flen = strlen(first);
        if(flen == 0 || flen >= total_len) {
            continue;
        }
        if(strncmp(pinyin, first, flen) != 0) {
            continue;
        }

        const char * rest = pinyin + flen;
        for(int j = 0; pinyin_dict[j].pinyin != NULL; j++) {
            if(strcmp(pinyin_dict[j].pinyin, rest) == 0) {
                return true;
            }
        }
    }

    return false;
}

static int lv_keyboard_utf8_char_count(const char * str)
{
    if(str == NULL) {
        return 0;
    }

    int count = 0;
    int idx = 0;
    while(str[idx] != '\0') {
        unsigned char c = (unsigned char)str[idx];
        if((c & 0xF8) == 0xF0) {
            idx += 4;
        }
        else if((c & 0xF0) == 0xE0) {
            idx += 3;
        }
        else if((c & 0xE0) == 0xC0) {
            idx += 2;
        }
        else {
            idx += 1;
        }
        count++;
    }

    return count;
}

#endif  /*LV_USE_KEYBOARD*/
