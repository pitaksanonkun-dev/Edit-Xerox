// ESP32Finder - finds the current IP of the Xerox Host DNS Bridge (ESP32-ETH01) on the LAN.
//
// Build : build.bat  (C# 5 / .NET Framework 4.5+ - included in Windows 10/11)
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyTitle("ESP32Finder")]
[assembly: System.Reflection.AssemblyDescription("Find the IP of the Xerox Host DNS Bridge board")]
[assembly: System.Reflection.AssemblyProduct("Xerox Host DNS Bridge")]
[assembly: System.Reflection.AssemblyVersion("1.0.0.0")]
[assembly: System.Reflection.AssemblyFileVersion("1.0.0.0")]

namespace ESP32Finder
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
                var t = new Timer { Interval = 12000 };   // after the start-up scan
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
