// MacCollector - collects this PC's name, MAC address and IP addresses into a JSON
// file for the "Import JSON" button of the Xerox Host DNS Bridge (ESP32) "Add Host" form.
//
// GUI      : MacCollector.exe
// Silent   : MacCollector.exe -OutDir \\server\share\macs -Quiet
// Build    : build.bat  (C# 5 / .NET Framework 4 - included in Windows 10/11)
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyTitle("MacCollector")]
[assembly: System.Reflection.AssemblyDescription("Collect PC name + MAC address for Xerox Host DNS Bridge")]
[assembly: System.Reflection.AssemblyProduct("Xerox Host DNS Bridge")]
[assembly: System.Reflection.AssemblyVersion("1.1.0.0")]
[assembly: System.Reflection.AssemblyFileVersion("1.1.0.0")]

namespace MacCollector
{
    static class Program
    {
        [DllImport("user32.dll")] static extern bool SetProcessDPIAware();

        [STAThread]
        static int Main(string[] args)
        {
            bool quiet = false;
            string outDir = null;
            for (int i = 0; i < args.Length; i++)
            {
                string a = args[i].TrimStart('-', '/').ToLowerInvariant();
                if (a == "quiet" || a == "q") quiet = true;
                else if (a == "outdir" && i + 1 < args.Length) outDir = args[++i];
                else if (a == "make-icon" && i + 1 < args.Length) { Theme.WriteIcon(args[++i]); return 0; }   // used by build.bat
            }

            if (quiet)   // silent mode for login scripts: write <COMPUTERNAME>.json and exit
            {
                try
                {
                    List<AdapterInfo> ad = Collector.GetAdapters();
                    if (ad.Count == 0) return 2;
                    HostRecord r = Collector.Collect(ad[0], ad);
                    string dir = outDir ?? AppDomain.CurrentDomain.BaseDirectory;
                    Directory.CreateDirectory(dir);
                    Collector.Save(Path.Combine(dir, r.Name + ".json"), Collector.ToJson(r));
                    return 0;
                }
                catch { return 1; }
            }

            try { SetProcessDPIAware(); } catch { }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
            return 0;
        }
    }
}
