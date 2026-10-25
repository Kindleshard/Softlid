using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // The Blink page: blink reminders, look-away breaks and stand-up breaks.
    partial class SettingsForm
    {
        // ---- Blink: blink reminders, look-away breaks and stand-up breaks ----

        void BuildBlinkPage(Stack st)
        {
            Header(st, "E890", "Blink reminder", "Relaxed eyes blink ~15 times a minute; staring at a screen, as few as ~5.", s.BlinkEnabled,
                v => { s.BlinkEnabled = v; app.Changed(false); });
            SliderRow(st, "Remind me every", 3, 120, s.BlinkInterval, defaults.BlinkInterval, v => Lang.F("{0} s", v),
                v => { s.BlinkInterval = v; app.Changed(false); });
            Row styleRow = st.Add(46);
            AddLabel(styleRow, "Blink style", Theme.Body, Theme.Fg, 24, 10, 120, 26);
            var style = new Segmented("Eye icon", "Eyelids") { Bounds = new Rectangle(150, 6, Edge - 150 - 120, 34) };
            style.Selected = s.BlinkStyle;
            tips.SetToolTip(style, "Eyelids: two dark lids close from the top and bottom of the screen, like your own blink");
            style.SelectedChanged += delegate
            {
                s.BlinkStyle = style.Selected;
                app.Changed(false);
                UpdateRows();
                app.Blink(false);   // show what it looks like
            };
            styleRow.Controls.Add(style);
            Button preview = Theme.MakeButton("Preview", new Rectangle(Edge - 110, 6, 110, 34));
            preview.Click += delegate { app.Blink(false); };
            styleRow.Controls.Add(preview);
            Func<bool> more = More(st, "blink");
            Func<bool> icon = And(more, () => s.BlinkStyle == 0);
            SliderRow(st, "Icon size", 32, 256, s.IconSize, defaults.IconSize, v => v + " px",
                v => { s.IconSize = v; app.Changed(false); app.Blink(true); }, icon);
            SliderRow(st, "Icon opacity", 10, 100, s.IconOpacity, defaults.IconOpacity, v => v + "%",
                v => { s.IconOpacity = v; app.Changed(false); app.Blink(true); }, icon);
            Row cornerRow = st.Add(72, icon);
            AddLabel(cornerRow, "Position", Theme.Body, Theme.Fg, 24, 22, 80, 26);
            var corner = new CornerPicker(s.IconCorner) { Bounds = new Rectangle(150, 4, 100, 62) };
            corner.SelectedChanged += delegate { s.IconCorner = corner.Selected; app.Changed(false); app.Blink(true); };
            cornerRow.Controls.Add(corner);
            ToggleRow(st, "Fade the screen with each blink", s.BlinkFade, v =>
            {
                s.BlinkFade = v;
                app.Changed(false);
                UpdateRows();
                if (v) app.Blink(false);   // show what it looks like
            }, icon);
            Func<bool> darkens = And(more, () => s.BlinkStyle == 1 || s.BlinkFade);
            SliderRow(st, "How dark the blink gets", 10, 100, s.BlinkFadeDepth, defaults.BlinkFadeDepth,
                v => s.BlinkStyle == 1 && v >= 100 ? "Fully closed" : v + "%",
                v => { s.BlinkFadeDepth = v; app.Changed(false); }, darkens);
            Note(st, "An on-screen shade; the display's backlight is never touched.", 1, darkens);

            Divider(st);
            Header(st, "E909", "Look-away reminder", "The 20-20-20 rule: every 20 min, look 20 ft (6 m) away for 20 s.", s.BreakEnabled,
                v => { s.BreakEnabled = v; app.Changed(false); });
            SliderRow(st, "Every", 5, 60, s.BreakInterval, defaults.BreakInterval, v => Lang.F("{0} min", v),
                v => { s.BreakInterval = v; app.Changed(false); });
            // The spoken lines set a floor: below their total length the voice is switched off rather than cut short.
            int voiceNeeds = Voice.MinimumSeconds(Voice.LookAwayKeys);
            int breakDefault = voiceNeeds > 0 ? voiceNeeds : defaults.BreakDuration;
            Toggle voiceToggle = null;
            Label voiceHint = null;
            Action voiceFits = delegate
            {
                bool fits = voiceNeeds == 0 || s.BreakDuration >= voiceNeeds;
                voiceToggle.Enabled = fits;
                voiceHint.Text = fits ? Lang.T("reads each exercise aloud") : Lang.F("needs breaks of {0} s or longer", voiceNeeds);
            };
            SliderRow(st, "Look away for", 10, 120, s.BreakDuration, breakDefault, v => Lang.F("{0} s", v),
                v => { s.BreakDuration = v; app.Changed(false); voiceFits(); });
            ToggleRow(st, "Guided eye exercises during the break", s.Exercises, v => { s.Exercises = v; app.Changed(false); UpdateRows(); });
            Row voiceRow = st.Add(34, () => s.Exercises);
            AddLabel(voiceRow, "Voice", Theme.Body, Theme.Fg, 48, 4, 56, 26);
            // Created with text: an empty label in a nested panel gets scaled for the display twice.
            voiceHint = AddLabel(voiceRow, "reads each exercise aloud", Theme.Small, Theme.Dim, 108, 8, 260, 20);
            voiceToggle = AddToggle(voiceRow, Edge - 44, 6, s.ExerciseVoice, v => { s.ExerciseVoice = v; app.Changed(false); });
            voiceFits();
            Row breakRow = st.Add(46);
            breakStatus = AddLabel(breakRow, app.BreakStatus(), Theme.Body, Theme.Dim, 24, 10, Edge - 176 - 32, 26);
            Button now = Theme.MakeButton("Take a break now", new Rectangle(Edge - 176, 6, 176, 34));
            now.Click += delegate { app.StartBreak(false); };
            breakRow.Controls.Add(now);
            Func<bool> breakMore = More(st, "break");
            ToggleRow(st, "Dark background behind the break", s.BreakBackdrop, v => { s.BreakBackdrop = v; app.Changed(false); }, breakMore);
            Note(st, "Off: the eye exercises move over your screen instead of a black background.", 1, breakMore);
            ToggleRow(st, "Chime when a break starts and ends", s.Sounds, v => { s.Sounds = v; app.Changed(false); }, breakMore);
            ToggleRow(st, "Force breaks (no Skip, Snooze or Esc)", s.ForceRest, v => { s.ForceRest = v; app.Changed(false); }, breakMore);
            Note(st, "Being away from the PC for 5+ minutes counts as a break.", 1, breakMore);

            Divider(st);
            Header(st, "E805", "Stand-up reminder", "Every hour or so, get up, move and stretch for a few minutes.", s.LongBreakEnabled,
                v => { s.LongBreakEnabled = v; app.Changed(false); });
            SliderRow(st, "Every", 30, 120, s.LongBreakInterval, defaults.LongBreakInterval, v => Lang.F("{0} min", v),
                v => { s.LongBreakInterval = v; app.Changed(false); });
            SliderRow(st, "Break length", 1, 15, s.LongBreakDuration, defaults.LongBreakDuration, v => Lang.F("{0} min", v),
                v => { s.LongBreakDuration = v; app.Changed(false); });
            Row longRow = st.Add(46);
            longStatus = AddLabel(longRow, app.LongBreakStatus(), Theme.Body, Theme.Dim, 24, 10, Edge - 176 - 32, 26);
            Button stand = Theme.MakeButton("Stand up now", new Rectangle(Edge - 176, 6, 176, 34));
            stand.Click += delegate { app.StartBreak(true); };
            longRow.Controls.Add(stand);
            Func<bool> standMore = More(st, "stand");
            ToggleRow(st, "Guided stretches with an animated coach", s.StretchGuided, v => { s.StretchGuided = v; app.Changed(false); UpdateRows(); }, standMore);
            Row stretchVoiceRow = st.Add(34, () => standMore() && s.StretchGuided);
            AddLabel(stretchVoiceRow, "Voice", Theme.Body, Theme.Fg, 48, 4, 56, 26);
            AddLabel(stretchVoiceRow, "announces each stretch aloud", Theme.Small, Theme.Dim, 108, 8, 260, 20);
            AddToggle(stretchVoiceRow, Edge - 44, 6, s.StretchVoice, v => { s.StretchVoice = v; app.Changed(false); });
            Note(st, "Being away from the PC for the break length counts as one. A look-away break due in the last 2 minutes before it is skipped.", 2, standMore);
            st.Add(16);
        }
    }
}
