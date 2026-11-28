/**
 * @file lv_keyboard_private.h
 *
 */

#ifndef LV_KEYBOARD_PRIVATE_H
#define LV_KEYBOARD_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "../../core/lv_obj_private.h"
#include "../../lvgl_public.h"

#if LV_USE_KEYBOARD

#include "../buttonmatrix/lv_buttonmatrix_private.h"

/*********************
 *      DEFINES
 *********************/

/** Longest pinyin sequence the Chinese IME keeps as its composition, excluding the terminator. */
#define LV_KEYBOARD_IME_PINYIN_BUF_SIZE 16

/**********************
 *      TYPEDEFS
 **********************/

/** Data of keyboard */
struct _lv_keyboard_t {
    lv_buttonmatrix_t btnm;
    lv_obj_t * ta;              /**< Pointer to the assigned text area */
    lv_keyboard_mode_t mode;    /**< Key map type */
    uint8_t popovers : 1;       /**< Show button titles in popovers on press */

    /*Chinese IME state. All of it stays empty unless LV_KEYBOARD_MODE_IME_CHN is
     *active, so a keyboard that never switches into that mode allocates nothing
     *beyond the struct itself.*/
    lv_obj_t * candidate_list;  /**< Container the candidate button matrix is created in */
    lv_obj_t * candidate_btnm;  /**< Candidate button matrix, created on first use */
    char pinyin_buf[LV_KEYBOARD_IME_PINYIN_BUF_SIZE]; /**< Pinyin composition buffer */
    uint16_t pinyin_start_pos;  /**< Textarea position where the composition starts */
    const char ** candidate_map; /**< Candidate button matrix map, owned by the keyboard */
    uint8_t candidate_cnt;      /**< Candidates visible on the current page */
    int8_t candidate_page;      /**< Index of the first candidate on the current page */
    int16_t total_candidates;   /**< Number of candidates in candidates_list */
    char ** candidates_list;    /**< Owned candidate strings for the current input */
};


/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_KEYBOARD */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_KEYBOARD_PRIVATE_H*/
