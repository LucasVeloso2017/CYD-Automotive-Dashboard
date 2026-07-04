#ifndef UI_NANOMODE_H
#define UI_NANOMODE_H

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_nanomode_screen_init(void);
extern void ui_nanomode_screen_destroy(void);
extern lv_obj_t *ui_nanomode;
extern lv_obj_t *ui_Image1;
extern lv_obj_t *ui_lambdanano;
extern lv_obj_t *ui_temperaturenano;
extern lv_obj_t *ui_baterynano;
extern lv_obj_t *uic_nanomode;
extern lv_obj_t *uic_lambdanano;
extern lv_obj_t *uic_temperaturenano;
extern lv_obj_t *uic_baterynano;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
