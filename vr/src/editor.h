/* Flaux VR: what the editor's files share (editor.c, screens.c, copilot.c). */
#ifndef EDITOR_H
#define EDITOR_H
#include "fx.h"

typedef struct {
    float dist, scale;            /* how far away the panel appears, and how big it is */
    float speed, sky, snap_size, build_size;
    int snap_turn, grid, snap, haptics, sound, shadows;
} Cfg;

enum { TAB_HOME, TAB_BUILD, TAB_ASSETS, TAB_SCENE, TAB_VIEWER, TAB_ROOMS, TAB_COPILOT, TAB_PROJECTS, TAB_SETTINGS, TAB_COUNT };

#define PW 1680.0f
#define PH 1040.0f
#define KW 1240.0f
#define KH 500.0f
#define PALETTE_N 20
#define LOG_N 9

typedef struct {
    int tab;
    uint32_t selected;
    int tool;                     /* the shape being placed, or -1 */
    int color;
    Pose head, aim[2], grip[2];   /* in the world */
    int hand_on[2];
    V3 rig_pos; float rig_yaw;    /* where the play space sits in the world */
    Panel main, kb;
    char project[32], data_dir[256], player[20];
    double time; float dt, fps;
    RT viewer; float v_yaw, v_pitch, v_dist; V3 v_target; int v_follow;
    GLuint thumbs;
    char *edit; int edit_max; unsigned edit_id; int edit_submit, kb_shift, kb_sym;
    char toast[128]; double toast_until;
    char prompt[96]; char log[LOG_N][112]; int log_user[LOG_N]; int log_n;
    char server[96], room_code[24];
} Ed;

extern Cfg cfg;
extern Ed ed;
extern const unsigned PALETTE[PALETTE_N];
extern const char *const PALETTE_NAMES[PALETTE_N];

void ed_toast(const char *fmt, ...);
void ed_select(uint32_t id);
void ed_summon_panel(void);
void ed_delete_selected(void);
void ed_duplicate_selected(void);
V3 ed_front_point(float dist);          /* a spot on the plate in front of you */
float ed_front_yaw(void);
void ed_teleport(V3 p);
Cam ed_viewer_cam(void);
void ed_viewer_pick(float u, float v);
void ed_save_settings(void);
int ed_project_save(const char *name);
int ed_project_open(const char *name);
void ed_project_new(void);
int ed_project_delete(const char *name);
int ed_project_list(char names[][32], int max);
int ed_project_export(void);

void screens_draw(void);
void keyboard_draw(void);
void copilot_run(const char *prompt);
void copilot_say(int user, const char *fmt, ...);
int copilot_preview(const char *prompt, Obj *out, int max);
void assets_prerender(void);
void screen_assets(void);
int ed_project_count(const char *name);

#endif
