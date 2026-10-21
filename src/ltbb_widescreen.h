#ifndef LTBB_WIDESCREEN_H
#define LTBB_WIDESCREEN_H

#include <stdbool.h>

/* Frame width for the host's prepare_frame: 256, or 256 + 2 * margin. */
int LtbbWidescreenFrameWidth(int widescreen_enabled);

/* Draw the PPU frame: decide this frame's margins, draw (line by line with the
 * court's per-line world origin when the court is widened), then black out
 * the margin columns not drawn. At 4:3 this is the plain frame draw. */
void LtbbWidescreenDraw(void);

/* Parts of LtbbWidescreenDraw. BeginDraw returns true when the frame needs
 * the per-line observer. */
bool LtbbWidescreenBeginDraw(void);
void LtbbWidescreenEndDraw(void);

#endif /* LTBB_WIDESCREEN_H */
