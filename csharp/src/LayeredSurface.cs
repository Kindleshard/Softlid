using System;
using System.Drawing;
using System.Runtime.InteropServices;

namespace BlinkBuddy
{
    // Premultiplied ARGB pixels in a DIB section, kept selected into a memory DC, that a per-pixel-alpha (layered)
    // window can show directly: write the rows, then Present a part of it at a place on screen. It grows as needed
    // and is reused, so showing a frame costs no allocation and no conversion.
    class LayeredSurface : IDisposable
    {
        [StructLayout(LayoutKind.Sequential)]
        struct BITMAPINFOHEADER
        {
            public int Size, Width, Height;
            public short Planes, BitCount;
            public int Compression, SizeImage, XPelsPerMeter, YPelsPerMeter, ClrUsed, ClrImportant;
        }
        [StructLayout(LayoutKind.Sequential)] struct POINT { public int X, Y; }
        [StructLayout(LayoutKind.Sequential)] struct SIZE { public int Cx, Cy; }
        [StructLayout(LayoutKind.Sequential, Pack = 1)] struct BLENDFUNCTION { public byte Op, Flags, SourceConstantAlpha, AlphaFormat; }

        [DllImport("gdi32.dll")] static extern IntPtr CreateDIBSection(IntPtr hdc, ref BITMAPINFOHEADER bmi, uint usage, out IntPtr bits, IntPtr section, uint offset);
        [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr hdc);
        [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr hdc, IntPtr obj);
        [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr obj);
        [DllImport("gdi32.dll")] static extern bool DeleteDC(IntPtr hdc);
        [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr hwnd);
        [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr hwnd, IntPtr hdc);
        [DllImport("user32.dll")] static extern bool UpdateLayeredWindow(IntPtr hwnd, IntPtr dst, ref POINT at, ref SIZE size, IntPtr src, ref POINT from, int key, ref BLENDFUNCTION blend, int flags);

        IntPtr dc, bitmap, previous, bits;
        int width, height;

        // Makes room for at least w x h pixels (with some spare, so a growing area doesn't reallocate every frame).
        public void Ensure(int w, int h)
        {
            if (w <= width && h <= height && dc != IntPtr.Zero) return;
            Free();
            width = Math.Max(w, width) + 64;
            height = Math.Max(h, height) + 64;
            var info = new BITMAPINFOHEADER
            {
                Size = Marshal.SizeOf(typeof(BITMAPINFOHEADER)), Width = width, Height = -height,   // top-down rows
                Planes = 1, BitCount = 32
            };
            IntPtr screen = GetDC(IntPtr.Zero);
            dc = CreateCompatibleDC(screen);
            bitmap = CreateDIBSection(screen, ref info, 0, out bits, IntPtr.Zero, 0);
            ReleaseDC(IntPtr.Zero, screen);
            previous = SelectObject(dc, bitmap);
        }

        // Where row y starts (rows are width pixels apart).
        public IntPtr Row(int y) { return bits + y * width * 4; }

        public void Present(IntPtr hwnd, Point at, Size size, byte opacity)
        {
            if (dc == IntPtr.Zero || size.Width <= 0 || size.Height <= 0) return;
            var dst = new POINT { X = at.X, Y = at.Y };
            var sz = new SIZE { Cx = Math.Min(size.Width, width), Cy = Math.Min(size.Height, height) };
            var src = new POINT();
            var blend = new BLENDFUNCTION { SourceConstantAlpha = opacity, AlphaFormat = 1 };   // AC_SRC_OVER, AC_SRC_ALPHA
            UpdateLayeredWindow(hwnd, IntPtr.Zero, ref dst, ref sz, dc, ref src, 0, ref blend, 2);   // ULW_ALPHA
        }

        void Free()
        {
            if (dc == IntPtr.Zero) return;
            SelectObject(dc, previous);
            DeleteObject(bitmap);
            DeleteDC(dc);
            dc = bitmap = bits = IntPtr.Zero;
        }

        public void Dispose() { Free(); }
    }
}
