/*
 * export_dialog.h — GUI dialog for exporting audio to WAV files.
 */

#ifndef SQ_EXPORT_DIALOG_H
#define SQ_EXPORT_DIALOG_H

#include "engine/engine.h"

struct nk_context;

/* Draw the export dialog. Returns 1 if dialog wants to close. */
int export_dialog_draw(struct nk_context *ctx, sq_engine_t *engine);

/* Show/hide the export dialog */
void export_dialog_show(void);
void export_dialog_hide(void);
int  export_dialog_visible(void);

#endif /* SQ_EXPORT_DIALOG_H */
