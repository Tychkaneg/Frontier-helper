#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wchar.h>
#include "helper_renderer.h"

static ID2D1Factory *factory;
static IDWriteFactory *write_factory;
static ID2D1DCRenderTarget *target;
static ID2D1SolidColorBrush *brush;
static int current_dpi=96;
struct FormatEntry {HFONT font;int dpi;IDWriteTextFormat *format;};
static FormatEntry formats[8];

static D2D1_COLOR_F color(COLORREF c){return D2D1::ColorF(GetRValue(c)/255.f,GetGValue(c)/255.f,GetBValue(c)/255.f,1.f);}
static IDWriteTextFormat *format_for(HFONT font){
 for(auto &e:formats)if(e.font==font&&e.dpi==current_dpi&&e.format)return e.format;
 LOGFONTW lf={};if(!GetObjectW(font,sizeof(lf),&lf))return nullptr;
 FormatEntry *slot=nullptr;for(auto &e:formats)if(!e.format){slot=&e;break;}
 if(!slot){slot=&formats[0];slot->format->Release();slot->format=nullptr;}
 float size=(float)(int)((lf.lfHeight<0?-lf.lfHeight:lf.lfHeight)*96.f/current_dpi+.5f);
 HRESULT hr=write_factory->CreateTextFormat(lf.lfFaceName,nullptr,(DWRITE_FONT_WEIGHT)lf.lfWeight,
  DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-us",&slot->format);
 if(FAILED(hr))return nullptr;
 slot->font=font;slot->dpi=current_dpi;slot->format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
 return slot->format;
}
extern "C" int helper_canvas_begin(HDC dc,int width,int height,int dpi){
 if(!factory&&FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&factory)))return 0;
 if(!write_factory&&FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),(IUnknown**)&write_factory)))return 0;
 if(!target){
  D2D1_RENDER_TARGET_PROPERTIES props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
   D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE));
  if(FAILED(factory->CreateDCRenderTarget(&props,&target)))return 0;
  if(FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0,0,0,1),&brush))){target->Release();target=nullptr;return 0;}
 }
 RECT area={0,0,width,height};if(FAILED(target->BindDC(dc,&area)))return 0;
 current_dpi=dpi;target->SetDpi((float)dpi,(float)dpi);
 target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
 target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
 target->BeginDraw();return 1;
}
extern "C" int helper_canvas_end(void){
 if(!target)return 0;HRESULT hr=target->EndDraw();
 if(hr==(HRESULT)D2DERR_RECREATE_TARGET){brush->Release();brush=nullptr;target->Release();target=nullptr;}
 return SUCCEEDED(hr);
}
extern "C" void helper_canvas_fill(float x,float y,float width,float height,COLORREF c){
 brush->SetColor(color(c));target->FillRectangle(D2D1::RectF(x,y,x+width,y+height),brush);
}
extern "C" void helper_canvas_line(float x1,float y1,float x2,float y2,COLORREF c){
 helper_canvas_stroke(x1,y1,x2,y2,c,1.f);
}
extern "C" void helper_canvas_stroke(float x1,float y1,float x2,float y2,COLORREF c,float thickness){
 brush->SetColor(color(c));target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
 target->DrawLine(D2D1::Point2F(x1,y1),D2D1::Point2F(x2,y2),brush,thickness);
 target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}
extern "C" void helper_canvas_round(float x,float y,float width,float height,float radius,COLORREF fill,COLORREF stroke,float thickness){
 D2D1_ROUNDED_RECT r=D2D1::RoundedRect(D2D1::RectF(x+.5f,y+.5f,x+width-.5f,y+height-.5f),radius,radius);
 target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
 brush->SetColor(color(fill));target->FillRoundedRectangle(r,brush);
 if(thickness>0){brush->SetColor(color(stroke));target->DrawRoundedRectangle(r,brush,thickness);}
 target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}
extern "C" void helper_canvas_circle(float x,float y,float radius,COLORREF c){
 target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
 brush->SetColor(color(c));target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),radius,radius),brush);
 target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}
extern "C" void helper_canvas_dim(float width,float height,float opacity){
 brush->SetColor(D2D1::ColorF(0,0,0,opacity));target->FillRectangle(D2D1::RectF(0,56,width,height),brush);
}
extern "C" void helper_canvas_text(float x,float y,float width,float height,const wchar_t *s,HFONT font,COLORREF c,int right,int centered){
 IDWriteTextFormat *fmt=format_for(font);if(!fmt)return;
 fmt->SetTextAlignment(right==2?DWRITE_TEXT_ALIGNMENT_CENTER:right?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING);
 fmt->SetParagraphAlignment(centered?DWRITE_PARAGRAPH_ALIGNMENT_CENTER:DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
 IDWriteTextLayout *layout=nullptr;
 if(FAILED(write_factory->CreateTextLayout(s,(UINT32)wcslen(s),fmt,width,height,&layout)))return;
 IDWriteInlineObject *ellipsis=nullptr;DWRITE_TRIMMING trim={DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};
 if(SUCCEEDED(write_factory->CreateEllipsisTrimmingSign(fmt,&ellipsis))){layout->SetTrimming(&trim,ellipsis);ellipsis->Release();}
 brush->SetColor(color(c));target->DrawTextLayout(D2D1::Point2F(x,y),layout,brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);
 layout->Release();
}
extern "C" void helper_canvas_clip(float x,float y,float width,float height){target->PushAxisAlignedClip(D2D1::RectF(x,y,x+width,y+height),D2D1_ANTIALIAS_MODE_ALIASED);}
extern "C" void helper_canvas_unclip(void){target->PopAxisAlignedClip();}
extern "C" void helper_canvas_release(void){
 for(auto &e:formats){if(e.format)e.format->Release();e={};}
 if(brush){brush->Release();brush=nullptr;}if(target){target->Release();target=nullptr;}
 if(write_factory){write_factory->Release();write_factory=nullptr;}if(factory){factory->Release();factory=nullptr;}
}
