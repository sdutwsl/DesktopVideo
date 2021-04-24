using System;
using System.Drawing;
using System.Net;
using System.Net.Sockets;
using System.Windows.Forms;

namespace DesktopVideo
{
    public partial class FormMain : Form
    {
        private bool isMouseDown = false;
        Point ptPoint = new Point();
        public Socket sockClient;
        private bool isMute = false;
        private bool isPlay = false;
        private int port = 1997;


        public FormMain()
        {
            InitializeComponent();

            //在这里初始化套接字
            try
            {
                string host = "127.0.0.1";
                IPAddress ip = IPAddress.Parse(host);
                IPEndPoint ipe = new IPEndPoint(ip, port);
                sockClient = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
                sockClient.Connect(ipe);
            }
            catch(Exception e)
            {
                this.SocketError(e.Message);
            }

        }

        private void SocketError(String message)
        {
            sockClient.Close();
            MessageBox.Show("网络错误，信息：" + message);
            System.Environment.Exit(-1);
        }

        private void Form1_Load(object sender, EventArgs e)
        {
            this.notifyIcon.Visible = true;
        }

        private void btnHide_Click(object sender, EventArgs e)
        {
            this.Hide();
        }

        private void FormMain_MouseDown(object sender, MouseEventArgs e)
        {
            if(MouseButtons.Left==e.Button)
            {
                isMouseDown = true;
                ptPoint = e.Location;
            }
        }

        private void FormMain_MouseMove(object sender, MouseEventArgs e)
        {
            if(isMouseDown)
            {
                this.Left = this.Left + ( e.X-ptPoint.X);
                this.Top = this.Top + (e.Y- ptPoint.Y);
               // ptPoint = e.Location;
            }
        }

        private void FormMain_MouseUp(object sender, MouseEventArgs e)
        {
            isMouseDown = false;
        }

        private void SetBlackBackground()
        {
            this.BackColor = Color.Black;
            this.btnHide.BackColor = Color.Black;
        }

        private void btnbackward_MouseEnter(object sender, EventArgs e)
        {
            this.SetBlackBackground();
        }

        private void btnpp_MouseEnter(object sender, EventArgs e)
        {
            this.SetBlackBackground();
        }

        private void btnforward_MouseEnter(object sender, EventArgs e)
        {
            this.SetBlackBackground();
        }

        private void btnVolume_MouseEnter(object sender, EventArgs e)
        {
            this.SetBlackBackground();
        }

        private void trackBar1_MouseEnter(object sender, EventArgs e)
        {
            this.SetBlackBackground();
        }

        private void btnHide_MouseEnter(object sender, EventArgs e)
        {
            this.SetBlackBackground();
        }

        private void FormMain_MouseLeave(object sender, EventArgs e)
        {
            this.BackColor = Color.Gray;
            this.btnHide.BackColor = Color.Gray;
        }

        private void 快进ToolStripMenuItem_Click(object sender, EventArgs e)
        {
            this.btnforward_Click(null,null);
        }

        private void 退出程序ToolStripMenuItem_Click(object sender, EventArgs e)
        {
            try
            {
                sockClient.Send(System.Text.Encoding.UTF8.GetBytes("B"));
            }
            catch (Exception ee)
            {
                this.SocketError(ee.Message);
            }

            this.Close();
        }

        private void 打开ToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if(DialogResult.Cancel==this.openFileDialog.ShowDialog())
            {
                return;
            }
            try
            {
                sockClient.Send(System.Text.Encoding.UTF8.GetBytes("P " + openFileDialog.FileName));
            }
            catch(Exception ee)
            {
                this.SocketError(ee.Message);
            }
            this.volumeBar.Value = 100;
            this.btnpp.Visible = false;
            this.btnPause.Visible = true;
            isPlay = true;
            isMute = false;
        }

        private void Change()
        {
            if (isPlay)
            {
                this.btnPause.Visible = true;
                this.btnpp.Visible = false;
            }
            else
            {
                this.btnPause.Visible = false;
                this.btnpp.Visible = true;
            }
            if (isMute)
            {
                this.btnVolume.Visible = false;
                this.btnMute.Visible = true;
            }
            else
            {
                this.btnVolume.Visible = true;
                this.btnMute.Visible = false;
            }
        }

        private void btnPause_Click(object sender, EventArgs e)
        {
            isPlay = false;
            this.Change();
            this.btnpp.Focus();
            try
            {
                sockClient.Send(System.Text.Encoding.UTF8.GetBytes("A"));
            }
            catch(Exception ee)
            {
                this.SocketError(ee.Message);
            }
        }

        private void btnpp_Click(object sender, EventArgs e)
        {
            isPlay = true;
            this.Change();
            this.btnPause.Focus();
            try
            {
                sockClient.Send(System.Text.Encoding.UTF8.GetBytes("A"));
            }
            catch (Exception ee)
            {
                this.SocketError(ee.Message);
            }
        }
        private void btnMute_Click(object sender, EventArgs e)
        {
            isMute = false;
            this.Change();
            this.btnVolume.Focus();
            try
            {
                sockClient.Send(System.Text.Encoding.UTF8.GetBytes("M"));
            }
            catch (Exception ee)
            {
                this.SocketError(ee.Message);
            }
        }

        private void btnVolume_Click(object sender, EventArgs e)
        {
            isMute = true;
            this.Change();
            this.btnMute.Focus();
            try
            {
                sockClient.Send(System.Text.Encoding.UTF8.GetBytes("M"));
            }
            catch (Exception ee)
            {
                this.SocketError(ee.Message);
            }
        }

        private void volumeBar_Scroll(object sender, EventArgs e)
        {
            try
            {
                sockClient.Send(System.Text.Encoding.UTF8.GetBytes("V "+this.volumeBar.Value));
            }
            catch (Exception ee)
            {
                this.SocketError(ee.Message);
            }
        }

        private void btnbackward_Click(object sender, EventArgs e)
        {
            try
            {
                sockClient.Send(System.Text.Encoding.UTF8.GetBytes("C B"));
            }
            catch (Exception ee)
            {
                this.SocketError(ee.Message);
            }
        }

        private void btnforward_Click(object sender, EventArgs e)
        {
            try
            {
                sockClient.Send(System.Text.Encoding.UTF8.GetBytes("C F"));
            }
            catch (Exception ee)
            {
                this.SocketError(ee.Message);
            }
        }

        private void 播放暂停ToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (isPlay)
            {
                this.btnPause_Click(null, null);
            }
            else
            {
                this.btnpp_Click(null, null);
            }
            this.Change();
        }

        private void 快退ToolStripMenuItem_Click(object sender, EventArgs e)
        {
            this.btnbackward_Click(null, null);
        }

        private void 增大音量ToolStripMenuItem_Click(object sender, EventArgs e)
        {
            int volume = this.volumeBar.Value;
            volume += volume / 10;
            if(volume>100)
            {
                volume = 100;
            }
            this.volumeBar.Value = volume;

            try
            {
                sockClient.Send(System.Text.Encoding.UTF8.GetBytes("V " + volume));
            }
            catch (Exception ee)
            {
                this.SocketError(ee.Message);
            }
        }

        private void 减小音量ToolStripMenuItem_Click(object sender, EventArgs e)
        {

            int volume = this.volumeBar.Value;
            volume -= volume / 10;
            if (volume < 0)
            {
                volume = 0;
            }
            this.volumeBar.Value = volume;

            try
            {
                sockClient.Send(System.Text.Encoding.UTF8.GetBytes("V " + volume));
            }
            catch (Exception ee)
            {
                this.SocketError(ee.Message);
            }
        }

        private void 静音取消ToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (isMute)
            {
                this.btnMute_Click(null, null);
            }
            else
            {
                this.btnVolume_Click(null, null);
            }
            this.Change();
        }

        private void 还原壁纸RToolStripMenuItem_Click(object sender, EventArgs e)
        {
            try
            {
                sockClient.Send(System.Text.Encoding.UTF8.GetBytes("B"));
            }
            catch (Exception ee)
            {
                this.SocketError(ee.Message);
            }
        }

        private void 控制面板ToolStripMenuItem_Click(object sender, EventArgs e)
        {
            this.Visible = true;
            this.Change();
        }

        private void 停止播放EToolStripMenuItem_Click(object sender, EventArgs e)
        {

            try
            {
                sockClient.Send(System.Text.Encoding.UTF8.GetBytes("S"));
            }
            catch (Exception ee)
            {
                this.SocketError(ee.Message);
            }
        }
    }
}
