
/**
 * @file lv_keyboard.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_keyboard.h"
#include "lv_ime_dict.h"
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
        if(keyboard->mode == LV_KEYBOARD_MODE_IME_CHN && keyboard->pinyin_buf[0] != '\0') {
            // Delete from pinyin buffer
            size_t len = strlen(keyboard->pinyin_buf);
            if(len > 0) {
                keyboard->pinyin_buf[len - 1] = '\0';
                lv_keyboard_handle_ime_chn(keyboard, txt);
            }
        } else {
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

    // Find matching pinyin entry and extract individual characters from the chinese string
    const char * chinese_chars = NULL;
    size_t pinyin_len = strlen(keyboard->pinyin_buf);
    
    for(int i = 0; pinyin_dict[i].pinyin != NULL; i++) {
        if(strncmp(pinyin_dict[i].pinyin, keyboard->pinyin_buf, pinyin_len) == 0) {
            // Prefix match found
            chinese_chars = pinyin_dict[i].chinese;
            break;
        }
    }

    if(chinese_chars == NULL || chinese_chars[0] == '\0') return;

    // Clear previous candidates
    lv_keyboard_clear_candidates(keyboard);

    // Count and extract individual Chinese characters (max LV_KEYBOARD_MAX_CANDIDATES)
    int count = 0;
    int positions[LV_KEYBOARD_MAX_CANDIDATES];  // Store byte position of each character
    int char_idx = 0;
    
    while(chinese_chars[char_idx] != '\0' && count < LV_KEYBOARD_MAX_CANDIDATES) {
        positions[count] = char_idx;
        // UTF-8: skip to next character
        if((unsigned char)chinese_chars[char_idx] >= 0xE0) {
            char_idx += 3;  // 3-byte UTF-8 character
        } else if((unsigned char)chinese_chars[char_idx] >= 0xC0) {
            char_idx += 2;  // 2-byte UTF-8 character
        } else {
            char_idx += 1;  // 1-byte ASCII character
        }
        count++;
    }

    if(count == 0) return;

    // Create button matrix for candidates
    keyboard->candidate_btnm = lv_btnmatrix_create(keyboard->candidate_list);
    if(!keyboard->candidate_btnm) return;  // Check if creation failed
    lv_obj_clear_flag(keyboard->candidate_btnm, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    // 10候选字符需要320px宽度
    // 计算总体宽度
    uint16_t buttonmatrix_width = 320 / 10 * count;
    lv_obj_set_size(keyboard->candidate_btnm, buttonmatrix_width, lv_obj_get_height(keyboard->candidate_list) - 5);
    lv_obj_add_flag(keyboard->candidate_btnm, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(keyboard->candidate_btnm, lv_keyboard_candidate_event_cb, LV_EVENT_VALUE_CHANGED, keyboard);

    // Allocate memory for candidate strings and map
    // Each candidate can be up to 4 bytes (3 bytes for UTF-8 char + 1 null terminator)
    char * candidates_buffer = lv_mem_alloc(count * 4 + 1);
    const char ** map = lv_mem_alloc((count + 1) * sizeof(const char *));
    
    if(!candidates_buffer || !map) {
        if(candidates_buffer) lv_mem_free(candidates_buffer);
        if(map) lv_mem_free(map);
        lv_keyboard_clear_candidates(keyboard);
        return;
    }
    
    // Copy each character to the buffer and create string pointers
    for(int i = 0; i < count; i++) {
        int start_pos = positions[i];
        int end_pos;
        if(i + 1 < count) {
            end_pos = positions[i + 1];
        } else {
            // For the last character, find where it ends
            end_pos = start_pos;
            if((unsigned char)chinese_chars[start_pos] >= 0xE0) {
                end_pos += 3;  // 3-byte UTF-8 character
            } else if((unsigned char)chinese_chars[start_pos] >= 0xC0) {
                end_pos += 2;  // 2-byte UTF-8 character
            } else {
                end_pos += 1;  // 1-byte ASCII character
            }
        }
        int char_len = end_pos - start_pos;
        
        char * dest = &candidates_buffer[i * 4];
        map[i] = dest;
        
        for(int j = 0; j < char_len && j < 3; j++) {
            dest[j] = chinese_chars[start_pos + j];
        }
        dest[char_len] = '\0';
    }
    map[count] = NULL;  // NULL terminate
    
    lv_btnmatrix_set_map(keyboard->candidate_btnm, map);
    
    // Set control map to make buttons trigger on click instead of release
    lv_btnmatrix_ctrl_t * ctrl_map = lv_mem_alloc((count + 1) * sizeof(lv_btnmatrix_ctrl_t));
    if(ctrl_map) {
        for(int i = 0; i < count; i++) {
            ctrl_map[i] = LV_BTNMATRIX_CTRL_CLICK_TRIG;
        }
        ctrl_map[count] = 0;  // NULL terminator
        lv_btnmatrix_set_ctrl_map(keyboard->candidate_btnm, ctrl_map);
        lv_mem_free(ctrl_map);
    }
    
    /* store count for bounds checking */
    keyboard->candidate_cnt = (uint8_t)count;
    keyboard->candidate_map = map;
    keyboard->candidates_list = (const char **)candidates_buffer;  // Store for cleanup
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

#endif  /*LV_USE_KEYBOARD*/
