using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;
using Timer = System.Windows.Forms.Timer;

namespace BlinkBuddy
{
    // Keyboard shortcuts: what each does, and registering them with Windows.
    partial class App
    {
        void OnHotkey(int id)
        {
            switch (id)
            {
                case Hotkeys.Filter: S.FilterEnabled = !S.FilterEnabled; ManualScreenChange(); break;
                case Hotkeys.Brighter: S.Brightness = Math.Min(100, S.Brightness + 5); S.FilterEnabled = true; ManualScreenChange(); break;
                case Hotkeys.Dimmer: S.Brightness = Math.Max(20, S.Brightness - 5); S.FilterEnabled = true; ManualScreenChange(); break;
                case Hotkeys.Warmer: S.BlueFilter = Math.Min(100, S.BlueFilter + 5); S.FilterEnabled = true; S.ColorEffect = ScreenFilter.NoEffect; PreviewNight(); ManualScreenChange(); break;
                case Hotkeys.Cooler: S.BlueFilter = Math.Max(S.CoolRange ? -75 : 0, S.BlueFilter - 5); S.FilterEnabled = true; S.ColorEffect = ScreenFilter.NoEffect; PreviewNight(); ManualScreenChange(); break;
                case Hotkeys.Strip: S.StripEnabled = !S.StripEnabled; break;
                case Hotkeys.Awake: SetKeepAwake(!KeepingAwake); return;
                case Hotkeys.NextMode: ChooseMode(S.Mode < 0 ? Modes.Health : (S.Mode + 1) % (Modes.Custom + 1)); return;
                case Hotkeys.BreakNow: StartBreak(false); return;
                case Hotkeys.Mini: if (mini != null) LeaveMiniMode(); else EnterMiniMode(); return;
                case Hotkeys.StripUp: strip.Nudge(0, -1); return;
                case Hotkeys.StripDown: strip.Nudge(0, 1); return;
                case Hotkeys.StripLeft: strip.Nudge(-1, 0); return;
                case Hotkeys.StripRight: strip.Nudge(1, 0); return;
                case Hotkeys.StripBigger: ResizeStrip(1); return;
                case Hotkeys.StripSmaller: ResizeStrip(-1); return;
                default: return;
            }
            Changed(true);
            if (settings != null) settings.SyncFromApp();
        }

        // ---- Keyboard shortcuts ----

        readonly HashSet<int> hotkeyFailed = new HashSet<int>();   // actions whose keys another program already uses
        bool stripKeysOn;

        // (Re)registers every shortcut from the settings. The strip's movement keys only while the strip stays put,
        // so they don't take Ctrl+Alt+arrows from other programs the rest of the time.
        public void RegisterHotkeys()
        {
            hotkeyFailed.Clear();
            stripKeysOn = S.StripEnabled && !S.StripFollow;
            for (int id = 1; id <= Hotkeys.Count; id++)
            {
                Native.UnregisterHotKey(msg.Handle, id);
                if (Hotkeys.IsStripMove(id) && !stripKeysOn) continue;
                int mods;
                Keys key;
                if (!Hotkeys.Parse(Hotkeys.Get(S, id), out mods, out key)) continue;
                if (!Hotkeys.Repeats(id)) mods |= Native.MOD_NOREPEAT;
                if (!Native.RegisterHotKey(msg.Handle, id, mods, (int)key)) hotkeyFailed.Add(id);
            }
            if (filterItem != null)
            {
                filterItem.ShortcutKeyDisplayString = Hotkeys.Pretty(Hotkeys.Get(S, Hotkeys.Filter)).Replace(" ", "");
                stripItem.ShortcutKeyDisplayString = Hotkeys.Pretty(Hotkeys.Get(S, Hotkeys.Strip)).Replace(" ", "");
            }
        }

        public bool HotkeyFailed(int id) { return hotkeyFailed.Contains(id); }

        // All shortcuts off for a moment (while the shortcuts window records new keys); RegisterHotkeys brings them back.
        public void SuspendHotkeys()
        {
            for (int id = 1; id <= Hotkeys.Count; id++) Native.UnregisterHotKey(msg.Handle, id);
        }

        // The strip's size keys: the band's height, or for a vertical band its width; both for a rectangle or circle.
        void ResizeStrip(int dir)
        {
            int step = 20 * dir;
            if (S.StripShape == 1) S.StripWidth = Math.Max(80, Math.Min(2000, S.StripWidth + step));
            else S.StripHeight = Math.Max(40, Math.Min(400, S.StripHeight + step));
            if (S.StripShape == 2) S.StripWidth = Math.Max(80, Math.Min(2000, S.StripWidth + step * 2));
            Changed(true);
        }
    }
}
