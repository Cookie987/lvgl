
/**
 * @file lv_keyboard.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_keyboard_private.h"

#if LV_USE_KEYBOARD

#include "../../core/lv_obj_class_private.h"
#include "lv_ime_dict.h"
#include "lv_ime_phrase_dict.h"

#include <ctype.h>

/*Testing of dependencies*/
#if LV_USE_BUTTONMATRIX == 0
    #error "lv_buttonmatrix is required. Enable it in lv_conf.h (LV_USE_BUTTONMATRIX  1) "
#endif

#if LV_USE_TEXTAREA == 0
    #error "lv_textarea is required. Enable it in lv_conf.h (LV_USE_TEXTAREA  1) "
#endif

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS (&lv_keyboard_class)
#define LV_KB_BTN(width) LV_BUTTONMATRIX_CTRL_POPOVER | width

/** Largest candidate list kept for one composition. */
#define LV_KEYBOARD_MAX_CANDIDATES 100
/** Candidates shown before a page break, and the digits `1`..`8` that pick one. */
#define LV_KEYBOARD_IME_PAGE_SIZE 8
/** Buttons that step to the previous/next page of candidates. */
#define LV_KEYBOARD_IME_NAV_PREV "<"
#define LV_KEYBOARD_IME_NAV_NEXT ">"
/** How many trie nodes one candidate search may visit before it gives up. */
#define LV_KEYBOARD_BUILD_VISIT_BUDGET 512
/** Above this many pinyin letters, character candidates are not mixed in. */
#define LV_KEYBOARD_CHAR_MIX_MAX_PINYIN_LEN 6
/** Longest pinyin sequence a single search may look at, including the terminator. */
#define LV_KEYBOARD_IME_LOOKUP_MAX (LV_KEYBOARD_IME_PINYIN_BUF_SIZE)

#ifndef LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_LOWER
    #define LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_LOWER     "abc"
#endif
#ifndef LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_UPPER
    #define LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_UPPER     "ABC"
#endif
#ifndef LV_KEYBOARD_CTRL_BUTTON_MODE_SPECIAL
    #define LV_KEYBOARD_CTRL_BUTTON_MODE_SPECIAL        "1#"
#endif
#ifndef LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_ARABIC
    #define LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_ARABIC    "أب"
#endif

/*Labels of the button that switches between the latin and pinyin layouts. Each
 *one names the language it switches *to*, the same way the existing `abc`/`ABC`
 *buttons do, so `英` sits in the latin layouts and `中` in the pinyin one.*/
#define LV_KEYBOARD_IME_BTN_ENTER_CHN "英"
#define LV_KEYBOARD_IME_BTN_ENTER_LATIN "中"

/*The wide bottom-row button stays a space key, but its label names the layout it
 *is on, so the row reads as a mode indicator. `lv_keyboard_def_event_cb()` maps
 *all three labels back to a space character.*/
#define LV_KEYBOARD_BTN_SPACE_LATIN "QWERTY"
#define LV_KEYBOARD_BTN_SPACE_SPECIAL "符号"
#define LV_KEYBOARD_BTN_SPACE_PINYIN "拼音"

/**********************
 *      TYPEDEFS
 **********************/

/** One node of the pinyin/phrase lookup trie. Children and per-node entries are
 *singly linked lists threaded through `sibling_idx` and `links[]`.*/
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

/** A trie allocated once on first use and kept for the lifetime of the program.
 *`status` is 0 = not built, 1 = ready, -1 = building failed and is not retried.*/
typedef struct {
    lv_keyboard_trie_node_t * nodes;
    lv_keyboard_trie_link_t * links;
    int32_t node_cnt;
    int32_t node_cap;
    int32_t link_cnt;
    int32_t link_cap;
    int8_t status;
} lv_keyboard_trie_t;

/** One pinyin syllable matching at a position in the input. */
typedef struct {
    int32_t node_idx;
    size_t key_len;
} lv_keyboard_trie_match_t;

/** A phrase that matched a prefix of the input, with its ranking score. */
typedef struct {
    const char * phrase;
    uint32_t score;
} lv_phrase_hit_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_keyboard_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);

static void lv_keyboard_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);

static void lv_keyboard_update_map(lv_obj_t * obj);

static void lv_keyboard_update_ctrl_map(lv_obj_t * obj);

/*Chinese IME*/
static void lv_keyboard_ime_handle_key(lv_keyboard_t * keyboard, const char * txt);
static void lv_keyboard_ime_commit_or_reset(lv_keyboard_t * keyboard);
static bool lv_keyboard_ime_has_composition(const lv_keyboard_t * keyboard);
static void lv_keyboard_ime_reset(lv_keyboard_t * keyboard, bool delete_text);
static void lv_keyboard_ime_append_letter(lv_keyboard_t * keyboard, char ch);
static void lv_keyboard_ime_backspace(lv_keyboard_t * keyboard);
static void lv_keyboard_ime_commit(lv_keyboard_t * keyboard, const char * txt);
static bool lv_keyboard_ime_page_step(lv_keyboard_t * keyboard, int dir);
static void lv_keyboard_ime_clear_candidates(lv_keyboard_t * keyboard);
static void lv_keyboard_ime_show_candidates(lv_keyboard_t * keyboard);
static void lv_keyboard_ime_update_page(lv_keyboard_t * keyboard);
static void lv_keyboard_ime_detach_candidates(lv_keyboard_t * keyboard);
static void lv_keyboard_ime_on_target_deleted(lv_event_t * e);
static void lv_keyboard_ime_candidate_event_cb(lv_event_t * e);
static void lv_keyboard_ime_replace_composition(lv_keyboard_t * keyboard, const char * text);
static uint8_t lv_keyboard_ime_visible_count(const lv_keyboard_t * keyboard, int start);
static uint16_t lv_keyboard_ime_button_width(const char * candidate);
static bool lv_keyboard_ime_ensure_buttonmatrix(lv_keyboard_t * keyboard);
static bool lv_keyboard_is_space_key(const char * txt);

/*Candidate search*/
static bool lv_keyboard_pinyin_index_init(void);
static bool lv_keyboard_phrase_index_init(void);
static int lv_keyboard_collect_mixed_candidates(const char * pinyin, char *** out_list);
static int lv_keyboard_collect_phrase_candidates(const char * pinyin, char ** out_list, int out_max);
static int lv_keyboard_collect_char_candidates(const char * pinyin, char ** out_list, int out_max);
static bool lv_keyboard_want_phrase_candidates(const char * pinyin);
static bool lv_keyboard_is_single_syllable(const char * pinyin);
static bool lv_keyboard_is_multi_syllable(const char * pinyin);
static uint32_t lv_keyboard_phrase_score(const char * input, const lv_ime_phrase_entry_t * entry);
static void lv_keyboard_phrase_try_insert(lv_phrase_hit_t * hits, int * hit_cnt, int max,
                                          const char * phrase, uint32_t score);
static void lv_keyboard_phrase_collect_hits(const lv_keyboard_trie_t * trie, int32_t node_idx,
                                            const char * input, lv_phrase_hit_t * hits, int * hit_cnt,
                                            int out_max, bool include_self, bool recursive);
static int lv_keyboard_pinyin_prefix_matches(const char * pinyin, size_t pos,
                                             lv_keyboard_trie_match_t * matches, int max_matches,
                                             int32_t * prefix_node_idx, size_t * prefix_len);
static void lv_keyboard_build_candidates(const char * pinyin, size_t pos, char * tmp, size_t tmp_len,
                                         size_t tmp_cap, char ** out_list, int * out_cnt, int out_max,
                                         bool allow_partial_tail, uint16_t * visit_budget);

/*Trie*/
static bool lv_keyboard_trie_init(lv_keyboard_trie_t * trie, int32_t node_cap, int32_t link_cap);
static void lv_keyboard_trie_deinit(lv_keyboard_trie_t * trie);
static int32_t lv_keyboard_trie_find_child(const lv_keyboard_trie_t * trie, int32_t parent_idx, char ch);
static int32_t lv_keyboard_trie_get_or_add_child(lv_keyboard_trie_t * trie, int32_t parent_idx, char ch);
static bool lv_keyboard_trie_insert(lv_keyboard_trie_t * trie, const char * key, uint32_t entry_idx);
static int32_t lv_keyboard_trie_follow(const lv_keyboard_trie_t * trie, const char * key);
static void lv_keyboard_trie_collect_entries(const lv_keyboard_trie_t * trie, int32_t node_idx,
                                             uint32_t * out, int * out_cnt, int out_max,
                                             bool include_self, bool recursive);

/*UTF-8 helpers*/
static uint8_t lv_keyboard_utf8_char_size(const char * str);
static int lv_keyboard_utf8_char_count(const char * str);

/**********************
 *  STATIC VARIABLES
 **********************/
#if LV_USE_OBJ_PROPERTY
static const lv_property_ops_t lv_keyboard_properties[] = {
    {
        .id = LV_PROPERTY_KEYBOARD_TEXTAREA,
        .setter = lv_keyboard_set_textarea,
        .getter = lv_keyboard_get_textarea,
    },
    {
        .id = LV_PROPERTY_KEYBOARD_MODE,
        .setter = lv_keyboard_set_mode,
        .getter = lv_keyboard_get_mode,
    },
    {
        .id = LV_PROPERTY_KEYBOARD_POPOVERS,
        .setter = lv_keyboard_set_popovers,
        .getter = lv_keyboard_get_popovers,
    },
    {
        .id = LV_PROPERTY_KEYBOARD_SELECTED_BUTTON,
        .setter = lv_buttonmatrix_set_selected_button,
        .getter = lv_keyboard_get_selected_button,
    },
};
#endif

const lv_obj_class_t lv_keyboard_class = {
    .constructor_cb = lv_keyboard_constructor,
    .destructor_cb = lv_keyboard_destructor,
    .width_def = LV_PCT(100),
    .height_def = LV_PCT(50),
    .instance_size = sizeof(lv_keyboard_t),
    .editable = 1,
    .base_class = &lv_buttonmatrix_class,
    .name = "lv_keyboard",
    LV_PROPERTY_CLASS_FIELDS(keyboard, KEYBOARD)
};

static const char * const default_kb_map_lc[] = {LV_KEYBOARD_CTRL_BUTTON_MODE_SPECIAL, "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
                                                 LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_UPPER, "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
                                                 "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
                                                 LV_SYMBOL_KEYBOARD, LV_KEYBOARD_IME_BTN_ENTER_CHN,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
                                                 LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_ARABIC,
#endif
                                                 LV_SYMBOL_LEFT, LV_KEYBOARD_BTN_SPACE_LATIN, LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
                                                };

static const lv_buttonmatrix_ctrl_t default_kb_ctrl_lc_map[] = {
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5, LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_BUTTONMATRIX_CTRL_CHECKED | 7,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6, LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_BUTTONMATRIX_CTRL_CHECKED | 7,
    LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
#endif
    LV_BUTTONMATRIX_CTRL_CHECKED | 2, 6, LV_BUTTONMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2
};

static const char * const default_kb_map_uc[] = {LV_KEYBOARD_CTRL_BUTTON_MODE_SPECIAL, "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
                                                 LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_LOWER, "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
                                                 "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
                                                 LV_SYMBOL_CLOSE, LV_KEYBOARD_IME_BTN_ENTER_CHN,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
                                                 LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_ARABIC,
#endif
                                                 LV_SYMBOL_LEFT, LV_KEYBOARD_BTN_SPACE_LATIN, LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
                                                };

static const lv_buttonmatrix_ctrl_t default_kb_ctrl_uc_map[] = {
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5, LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_BUTTONMATRIX_CTRL_CHECKED | 7,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6, LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_BUTTONMATRIX_CTRL_CHECKED | 7,
    LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
#endif
    LV_BUTTONMATRIX_CTRL_CHECKED | 2, 6, LV_BUTTONMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2
};

#if LV_USE_ARABIC_PERSIAN_CHARS == 1
static const char * const default_kb_map_ar[] = {
    LV_KEYBOARD_CTRL_BUTTON_MODE_SPECIAL, "ض", "ص", "ث", "ق", "ف", "غ", "ع", "ه", "خ", "ح", "ج", "\n",
    "ش", "س", "ي", "ب", "ل", "ا", "ت", "ن", "م", "ك", "ط", LV_SYMBOL_BACKSPACE, "\n",
    "ذ", "ء", "ؤ", "ر", "ى", "ة", "و", "ز", "ظ", "د", "ز", "ظ", "د", "\n",
    LV_SYMBOL_CLOSE, LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_LOWER, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_NEW_LINE, LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t default_kb_ctrl_ar_map[] = {
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 7,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, 2, 6, 2, 3, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2
};
#endif

static const char * const default_kb_map_spec[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
                                                   LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_LOWER, "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
                                                   "\\",  "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
                                                   LV_SYMBOL_KEYBOARD,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
                                                   LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_ARABIC,
#endif
                                                   LV_SYMBOL_LEFT, LV_KEYBOARD_BTN_SPACE_SPECIAL, LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
                                                  };

static const lv_buttonmatrix_ctrl_t default_kb_ctrl_spec_map[] = {
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | 2,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
#endif
    LV_BUTTONMATRIX_CTRL_CHECKED | 2, 6, LV_BUTTONMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2
};

static const char * const default_kb_map_num[] = {"1", "2", "3", LV_SYMBOL_KEYBOARD, "\n",
                                                  "4", "5", "6", LV_SYMBOL_OK, "\n",
                                                  "7", "8", "9", LV_SYMBOL_BACKSPACE, "\n",
                                                  "+/-", "0", ".", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, ""
                                                 };

static const lv_buttonmatrix_ctrl_t default_kb_ctrl_num_map[] = {
    1, 1, 1, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
    1, 1, 1, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
    1, 1, 1, 2,
    1, 1, 1, 1, 1
};

/*The Chinese IME layout: a QWERTY letter block with pinyin-only bottom row.
 * The `1`..`8` digits pick a candidate, and the mode buttons are labelled in
 * Chinese so they read as "switch to 中/英" rather than "abc/ABC".*/
static const char * const ime_kb_map_chn[] = {LV_KEYBOARD_CTRL_BUTTON_MODE_SPECIAL, "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
                                              LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_UPPER, "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
                                              "_", "-", "z", "x", "c", "v", "b", "n", "m", "。", "，", "：", "\n",
                                              LV_SYMBOL_KEYBOARD, LV_KEYBOARD_IME_BTN_ENTER_LATIN, LV_SYMBOL_LEFT, LV_KEYBOARD_BTN_SPACE_PINYIN, LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
                                             };

static const lv_buttonmatrix_ctrl_t ime_kb_ctrl_chn_map[] = {
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5, LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_BUTTONMATRIX_CTRL_CHECKED | 7,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6, LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_BUTTONMATRIX_CTRL_CHECKED | 7,
    LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(1),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_BUTTONMATRIX_CTRL_CHECKED | 2, 6, LV_BUTTONMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2
};

/*Map and control map of the candidate button matrix, rebuilt for each page. Both
 *live here so the translations cannot get out of step, which matters because a
 *string set to just {""} is how the matrix is told to show nothing.*/
static const char * const ime_no_candidates[] = {""};

/*``[mode] = ...`` rather than positional initialisers: the Arabic entry is
 *conditional, so positional entries would shift every mode after IME_CHN
 *depending on whether Arabic is enabled.*/
static const char * const * kb_map[] = {
    [LV_KEYBOARD_MODE_TEXT_LOWER] = default_kb_map_lc,
    [LV_KEYBOARD_MODE_TEXT_UPPER] = default_kb_map_uc,
    [LV_KEYBOARD_MODE_SPECIAL] = default_kb_map_spec,
    [LV_KEYBOARD_MODE_NUMBER] = default_kb_map_num,
    [LV_KEYBOARD_MODE_USER_1] = default_kb_map_lc,
    [LV_KEYBOARD_MODE_USER_2] = default_kb_map_lc,
    [LV_KEYBOARD_MODE_USER_3] = default_kb_map_lc,
    [LV_KEYBOARD_MODE_USER_4] = default_kb_map_lc,
    [LV_KEYBOARD_MODE_IME_CHN] = ime_kb_map_chn,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
    [LV_KEYBOARD_MODE_TEXT_ARABIC] = default_kb_map_ar,
#endif
};

static const lv_buttonmatrix_ctrl_t * kb_ctrl[] = {
    [LV_KEYBOARD_MODE_TEXT_LOWER] = default_kb_ctrl_lc_map,
    [LV_KEYBOARD_MODE_TEXT_UPPER] = default_kb_ctrl_uc_map,
    [LV_KEYBOARD_MODE_SPECIAL] = default_kb_ctrl_spec_map,
    [LV_KEYBOARD_MODE_NUMBER] = default_kb_ctrl_num_map,
    [LV_KEYBOARD_MODE_USER_1] = default_kb_ctrl_lc_map,
    [LV_KEYBOARD_MODE_USER_2] = default_kb_ctrl_lc_map,
    [LV_KEYBOARD_MODE_USER_3] = default_kb_ctrl_lc_map,
    [LV_KEYBOARD_MODE_USER_4] = default_kb_ctrl_lc_map,
    [LV_KEYBOARD_MODE_IME_CHN] = ime_kb_ctrl_chn_map,
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
    [LV_KEYBOARD_MODE_TEXT_ARABIC] = default_kb_ctrl_ar_map,
#endif
};

/*Built on first use and kept for the life of the program: the dictionaries are
 *fixed, so the tries only ever need to be built once.*/
static lv_keyboard_trie_t pinyin_index;
static lv_keyboard_trie_t phrase_pinyin_index;
static lv_keyboard_trie_t phrase_initials_index;

/**********************
 *      MACROS
 **********************/

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

/*=====================
 * Setter functions
 *====================*/

void lv_keyboard_set_textarea(lv_obj_t * obj, lv_obj_t * ta)
{
    if(ta) {
        LV_CHECK_OBJ(ta, &lv_textarea_class, return);
    }

    LV_CHECK_OBJ(obj, MY_CLASS, return);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;

    /*The composition tracks positions in the old text area, so it cannot survive
     *a switch to another one.*/
    lv_keyboard_ime_reset(keyboard, false);

    /*Hide the cursor of the old Text area if cursor management is enabled*/
    if(keyboard->ta) {
        lv_obj_remove_state(keyboard->ta, LV_STATE_FOCUSED);
    }

    keyboard->ta = ta;

    /*Show the cursor of the new Text area if cursor management is enabled*/
    if(keyboard->ta) {
        lv_obj_add_state(keyboard->ta, LV_STATE_FOCUSED);
    }
}

void lv_keyboard_set_mode(lv_obj_t * obj, lv_keyboard_mode_t mode)
{
    LV_CHECK_OBJ(obj, MY_CLASS, return);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    if(keyboard->mode == mode) return;

    /*Leaving pinyin mode with an uncommitted composition would strand the pinyin
     *in the text area with no candidate list to resolve it, so commit it first.*/
    lv_keyboard_ime_commit_or_reset(keyboard);

    keyboard->mode = mode;
    lv_keyboard_update_map(obj);
}

void lv_keyboard_set_candidate_list(lv_obj_t * obj, lv_obj_t * list)
{
    LV_CHECK_OBJ(obj, MY_CLASS, return);
    LV_CHECK_ARG(list == NULL || lv_obj_is_valid(list), return);

    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    if(keyboard->candidate_list == list) return;

    if(keyboard->candidate_list && lv_obj_is_valid(keyboard->candidate_list)) {
        lv_obj_remove_event_cb_with_user_data(keyboard->candidate_list, lv_keyboard_ime_on_target_deleted, keyboard);
    }

    lv_keyboard_ime_detach_candidates(keyboard);
    keyboard->candidate_list = list;

    if(keyboard->candidate_list) {
        lv_obj_add_event_cb(keyboard->candidate_list, lv_keyboard_ime_on_target_deleted, LV_EVENT_DELETE, keyboard);
    }
}

void lv_keyboard_reset_ime(lv_obj_t * obj)
{
    LV_CHECK_OBJ(obj, MY_CLASS, return);
    lv_keyboard_ime_reset((lv_keyboard_t *)obj, false);
}

void lv_keyboard_set_popovers(lv_obj_t * obj, bool en)
{
    LV_CHECK_OBJ(obj, MY_CLASS, return);

    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;

    if(keyboard->popovers == en) {
        return;
    }

    keyboard->popovers = en;
    lv_keyboard_update_ctrl_map(obj);
}

void lv_keyboard_set_map(lv_obj_t * obj, lv_keyboard_mode_t mode, const char * const map[],
                         const lv_buttonmatrix_ctrl_t ctrl_map[])
{
    LV_CHECK_OBJ(obj, MY_CLASS, return);
    LV_CHECK_ARG(map != NULL, return);
    LV_CHECK_ARG(ctrl_map != NULL, return);
    LV_CHECK_ARG(mode < LV_ARRAYLEN(kb_map), return);
    LV_CHECK_ARG(mode < LV_ARRAYLEN(kb_ctrl), return);
    kb_map[mode] = map;
    kb_ctrl[mode] = ctrl_map;
    lv_keyboard_update_map(obj);
}

/*=====================
 * Getter functions
 *====================*/

lv_obj_t * lv_keyboard_get_textarea(const lv_obj_t * obj)
{
    LV_CHECK_OBJ(obj, MY_CLASS, return NULL);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    return keyboard->ta;
}

lv_keyboard_mode_t lv_keyboard_get_mode(const lv_obj_t * obj)
{
    LV_CHECK_OBJ(obj, MY_CLASS, return 0);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    return keyboard->mode;
}

bool lv_keyboard_get_popovers(const lv_obj_t * obj)
{
    LV_CHECK_OBJ(obj, MY_CLASS, return false);

    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    return keyboard->popovers;
}

/*=====================
 * Other functions
 *====================*/

void lv_keyboard_def_event_cb(lv_event_t * e)
{
    LV_CHECK_ARG(e != NULL, return);

    lv_obj_t * obj = lv_event_get_current_target(e);

    LV_CHECK_OBJ(obj, MY_CLASS, return);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    uint32_t btn_id = lv_buttonmatrix_get_selected_button(obj);
    if(btn_id == LV_BUTTONMATRIX_BUTTON_NONE) return;

    const char * txt = lv_buttonmatrix_get_button_text(obj, btn_id);
    if(txt == NULL) return;

    if(lv_strcmp(txt, LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_LOWER) == 0) {
        lv_keyboard_set_mode(obj, LV_KEYBOARD_MODE_TEXT_LOWER);
        return;
    }
#if LV_USE_ARABIC_PERSIAN_CHARS == 1
    else if(lv_strcmp(txt, LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_ARABIC) == 0) {
        lv_keyboard_set_mode(obj, LV_KEYBOARD_MODE_TEXT_ARABIC);
        return;
    }
#endif
    else if(lv_strcmp(txt, LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_UPPER) == 0) {
        lv_keyboard_set_mode(obj, LV_KEYBOARD_MODE_TEXT_UPPER);
        return;
    }
    else if(lv_strcmp(txt, LV_KEYBOARD_CTRL_BUTTON_MODE_SPECIAL) == 0) {
        lv_keyboard_set_mode(obj, LV_KEYBOARD_MODE_SPECIAL);
        return;
    }
    else if(lv_strcmp(txt, LV_KEYBOARD_IME_BTN_ENTER_CHN) == 0) {
        lv_keyboard_set_mode(obj, LV_KEYBOARD_MODE_IME_CHN);
        return;
    }
    else if(lv_strcmp(txt, LV_KEYBOARD_IME_BTN_ENTER_LATIN) == 0) {
        lv_keyboard_set_mode(obj, LV_KEYBOARD_MODE_TEXT_LOWER);
        return;
    }
    else if(lv_strcmp(txt, LV_SYMBOL_CLOSE) == 0 || lv_strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) {
        /*Cancelling must not leave a half-typed composition in the text area.*/
        lv_keyboard_ime_reset(keyboard, false);

        lv_result_t res = lv_obj_send_event(obj, LV_EVENT_CANCEL, NULL);
        if(res != LV_RESULT_OK) return;

        if(keyboard->ta) {
            res = lv_obj_send_event(keyboard->ta, LV_EVENT_CANCEL, NULL);
            if(res != LV_RESULT_OK) return;
        }
        return;
    }
    else if(lv_strcmp(txt, LV_SYMBOL_OK) == 0) {
        /*Ready confirms what was typed, so an open composition becomes its first
         *candidate rather than being thrown away.*/
        lv_keyboard_ime_commit_or_reset(keyboard);

        lv_result_t res = lv_obj_send_event(obj, LV_EVENT_READY, NULL);
        if(res != LV_RESULT_OK) return;

        if(keyboard->ta) {
            res = lv_obj_send_event(keyboard->ta, LV_EVENT_READY, NULL);
            if(res != LV_RESULT_OK) return;
        }
        return;
    }

    /*Add the characters to the text area if set*/
    if(keyboard->ta == NULL) return;

    /*Pinyin mode owns the letter, digit, space, punctuation and backspace keys:
     *they edit the composition instead of going straight to the text area.*/
    if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN) {
        lv_keyboard_ime_handle_key(keyboard, txt);
        return;
    }

    if(lv_strcmp(txt, "Enter") == 0 || lv_strcmp(txt, LV_SYMBOL_NEW_LINE) == 0) {
        lv_textarea_add_char(keyboard->ta, '\n');
        if(lv_textarea_get_one_line(keyboard->ta)) {
            lv_result_t res = lv_obj_send_event(keyboard->ta, LV_EVENT_READY, NULL);
            if(res != LV_RESULT_OK) return;
        }
    }
    else if(lv_strcmp(txt, LV_SYMBOL_LEFT) == 0) {
        lv_textarea_cursor_left(keyboard->ta);
    }
    else if(lv_strcmp(txt, LV_SYMBOL_RIGHT) == 0) {
        lv_textarea_cursor_right(keyboard->ta);
    }
    else if(lv_strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_delete_char(keyboard->ta);
    }
    else if(lv_strcmp(txt, "+/-") == 0) {
        uint32_t cur        = lv_textarea_get_cursor_pos(keyboard->ta);
        const char * ta_txt = lv_textarea_get_text(keyboard->ta);
        if(ta_txt[0] == '-') {
            lv_textarea_set_cursor_pos(keyboard->ta, 1);
            lv_textarea_delete_char(keyboard->ta);
            lv_textarea_add_char(keyboard->ta, '+');
            lv_textarea_set_cursor_pos(keyboard->ta, cur);
        }
        else if(ta_txt[0] == '+') {
            lv_textarea_set_cursor_pos(keyboard->ta, 1);
            lv_textarea_delete_char(keyboard->ta);
            lv_textarea_add_char(keyboard->ta, '-');
            lv_textarea_set_cursor_pos(keyboard->ta, cur);
        }
        else {
            lv_textarea_set_cursor_pos(keyboard->ta, 0);
            lv_textarea_add_char(keyboard->ta, '-');
            lv_textarea_set_cursor_pos(keyboard->ta, cur + 1);
        }
    }
    else if(lv_keyboard_is_space_key(txt)) {
        lv_textarea_add_char(keyboard->ta, ' ');
    }
    else {
        lv_textarea_add_text(keyboard->ta, txt);
    }
}

const char * const * lv_keyboard_get_map_array(const lv_obj_t * kb)
{
    LV_CHECK_OBJ(kb, MY_CLASS, return NULL);

    return lv_buttonmatrix_get_map(kb);
}

uint32_t lv_keyboard_get_selected_button(const lv_obj_t * obj)
{
    LV_CHECK_OBJ(obj, MY_CLASS, return 0);

    return lv_buttonmatrix_get_selected_button(obj);
}

const char * lv_keyboard_get_button_text(const lv_obj_t * obj, uint32_t btn_id)
{
    LV_CHECK_OBJ(obj, MY_CLASS, return NULL);

    return lv_buttonmatrix_get_button_text(obj, btn_id);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_keyboard_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_ASSERT(obj != NULL);
    LV_UNUSED(class_p);
    lv_obj_set_click_focusable(obj, false);

    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    keyboard->ta         = NULL;
    keyboard->mode       = LV_KEYBOARD_MODE_TEXT_LOWER;
    keyboard->popovers   = 0;

    keyboard->candidate_list = NULL;
    keyboard->candidate_btnm = NULL;
    keyboard->pinyin_buf[0] = '\0';
    keyboard->pinyin_start_pos = 0;
    keyboard->candidate_map = NULL;
    keyboard->candidate_cnt = 0;
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
    LV_ASSERT(obj != NULL);
    LV_UNUSED(class_p);

    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;

    /*The text area and the candidate list can outlive the keyboard, so their
     *delete callbacks have to be dropped or they would call back into freed
     *memory.*/
    if(keyboard->ta && lv_obj_is_valid(keyboard->ta)) {
        lv_obj_remove_event_cb_with_user_data(keyboard->ta, lv_keyboard_ime_on_target_deleted, keyboard);
    }
    if(keyboard->candidate_list && lv_obj_is_valid(keyboard->candidate_list)) {
        lv_obj_remove_event_cb_with_user_data(keyboard->candidate_list, lv_keyboard_ime_on_target_deleted, keyboard);
    }

    lv_keyboard_ime_detach_candidates(keyboard);
}

/**
 * Update the key and control map for the current mode
 * @param obj pointer to a keyboard object
 */
static void lv_keyboard_update_map(lv_obj_t * obj)
{
    LV_ASSERT(obj != NULL);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    lv_buttonmatrix_set_map(obj, kb_map[keyboard->mode]);
    lv_keyboard_update_ctrl_map(obj);
}

/**
 * Update the control map for the current mode
 * @param obj pointer to a keyboard object
 */
static void lv_keyboard_update_ctrl_map(lv_obj_t * obj)
{
    LV_ASSERT(obj != NULL);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;

    if(keyboard->popovers) {
        /*Apply the current control map (already includes LV_BUTTONMATRIX_CTRL_POPOVER flags)*/
        lv_buttonmatrix_set_ctrl_map(obj, kb_ctrl[keyboard->mode]);
    }
    else {
        /*Make a copy of the current control map*/
        lv_buttonmatrix_t * btnm = (lv_buttonmatrix_t *)obj;
        lv_buttonmatrix_ctrl_t * ctrl_map = lv_malloc(btnm->btn_cnt * sizeof(lv_buttonmatrix_ctrl_t));
        lv_memcpy(ctrl_map, kb_ctrl[keyboard->mode], sizeof(lv_buttonmatrix_ctrl_t) * btnm->btn_cnt);

        /*Remove all LV_BUTTONMATRIX_CTRL_POPOVER flags*/
        uint32_t i;
        for(i = 0; i < btnm->btn_cnt; i++) {
            ctrl_map[i] &= (~LV_BUTTONMATRIX_CTRL_POPOVER);
        }

        /*Apply new control map and clean up*/
        lv_buttonmatrix_set_ctrl_map(obj, ctrl_map);
        lv_free(ctrl_map);
    }
}



/**********************
 *   CHINESE IME
 **********************/

/**
 * Rewrite the part of the text area that holds the composition. The composed
 * pinyin was echoed into the text area starting at `pinyin_start_pos`, so it is
 * deleted from the back: deleting a multi-byte character moves the cursor left
 * by that character's full width.
 * @param keyboard pointer to the keyboard
 * @param text     text to leave in place of the composition, or NULL to just
 *                 remove it
 */
static void lv_keyboard_ime_replace_composition(lv_keyboard_t * keyboard, const char * text)
{
    if(keyboard->ta == NULL || !lv_obj_is_valid(keyboard->ta)) {
        keyboard->ta = NULL;
        return;
    }

    size_t len = lv_strlen(keyboard->pinyin_buf);
    if(len > 0) {
        lv_textarea_set_cursor_pos(keyboard->ta, keyboard->pinyin_start_pos + (uint32_t)len);
        for(size_t i = 0; i < len; i++) {
            lv_textarea_delete_char(keyboard->ta);
        }
    }

    if(text != NULL && text[0] != '\0') {
        lv_textarea_add_text(keyboard->ta, text);
    }
}

static bool lv_keyboard_ime_has_composition(const lv_keyboard_t * keyboard)
{
    return keyboard != NULL && keyboard->pinyin_buf[0] != '\0';
}

/**
 * Whether a button is the wide bottom-row space key. Its label names the layout
 * it belongs to rather than showing a space, so every layout's label counts.
 * @param txt the button label
 * @return    true when the button should insert a space
 */
static bool lv_keyboard_is_space_key(const char * txt)
{
    return lv_strcmp(txt, " ") == 0 ||
           lv_strcmp(txt, LV_KEYBOARD_BTN_SPACE_LATIN) == 0 ||
           lv_strcmp(txt, LV_KEYBOARD_BTN_SPACE_SPECIAL) == 0 ||
           lv_strcmp(txt, LV_KEYBOARD_BTN_SPACE_PINYIN) == 0;
}

/**
 * End the composition by accepting its first candidate. Called whenever the
 * composition cannot continue: switching mode, confirming with OK, or typing
 * something that is not pinyin.
 * @param keyboard pointer to the keyboard
 */
static void lv_keyboard_ime_commit_or_reset(lv_keyboard_t * keyboard)
{
    if(!lv_keyboard_ime_has_composition(keyboard)) {
        return;
    }

    if(keyboard->total_candidates > 0 && keyboard->candidates_list != NULL) {
        lv_keyboard_ime_commit(keyboard, keyboard->candidates_list[0]);
    }
    else {
        /*Nothing matched the pinyin, so the letters themselves are all the user
         *has to show for it.*/
        lv_keyboard_ime_reset(keyboard, false);
    }
}

/**
 * Forget the composition.
 * @param keyboard    pointer to the keyboard
 * @param delete_text true to also remove the echoed pinyin from the text area
 */
static void lv_keyboard_ime_reset(lv_keyboard_t * keyboard, bool delete_text)
{
    if(keyboard == NULL) {
        return;
    }

    if(delete_text) {
        lv_keyboard_ime_replace_composition(keyboard, NULL);
    }

    keyboard->pinyin_buf[0] = '\0';
    keyboard->pinyin_start_pos = 0;
    lv_keyboard_ime_clear_candidates(keyboard);
}

/**
 * Replace the composition with `txt` and end it. This is how a candidate is
 * accepted, whether it was tapped, picked with a digit, or committed as the
 * first entry.
 * @param keyboard pointer to the keyboard
 * @param txt      text to put in place of the composition
 */
static void lv_keyboard_ime_commit(lv_keyboard_t * keyboard, const char * txt)
{
    if(txt == NULL || txt[0] == '\0') {
        lv_keyboard_ime_reset(keyboard, true);
        return;
    }

    lv_keyboard_ime_replace_composition(keyboard, txt);
    keyboard->pinyin_buf[0] = '\0';
    keyboard->pinyin_start_pos = 0;
    lv_keyboard_ime_clear_candidates(keyboard);
}
/**
 * Append a letter to the composition, echoing it into the text area so the user
 * sees what is being typed.
 * @param keyboard pointer to the keyboard
 * @param ch       the lowercase letter
 */
static void lv_keyboard_ime_append_letter(lv_keyboard_t * keyboard, char ch)
{
    if(keyboard->ta == NULL || !lv_obj_is_valid(keyboard->ta)) {
        keyboard->ta = NULL;
        return;
    }

    size_t len = lv_strlen(keyboard->pinyin_buf);
    if(len + 1 >= sizeof(keyboard->pinyin_buf)) {
        /*Full. Ignore the key rather than dropping the whole composition.*/
        return;
    }

    if(len == 0) {
        keyboard->pinyin_start_pos = lv_textarea_get_cursor_pos(keyboard->ta);
    }

    lv_textarea_add_char(keyboard->ta, (uint32_t)ch);
    keyboard->pinyin_buf[len] = ch;
    keyboard->pinyin_buf[len + 1] = '\0';
    lv_keyboard_ime_show_candidates(keyboard);
}

/**
 * Drop the last letter of the composition, or a character from the text area
 * once nothing is being composed.
 * @param keyboard pointer to the keyboard
 */
static void lv_keyboard_ime_backspace(lv_keyboard_t * keyboard)
{
    if(keyboard->ta == NULL || !lv_obj_is_valid(keyboard->ta)) {
        keyboard->ta = NULL;
        return;
    }

    if(!lv_keyboard_ime_has_composition(keyboard)) {
        lv_textarea_delete_char(keyboard->ta);
        return;
    }

    size_t len = lv_strlen(keyboard->pinyin_buf);
    lv_textarea_delete_char(keyboard->ta);
    keyboard->pinyin_buf[len - 1] = '\0';

    if(keyboard->pinyin_buf[0] == '\0') {
        keyboard->pinyin_start_pos = 0;
        lv_keyboard_ime_clear_candidates(keyboard);
        return;
    }

    lv_keyboard_ime_show_candidates(keyboard);
}

/**
 * Route one key press into the composition. Keys the pinyin layout can produce
 * are handled here; anything else ends the composition and is passed on to the
 * text area, so characters such as `。` stay usable inline.
 * @param keyboard pointer to the keyboard
 * @param txt      label of the pressed button
 */
static void lv_keyboard_ime_handle_key(lv_keyboard_t * keyboard, const char * txt)
{
    if(keyboard->ta == NULL || !lv_obj_is_valid(keyboard->ta)) {
        keyboard->ta = NULL;
        return;
    }

    if(txt[0] != '\0' && txt[1] == '\0' && isalpha((unsigned char)txt[0]) != 0) {
        lv_keyboard_ime_append_letter(keyboard, (char)tolower((unsigned char)txt[0]));
        return;
    }

    if(lv_strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        lv_keyboard_ime_backspace(keyboard);
        return;
    }

    if(lv_keyboard_ime_has_composition(keyboard)) {
        /*`1`..`8` pick a candidate from the page on screen.*/
        if(txt[0] >= '1' && txt[0] <= '8' && txt[1] == '\0') {
            int idx = keyboard->candidate_page + (txt[0] - '1');
            if(idx < keyboard->total_candidates && keyboard->candidates_list != NULL) {
                lv_keyboard_ime_commit(keyboard, keyboard->candidates_list[idx]);
            }
            return;
        }

        /*The arrows page through candidates first and only move the text cursor
         *once there is nothing left to page.*/
        if(lv_strcmp(txt, LV_SYMBOL_LEFT) == 0) {
            if(!lv_keyboard_ime_page_step(keyboard, -1)) {
                lv_textarea_cursor_left(keyboard->ta);
            }
            return;
        }

        if(lv_strcmp(txt, LV_SYMBOL_RIGHT) == 0) {
            if(!lv_keyboard_ime_page_step(keyboard, 1)) {
                lv_textarea_cursor_right(keyboard->ta);
            }
            return;
        }

        if(lv_keyboard_is_space_key(txt)) {
            lv_keyboard_ime_commit_or_reset(keyboard);
            return;
        }
    }

    /*Everything else ends the composition and then behaves normally. This also
     *covers the space key when nothing is being composed.*/
    lv_keyboard_ime_commit_or_reset(keyboard);
    lv_textarea_add_text(keyboard->ta, txt);
}
/**
 * Step the candidate list by one page.
 * @param keyboard pointer to the keyboard
 * @param dir      negative for the previous page, positive for the next
 * @return         true when the page changed, so the caller knows the key was
 *                 consumed
 */
static bool lv_keyboard_ime_page_step(lv_keyboard_t * keyboard, int dir)
{
    if(keyboard->candidate_cnt == 0) {
        return false;
    }

    if(dir > 0) {
        int next = keyboard->candidate_page + keyboard->candidate_cnt;
        if(next >= keyboard->total_candidates) {
            return false;
        }

        keyboard->candidate_page = (int8_t)next;
        lv_keyboard_ime_update_page(keyboard);
        return true;
    }

    if(dir < 0) {
        if(keyboard->candidate_page <= 0) {
            return false;
        }

        /*Pages hold a variable number of candidates, so walk forward from the
         *start to find the one before the current page.*/
        int prev = 0;
        int scan = 0;
        while(scan < keyboard->candidate_page) {
            uint8_t visible = lv_keyboard_ime_visible_count(keyboard, scan);
            if(visible == 0) {
                break;
            }

            int next = scan + visible;
            if(next >= keyboard->candidate_page) {
                break;
            }

            prev = scan;
            scan = next;
        }

        keyboard->candidate_page = (int8_t)prev;
        lv_keyboard_ime_update_page(keyboard);
        return true;
    }

    return false;
}

/**
 * Free the candidate strings and the map that points into them, detaching the
 * button matrix first so a pending redraw cannot read freed text.
 * @param keyboard pointer to the keyboard
 */
static void lv_keyboard_ime_detach_candidates(lv_keyboard_t * keyboard)
{
    if(keyboard->candidate_btnm && lv_obj_is_valid(keyboard->candidate_btnm)) {
        lv_buttonmatrix_set_map(keyboard->candidate_btnm, ime_no_candidates);
        lv_obj_set_hidden(keyboard->candidate_btnm, true);
        lv_buttonmatrix_set_selected_button(keyboard->candidate_btnm, LV_BUTTONMATRIX_BUTTON_NONE);
    }

    if(keyboard->candidate_map) {
        lv_free((void *)keyboard->candidate_map);
        keyboard->candidate_map = NULL;
    }

    if(keyboard->candidates_list) {
        for(int i = 0; i < keyboard->total_candidates; i++) {
            lv_free(keyboard->candidates_list[i]);
        }
        lv_free(keyboard->candidates_list);
        keyboard->candidates_list = NULL;
    }

    keyboard->candidate_cnt = 0;
    keyboard->candidate_page = 0;
    keyboard->total_candidates = 0;
}

static void lv_keyboard_ime_clear_candidates(lv_keyboard_t * keyboard)
{
    lv_keyboard_ime_detach_candidates(keyboard);

    if(keyboard->candidate_btnm && lv_obj_is_valid(keyboard->candidate_btnm)) {
        lv_obj_set_hidden(keyboard->candidate_btnm, true);
    }
}

/**
 * Count the candidates that fit on the page starting at `start`. Short
 * candidates take less width, so a page of single characters holds more than a
 * page of long phrases.
 * @param keyboard pointer to the keyboard
 * @param start    index of the first candidate on the page
 * @return         number of candidates on that page
 */
static uint8_t lv_keyboard_ime_visible_count(const lv_keyboard_t * keyboard, int start)
{
    if(keyboard->candidates_list == NULL || start < 0 || start >= keyboard->total_candidates) {
        return 0;
    }

    int chars = lv_keyboard_utf8_char_count(keyboard->candidates_list[start]);
    uint8_t limit;
    if(chars <= 1) {
        limit = LV_KEYBOARD_IME_PAGE_SIZE;
    }
    else if(chars == 2) {
        limit = 5;
    }
    else if(chars == 3) {
        limit = 4;
    }
    else {
        limit = 3;
    }

    int remaining = keyboard->total_candidates - start;
    return (uint8_t)(remaining < limit ? remaining : limit);
}

/**
 * Width of one candidate button in button matrix units.
 * @param candidate pointer to the candidate text
 * @return          1 to 4 units
 */
static uint16_t lv_keyboard_ime_button_width(const char * candidate)
{
    int chars = lv_keyboard_utf8_char_count(candidate);
    if(chars <= 1) {
        return 1;
    }
    if(chars == 2) {
        return 2;
    }
    if(chars == 3) {
        return 3;
    }
    return 4;
}
/**
 * Create the candidate button matrix on first use. It is parented to
 * `candidate_list`, so it goes away with it, and is recreated if the list
 * outlives a deletion.
 * @param keyboard pointer to the keyboard
 * @return         true when there is a usable matrix
 */
static bool lv_keyboard_ime_ensure_buttonmatrix(lv_keyboard_t * keyboard)
{
    if(keyboard->candidate_list == NULL || !lv_obj_is_valid(keyboard->candidate_list)) {
        keyboard->candidate_list = NULL;
        return false;
    }

    if(keyboard->candidate_btnm && !lv_obj_is_valid(keyboard->candidate_btnm)) {
        keyboard->candidate_btnm = NULL;
    }

    if(keyboard->candidate_btnm == NULL) {
        keyboard->candidate_btnm = lv_buttonmatrix_create(keyboard->candidate_list);
        if(keyboard->candidate_btnm == NULL) {
            return false;
        }

        lv_obj_set_click_focusable(keyboard->candidate_btnm, false);
        lv_obj_set_event_bubble(keyboard->candidate_btnm, true);
        lv_obj_add_event_cb(keyboard->candidate_btnm, lv_keyboard_ime_candidate_event_cb,
                            LV_EVENT_VALUE_CHANGED, keyboard);
        lv_obj_set_style_base_dir(keyboard->candidate_btnm, LV_BASE_DIR_LTR, 0);
    }

    return true;
}

/**
 * Rebuild the candidate button matrix for the page at `candidate_page`.
 * @param keyboard pointer to the keyboard
 */
static void lv_keyboard_ime_update_page(lv_keyboard_t * keyboard)
{
    if(keyboard->total_candidates <= 0 || keyboard->candidates_list == NULL) {
        lv_keyboard_ime_clear_candidates(keyboard);
        return;
    }

    if(!lv_keyboard_ime_ensure_buttonmatrix(keyboard)) {
        keyboard->candidate_cnt = 0;
        return;
    }

    if(keyboard->candidate_page < 0) {
        keyboard->candidate_page = 0;
    }
    if(keyboard->candidate_page >= keyboard->total_candidates) {
        keyboard->candidate_page = (int8_t)(keyboard->total_candidates - 1);
    }

    int start = keyboard->candidate_page;
    uint8_t visible = lv_keyboard_ime_visible_count(keyboard, start);
    bool has_prev = start > 0;
    bool has_next = (start + visible) < keyboard->total_candidates;

    uint16_t item_cnt = (uint16_t)(visible + (has_prev ? 1 : 0) + (has_next ? 1 : 0));

    /*The button matrix keeps pointers into the string map between redraws, so
     *that array has to outlive this call; the control map does not.*/
    const char ** old_map = keyboard->candidate_map;
    const char ** map = lv_malloc(sizeof(char *) * (item_cnt + 1));
    lv_buttonmatrix_ctrl_t * ctrl_map = lv_malloc(sizeof(lv_buttonmatrix_ctrl_t) * item_cnt);
    if(map == NULL || ctrl_map == NULL) {
        lv_free((void *)map);
        lv_free(ctrl_map);
        keyboard->candidate_cnt = 0;
        lv_obj_set_hidden(keyboard->candidate_btnm, true);
        return;
    }

    uint16_t map_idx = 0;
    uint16_t ctrl_idx = 0;

    if(has_prev) {
        map[map_idx++] = LV_KEYBOARD_IME_NAV_PREV;
        ctrl_map[ctrl_idx++] = LV_BUTTONMATRIX_CTRL_CLICK_TRIG | 1;
    }

    for(int i = 0; i < visible; i++) {
        const char * candidate = keyboard->candidates_list[start + i];
        map[map_idx++] = candidate;
        ctrl_map[ctrl_idx++] = LV_BUTTONMATRIX_CTRL_CLICK_TRIG | lv_keyboard_ime_button_width(candidate);
    }

    if(has_next) {
        map[map_idx++] = LV_KEYBOARD_IME_NAV_NEXT;
        ctrl_map[ctrl_idx++] = LV_BUTTONMATRIX_CTRL_CLICK_TRIG | 1;
    }

    map[map_idx] = "";

    lv_buttonmatrix_set_map(keyboard->candidate_btnm, map);
    lv_buttonmatrix_set_ctrl_map(keyboard->candidate_btnm, ctrl_map);
    lv_obj_set_size(keyboard->candidate_btnm, lv_obj_get_width(keyboard->candidate_list),
                    lv_obj_get_height(keyboard->candidate_list));
    lv_obj_center(keyboard->candidate_btnm);
    lv_obj_set_hidden(keyboard->candidate_btnm, false);

    keyboard->candidate_map = map;
    keyboard->candidate_cnt = visible;

    lv_free((void *)old_map);
    lv_free(ctrl_map);
}

/**
 * Recompute the candidates for the current composition and show the first page.
 * An empty result leaves the matrix hidden, which is also how a caller can tell
 * that there is nothing to show.
 * @param keyboard pointer to the keyboard
 */
static void lv_keyboard_ime_show_candidates(lv_keyboard_t * keyboard)
{
    if(!lv_keyboard_ime_has_composition(keyboard)) {
        lv_keyboard_ime_clear_candidates(keyboard);
        return;
    }

    char ** candidates = NULL;
    int count = lv_keyboard_collect_mixed_candidates(keyboard->pinyin_buf, &candidates);

    /*The search above still reads the old strings through the trie, so the
     *previous list is only released now that it is done with.*/
    lv_keyboard_ime_detach_candidates(keyboard);

    if(count <= 0 || candidates == NULL) {
        return;
    }

    keyboard->candidates_list = candidates;
    keyboard->total_candidates = (int16_t)count;
    keyboard->candidate_page = 0;
    lv_keyboard_ime_update_page(keyboard);
}
/**
 * Keep the keyboard's copies of the text area and the candidate list in step
 * when either one is deleted, so a later key press cannot reach freed memory.
 * @param e the LV_EVENT_DELETE event of the watched object
 */
static void lv_keyboard_ime_on_target_deleted(lv_event_t * e)
{
    lv_keyboard_t * keyboard = (lv_keyboard_t *)lv_event_get_user_data(e);
    if(keyboard == NULL) {
        return;
    }

    lv_obj_t * target = lv_event_get_target(e);
    if(target == keyboard->ta) {
        keyboard->ta = NULL;
        keyboard->pinyin_buf[0] = '\0';
        keyboard->pinyin_start_pos = 0;
        lv_keyboard_ime_clear_candidates(keyboard);
        return;
    }

    if(target == keyboard->candidate_list) {
        /*The matrix is a child of the list, so it went away with it.*/
        keyboard->candidate_list = NULL;
        keyboard->candidate_btnm = NULL;
        lv_keyboard_ime_detach_candidates(keyboard);
    }
}

/**
 * Accept the candidate the user tapped, or follow a page arrow.
 * @param e the LV_EVENT_VALUE_CHANGED event of the candidate button matrix
 */
static void lv_keyboard_ime_candidate_event_cb(lv_event_t * e)
{
    lv_obj_t * btnm = lv_event_get_target(e);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)lv_event_get_user_data(e);
    if(keyboard == NULL) {
        return;
    }

    uint32_t btn_id = lv_buttonmatrix_get_selected_button(btnm);
    if(btn_id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }

    const char * txt = lv_buttonmatrix_get_button_text(btnm, btn_id);
    if(txt == NULL) {
        return;
    }

    if(lv_strcmp(txt, LV_KEYBOARD_IME_NAV_PREV) == 0) {
        lv_keyboard_ime_page_step(keyboard, -1);
        return;
    }

    if(lv_strcmp(txt, LV_KEYBOARD_IME_NAV_NEXT) == 0) {
        lv_keyboard_ime_page_step(keyboard, 1);
        return;
    }

    lv_keyboard_ime_commit(keyboard, txt);
}

/**********************
 *   PINYIN INDEX
 **********************/

/**
 * Build the pinyin syllable index once and keep it. The dictionary is fixed, so
 * it never needs rebuilding.
 * @return true when the index is usable
 */
static bool lv_keyboard_pinyin_index_init(void)
{
    if(pinyin_index.status == 1) {
        return true;
    }
    if(pinyin_index.status == -1) {
        return false;
    }

    int32_t node_cap = 1;
    int32_t link_cap = 0;
    for(int i = 0; pinyin_dict[i].pinyin != NULL; i++) {
        node_cap += (int32_t)lv_strlen(pinyin_dict[i].pinyin);
        link_cap++;
    }

    if(!lv_keyboard_trie_init(&pinyin_index, node_cap, link_cap)) {
        pinyin_index.status = -1;
        return false;
    }

    for(uint32_t i = 0; pinyin_dict[i].pinyin != NULL; i++) {
        if(!lv_keyboard_trie_insert(&pinyin_index, pinyin_dict[i].pinyin, i)) {
            lv_keyboard_trie_deinit(&pinyin_index);
            pinyin_index.status = -1;
            return false;
        }
    }

    pinyin_index.status = 1;
    return true;
}

/**
 * Build the phrase indexes once and keep them: one keyed by full pinyin and one
 * by the initials of each syllable.
 * @return true when both indexes are usable
 */
static bool lv_keyboard_phrase_index_init(void)
{
    if(phrase_pinyin_index.status == 1 && phrase_initials_index.status == 1) {
        return true;
    }
    if(phrase_pinyin_index.status == -1 || phrase_initials_index.status == -1) {
        return false;
    }

    int32_t pinyin_nodes = 1;
    int32_t initials_nodes = 1;
    int32_t pinyin_links = 0;
    int32_t initials_links = 0;

    for(int i = 0; ime_phrase_dict[i].pinyin != NULL; i++) {
        pinyin_nodes += (int32_t)lv_strlen(ime_phrase_dict[i].pinyin);
        pinyin_links++;
        if(ime_phrase_dict[i].initials != NULL) {
            initials_nodes += (int32_t)lv_strlen(ime_phrase_dict[i].initials);
            initials_links++;
        }
    }

    if(!lv_keyboard_trie_init(&phrase_pinyin_index, pinyin_nodes, pinyin_links) ||
       !lv_keyboard_trie_init(&phrase_initials_index, initials_nodes, initials_links)) {
        lv_keyboard_trie_deinit(&phrase_pinyin_index);
        lv_keyboard_trie_deinit(&phrase_initials_index);
        phrase_pinyin_index.status = -1;
        phrase_initials_index.status = -1;
        return false;
    }

    for(uint32_t i = 0; ime_phrase_dict[i].pinyin != NULL; i++) {
        bool ok = lv_keyboard_trie_insert(&phrase_pinyin_index, ime_phrase_dict[i].pinyin, i);
        if(ok && ime_phrase_dict[i].initials != NULL) {
            ok = lv_keyboard_trie_insert(&phrase_initials_index, ime_phrase_dict[i].initials, i);
        }

        if(!ok) {
            lv_keyboard_trie_deinit(&phrase_pinyin_index);
            lv_keyboard_trie_deinit(&phrase_initials_index);
            phrase_pinyin_index.status = -1;
            phrase_initials_index.status = -1;
            return false;
        }
    }

    phrase_pinyin_index.status = 1;
    phrase_initials_index.status = 1;
    return true;
}
static bool lv_keyboard_trie_init(lv_keyboard_trie_t * trie, int32_t node_cap, int32_t link_cap)
{
    lv_keyboard_trie_deinit(trie);

    trie->nodes = lv_malloc(sizeof(lv_keyboard_trie_node_t) * node_cap);
    trie->links = lv_malloc(sizeof(lv_keyboard_trie_link_t) * (link_cap > 0 ? link_cap : 1));
    if(trie->nodes == NULL || trie->links == NULL) {
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
    lv_free(trie->nodes);
    lv_free(trie->links);
    lv_memzero(trie, sizeof(*trie));
}

static int32_t lv_keyboard_trie_find_child(const lv_keyboard_trie_t * trie, int32_t parent_idx, char ch)
{
    if(trie->nodes == NULL || parent_idx < 0) {
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
    if(trie->nodes == NULL || key == NULL || key[0] == '\0') {
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
    if(trie->nodes == NULL || key == NULL || key[0] == '\0') {
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

/**
 * Collect the entries stored at a node, and optionally at every node below it.
 * @param trie        pointer to the trie
 * @param node_idx    node to start at
 * @param out         array receiving the entry indices
 * @param out_cnt     in/out count of entries collected
 * @param out_max     capacity of `out`
 * @param include_self whether to collect the start node's own entries
 * @param recursive    whether to descend into children
 */
static void lv_keyboard_trie_collect_entries(const lv_keyboard_trie_t * trie, int32_t node_idx,
                                             uint32_t * out, int * out_cnt, int out_max,
                                             bool include_self, bool recursive)
{
    if(trie->nodes == NULL || node_idx < 0 || out == NULL || out_cnt == NULL) {
        return;
    }

    if(include_self) {
        int32_t link_idx = trie->nodes[node_idx].entry_head;
        while(link_idx >= 0 && *out_cnt < out_max) {
            out[*out_cnt] = trie->links[link_idx].entry_idx;
            (*out_cnt)++;
            link_idx = trie->links[link_idx].next_idx;
        }
    }

    if(!recursive || *out_cnt >= out_max) {
        return;
    }

    int32_t child_idx = trie->nodes[node_idx].child_idx;
    while(child_idx >= 0 && *out_cnt < out_max) {
        lv_keyboard_trie_collect_entries(trie, child_idx, out, out_cnt, out_max, true, true);
        child_idx = trie->nodes[child_idx].sibling_idx;
    }
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

static int lv_keyboard_utf8_char_count(const char * str)
{
    int count = 0;
    int idx = 0;
    while(str[idx] != '\0') {
        idx += lv_keyboard_utf8_char_size(&str[idx]);
        count++;
    }
    return count;
}
/**
 * Score a phrase against the input. Longer, exact matches win: an exact pinyin
 * match beats a prefix match, which beats an initials match, and within each
 * group the dictionary frequency decides.
 * @param input the pinyin typed so far
 * @param entry the phrase entry to score
 * @return      the score, or 0 when the entry does not match at all
 */
static uint32_t lv_keyboard_phrase_score(const char * input, const lv_ime_phrase_entry_t * entry)
{
    size_t in_len = lv_strlen(input);
    if(in_len == 0) {
        return 0;
    }

    if(lv_strcmp(input, entry->pinyin) == 0) {
        return 3000000000u + entry->freq;
    }

    if(lv_strncmp(entry->pinyin, input, in_len) == 0) {
        return 2000000000u + entry->freq;
    }

    if(entry->initials != NULL && lv_strncmp(entry->initials, input, in_len) == 0) {
        return 1000000000u + entry->freq;
    }

    return 0;
}

/**
 * Insert a phrase into the bounded hit table, keeping the highest scoring
 * entries and dropping duplicates.
 * @param hits    the hit table
 * @param hit_cnt in/out number of entries in it
 * @param max     capacity of the table
 * @param phrase  the phrase text, borrowed from the dictionary
 * @param score   its score; 0 means it did not match
 */
static void lv_keyboard_phrase_try_insert(lv_phrase_hit_t * hits, int * hit_cnt, int max,
                                          const char * phrase, uint32_t score)
{
    if(phrase == NULL || score == 0) {
        return;
    }

    for(int i = 0; i < *hit_cnt; i++) {
        if(lv_strcmp(hits[i].phrase, phrase) == 0) {
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

    /*Full: replace the weakest entry when this one scores higher.*/
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

static void lv_keyboard_phrase_collect_hits(const lv_keyboard_trie_t * trie, int32_t node_idx,
                                            const char * input, lv_phrase_hit_t * hits, int * hit_cnt,
                                            int out_max, bool include_self, bool recursive)
{
    if(trie->nodes == NULL || node_idx < 0) {
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

/**
 * Find the syllable matches for the input starting at `pos`, returning them
 * shortest first so the caller can try the longest first.
 * @param pinyin         the whole input
 * @param pos            offset to start matching at
 * @param matches        array receiving the matches
 * @param max_matches    capacity of `matches`
 * @param prefix_node_idx out: node reached by the longest partial syllable, or -1
 * @param prefix_len     out: length of that partial syllable
 * @return               number of matches
 */
static int lv_keyboard_pinyin_prefix_matches(const char * pinyin, size_t pos,
                                            lv_keyboard_trie_match_t * matches, int max_matches,
                                            int32_t * prefix_node_idx, size_t * prefix_len)
{
    *prefix_node_idx = -1;
    *prefix_len = 0;

    if(!lv_keyboard_pinyin_index_init()) {
        return 0;
    }

    int32_t node_idx = 0;
    size_t matched_len = 0;
    int match_cnt = 0;

    for(const char * p = &pinyin[pos]; *p != '\0'; p++) {
        node_idx = lv_keyboard_trie_find_child(&pinyin_index, node_idx, *p);
        if(node_idx < 0) {
            break;
        }

        matched_len++;
        if(pinyin_index.nodes[node_idx].entry_head >= 0 && match_cnt < max_matches) {
            matches[match_cnt].node_idx = node_idx;
            matches[match_cnt].key_len = matched_len;
            match_cnt++;
        }
    }

    if(matched_len > 0) {
        *prefix_node_idx = node_idx;
        *prefix_len = matched_len;
    }

    return match_cnt;
}

/**
 * Recursively build every character combination the input can spell. Each
 * syllable match appends one character and recurses on what is left, so
 * `nihao` can produce `你好` through `ni`+`hao` or any other split the
 * dictionary allows.
 * @param pinyin          the whole input
 * @param pos             offset consumed so far
 * @param tmp             scratch buffer holding the characters built so far
 * @param tmp_len         bytes used in `tmp`
 * @param tmp_cap         capacity of `tmp`, excluding the terminator
 * @param out_list        array receiving the finished strings
 * @param out_cnt         in/out number of strings collected
 * @param out_max         capacity of `out_list`
 * @param allow_partial_tail whether to also accept a trailing partial syllable
 * @param visit_budget    in/out cap on recursion, so a long input cannot stall
 */
static void lv_keyboard_build_candidates(const char * pinyin, size_t pos, char * tmp, size_t tmp_len,
                                         size_t tmp_cap, char ** out_list, int * out_cnt, int out_max,
                                         bool allow_partial_tail, uint16_t * visit_budget)
{
    if(*visit_budget == 0 || *out_cnt >= out_max) {
        return;
    }

    (*visit_budget)--;

    size_t total_len = lv_strlen(pinyin);
    if(pos >= total_len) {
        char * out = lv_malloc(tmp_len + 1);
        if(out != NULL) {
            lv_memcpy(out, tmp, tmp_len);
            out[tmp_len] = '\0';
            out_list[*out_cnt] = out;
            (*out_cnt)++;
        }
        return;
    }

    lv_keyboard_trie_match_t matches[16];
    int32_t prefix_node_idx;
    size_t prefix_len;
    int match_cnt = lv_keyboard_pinyin_prefix_matches(pinyin, pos, matches, 16, &prefix_node_idx, &prefix_len);

    /*Longest syllable first: that is what makes the multi-character readings
     *come out ahead of the single-character ones.*/
    for(int i = match_cnt - 1; i >= 0 && *out_cnt < out_max; i--) {
        uint32_t entry_ids[8];
        int entry_cnt = 0;
        lv_keyboard_trie_collect_entries(&pinyin_index, matches[i].node_idx, entry_ids, &entry_cnt,
                                         (int)(sizeof(entry_ids) / sizeof(entry_ids[0])), true, false);

        for(int e = 0; e < entry_cnt && *out_cnt < out_max; e++) {
            const char * chinese = pinyin_dict[entry_ids[e]].chinese;
            for(const char * p = chinese; *p != '\0' && *out_cnt < out_max; p += lv_keyboard_utf8_char_size(p)) {
                uint8_t char_len = lv_keyboard_utf8_char_size(p);
                if(tmp_len + char_len <= tmp_cap) {
                    lv_memcpy(&tmp[tmp_len], p, char_len);
                    lv_keyboard_build_candidates(pinyin, pos + matches[i].key_len, tmp, tmp_len + char_len,
                                                 tmp_cap, out_list, out_cnt, out_max, allow_partial_tail,
                                                 visit_budget);
                }
            }
        }
    }

    if(!allow_partial_tail || *out_cnt >= out_max) {
        return;
    }

    /*The tail is a prefix of a syllable the user is still typing, so offer every
     *character that syllable can begin with.*/
    size_t remain_len = total_len - pos;
    if(remain_len == 0 || prefix_node_idx < 0 || prefix_len != remain_len) {
        return;
    }

    uint32_t partial_ids[LV_KEYBOARD_MAX_CANDIDATES];
    int partial_cnt = 0;
    lv_keyboard_trie_collect_entries(&pinyin_index, prefix_node_idx, partial_ids, &partial_cnt,
                                     LV_KEYBOARD_MAX_CANDIDATES, false, true);

    for(int i = 0; i < partial_cnt && *out_cnt < out_max; i++) {
        const char * chinese = pinyin_dict[partial_ids[i]].chinese;
        for(const char * p = chinese; *p != '\0' && *out_cnt < out_max; p += lv_keyboard_utf8_char_size(p)) {
            uint8_t char_len = lv_keyboard_utf8_char_size(p);
            if(tmp_len + char_len > tmp_cap) {
                break;
            }
            char * out = lv_malloc(tmp_len + char_len + 1);
            if(out == NULL) {
                break;
            }
            lv_memcpy(out, tmp, tmp_len);
            lv_memcpy(out + tmp_len, p, char_len);
            out[tmp_len + char_len] = '\0';
            out_list[*out_cnt] = out;
            (*out_cnt)++;
        }
    }
}
/**
 * Collect phrase candidates from the phrase indexes, best first.
 * @param pinyin  the input
 * @param out     array receiving newly allocated strings
 * @param out_max capacity of `out`
 * @return        number of strings written
 */
static int lv_keyboard_collect_phrase_candidates(const char * pinyin, char ** out, int out_max)
{
    lv_phrase_hit_t hits[LV_KEYBOARD_MAX_CANDIDATES];
    int hit_cnt = 0;

    if(lv_keyboard_phrase_index_init()) {
        int32_t node_idx = lv_keyboard_trie_follow(&phrase_pinyin_index, pinyin);
        if(node_idx >= 0) {
            lv_keyboard_phrase_collect_hits(&phrase_pinyin_index, node_idx, pinyin, hits, &hit_cnt,
                                            out_max, true, true);
        }

        node_idx = lv_keyboard_trie_follow(&phrase_initials_index, pinyin);
        if(node_idx >= 0) {
            lv_keyboard_phrase_collect_hits(&phrase_initials_index, node_idx, pinyin, hits, &hit_cnt,
                                            out_max, true, true);
        }
    }
    else {
        for(int i = 0; ime_phrase_dict[i].pinyin != NULL; i++) {
            uint32_t score = lv_keyboard_phrase_score(pinyin, &ime_phrase_dict[i]);
            lv_keyboard_phrase_try_insert(hits, &hit_cnt, out_max, ime_phrase_dict[i].phrase, score);
        }
    }

    /*Insertion sort, highest score first. The table is small and this runs only
     *when the composition changes.*/
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
        size_t len = lv_strlen(hits[i].phrase);
        char * copy = lv_malloc(len + 1);
        if(copy == NULL) {
            continue;
        }
        lv_memcpy(copy, hits[i].phrase, len);
        copy[len] = '\0';
        out[out_cnt++] = copy;
    }

    return out_cnt;
}

/**
 * Collect single-character candidates by trying every split of the input.
 * @param pinyin  the input
 * @param out     array receiving newly allocated strings
 * @param out_max capacity of `out`
 * @return        number of strings written
 */
static int lv_keyboard_collect_char_candidates(const char * pinyin, char ** out, int out_max)
{
    int count = 0;
    char tmp[64];

    /*Exact readings first, then readings whose tail is still partial.*/
    uint16_t budget = LV_KEYBOARD_BUILD_VISIT_BUDGET;
    lv_keyboard_build_candidates(pinyin, 0, tmp, 0, sizeof(tmp) - 1, out, &count, out_max, false, &budget);

    if(count < out_max) {
        char ** partial = lv_malloc(sizeof(char *) * out_max);
        if(partial != NULL) {
            int partial_cnt = 0;
            uint16_t partial_budget = LV_KEYBOARD_BUILD_VISIT_BUDGET;
            lv_keyboard_build_candidates(pinyin, 0, tmp, 0, sizeof(tmp) - 1, partial, &partial_cnt, out_max,
                                         true, &partial_budget);

            for(int i = 0; i < partial_cnt && count < out_max; i++) {
                bool dup = false;
                for(int j = 0; j < count; j++) {
                    if(lv_strcmp(out[j], partial[i]) == 0) {
                        dup = true;
                        break;
                    }
                }

                if(dup) {
                    lv_free(partial[i]);
                }
                else {
                    out[count++] = partial[i];
                    partial[i] = NULL;
                }
            }

            for(int i = 0; i < partial_cnt; i++) {
                lv_free(partial[i]);
            }
            lv_free(partial);
        }
    }

    return count;
}

/**
 * Whether the input is a single complete syllable, in which case phrases cannot
 * match and the search can skip them.
 * @param pinyin the input
 * @return       true when `pinyin` is one complete syllable
 */
static bool lv_keyboard_is_single_syllable(const char * pinyin)
{
    if(pinyin == NULL || pinyin[0] == '\0') {
        return false;
    }

    if(lv_keyboard_pinyin_index_init()) {
        int32_t node_idx = lv_keyboard_trie_follow(&pinyin_index, pinyin);
        return node_idx >= 0 && pinyin_index.nodes[node_idx].entry_head >= 0;
    }

    for(int i = 0; pinyin_dict[i].pinyin != NULL; i++) {
        if(lv_strcmp(pinyin_dict[i].pinyin, pinyin) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * Whether the input splits into two or more complete syllables.
 * @param pinyin the input
 * @return       true when every letter is consumed by complete syllables
 */
static bool lv_keyboard_is_multi_syllable(const char * pinyin)
{
    size_t total_len = lv_strlen(pinyin);
    if(total_len < 2 || !lv_keyboard_pinyin_index_init()) {
        return false;
    }

    /*`reached[i]` is the largest number of syllables that can end exactly at
     *offset i, or -1 when no split reaches it.*/
    int8_t reached[LV_KEYBOARD_IME_LOOKUP_MAX];
    for(size_t i = 0; i <= total_len && i < LV_KEYBOARD_IME_LOOKUP_MAX; i++) {
        reached[i] = -1;
    }
    reached[0] = 0;

    for(size_t pos = 0; pos < total_len && pos < LV_KEYBOARD_IME_LOOKUP_MAX; pos++) {
        if(reached[pos] < 0) {
            continue;
        }

        lv_keyboard_trie_match_t matches[16];
        int32_t prefix_node_idx;
        size_t prefix_len;
        int match_cnt = lv_keyboard_pinyin_prefix_matches(pinyin, pos, matches, 16, &prefix_node_idx, &prefix_len);

        for(int i = 0; i < match_cnt; i++) {
            size_t next = pos + matches[i].key_len;
            if(next >= LV_KEYBOARD_IME_LOOKUP_MAX) {
                continue;
            }
            int8_t cnt = (int8_t)(reached[pos] + 1);
            if(cnt > reached[next]) {
                reached[next] = cnt;
            }
        }

        /*A complete syllable followed by a syllable still being typed is also a
         *phrase input, e.g. `nih`.*/
        if(reached[pos] >= 1 && prefix_node_idx >= 0 && prefix_len == (total_len - pos)) {
            return true;
        }
    }

    if(total_len < LV_KEYBOARD_IME_LOOKUP_MAX && reached[total_len] >= 2) {
        return true;
    }

    for(size_t pos = 1; pos < total_len && pos < LV_KEYBOARD_IME_LOOKUP_MAX; pos++) {
        if(reached[pos] < 1) {
            continue;
        }

        lv_keyboard_trie_match_t matches[16];
        int32_t prefix_node_idx;
        size_t prefix_len;
        lv_keyboard_pinyin_prefix_matches(pinyin, pos, matches, 16, &prefix_node_idx, &prefix_len);
        if(prefix_node_idx >= 0 && prefix_len == (total_len - pos)) {
            return true;
        }
    }

    return false;
}

/**
 * Whether phrases are worth searching for this input. Single syllables can only
 * ever produce single characters, so the phrase search is skipped for them.
 * @param pinyin the input
 * @return       true when the phrase index should be queried
 */
static bool lv_keyboard_want_phrase_candidates(const char * pinyin)
{
    size_t len = lv_strlen(pinyin);
    if(len <= 1) {
        return false;
    }

    if(lv_keyboard_is_single_syllable(pinyin)) {
        return false;
    }

    if(lv_keyboard_is_multi_syllable(pinyin)) {
        return true;
    }

    if(lv_keyboard_phrase_index_init()) {
        return lv_keyboard_trie_follow(&phrase_initials_index, pinyin) >= 0;
    }

    return false;
}

/**
 * Build the full candidate list for the current composition: phrases first, then
 * single characters, with duplicates dropped.
 * @param pinyin the input
 * @param out    out: newly allocated array of newly allocated strings
 * @return       number of candidates, 0 when nothing matched
 */
static int lv_keyboard_collect_mixed_candidates(const char * pinyin, char *** out)
{
    if(pinyin == NULL || out == NULL || pinyin[0] == '\0') {
        return 0;
    }

    char ** list = lv_malloc(LV_KEYBOARD_MAX_CANDIDATES * sizeof(char *));
    if(list == NULL) {
        return 0;
    }

    int count = 0;
    if(lv_keyboard_want_phrase_candidates(pinyin)) {
        count = lv_keyboard_collect_phrase_candidates(pinyin, list, LV_KEYBOARD_MAX_CANDIDATES);
    }

    /*Mixing characters in keeps short inputs useful while the user is still
     *typing, but a long input already has plenty of phrase readings.*/
    bool mix_chars = count < LV_KEYBOARD_MAX_CANDIDATES;
    if(count > 0 && lv_strlen(pinyin) > LV_KEYBOARD_CHAR_MIX_MAX_PINYIN_LEN) {
        mix_chars = false;
    }

    if(mix_chars) {
        char ** extra = lv_malloc(LV_KEYBOARD_MAX_CANDIDATES * sizeof(char *));
        if(extra != NULL) {
            int extra_cnt = lv_keyboard_collect_char_candidates(pinyin, extra, LV_KEYBOARD_MAX_CANDIDATES);
            int i = 0;
            for(; i < extra_cnt && count < LV_KEYBOARD_MAX_CANDIDATES; i++) {
                bool dup = false;
                for(int j = 0; j < count; j++) {
                    if(lv_strcmp(list[j], extra[i]) == 0) {
                        dup = true;
                        break;
                    }
                }

                if(dup) {
                    lv_free(extra[i]);
                }
                else {
                    list[count++] = extra[i];
                    extra[i] = NULL;
                }
            }

            for(; i < extra_cnt; i++) {
                lv_free(extra[i]);
            }
            lv_free(extra);
        }
    }

    if(count == 0) {
        lv_free(list);
        return 0;
    }

    *out = list;
    return count;
}

#endif  /*LV_USE_KEYBOARD*/
