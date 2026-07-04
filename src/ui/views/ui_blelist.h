#ifndef UI_BLELIST_H
#define UI_BLELIST_H

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_blelist_screen_init(void);
extern void ui_blelist_screen_destroy(void);
extern void ui_blelist_refresh(void);
extern lv_obj_t *ui_blelist;
extern lv_obj_t *ui_Image4;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
