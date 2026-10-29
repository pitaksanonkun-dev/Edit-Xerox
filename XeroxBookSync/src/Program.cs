// XeroxBookSync - adds the scan destinations of the Xerox Host DNS Bridge (ESP32) to the address
// book of a Xerox AltaLink: fill in the contact, run Destination Test, Save.
//
// Build : build.bat  (C# 5 / .NET Framework 4.5+ - included in Windows 10/11)
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyTitle("XeroxBookSync")]
[assembly: System.Reflection.AssemblyDescription("Add Xerox Host DNS Bridge scan destinations to a Xerox address book")]
[assembly: System.Reflection.AssemblyProduct("Xerox Host DNS Bridge")]
[assembly: System.Reflection.AssemblyVersion("1.0.1.0")]
[assembly: System.Reflection.AssemblyFileVersion("1.0.1.0")]

namespace XeroxBookSync
{
    static class Program
    {
        [DllImport("user32.dll")] static extern bool SetProcessDPIAware();

        [STAThread]
        static int Main(string[] args)
        {
            string shot = null;
            for (int i = 0; i < args.Length; i++)
            {
                string a = args[i].TrimStart('-', '/').ToLowerInvariant();
                if (a == "make-icon" && i + 1 < args.Length) { Theme.WriteIcon(args[++i]); return 0; }   // used by build.bat
                if (a == "shot" && i + 1 < args.Length) shot = args[++i];                                 // screenshot for docs
            }

            try { SetProcessDPIAware(); } catch { }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            var form = new MainForm();
            if (shot != null)
            {
                var t = new Timer { Interval = 2500 };
                t.Tick += (s, e) =>
                {
                    t.Stop();
                    using (var bmp = new Bitmap(form.Width, form.Height))
                    {
                        using (Graphics g = Graphics.FromImage(bmp)) g.CopyFromScreen(form.Location, Point.Empty, form.Size);
                        bmp.Save(shot, ImageFormat.Png);
                    }
                    form.Close();
                };
                form.Shown += (s, e) => { form.TopMost = true; t.Start(); };
            }
            Application.Run(form);
            return 0;
        }
    }
}
