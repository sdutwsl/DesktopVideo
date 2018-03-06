namespace DesktopVideo
{
    partial class FormMain
    {
        /// <summary>
        /// 必需的设计器变量。
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// 清理所有正在使用的资源。
        /// </summary>
        /// <param name="disposing">如果应释放托管资源，为 true；否则为 false。</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows 窗体设计器生成的代码

        /// <summary>
        /// 设计器支持所需的方法 - 不要修改
        /// 使用代码编辑器修改此方法的内容。
        /// </summary>
        private void InitializeComponent()
        {
            this.components = new System.ComponentModel.Container();
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(FormMain));
            this.volumeBar = new System.Windows.Forms.TrackBar();
            this.btnHide = new System.Windows.Forms.Button();
            this.tTipBackward = new System.Windows.Forms.ToolTip(this.components);
            this.btnbackward = new System.Windows.Forms.Button();
            this.tTippp = new System.Windows.Forms.ToolTip(this.components);
            this.btnPause = new System.Windows.Forms.Button();
            this.btnpp = new System.Windows.Forms.Button();
            this.tTipForward = new System.Windows.Forms.ToolTip(this.components);
            this.btnforward = new System.Windows.Forms.Button();
            this.tTipSound = new System.Windows.Forms.ToolTip(this.components);
            this.btnMute = new System.Windows.Forms.Button();
            this.btnVolume = new System.Windows.Forms.Button();
            this.tTipHide = new System.Windows.Forms.ToolTip(this.components);
            this.notifyIcon = new System.Windows.Forms.NotifyIcon(this.components);
            this.contextMenu = new System.Windows.Forms.ContextMenuStrip(this.components);
            this.打开ToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.toolStripMenuItem1 = new System.Windows.Forms.ToolStripSeparator();
            this.播放暂停ToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.快进ToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.快退ToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.停止播放EToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.toolStripMenuItem2 = new System.Windows.Forms.ToolStripSeparator();
            this.增大音量ToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.减小音量ToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.静音取消ToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.toolStripMenuItem3 = new System.Windows.Forms.ToolStripSeparator();
            this.还原壁纸RToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.控制面板ToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.toolStripMenuItem4 = new System.Windows.Forms.ToolStripSeparator();
            this.退出程序ToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.openFileDialog = new System.Windows.Forms.OpenFileDialog();
            ((System.ComponentModel.ISupportInitialize)(this.volumeBar)).BeginInit();
            this.contextMenu.SuspendLayout();
            this.SuspendLayout();
            // 
            // volumeBar
            // 
            this.volumeBar.Location = new System.Drawing.Point(459, 88);
            this.volumeBar.Maximum = 100;
            this.volumeBar.Name = "volumeBar";
            this.volumeBar.Size = new System.Drawing.Size(173, 114);
            this.volumeBar.TabIndex = 3;
            this.volumeBar.TickFrequency = 0;
            this.volumeBar.TickStyle = System.Windows.Forms.TickStyle.None;
            this.volumeBar.Scroll += new System.EventHandler(this.volumeBar_Scroll);
            this.volumeBar.MouseEnter += new System.EventHandler(this.trackBar1_MouseEnter);
            // 
            // btnHide
            // 
            this.btnHide.BackColor = System.Drawing.Color.Gray;
            this.btnHide.BackgroundImageLayout = System.Windows.Forms.ImageLayout.Stretch;
            this.btnHide.FlatAppearance.BorderColor = System.Drawing.Color.Black;
            this.btnHide.FlatAppearance.BorderSize = 0;
            this.btnHide.FlatAppearance.MouseDownBackColor = System.Drawing.Color.Black;
            this.btnHide.FlatAppearance.MouseOverBackColor = System.Drawing.Color.Black;
            this.btnHide.FlatStyle = System.Windows.Forms.FlatStyle.Flat;
            this.btnHide.ForeColor = System.Drawing.Color.Cyan;
            this.btnHide.Location = new System.Drawing.Point(594, 9);
            this.btnHide.Margin = new System.Windows.Forms.Padding(0);
            this.btnHide.Name = "btnHide";
            this.btnHide.Size = new System.Drawing.Size(50, 50);
            this.btnHide.TabIndex = 5;
            this.btnHide.Text = "x";
            this.tTipHide.SetToolTip(this.btnHide, "隐藏窗体");
            this.btnHide.UseVisualStyleBackColor = false;
            this.btnHide.Click += new System.EventHandler(this.btnHide_Click);
            this.btnHide.MouseEnter += new System.EventHandler(this.btnHide_MouseEnter);
            // 
            // tTipBackward
            // 
            this.tTipBackward.ShowAlways = true;
            // 
            // btnbackward
            // 
            this.btnbackward.BackgroundImage = global::DesktopVideo.Properties.Resources.back;
            this.btnbackward.BackgroundImageLayout = System.Windows.Forms.ImageLayout.Stretch;
            this.btnbackward.Location = new System.Drawing.Point(44, 58);
            this.btnbackward.Name = "btnbackward";
            this.btnbackward.Size = new System.Drawing.Size(100, 100);
            this.btnbackward.TabIndex = 1;
            this.tTipBackward.SetToolTip(this.btnbackward, "后退15秒");
            this.btnbackward.UseVisualStyleBackColor = true;
            this.btnbackward.Click += new System.EventHandler(this.btnbackward_Click);
            this.btnbackward.MouseEnter += new System.EventHandler(this.btnbackward_MouseEnter);
            // 
            // btnPause
            // 
            this.btnPause.BackgroundImage = global::DesktopVideo.Properties.Resources.pause;
            this.btnPause.BackgroundImageLayout = System.Windows.Forms.ImageLayout.Stretch;
            this.btnPause.Location = new System.Drawing.Point(150, 58);
            this.btnPause.Name = "btnPause";
            this.btnPause.Size = new System.Drawing.Size(100, 100);
            this.btnPause.TabIndex = 6;
            this.tTippp.SetToolTip(this.btnPause, "播放/暂停");
            this.btnPause.UseVisualStyleBackColor = true;
            this.btnPause.Visible = false;
            this.btnPause.Click += new System.EventHandler(this.btnPause_Click);
            // 
            // btnpp
            // 
            this.btnpp.BackgroundImage = global::DesktopVideo.Properties.Resources.play;
            this.btnpp.BackgroundImageLayout = System.Windows.Forms.ImageLayout.Stretch;
            this.btnpp.Location = new System.Drawing.Point(150, 58);
            this.btnpp.Name = "btnpp";
            this.btnpp.Size = new System.Drawing.Size(100, 100);
            this.btnpp.TabIndex = 0;
            this.tTippp.SetToolTip(this.btnpp, "播放/暂停");
            this.btnpp.UseVisualStyleBackColor = true;
            this.btnpp.Click += new System.EventHandler(this.btnpp_Click);
            this.btnpp.MouseEnter += new System.EventHandler(this.btnpp_MouseEnter);
            // 
            // btnforward
            // 
            this.btnforward.BackgroundImage = global::DesktopVideo.Properties.Resources.forward;
            this.btnforward.BackgroundImageLayout = System.Windows.Forms.ImageLayout.Stretch;
            this.btnforward.Location = new System.Drawing.Point(256, 58);
            this.btnforward.Name = "btnforward";
            this.btnforward.Size = new System.Drawing.Size(100, 100);
            this.btnforward.TabIndex = 2;
            this.tTipForward.SetToolTip(this.btnforward, "前进15秒");
            this.btnforward.UseVisualStyleBackColor = true;
            this.btnforward.Click += new System.EventHandler(this.btnforward_Click);
            this.btnforward.MouseEnter += new System.EventHandler(this.btnforward_MouseEnter);
            // 
            // btnMute
            // 
            this.btnMute.BackgroundImage = global::DesktopVideo.Properties.Resources.mute;
            this.btnMute.BackgroundImageLayout = System.Windows.Forms.ImageLayout.Stretch;
            this.btnMute.Location = new System.Drawing.Point(362, 58);
            this.btnMute.Name = "btnMute";
            this.btnMute.Size = new System.Drawing.Size(100, 100);
            this.btnMute.TabIndex = 7;
            this.tTipSound.SetToolTip(this.btnMute, "调整声音");
            this.btnMute.UseVisualStyleBackColor = true;
            this.btnMute.Visible = false;
            this.btnMute.Click += new System.EventHandler(this.btnMute_Click);
            // 
            // btnVolume
            // 
            this.btnVolume.BackgroundImage = global::DesktopVideo.Properties.Resources.volume;
            this.btnVolume.BackgroundImageLayout = System.Windows.Forms.ImageLayout.Stretch;
            this.btnVolume.Location = new System.Drawing.Point(362, 58);
            this.btnVolume.Name = "btnVolume";
            this.btnVolume.Size = new System.Drawing.Size(100, 100);
            this.btnVolume.TabIndex = 4;
            this.tTipSound.SetToolTip(this.btnVolume, "调整声音");
            this.btnVolume.UseVisualStyleBackColor = true;
            this.btnVolume.Click += new System.EventHandler(this.btnVolume_Click);
            this.btnVolume.MouseEnter += new System.EventHandler(this.btnVolume_MouseEnter);
            // 
            // notifyIcon
            // 
            this.notifyIcon.ContextMenuStrip = this.contextMenu;
            this.notifyIcon.Icon = ((System.Drawing.Icon)(resources.GetObject("notifyIcon.Icon")));
            this.notifyIcon.Text = "notifyIcon";
            this.notifyIcon.Visible = true;
            // 
            // contextMenu
            // 
            this.contextMenu.ImageScalingSize = new System.Drawing.Size(40, 40);
            this.contextMenu.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.打开ToolStripMenuItem,
            this.toolStripMenuItem1,
            this.播放暂停ToolStripMenuItem,
            this.快进ToolStripMenuItem,
            this.快退ToolStripMenuItem,
            this.停止播放EToolStripMenuItem,
            this.toolStripMenuItem2,
            this.增大音量ToolStripMenuItem,
            this.减小音量ToolStripMenuItem,
            this.静音取消ToolStripMenuItem,
            this.toolStripMenuItem3,
            this.还原壁纸RToolStripMenuItem,
            this.控制面板ToolStripMenuItem,
            this.toolStripMenuItem4,
            this.退出程序ToolStripMenuItem});
            this.contextMenu.Name = "contextMenu";
            this.contextMenu.Size = new System.Drawing.Size(312, 567);
            // 
            // 打开ToolStripMenuItem
            // 
            this.打开ToolStripMenuItem.Name = "打开ToolStripMenuItem";
            this.打开ToolStripMenuItem.Size = new System.Drawing.Size(311, 44);
            this.打开ToolStripMenuItem.Text = "打开(&O)";
            this.打开ToolStripMenuItem.Click += new System.EventHandler(this.打开ToolStripMenuItem_Click);
            // 
            // toolStripMenuItem1
            // 
            this.toolStripMenuItem1.Name = "toolStripMenuItem1";
            this.toolStripMenuItem1.Size = new System.Drawing.Size(308, 6);
            // 
            // 播放暂停ToolStripMenuItem
            // 
            this.播放暂停ToolStripMenuItem.Name = "播放暂停ToolStripMenuItem";
            this.播放暂停ToolStripMenuItem.Size = new System.Drawing.Size(311, 44);
            this.播放暂停ToolStripMenuItem.Text = "播放/暂停(&P)";
            this.播放暂停ToolStripMenuItem.Click += new System.EventHandler(this.播放暂停ToolStripMenuItem_Click);
            // 
            // 快进ToolStripMenuItem
            // 
            this.快进ToolStripMenuItem.Name = "快进ToolStripMenuItem";
            this.快进ToolStripMenuItem.Size = new System.Drawing.Size(311, 44);
            this.快进ToolStripMenuItem.Text = "快进(&F)";
            this.快进ToolStripMenuItem.Click += new System.EventHandler(this.快进ToolStripMenuItem_Click);
            // 
            // 快退ToolStripMenuItem
            // 
            this.快退ToolStripMenuItem.Name = "快退ToolStripMenuItem";
            this.快退ToolStripMenuItem.Size = new System.Drawing.Size(311, 44);
            this.快退ToolStripMenuItem.Text = "快退(&B)";
            this.快退ToolStripMenuItem.Click += new System.EventHandler(this.快退ToolStripMenuItem_Click);
            // 
            // 停止播放EToolStripMenuItem
            // 
            this.停止播放EToolStripMenuItem.Name = "停止播放EToolStripMenuItem";
            this.停止播放EToolStripMenuItem.Size = new System.Drawing.Size(311, 44);
            this.停止播放EToolStripMenuItem.Text = "停止播放(&E)";
            this.停止播放EToolStripMenuItem.Click += new System.EventHandler(this.停止播放EToolStripMenuItem_Click);
            // 
            // toolStripMenuItem2
            // 
            this.toolStripMenuItem2.Name = "toolStripMenuItem2";
            this.toolStripMenuItem2.Size = new System.Drawing.Size(308, 6);
            // 
            // 增大音量ToolStripMenuItem
            // 
            this.增大音量ToolStripMenuItem.Name = "增大音量ToolStripMenuItem";
            this.增大音量ToolStripMenuItem.Size = new System.Drawing.Size(311, 44);
            this.增大音量ToolStripMenuItem.Text = "增大音量(&U)";
            this.增大音量ToolStripMenuItem.Click += new System.EventHandler(this.增大音量ToolStripMenuItem_Click);
            // 
            // 减小音量ToolStripMenuItem
            // 
            this.减小音量ToolStripMenuItem.Name = "减小音量ToolStripMenuItem";
            this.减小音量ToolStripMenuItem.Size = new System.Drawing.Size(311, 44);
            this.减小音量ToolStripMenuItem.Text = "减小音量(&D)";
            this.减小音量ToolStripMenuItem.Click += new System.EventHandler(this.减小音量ToolStripMenuItem_Click);
            // 
            // 静音取消ToolStripMenuItem
            // 
            this.静音取消ToolStripMenuItem.Name = "静音取消ToolStripMenuItem";
            this.静音取消ToolStripMenuItem.Size = new System.Drawing.Size(311, 44);
            this.静音取消ToolStripMenuItem.Text = "静音/取消(&M)";
            this.静音取消ToolStripMenuItem.Click += new System.EventHandler(this.静音取消ToolStripMenuItem_Click);
            // 
            // toolStripMenuItem3
            // 
            this.toolStripMenuItem3.Name = "toolStripMenuItem3";
            this.toolStripMenuItem3.Size = new System.Drawing.Size(308, 6);
            // 
            // 还原壁纸RToolStripMenuItem
            // 
            this.还原壁纸RToolStripMenuItem.Name = "还原壁纸RToolStripMenuItem";
            this.还原壁纸RToolStripMenuItem.Size = new System.Drawing.Size(311, 44);
            this.还原壁纸RToolStripMenuItem.Text = "还原壁纸(&R)";
            this.还原壁纸RToolStripMenuItem.Click += new System.EventHandler(this.还原壁纸RToolStripMenuItem_Click);
            // 
            // 控制面板ToolStripMenuItem
            // 
            this.控制面板ToolStripMenuItem.Name = "控制面板ToolStripMenuItem";
            this.控制面板ToolStripMenuItem.Size = new System.Drawing.Size(311, 44);
            this.控制面板ToolStripMenuItem.Text = "显示控制面板(&S)";
            this.控制面板ToolStripMenuItem.Click += new System.EventHandler(this.控制面板ToolStripMenuItem_Click);
            // 
            // toolStripMenuItem4
            // 
            this.toolStripMenuItem4.Name = "toolStripMenuItem4";
            this.toolStripMenuItem4.Size = new System.Drawing.Size(308, 6);
            // 
            // 退出程序ToolStripMenuItem
            // 
            this.退出程序ToolStripMenuItem.Name = "退出程序ToolStripMenuItem";
            this.退出程序ToolStripMenuItem.Size = new System.Drawing.Size(311, 44);
            this.退出程序ToolStripMenuItem.Text = "退出程序(&H)";
            this.退出程序ToolStripMenuItem.Click += new System.EventHandler(this.退出程序ToolStripMenuItem_Click);
            // 
            // openFileDialog
            // 
            this.openFileDialog.AddExtension = false;
            this.openFileDialog.Title = "打开文件";
            // 
            // FormMain
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(15F, 30F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.BackColor = System.Drawing.Color.Gray;
            this.ClientSize = new System.Drawing.Size(653, 222);
            this.ControlBox = false;
            this.Controls.Add(this.btnMute);
            this.Controls.Add(this.btnPause);
            this.Controls.Add(this.btnHide);
            this.Controls.Add(this.btnVolume);
            this.Controls.Add(this.volumeBar);
            this.Controls.Add(this.btnforward);
            this.Controls.Add(this.btnbackward);
            this.Controls.Add(this.btnpp);
            this.ForeColor = System.Drawing.Color.Red;
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.None;
            this.Icon = ((System.Drawing.Icon)(resources.GetObject("$this.Icon")));
            this.Margin = new System.Windows.Forms.Padding(2);
            this.Name = "FormMain";
            this.Opacity = 0.5D;
            this.ShowIcon = false;
            this.ShowInTaskbar = false;
            this.SizeGripStyle = System.Windows.Forms.SizeGripStyle.Hide;
            this.Text = "DesktopVideoController";
            this.TopMost = true;
            this.TransparencyKey = System.Drawing.Color.Gray;
            this.Load += new System.EventHandler(this.Form1_Load);
            this.MouseDown += new System.Windows.Forms.MouseEventHandler(this.FormMain_MouseDown);
            this.MouseLeave += new System.EventHandler(this.FormMain_MouseLeave);
            this.MouseMove += new System.Windows.Forms.MouseEventHandler(this.FormMain_MouseMove);
            this.MouseUp += new System.Windows.Forms.MouseEventHandler(this.FormMain_MouseUp);
            ((System.ComponentModel.ISupportInitialize)(this.volumeBar)).EndInit();
            this.contextMenu.ResumeLayout(false);
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.Button btnpp;
        private System.Windows.Forms.Button btnbackward;
        private System.Windows.Forms.Button btnforward;
        private System.Windows.Forms.TrackBar volumeBar;
        private System.Windows.Forms.Button btnVolume;
        private System.Windows.Forms.Button btnHide;
        private System.Windows.Forms.ToolTip tTipBackward;
        private System.Windows.Forms.ToolTip tTipSound;
        private System.Windows.Forms.ToolTip tTipForward;
        private System.Windows.Forms.ToolTip tTippp;
        private System.Windows.Forms.ToolTip tTipHide;
        private System.Windows.Forms.NotifyIcon notifyIcon;
        private System.Windows.Forms.ContextMenuStrip contextMenu;
        private System.Windows.Forms.ToolStripMenuItem 打开ToolStripMenuItem;
        private System.Windows.Forms.ToolStripSeparator toolStripMenuItem1;
        private System.Windows.Forms.ToolStripMenuItem 快进ToolStripMenuItem;
        private System.Windows.Forms.ToolStripMenuItem 播放暂停ToolStripMenuItem;
        private System.Windows.Forms.ToolStripMenuItem 快退ToolStripMenuItem;
        private System.Windows.Forms.ToolStripSeparator toolStripMenuItem2;
        private System.Windows.Forms.ToolStripMenuItem 增大音量ToolStripMenuItem;
        private System.Windows.Forms.ToolStripMenuItem 减小音量ToolStripMenuItem;
        private System.Windows.Forms.ToolStripMenuItem 静音取消ToolStripMenuItem;
        private System.Windows.Forms.ToolStripSeparator toolStripMenuItem3;
        private System.Windows.Forms.ToolStripMenuItem 控制面板ToolStripMenuItem;
        private System.Windows.Forms.ToolStripSeparator toolStripMenuItem4;
        private System.Windows.Forms.ToolStripMenuItem 退出程序ToolStripMenuItem;
        private System.Windows.Forms.OpenFileDialog openFileDialog;
        private System.Windows.Forms.ToolStripMenuItem 还原壁纸RToolStripMenuItem;
        private System.Windows.Forms.Button btnPause;
        private System.Windows.Forms.Button btnMute;
        private System.Windows.Forms.ToolStripMenuItem 停止播放EToolStripMenuItem;
    }
}

