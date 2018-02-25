using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace DesktopVideo
{
    public partial class Form1 : Form
    {
        public Socket sockClient;
        public Form1()
        {
            InitializeComponent();
        }

        private void Form1_Load(object sender, EventArgs e)
        {
            sockClient = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
            IPAddress ipAddress = new IPAddress(new byte[] { 127, 0, 0, 1 });
            EndPoint point = new IPEndPoint(ipAddress, 1997);
            sockClient.Connect(point);
            byte[] data = new byte[1024];
            int leng=sockClient.Receive(data);
            MessageBox.Show(Encoding.UTF8.GetString(data, 0, leng));
        }

        private void button1_Click(object sender, EventArgs e)
        {
            sockClient.Send(System.Text.Encoding.UTF8.GetBytes(textBox1.Text));
        }
    }
}
