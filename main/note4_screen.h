#ifndef NOTE4_SCREEN_H
#define NOTE4_SCREEN_H

#include "esp_err.h"
#include "button_input.h"
#include "provisioning_state.h"
#include "quota_snapshot.h"
#include "display_prefs.h"

typedef void (*note4_screen_action_handler_t)(note4_provisioning_event_type_t action, void *context);

esp_err_t note4_screen_init(note4_screen_action_handler_t handler, void *context);
esp_err_t note4_screen_render(const note4_provisioning_snapshot_t *snapshot);
/* Retains the latest complete snapshot; NULL clears data for a changed instance. */
void note4_screen_publish_quotas(note4_quota_snapshot_t *snapshot);
void note4_screen_publish_prefs(note4_display_prefs_t *prefs);
void note4_screen_button(note4_button_id_t button);

#endif
