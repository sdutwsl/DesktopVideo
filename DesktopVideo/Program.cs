using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Diagnostics;
namespace DesktopVideo
{
    static class Program
    {
        /// <summary>
        /// 应用程序的主入口点。
        /// </summary>
        [STAThread]
        static void Main()
        {
            //在这里启动服务端ProcessStartInfo procInfo = new ProcessStartInfo();
            Process proc;
            ProcessStartInfo procInfo = new ProcessStartInfo();
            procInfo.FileName = "DesktopVideoSev.exe";
            procInfo.Arguments = "sstart";
            procInfo.WindowStyle = ProcessWindowStyle.Hidden;
            proc = Process.Start(procInfo);

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new FormMain());
        }
    }
}
