// dkr_scene.c -- the scene hooks the game calls under TARGET_PC.
//
// The Dreamcast port's scene gate (dreamcast/scene_gate.c) loads a level's
// textures, models, asset data and songs from disc in one pass around these
// calls. Here everything is in RAM already, so what is left is the part that
// matters to any host: when a level has loaded and taken its first logic step,
// the frame pacer and the 60 fps interpolation (dreamcast/frame_interp.c) start
// afresh, so the time the load took is not played back as game time.

#include <ultra64.h>

extern void pc_retrace_reset(void);
extern void frame_interp_reset(void);

static s32 sAwaitingStep; // loaded, but its first logic step has not run

void pc_scene_loading(s32 level) {
    (void) level;
    sAwaitingStep = FALSE;
}

void pc_scene_ready(void) {
    // The scene's first logic step still belongs to the load: it spawns the
    // objects.
    sAwaitingStep = TRUE;
}

void pc_scene_step_done(void) {
    if (!sAwaitingStep) {
        return;
    }
    sAwaitingStep = FALSE;
    pc_retrace_reset();
    frame_interp_reset();
}

s32 pc_scene_is_ready(void) {
    return TRUE;
}

// What the Dreamcast port traces and caches as the game loads; nothing to do.
void pc_texture_loaded(void *first, s32 id, s32 frames) {
}

void pc_model_loaded(s32 modelID) {
}
