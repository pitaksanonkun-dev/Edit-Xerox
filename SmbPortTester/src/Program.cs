// SmbPortTester - opens extra TCP ports (e.g. 4451) that forward to this PC's own SMB server (445),
// so a Xerox can be tested with "IP : 4451" before the ESP32 relay is installed, and puts
// everything back (only SMB 445 left) with one click.
//
// Build : build.bat  (C# 5 / .NET Framework 4.5+ - included in Windows 10/11)
// Runs as administrator (app.manifest asks for UAC elevation).
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyTitle("SmbPortTester")]
[assembly: System.Reflection.AssemblyDescription("Open / close extra SMB ports for Xerox scan tests")]
[assembly: System.Reflection.AssemblyProduct("Xerox Host DNS Bridge")]
[assembly: System.Reflection.AssemblyVersion("1.0.0.0")]
[assembly: System.Reflection.AssemblyFileVersion("1.0.0.0")]

namespace SmbPortTester
{
    static class Program
    {
        [DllImport("user32.dll")] static extern bool SetProcessDPIAware();

        public static readonly bool IsAdmin = new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);

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
                var t = new Timer { Interval = 1500 };
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
