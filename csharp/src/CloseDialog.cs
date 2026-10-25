using System;
using System.Drawing;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // Asked when the settings window is closed (if Options says to ask): keep BlinkBuddy running in the tray, so the
    // reminders and the screen filter go on, or exit it. "Remember my choice" stores the answer in Options.
    class CloseDialog : Form
    {
        int choice;   // 0 cancelled, 1 tray, 2 exit
        readonly Toggle remember;

        CloseDialog()
        {
            SuspendLayout();
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = MinimizeBox = false;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.CenterParent;
            Text = "BlinkBuddy";
            BackColor = Theme.Bg;
            ForeColor = Theme.Fg;
            Font = Theme.Body;
            KeyPreview = true;
            ClientSize = new Size(420, 196);
            Controls.Add(Theme.MakeLabel("Close BlinkBuddy?", Theme.Heading, Theme.Fg, new Rectangle(20, 16, 380, 28)));
            Label text = Theme.MakeLabel("In the tray it keeps reminding you to blink and look away, and keeps the screen filter on. " +
                "Exiting stops all of that until you start it again.", Theme.Small, Theme.Dim, new Rectangle(20, 48, 380, 54));
            text.TextAlign = ContentAlignment.TopLeft;
            Controls.Add(text);
            var tray = new RoundButton { Text = "Keep running in the tray", Bounds = new Rectangle(20, 108, 220, 36), Tint = Theme.Accent };
            var exit = new RoundButton { Text = "Exit", Bounds = new Rectangle(250, 108, 150, 36) };
            tray.Click += delegate { choice = 1; Close(); };
            exit.Click += delegate { choice = 2; Close(); };
            Controls.Add(tray);
            Controls.Add(exit);
            remember = new Toggle(false) { Location = new Point(20, 157) };
            Controls.Add(remember);
            Label rememberText = Theme.MakeLabel("Remember my choice (change it in Options)", Theme.Small, Theme.Dim, new Rectangle(74, 155, 330, 26));
            rememberText.Click += delegate { remember.Checked = !remember.Checked; };
            Controls.Add(rememberText);
            ResumeLayout(false);
            AcceptButton = null;
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            Native.DarkTitleBar(Handle);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Escape) Close();
        }

        public static int Ask(IWin32Window owner, out bool rememberIt)
        {
            using (var d = new CloseDialog())
            {
                d.ShowDialog(owner);
                rememberIt = d.remember.Checked && d.choice != 0;
                return d.choice;
            }
        }
    }
}
