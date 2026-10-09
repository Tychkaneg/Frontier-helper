#ifndef HELPER_RENDERER_H
#define HELPER_RENDERER_H
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
int helper_canvas_begin(HDC dc,int width,int height,int dpi);
int helper_canvas_end(void);
void helper_canvas_fill(float x,float y,float width,float height,COLORREF color);
void helper_canvas_line(float x1,float y1,float x2,float y2,COLORREF color);
void helper_canvas_stroke(float x1,float y1,float x2,float y2,COLORREF color,float thickness);
void helper_canvas_round(float x,float y,float width,float height,float radius,COLORREF fill,COLORREF stroke,float thickness);
void helper_canvas_circle(float x,float y,float radius,COLORREF color);
void helper_canvas_dim(float width,float height,float opacity);
void helper_canvas_text(float x,float y,float width,float height,const wchar_t *s,HFONT font,COLORREF color,int right,int centered);
void helper_canvas_clip(float x,float y,float width,float height);
void helper_canvas_unclip(void);
void helper_canvas_release(void);
#ifdef __cplusplus
}
#endif
#endif
