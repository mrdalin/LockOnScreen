// 独立验证：用与 OsdRender 相同的 GDI+ 逻辑绘制 OSD 到 PNG，检查布局与颜色
#include <windows.h>
#include <gdiplus.h>
#include <stdio.h>
using namespace Gdiplus;
#pragma comment(lib, "gdiplus")

#define OSD_H 48
#define OSD_TEXT_SIZE 18
#define OSD_ICON_SIZE 26
#define OSD_GAP 8
#define OSD_ICON_W 30
#define OSD_CORNER 12

static void Premult(BYTE *p, int w, int h) {
    int n = w * h;
    for (int i = 0; i < n; i++, p += 4) {
        BYTE a = p[3];
        if (a == 0) p[0] = p[1] = p[2] = 0;
        else if (a < 255) { p[0]=(BYTE)((p[0]*a)/255); p[1]=(BYTE)((p[1]*a)/255); p[2]=(BYTE)((p[2]*a)/255); }
    }
}

int main() {
    GdiplusStartupInput in; ULONG_PTR tok;
    in.GdiplusVersion = 1; in.DebugEventCallback = NULL;
    in.SuppressBackgroundThread = FALSE; in.SuppressExternalCodecs = FALSE;
    GdiplusStartup(&tok, &in, NULL);

    {
    /* 模拟测量 */
    INT textW = 0;
    {
        Bitmap tmp(1,1,PixelFormat32bppARGB); Graphics g(&tmp);
        FontFamily ff(L"Microsoft YaHei");
        Font f(&ff,(REAL)OSD_TEXT_SIZE,FontStyleBold,UnitPixel);
        RectF ml(0,0,1000,100), mb;
        g.MeasureString(L"大写开",-1,&f,ml,&mb); textW=(INT)(mb.Width+2);
    }
    int w = OSD_GAP + OSD_ICON_W + OSD_GAP + textW + OSD_GAP;
    int h = OSD_H;
    printf("computed: w=%d h=%d textW=%d\n", w, h, textW);

    /* 绘制 */
    Bitmap bmp(w,h,PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0,0,0,0));

    GraphicsPath path;
    { INT d=OSD_CORNER*2;
      path.AddArc(0,0,d,d,180,90); path.AddArc(w-d,0,d,d,270,90);
      path.AddArc(w-d,h-d,d,d,0,90); path.AddArc(0,h-d,d,d,90,90); path.CloseFigure(); }
    { GraphicsPath sh; sh.AddPath(&path,TRUE);
      Matrix m(1,0,0,1,0,3); sh.Transform(&m);
      PathGradientBrush sb(&sh); sb.SetCenterColor(Color(110,0,0,0));
      Color sr[1]={Color(0,0,0,0)}; INT sn=1; sb.SetSurroundColors(sr,&sn);
      g.FillPath(&sb,&sh); }
    { LinearGradientBrush bg(Point(0,0),Point(0,h),Color(255,82,86,96),Color(255,54,56,64));
      g.FillPath(&bg,&path); }
    { Pen ep(Color(72,255,255,255),1.0f); g.DrawPath(&ep,&path); }

    REAL boxX=OSD_GAP, boxY=(REAL)((h-OSD_ICON_W)/2), boxCX=boxX+OSD_ICON_W/2;
    REAL textX=boxX+OSD_ICON_W+OSD_GAP;
    RectF layout,bounds;
    { GraphicsPath ib; INT bs=OSD_ICON_W, d=7;
      ib.AddArc((INT)boxX,(INT)boxY,d,d,180,90); ib.AddArc((INT)boxX+bs-d,(INT)boxY,d,d,270,90);
      ib.AddArc((INT)boxX+bs-d,(INT)boxY+bs-d,d,d,0,90); ib.AddArc((INT)boxX,(INT)boxY+bs-d,d,d,90,90);
      ib.CloseFigure();
      LinearGradientBrush ibb(Point((INT)boxX,(INT)boxY),Point((INT)boxX,(INT)(boxY+OSD_ICON_W)),
                              Color(255,30,145,245),Color(255,0,120,215));
      g.FillPath(&ibb,&ib);
      Pen ibe(Color(90,255,255,255),1.0f); g.DrawPath(&ibe,&ib);
      FontFamily ffi(L"Segoe UI Symbol");
      Font fi(&ffi,(REAL)OSD_ICON_SIZE,FontStyleBold,UnitPixel);
      SolidBrush fwb(Color(255,255,255,255));
      layout.X=boxX; layout.Y=boxY; layout.Width=(REAL)OSD_ICON_W; layout.Height=(REAL)OSD_ICON_W;
      g.MeasureString(L"A",-1,&fi,layout,&bounds);
      g.DrawString(L"A",-1,&fi,PointF(boxCX-bounds.Width/2-bounds.X,
                     boxY+(REAL)OSD_ICON_W/2-bounds.Height/2-bounds.Y),&fwb);
    }
    { FontFamily fft(L"Microsoft YaHei");
      Font ft(&fft,(REAL)OSD_TEXT_SIZE,FontStyleBold,UnitPixel);
      SolidBrush fy(Color(255,255,210,70));
      layout.X=textX; layout.Y=0; layout.Width=w-textX-OSD_GAP; layout.Height=(REAL)h;
      g.MeasureString(L"大写开",-1,&ft,layout,&bounds);
      g.DrawString(L"大写开",-1,&ft,PointF(textX,(REAL)h/2-bounds.Height/2-bounds.Y),&fy);
    }

    /* 保存 PNG */
    CLSID pngClsid;
    { UINT num=0, sz=0; GetImageEncodersSize(&num,&sz);
      ImageCodecInfo *infos=(ImageCodecInfo*)malloc(sz);
      GetImageEncoders(num,sz,infos);
      for(UINT i=0;i<num;i++) if(wcscmp(infos[i].MimeType,L"image/png")==0) pngClsid=infos[i].Clsid;
      free(infos); }
    bmp.Save(L"osd_render.png",&pngClsid,NULL);

    /* 从原始 Bitmap 直接读像素检查（避免文件重载问题） */
    for(int yy=0; yy<h; yy+=12) {
        printf("y=%3d:", yy);
        for(int xx=0; xx<w; xx+=12) {
            Color c; bmp.GetPixel(xx,yy,&c);
            printf("(x%3d:R%3dG%3dB%3d)", xx, c.GetRed(), c.GetGreen(), c.GetBlue());
        }
        printf("\n");
    }
    /* 关键检查：蓝色方块范围（B>150 且 B>R+60） */
    int fb=-1,lb=-1;
    for(int xx=0;xx<w;xx++){ Color c; bmp.GetPixel(xx,h/2,&c);
        if(c.GetBlue()>150 && c.GetBlue()>c.GetRed()+60){ if(fb<0)fb=xx; lb=xx; } }
    printf("blue box at mid-y: x %d..%d (expect 8..37)\n", fb, lb);
    } /* 块结束：所有 GDI+ 对象析构 */

    GdiplusShutdown(tok);
    return 0;
}