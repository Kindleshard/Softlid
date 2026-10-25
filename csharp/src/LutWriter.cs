using System;
using System.Globalization;
using System.IO;
using System.Text;

namespace BlinkBuddy
{
    // BlinkBuddy.exe --write-luts <folder>
    // Writes colour-preserving 3D LUTs (.cube, 65 points) for tools that load a LUT into the Windows compositor,
    // such as dwm_lut. Each colour's overall brightness goes through the same remapping as the Smart filters (bright
    // toward mid-grey, dark a little deeper, never brighter), and red, green and blue are scaled by one factor, so
    // hue and saturation are kept and nothing clips. The warm versions then apply the blue light filter on top.
    static class LutWriter
    {
        const int Size = 65;
        const int WarmPercent = 35;   // blue light filter strength for the warm versions (about 4,900 K)

        public static void Run(string folder)
        {
            Directory.CreateDirectory(folder);
            var warm = new Settings { BlueFilter = WarmPercent };
            float wr, wg, wb;
            ScreenFilter.Scale(warm, 1f, out wr, out wg, out wb);
            for (int level = 0; level < Comfort.Levels.Length; level++)
            {
                Write(Path.Combine(folder, "BlinkBuddy " + Comfort.Levels[level] + ".cube"), level, 1, 1, 1,
                    "BlinkBuddy " + Comfort.Levels[level]);
                Write(Path.Combine(folder, "BlinkBuddy " + Comfort.Levels[level] + " warm.cube"), level, wr, wg, wb,
                    string.Format("BlinkBuddy {0}, warm ({1} K)", Comfort.Levels[level], ScreenFilter.KelvinFor(WarmPercent)));
            }
        }

        static void Write(string path, int level, double wr, double wg, double wb, string title, int size = Size)
        {
            var sb = new StringBuilder(size * size * size * 26);
            sb.AppendLine("TITLE \"" + title + "\"");
            sb.AppendLine("# Colour-preserving tone map from BlinkBuddy. Input and output are display (sRGB-encoded) values.");
            sb.AppendLine("LUT_3D_SIZE " + size);
            sb.AppendLine("DOMAIN_MIN 0.0 0.0 0.0");
            sb.AppendLine("DOMAIN_MAX 1.0 1.0 1.0");
            double r, g, b;
            for (int bi = 0; bi < size; bi++)            // blue changes slowest, red fastest (the .cube convention)
                for (int gi = 0; gi < size; gi++)
                    for (int ri = 0; ri < size; ri++)
                    {
                        Map(ri / (size - 1.0), gi / (size - 1.0), bi / (size - 1.0), level, out r, out g, out b);
                        sb.Append((r * wr).ToString("0.000000", CultureInfo.InvariantCulture)).Append(' ')
                          .Append((g * wg).ToString("0.000000", CultureInfo.InvariantCulture)).Append(' ')
                          .Append((b * wb).ToString("0.000000", CultureInfo.InvariantCulture)).Append('\n');
                    }
            File.WriteAllText(path, sb.ToString());
        }

        // The LUT BlinkBuddy hands DesktopLUT for a strength: the smart filters' curve applied to whole colours (level -1
        // changes nothing, for comparing). 33 points is the usual size for desktop LUTs and writes in a blink, so a
        // strength change only waits on DesktopLUT.
        public static void WriteLevel(string path, int level)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            Write(path, level, 1, 1, 1, "BlinkBuddy " + (level < 0 ? "unchanged" : Comfort.Levels[level]), 33);
        }

        // One colour through the tone map, all values 0..1 as the display stores them.
        public static void Map(double r, double g, double b, int level, out double or, out double og, out double ob)
        {
            double y = 0.2126 * Math.Pow(r, 2.2) + 0.7152 * Math.Pow(g, 2.2) + 0.0722 * Math.Pow(b, 2.2);
            if (level < 0)
            {
                or = r;
                og = g;
                ob = b;
                return;
            }
            if (y <= 1e-7)
            {
                or = og = ob = 0;
                return;
            }
            double l = Math.Log(y, 2);
            // The same factor on all three channels (in linear light), at most 1: hue kept, nothing clips.
            double k = Math.Pow(Math.Pow(2, SmartFilter.Remap(level, l) - l), 1 / 2.2);
            or = r * k;
            og = g * k;
            ob = b * k;
        }
    }
}
