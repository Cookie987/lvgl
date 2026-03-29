
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

#include "../../../widgets/lv_textarea.h"
#include "../../../misc/lv_assert.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS    &lv_keyboard_class
#define LV_KB_BTN(width) LV_BTNMATRIX_CTRL_POPOVER | width
#define LV_KEYBOARD_MAX_CANDIDATES 100  /* Maximum number of candidate characters to display */

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_keyboard_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);

static void lv_keyboard_update_map(lv_obj_t * obj);

static void lv_keyboard_update_ctrl_map(lv_obj_t * obj);

static void lv_keyboard_handle_ime_chn(lv_keyboard_t * keyboard, const char * txt);

static void lv_keyboard_show_candidates(lv_keyboard_t * keyboard);

static void lv_keyboard_clear_candidates(lv_keyboard_t * keyboard);

static void lv_keyboard_select_candidate(lv_keyboard_t * keyboard, int idx);

static void lv_keyboard_candidate_event_cb(lv_event_t * e);

typedef struct {
    const char * ptr;
    uint8_t len;
} lv_utf8_span_t;

typedef struct {
    const char * phrase;
    uint32_t score;
} lv_phrase_hit_t;

static int lv_keyboard_extract_utf8_spans(const char * str, lv_utf8_span_t * spans, int max_spans);

static void lv_keyboard_build_candidates(const char * pinyin, size_t pos,
                                         char * tmp, size_t tmp_len, size_t tmp_cap,
                                         char ** out_list, int * out_cnt, int out_max);

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
    .width_def = LV_PCT(100),
    .height_def = LV_PCT(50),
    .instance_size = sizeof(lv_keyboard_t),
    .editable = 1,
    .base_class = &lv_btnmatrix_class
};

static const char * const ime_chn_kb_map[] = {"1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
                                                "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
                                                "_", "-", "z", "x", "c", "v", "b", "n", "m", "。", "，", "：", "\n",
                                                LV_SYMBOL_KEYBOARD, "中",LV_SYMBOL_LEFT, "拼音", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
                                                };
static const lv_btnmatrix_ctrl_t ime_kb_ctrl_chn_map[] = {
    LV_KEYBOARD_CTRL_BTN_FLAGS | 5, LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 6, LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1),
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6, LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};




static const char * const default_kb_map_lc[] = {"1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
                                                "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
                                                "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
                                                LV_SYMBOL_KEYBOARD, "英", LV_SYMBOL_LEFT, "QWERTY", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
                                                };

static const lv_btnmatrix_ctrl_t default_kb_ctrl_lc_map[] = {
    LV_KEYBOARD_CTRL_BTN_FLAGS | 5, LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 6, LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1),
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6, LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

static const char * const default_kb_map_uc[] = {"1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
                                                 "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
                                                 "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
                                                 LV_SYMBOL_KEYBOARD, "英", LV_SYMBOL_LEFT, "QWERTY", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
                                                };

static const lv_btnmatrix_ctrl_t default_kb_ctrl_uc_map[] = {
    LV_KEYBOARD_CTRL_BTN_FLAGS | 5, LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_KB_BTN(4), LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 6, LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_KB_BTN(3), LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | LV_KB_BTN(1),
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6, LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

static const char * const default_kb_map_spec[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
                                                   "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
                                                   "\\",  "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
                                                   LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, "符号", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
                                                  };

static const lv_btnmatrix_ctrl_t default_kb_ctrl_spec_map[] = {
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | 2,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1), LV_KB_BTN(1),
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6, LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

static const char * const default_kb_map_num[] = {"1", "2", "3", LV_SYMBOL_KEYBOARD, "\n",
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

static const char * * kb_map[10] = {
    (const char * *)default_kb_map_lc,
    (const char * *)default_kb_map_uc,
    (const char * *)default_kb_map_spec,
    (const char * *)default_kb_map_num,
    (const char * *)default_kb_map_lc,
    (const char * *)default_kb_map_lc,
    (const char * *)default_kb_map_lc,
    (const char * *)default_kb_map_lc,
    (const char * *)ime_chn_kb_map,
    (const char * *)NULL,
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
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Create a Keyboard object
 * @param parent pointer to an object, it will be the parent of the new keyboard
 * @return pointer to the created keyboard
 */
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

void lv_keyboard_reset_ime(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;

    if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN) {
        keyboard->pinyin_buf[0] = '\0';
        lv_keyboard_clear_candidates(keyboard);
    }
}

/**
 * Assign a Text Area to the Keyboard. The pressed characters will be put there.
 * @param kb pointer to a Keyboard object
 * @param ta pointer to a Text Area object to write there
 */
void lv_keyboard_set_textarea(lv_obj_t * obj, lv_obj_t * ta)
{
    if(ta) {
        LV_ASSERT_OBJ(ta, &lv_textarea_class);
    }

    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;

    /*Hide the cursor of the old Text area if cursor management is enabled*/
    if(keyboard->ta) {
        lv_obj_clear_state(obj, LV_STATE_FOCUSED);
    }

    keyboard->ta = ta;

    /*Show the cursor of the new Text area if cursor management is enabled*/
    if(keyboard->ta) {
        lv_obj_add_flag(obj, LV_STATE_FOCUSED);
    }
}

/**
 * Set a new a mode (text or number map)
 * @param kb pointer to a Keyboard object
 * @param mode the mode from 'lv_keyboard_mode_t'
 */
void lv_keyboard_set_mode(lv_obj_t * obj, lv_keyboard_mode_t mode)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    if(keyboard->mode == mode) return;

    keyboard->mode = mode;
    keyboard->pinyin_buf[0] = '\0';  // Clear pinyin buffer
    lv_keyboard_clear_candidates(keyboard);
    lv_keyboard_update_map(obj);
}

/**
 * Set the candidate list object for IME CHN mode
 * @param obj pointer to a Keyboard object
 * @param list pointer to the candidate list object
 */
void lv_keyboard_set_candidate_list(lv_obj_t * obj, lv_obj_t * list)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    keyboard->candidate_list = list;
    /* Ensure candidate_btnm is NULL initially */
    keyboard->candidate_btnm = NULL;
}

/**
 * Show the button title in a popover when pressed.
 * @param kb pointer to a Keyboard object
 * @param en whether "popovers" mode is enabled
 */
void lv_keyboard_set_popovers(lv_obj_t * obj, bool en)
{
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;

    if(keyboard->popovers == en) {
        return;
    }

    keyboard->popovers = en;
    lv_keyboard_update_ctrl_map(obj);
}

/**
 * Set a new map for the keyboard
 * @param kb pointer to a Keyboard object
 * @param mode keyboard map to alter 'lv_keyboard_mode_t'
 * @param map pointer to a string array to describe the map.
 *            See 'lv_btnmatrix_set_map()' for more info.
 */
void lv_keyboard_set_map(lv_obj_t * obj, lv_keyboard_mode_t mode, const char * map[],
                         const lv_btnmatrix_ctrl_t ctrl_map[])
{
    kb_map[mode] = map;
    kb_ctrl[mode] = ctrl_map;
    lv_keyboard_update_map(obj);
}

/*=====================
 * Getter functions
 *====================*/

/**
 * Assign a Text Area to the Keyboard. The pressed characters will be put there.
 * @param kb pointer to a Keyboard object
 * @return pointer to the assigned Text Area object
 */
lv_obj_t * lv_keyboard_get_textarea(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    return keyboard->ta;
}

/**
 * Set a new a mode (text or number map)
 * @param kb pointer to a Keyboard object
 * @return the current mode from 'lv_keyboard_mode_t'
 */
lv_keyboard_mode_t lv_keyboard_get_mode(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    return keyboard->mode;
}

/**
 * Tell whether "popovers" mode is enabled or not.
 * @param kb pointer to a Keyboard object
 * @return true: "popovers" mode is enabled; false: disabled
 */
bool lv_btnmatrix_get_popovers(const lv_obj_t * obj)
{
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    return keyboard->popovers;
}

/*=====================
 * Other functions
 *====================*/

/**
 * Default keyboard event to add characters to the Text area and change the map.
 * If a custom `event_cb` is added to the keyboard this function can be called from it to handle the
 * button clicks
 * @param kb pointer to a keyboard
 * @param event the triggering event
 */
void lv_keyboard_def_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);

    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    uint16_t btn_id   = lv_btnmatrix_get_selected_btn(obj);
    if(btn_id == LV_BTNMATRIX_BTN_NONE) return;

    const char * txt = lv_btnmatrix_get_btn_text(obj, lv_btnmatrix_get_selected_btn(obj));
    if(txt == NULL) return;

    if(strcmp(txt, "abc") == 0) {
        keyboard->mode = LV_KEYBOARD_MODE_TEXT_LOWER;
        lv_btnmatrix_set_map(obj, kb_map[LV_KEYBOARD_MODE_TEXT_LOWER]);
        lv_keyboard_update_ctrl_map(obj);
        return;
    }
    else if(strcmp(txt, "ABC") == 0) {
        // 切换走的时候先清除拼音输入buffer
        if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN && keyboard->pinyin_buf[0] != '\0') {
            // Delete pinyin buffer
            keyboard->pinyin_buf[0] = '\0';
            // 无需删除textarea里的字符，按下enter时应当保留它们在输入框中
            lv_keyboard_clear_candidates(keyboard);
        }
        keyboard->mode = LV_KEYBOARD_MODE_TEXT_UPPER;
        lv_btnmatrix_set_map(obj, kb_map[LV_KEYBOARD_MODE_TEXT_UPPER]);
        lv_keyboard_update_ctrl_map(obj);
        return;
    }
    else if(strcmp(txt, "英") == 0) {
        keyboard->mode = LV_KEYBOARD_MODE_IME_CHN;
        lv_btnmatrix_set_map(obj, kb_map[LV_KEYBOARD_MODE_IME_CHN]);
        lv_keyboard_update_ctrl_map(obj);
        return;
    }
    else if(strcmp(txt, "中") == 0) {
        // 切换走的时候先清除拼音输入buffer
        if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN && keyboard->pinyin_buf[0] != '\0') {
            // Delete pinyin buffer
            keyboard->pinyin_buf[0] = '\0';
            // 无需删除textarea里的字符，按下enter时应当保留它们在输入框中
            lv_keyboard_clear_candidates(keyboard);
        }
        keyboard->mode = LV_KEYBOARD_MODE_TEXT_LOWER;
        lv_btnmatrix_set_map(obj, kb_map[LV_KEYBOARD_MODE_TEXT_LOWER]);
        lv_keyboard_update_ctrl_map(obj);
        return;
    }
    else if(strcmp(txt, "1#") == 0) {
        // 切换走的时候先清除拼音输入buffer
        if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN && keyboard->pinyin_buf[0] != '\0') {
            // Delete pinyin buffer
            keyboard->pinyin_buf[0] = '\0';
            // 无需删除textarea里的字符，按下enter时应当保留它们在输入框中
            lv_keyboard_clear_candidates(keyboard);
        }
        keyboard->mode = LV_KEYBOARD_MODE_SPECIAL;
        lv_btnmatrix_set_map(obj, kb_map[LV_KEYBOARD_MODE_SPECIAL]);
        lv_keyboard_update_ctrl_map(obj);
        return;
    }
    else if(strcmp(txt, LV_SYMBOL_CLOSE) == 0 || strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) {
        lv_res_t res = lv_event_send(obj, LV_EVENT_CANCEL, NULL);
        if(res != LV_RES_OK) return;

        if(keyboard->ta) {
            res = lv_event_send(keyboard->ta, LV_EVENT_CANCEL, NULL);
            if(res != LV_RES_OK) return;
        }
        return;
    }
    else if(strcmp(txt, LV_SYMBOL_OK) == 0) {
        lv_res_t res = lv_event_send(obj, LV_EVENT_READY, NULL);
        if(res != LV_RES_OK) return;

        if(keyboard->ta) {
            res = lv_event_send(keyboard->ta, LV_EVENT_READY, NULL);
            if(res != LV_RES_OK) return;
        }
        return;
    }

    /*Add the characters to the text area if set*/
    if(keyboard->ta == NULL) return;

    if(strcmp(txt, "Enter") == 0 || strcmp(txt, LV_SYMBOL_NEW_LINE) == 0) {
        if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN && keyboard->pinyin_buf[0] != '\0') {
            // Delete pinyin buffer
            keyboard->pinyin_buf[0] = '\0';
            // 无需删除textarea里的字符，按下enter时应当保留它们在输入框中
            lv_keyboard_clear_candidates(keyboard);
            // 直接返回
            return;
        }
        lv_textarea_add_char(keyboard->ta, '\n');
        if(lv_textarea_get_one_line(keyboard->ta)) {
            lv_res_t res = lv_event_send(keyboard->ta, LV_EVENT_READY, NULL);
            if(res != LV_RES_OK) return;
        }
    }
    else if(strcmp(txt, LV_SYMBOL_LEFT) == 0) {
        lv_textarea_cursor_left(keyboard->ta);
    }
    else if(strcmp(txt, LV_SYMBOL_RIGHT) == 0) {
        lv_textarea_cursor_right(keyboard->ta);
    }
    else if(strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN) {
            lv_keyboard_handle_ime_chn(keyboard, txt);
        }
        else {
            lv_keyboard_clear_candidates(keyboard);
            lv_textarea_del_char(keyboard->ta);
        }
    }
    else if(strcmp(txt, "QWERTY") == 0 ) {
        lv_textarea_add_char(keyboard->ta, ' ');
    }
    else if(strcmp(txt, "拼音") == 0 ) {
        lv_textarea_add_char(keyboard->ta, ' ');
    }
    else if(strcmp(txt, "符号") == 0 ) {
        lv_textarea_add_char(keyboard->ta, ' ');
    }
    else if(strcmp(txt, "+/-") == 0) {
        uint16_t cur        = lv_textarea_get_cursor_pos(keyboard->ta);
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
    }
    else {
        if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN) {
            lv_keyboard_handle_ime_chn(keyboard, txt);
        } else {
            lv_textarea_add_text(keyboard->ta, txt);
        }
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_keyboard_handle_ime_chn(lv_keyboard_t * keyboard, const char * txt)
{
    if(strlen(txt) == 1 && isalpha((unsigned char)*txt)) {
        // Append letter to pinyin buffer
        size_t len = strlen(keyboard->pinyin_buf);
        if(len < sizeof(keyboard->pinyin_buf) - 1) {
            // If this is the first letter, record start position
            if(len == 0) {
                keyboard->pinyin_start_pos = lv_textarea_get_cursor_pos(keyboard->ta);
            }
            // Add letter to text area
            lv_textarea_add_text(keyboard->ta, txt);
            // Update pinyin buffer
            keyboard->pinyin_buf[len] = tolower((unsigned char)*txt);
            keyboard->pinyin_buf[len + 1] = '\0';
            // Show candidates after adding letter
            lv_keyboard_show_candidates(keyboard);
        }
    } else if(strcmp(txt, " ") == 0) {
        // Space: convert pinyin and show candidates (if not already shown)
        lv_keyboard_show_candidates(keyboard);
    } else if(isdigit((unsigned char)*txt)) {
        // Number: select candidate
        int idx = *txt - '0' - 1;  // 1-based to 0-based
        lv_keyboard_select_candidate(keyboard, idx);
    } else if(strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        if(keyboard->pinyin_buf[0] != '\0') {
            // Delete from pinyin buffer and text area
            size_t len = strlen(keyboard->pinyin_buf);
            if(len > 0) {
                // Delete last character from text area
                lv_textarea_del_char(keyboard->ta);
                // Update pinyin buffer
                keyboard->pinyin_buf[len - 1] = '\0';
                // Show updated candidates after deleting
                if(keyboard->pinyin_buf[0] != '\0') {
                    lv_keyboard_show_candidates(keyboard);
                } else {
                    lv_keyboard_clear_candidates(keyboard);
                }
            }
        } else {
            // Normal backspace
            lv_textarea_del_char(keyboard->ta);
        }
    } else {
        // Other keys: add directly
        lv_textarea_add_text(keyboard->ta, txt);
    }
}

static void lv_keyboard_show_candidates(lv_keyboard_t * keyboard)
{
    if(!keyboard->candidate_list || keyboard->pinyin_buf[0] == '\0') return;

    char ** candidate_list = NULL;
    int count = lv_keyboard_collect_phrase_candidates(keyboard->pinyin_buf, &candidate_list);

    if(count <= 0 || candidate_list == NULL) return;

    /* Clear previous candidates */
    lv_keyboard_clear_candidates(keyboard);

    // Create button matrix for candidates
    keyboard->candidate_btnm = lv_btnmatrix_create(keyboard->candidate_list);
    if(!keyboard->candidate_btnm) return;  // Check if creation failed
    lv_obj_clear_flag(keyboard->candidate_btnm, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    // 10候选字符需要320px宽度
    // 计算总体宽度
    lv_obj_add_flag(keyboard->candidate_btnm, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(keyboard->candidate_btnm, lv_keyboard_candidate_event_cb, LV_EVENT_VALUE_CHANGED, keyboard);

    const char ** map = lv_mem_alloc((count + 1) * sizeof(const char *));
    if(!map) {
        lv_keyboard_clear_candidates(keyboard);
        for(int i = 0; i < count; i++) {
            lv_mem_free(candidate_list[i]);
        }
        lv_mem_free(candidate_list);
        return;
    }

    uint16_t width_units[LV_KEYBOARD_MAX_CANDIDATES];
    uint16_t total_units = 0;
    for(int i = 0; i < count; i++) {
        map[i] = candidate_list[i];
        int char_cnt = lv_keyboard_utf8_char_count(candidate_list[i]);
        uint16_t w = (char_cnt <= 0) ? 1 : (uint16_t)char_cnt;
        if(w > 15) w = 15;
        width_units[i] = w;
        total_units = (uint16_t)(total_units + w);
    }
    map[count] = NULL;  /* NULL terminate */
    
    lv_btnmatrix_set_map(keyboard->candidate_btnm, map);

    // 10å€™é€‰å­—ç¬¦éœ€è¦320pxå®½åº¦
    // è®¡ç®—æ€»ä½“å®½åº¦ (æŒ‰å­—ç¬¦å®½åº¦)
    uint16_t unit_px = 320 / 10;
    uint16_t buttonmatrix_width = (uint16_t)(unit_px * (total_units > 0 ? total_units : (uint16_t)count));
    lv_obj_set_size(keyboard->candidate_btnm, buttonmatrix_width, lv_obj_get_height(keyboard->candidate_list) - 5);
    
    // Set control map to make buttons trigger on click instead of release
    lv_btnmatrix_ctrl_t * ctrl_map = lv_mem_alloc((count + 1) * sizeof(lv_btnmatrix_ctrl_t));
    if(ctrl_map) {
        for(int i = 0; i < count; i++) {
            ctrl_map[i] = (lv_btnmatrix_ctrl_t)(LV_BTNMATRIX_CTRL_CLICK_TRIG | width_units[i]);
        }
        ctrl_map[count] = 0;  // NULL terminator
        lv_btnmatrix_set_ctrl_map(keyboard->candidate_btnm, ctrl_map);
        lv_mem_free(ctrl_map);
    }
    
    /* store count for bounds checking */
    keyboard->candidate_cnt = (uint8_t)count;
    keyboard->candidate_map = map;
    keyboard->candidates_list = (const char **)candidate_list;  // Store for cleanup
}

static void lv_keyboard_clear_candidates(lv_keyboard_t * keyboard)
{
    if(keyboard->candidate_btnm) {
        lv_obj_del(keyboard->candidate_btnm);
        keyboard->candidate_btnm = NULL;
    }
    if(keyboard->candidate_map) {
        lv_mem_free(keyboard->candidate_map);
        keyboard->candidate_map = NULL;
    }
    if(keyboard->candidates_list) {
        for(uint8_t i = 0; i < keyboard->candidate_cnt; i++) {
            lv_mem_free((void *)keyboard->candidates_list[i]);
        }
        lv_mem_free(keyboard->candidates_list);
        keyboard->candidates_list = NULL;
    }
    keyboard->candidate_cnt = 0;
}

static void lv_keyboard_select_candidate(lv_keyboard_t * keyboard, int idx)
{
    if(!keyboard->candidate_btnm) return;
    if(idx < 0 || idx >= (int)keyboard->candidate_cnt) return; /* bounds check */

    const char * txt = lv_btnmatrix_get_btn_text(keyboard->candidate_btnm, idx);
    if(txt && txt[0] != '\0') {
        // Replace pinyin with Chinese character
        size_t plen = strlen(keyboard->pinyin_buf);
        if(plen > 0) {
            // Move cursor to end of pinyin (start + length)
            lv_textarea_set_cursor_pos(keyboard->ta, keyboard->pinyin_start_pos + plen);
            // Delete plen characters (backward)
            for(size_t i = 0; i < plen; i++) {
                lv_textarea_del_char(keyboard->ta);
            }
            // Insert Chinese character
            lv_textarea_add_text(keyboard->ta, txt);
        } else {
            // Fallback: just add text
            lv_textarea_add_text(keyboard->ta, txt);
        }
        keyboard->pinyin_buf[0] = '\0';  /* Clear pinyin */
        lv_keyboard_clear_candidates(keyboard);
    }
}

static void lv_keyboard_candidate_event_cb(lv_event_t * e)
{
    lv_obj_t * btnm = lv_event_get_target(e);
    void * ud = lv_event_get_user_data(e);
    if(!ud) return;
    lv_keyboard_t * keyboard = (lv_keyboard_t *)ud;
    /* Sanity check: keyboard pointer should be a valid object */
    if(!keyboard) return;
    uint16_t btn_id = lv_btnmatrix_get_selected_btn(btnm);
    if(btn_id != LV_BTNMATRIX_BTN_NONE) {
        /* Ensure btn_id fits in int and within count */
        lv_keyboard_select_candidate(keyboard, (int)btn_id);
    }
}

static void lv_keyboard_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    keyboard->ta         = NULL;
    keyboard->mode       = LV_KEYBOARD_MODE_TEXT_LOWER;
    keyboard->popovers   = 0;
    keyboard->pinyin_buf[0] = '\0';
    keyboard->candidate_list = NULL;
    keyboard->candidate_btnm = NULL;
    keyboard->candidate_cnt = 0;
    keyboard->candidate_map = NULL;
    keyboard->pinyin_start_pos = 0;

    lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(obj, lv_keyboard_def_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_base_dir(obj, LV_BASE_DIR_LTR, 0);

    lv_keyboard_update_map(obj);
}

/**
 * Update the key and control map for the current mode
 * @param obj pointer to a keyboard object
 */
static void lv_keyboard_update_map(lv_obj_t * obj)
{
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;
    lv_btnmatrix_set_map(obj, kb_map[keyboard->mode]);
    lv_keyboard_update_ctrl_map(obj);
}

/**
 * Update the control map for the current mode
 * @param obj pointer to a keyboard object
 */
static void lv_keyboard_update_ctrl_map(lv_obj_t * obj)
{
    lv_keyboard_t * keyboard = (lv_keyboard_t *)obj;

    if(keyboard->popovers) {
        /*Apply the current control map (already includes LV_BTNMATRIX_CTRL_POPOVER flags)*/
        lv_btnmatrix_set_ctrl_map(obj, kb_ctrl[keyboard->mode]);
    }
    else {
        /*Make a copy of the current control map*/
        lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)obj;
        lv_btnmatrix_ctrl_t * ctrl_map = lv_mem_alloc(btnm->btn_cnt * sizeof(lv_btnmatrix_ctrl_t));
        lv_memcpy(ctrl_map, kb_ctrl[keyboard->mode], sizeof(lv_btnmatrix_ctrl_t) * btnm->btn_cnt);

        /*Remove all LV_BTNMATRIX_CTRL_POPOVER flags*/
        for(uint16_t i = 0; i < btnm->btn_cnt; i++) {
            ctrl_map[i] &= (~LV_BTNMATRIX_CTRL_POPOVER);
        }

        /*Apply new control map and clean up*/
        lv_btnmatrix_set_ctrl_map(obj, ctrl_map);
        lv_mem_free(ctrl_map);
    }
}

static int lv_keyboard_extract_utf8_spans(const char * str, lv_utf8_span_t * spans, int max_spans)
{
    if(!str || !spans || max_spans <= 0) return 0;

    int count = 0;
    int idx = 0;
    while(str[idx] != '\0' && count < max_spans) {
        spans[count].ptr = &str[idx];
        if((unsigned char)str[idx] >= 0xE0) {
            spans[count].len = 3;
        }
        else if((unsigned char)str[idx] >= 0xC0) {
            spans[count].len = 2;
        }
        else {
            spans[count].len = 1;
        }
        idx += spans[count].len;
        count++;
    }
    return count;
}

static void lv_keyboard_build_candidates(const char * pinyin, size_t pos,
                                         char * tmp, size_t tmp_len, size_t tmp_cap,
                                         char ** out_list, int * out_cnt, int out_max)
{
    if(*out_cnt >= out_max) return;
    size_t total_len = strlen(pinyin);
    if(pos >= total_len) {
        char * out = lv_mem_alloc(tmp_len + 1);
        if(!out) return;
        lv_memcpy(out, tmp, tmp_len);
        out[tmp_len] = '\0';
        out_list[*out_cnt] = out;
        (*out_cnt)++;
        return;
    }

    int max_match_len = 0;
    for(int i = 0; pinyin_dict[i].pinyin != NULL; i++) {
        size_t plen = strlen(pinyin_dict[i].pinyin);
        if(plen == 0) continue;
        if(strncmp(&pinyin[pos], pinyin_dict[i].pinyin, plen) == 0) {
            if((int)plen > max_match_len) max_match_len = (int)plen;
        }
    }

    for(int cur_len = max_match_len; cur_len >= 1 && *out_cnt < out_max; cur_len--) {
        for(int i = 0; pinyin_dict[i].pinyin != NULL; i++) {
            size_t plen = strlen(pinyin_dict[i].pinyin);
            if(plen == 0) continue;
            if((int)plen != cur_len) continue;
            if(strncmp(&pinyin[pos], pinyin_dict[i].pinyin, plen) != 0) continue;

            lv_utf8_span_t spans[12];
            int span_cnt = lv_keyboard_extract_utf8_spans(pinyin_dict[i].chinese, spans, 12);
            if(span_cnt <= 0) continue;

            for(int s = 0; s < span_cnt && *out_cnt < out_max; s++) {
                if(tmp_len + spans[s].len > tmp_cap) continue;
                lv_memcpy(&tmp[tmp_len], spans[s].ptr, spans[s].len);
                lv_keyboard_build_candidates(pinyin, pos + plen,
                                             tmp, tmp_len + spans[s].len, tmp_cap,
                                             out_list, out_cnt, out_max);
            }
        }
    }
}

static int lv_keyboard_collect_phrase_candidates(const char * pinyin, char *** out_list)
{
    if(!pinyin || !out_list) return 0;

    char ** list = lv_mem_alloc(LV_KEYBOARD_MAX_CANDIDATES * sizeof(char *));
    if(!list) return 0;

    int count = 0;
    if(!lv_keyboard_is_single_syllable(pinyin) || lv_keyboard_is_two_syllables(pinyin)) {
        count = lv_keyboard_collect_rime_candidates(pinyin, list, LV_KEYBOARD_MAX_CANDIDATES);
    }

    if(count < LV_KEYBOARD_MAX_CANDIDATES) {
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
                if(extra[i]) lv_mem_free(extra[i]);
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
    if(!input || !entry || !entry->pinyin || !entry->phrase) return 0;
    size_t in_len = strlen(input);
    if(in_len == 0) return 0;

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
    if(!hits || !hit_cnt || !phrase || score == 0) return;

    for(int i = 0; i < *hit_cnt; i++) {
        if(strcmp(hits[i].phrase, phrase) == 0) {
            if(score > hits[i].score) hits[i].score = score;
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
        if(hits[i].score < hits[min_idx].score) min_idx = i;
    }
    if(score > hits[min_idx].score) {
        hits[min_idx].phrase = phrase;
        hits[min_idx].score = score;
    }
}

static int lv_keyboard_collect_rime_candidates(const char * pinyin, char ** out_list, int out_max)
{
    if(!pinyin || !out_list || out_max <= 0) return 0;

    lv_phrase_hit_t hits[LV_KEYBOARD_MAX_CANDIDATES];
    int hit_cnt = 0;

    for(int i = 0; ime_phrase_dict[i].pinyin != NULL; i++) {
        uint32_t score = lv_keyboard_phrase_score(pinyin, &ime_phrase_dict[i]);
        if(score == 0) continue;
        lv_keyboard_phrase_try_insert(hits, &hit_cnt, out_max, ime_phrase_dict[i].phrase, score);
    }

    if(hit_cnt == 0) return 0;

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
        if(!out) continue;
        lv_memcpy(out, hits[i].phrase, len);
        out[len] = '\0';
        out_list[out_cnt++] = out;
    }
    return out_cnt;
}

static int lv_keyboard_collect_char_candidates(const char * pinyin, char ** out_list, int out_max)
{
    if(!pinyin || !out_list || out_max <= 0) return 0;

    int count = 0;
    char tmp[64];
    lv_keyboard_build_candidates(pinyin, 0, tmp, 0, sizeof(tmp) - 1,
                                 out_list, &count, out_max);

    if(count == 0) {
        size_t pinyin_len = strlen(pinyin);
        for(int i = 0; pinyin_dict[i].pinyin != NULL; i++) {
            if(strncmp(pinyin_dict[i].pinyin, pinyin, pinyin_len) != 0) continue;

            lv_utf8_span_t spans[LV_KEYBOARD_MAX_CANDIDATES];
            int span_cnt = lv_keyboard_extract_utf8_spans(pinyin_dict[i].chinese, spans, LV_KEYBOARD_MAX_CANDIDATES);
            if(span_cnt <= 0) break;

            for(int s = 0; s < span_cnt && count < out_max; s++) {
                char * out = lv_mem_alloc(spans[s].len + 1);
                if(!out) break;
                lv_memcpy(out, spans[s].ptr, spans[s].len);
                out[spans[s].len] = '\0';
                out_list[count++] = out;
            }
            break;
        }
    }

    return count;
}

static bool lv_keyboard_is_single_syllable(const char * pinyin)
{
    if(!pinyin || pinyin[0] == '\0') return false;
    for(int i = 0; pinyin_dict[i].pinyin != NULL; i++) {
        if(strcmp(pinyin_dict[i].pinyin, pinyin) == 0) {
            return true;
        }
    }
    return false;
}

static bool lv_keyboard_is_two_syllables(const char * pinyin)
{
    if(!pinyin || pinyin[0] == '\0') return false;
    size_t total_len = strlen(pinyin);
    if(total_len < 2) return false;

    for(int i = 0; pinyin_dict[i].pinyin != NULL; i++) {
        const char * first = pinyin_dict[i].pinyin;
        size_t flen = strlen(first);
        if(flen == 0 || flen >= total_len) continue;
        if(strncmp(pinyin, first, flen) != 0) continue;

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
    if(!str) return 0;
    int count = 0;
    int idx = 0;
    while(str[idx] != '\0') {
        unsigned char c = (unsigned char)str[idx];
        if(c >= 0xE0) idx += 3;
        else if(c >= 0xC0) idx += 2;
        else idx += 1;
        count++;
    }
    return count;
}

#endif  /*LV_USE_KEYBOARD*/
