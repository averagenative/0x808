/*
 * export_dialog.h — GUI dialog for exporting audio to WAV files.
 */

#ifndef SQ_EXPORT_DIALOG_H
#define SQ_EXPORT_DIALOG_H

#include "engine/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the export dialog (floating window). */
void export_dialog_draw(sq_engine_t *engine);

/* Show/hide the export dialog */
void export_dialog_show(void);
void export_dialog_hide(void);
int  export_dialog_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* SQ_EXPORT_DIALOG_H */
