using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO.Ports;
using System.Linq;
using System.Management;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace MwdMotorDetector
{
    internal sealed class MotorStatus
    {
        public string Port { get; set; }
        public int BaudRate { get; set; }
        public int MotorId { get; set; }
        public int TemperatureC { get; set; }
        public int ControlValue { get; set; }
        public int SpeedDps { get; set; }
        public int OutputAngleDeg { get; set; }
    }

    internal sealed class SerialPortItem
    {
        public string PortName { get; set; }
        public string FriendlyName { get; set; }
        public string PnpDeviceId { get; set; }

        public bool IsKnownCanAdapter
        {
            get
            {
                string id = PnpDeviceId ?? string.Empty;
                return id.IndexOf("VID_16D0&PID_117E",
                    StringComparison.OrdinalIgnoreCase) >= 0;
            }
        }

        public override string ToString()
        {
            string name = string.IsNullOrWhiteSpace(FriendlyName)
                ? PortName
                : FriendlyName;
            return IsKnownCanAdapter
                ? name + " - CANable2 USB-CAN"
                : name;
        }
    }

    internal static class MwdDetector
    {
        private const byte FrameHeader = 0x3E;
        private const byte ReadStatus2 = 0x9C;
        private const int FrameLength = 13;
        private const byte DataLength = 8;

        public static List<MotorStatus> Scan(
            IList<string> ports,
            IList<int> baudRates,
            IList<int> motorIds,
            int timeoutMs,
            CancellationToken cancellationToken,
            Action<string> log,
            Action<int, int, string> progress)
        {
            var results = new List<MotorStatus>();
            int completed = 0;
            int total = ports.Count * baudRates.Count * motorIds.Count;

            foreach (string portName in ports)
            {
                cancellationToken.ThrowIfCancellationRequested();
                foreach (int baudRate in baudRates)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    using (var serial = new SerialPort(
                        portName, baudRate, Parity.None, 8, StopBits.One))
                    {
                        serial.Handshake = Handshake.None;
                        serial.ReadTimeout = timeoutMs;
                        serial.WriteTimeout = 500;
                        serial.DtrEnable = false;
                        serial.RtsEnable = false;

                        try
                        {
                            serial.Open();
                            serial.DiscardInBuffer();
                            serial.DiscardOutBuffer();
                            log(string.Format("{0}: opened at {1} bps", portName, baudRate));

                            foreach (int motorId in motorIds)
                            {
                                cancellationToken.ThrowIfCancellationRequested();
                                progress(completed, total,
                                    string.Format("{0} - {1} bps - ID {2}",
                                        portName, baudRate, motorId));

                                MotorStatus status = ReadStatus(
                                    serial, motorId, timeoutMs, cancellationToken);
                                completed++;
                                if (status == null)
                                {
                                    continue;
                                }

                                status.Port = portName;
                                status.BaudRate = baudRate;
                                results.Add(status);
                                log(string.Format(
                                    "FOUND: {0}, {1} bps, ID {2}, temperature {3} C, speed {4} dps",
                                    portName, baudRate, motorId,
                                    status.TemperatureC, status.SpeedDps));
                            }
                        }
                        catch (OperationCanceledException)
                        {
                            throw;
                        }
                        catch (Exception exception)
                        {
                            completed += motorIds.Count;
                            if (completed > total)
                            {
                                completed = total;
                            }
                            log(string.Format("{0} at {1} bps: {2}",
                                portName, baudRate, exception.Message));
                        }
                        finally
                        {
                            if (serial.IsOpen)
                            {
                                serial.Close();
                            }
                        }
                    }
                }
            }

            progress(total, total, string.Empty);
            return results;
        }

        private static MotorStatus ReadStatus(
            SerialPort serial,
            int motorId,
            int timeoutMs,
            CancellationToken cancellationToken)
        {
            serial.DiscardInBuffer();
            byte[] query = BuildStatusQuery(motorId);
            serial.Write(query, 0, query.Length);

            var received = new List<byte>();
            var timer = Stopwatch.StartNew();
            while (timer.ElapsedMilliseconds < timeoutMs)
            {
                cancellationToken.ThrowIfCancellationRequested();
                int available = serial.BytesToRead;
                if (available > 0)
                {
                    var chunk = new byte[available];
                    int count = serial.Read(chunk, 0, chunk.Length);
                    for (int index = 0; index < count; index++)
                    {
                        received.Add(chunk[index]);
                    }

                    MotorStatus status = FindStatusResponse(received, motorId);
                    if (status != null)
                    {
                        return status;
                    }
                }
                Thread.Sleep(2);
            }
            return null;
        }

        private static byte[] BuildStatusQuery(int motorId)
        {
            var query = new byte[FrameLength];
            query[0] = FrameHeader;
            query[1] = (byte)motorId;
            query[2] = DataLength;
            query[3] = ReadStatus2;
            ushort crc = Crc16(query, 0, FrameLength - 2);
            query[11] = (byte)(crc & 0xFF);
            query[12] = (byte)(crc >> 8);
            return query;
        }

        private static MotorStatus FindStatusResponse(List<byte> bytes, int expectedMotorId)
        {
            for (int offset = 0; offset <= bytes.Count - FrameLength; offset++)
            {
                if (bytes[offset] != FrameHeader)
                {
                    continue;
                }

                if (bytes[offset + 1] != expectedMotorId ||
                    bytes[offset + 2] != DataLength ||
                    bytes[offset + 3] != ReadStatus2)
                {
                    continue;
                }

                ushort receivedCrc = (ushort)(bytes[offset + 11] |
                    (bytes[offset + 12] << 8));
                if (Crc16(bytes, offset, FrameLength - 2) != receivedCrc)
                {
                    continue;
                }

                return new MotorStatus
                {
                    MotorId = expectedMotorId,
                    TemperatureC = ToSigned8(bytes[offset + 4]),
                    ControlValue = ToSigned16(bytes[offset + 5], bytes[offset + 6]),
                    SpeedDps = ToSigned16(bytes[offset + 7], bytes[offset + 8]),
                    OutputAngleDeg = ToSigned16(bytes[offset + 9], bytes[offset + 10])
                };
            }
            return null;
        }

        private static ushort Crc16(IList<byte> bytes, int offset, int count)
        {
            ushort crc = 0xFFFF;
            for (int index = 0; index < count; index++)
            {
                crc ^= bytes[offset + index];
                for (int bit = 0; bit < 8; bit++)
                {
                    crc = (ushort)(((crc & 1) != 0)
                        ? ((crc >> 1) ^ 0xA001)
                        : (crc >> 1));
                }
            }
            return crc;
        }

        private static int ToSigned8(byte value)
        {
            return value >= 0x80 ? value - 0x100 : value;
        }

        private static int ToSigned16(byte low, byte high)
        {
            int value = low | (high << 8);
            return value >= 0x8000 ? value - 0x10000 : value;
        }

        internal static void SelfTest()
        {
            byte[] query = BuildStatusQuery(1);
            string queryHex = BitConverter.ToString(query);
            if (queryHex != "3E-01-08-9C-00-00-00-00-00-00-00-F2-30")
            {
                throw new InvalidOperationException("Query self-test failed: " + queryHex);
            }

            var response = new List<byte>
            {
                0x3E, 0x01, 0x08, 0x9C, 0x19,
                0x34, 0x12, 0x9C, 0xFF, 0x78, 0x56, 0x7A, 0xD8
            };
            MotorStatus parsed = FindStatusResponse(response, 1);
            if (parsed == null || parsed.SpeedDps != -100 ||
                parsed.OutputAngleDeg != 0x5678 ||
                parsed.ControlValue != 0x1234)
            {
                throw new InvalidOperationException("Response parser self-test failed.");
            }
        }
    }

    internal sealed class MainForm : Form
    {
        private readonly CheckedListBox portList = new CheckedListBox();
        private readonly ComboBox baudBox = new ComboBox();
        private readonly ComboBox idRangeBox = new ComboBox();
        private readonly NumericUpDown timeoutBox = new NumericUpDown();
        private readonly Button refreshButton = new Button();
        private readonly Button selectAllButton = new Button();
        private readonly Button scanButton = new Button();
        private readonly Button cancelButton = new Button();
        private readonly DataGridView resultGrid = new DataGridView();
        private readonly TextBox logBox = new TextBox();
        private readonly Label statusLabel = new Label();
        private readonly ProgressBar progressBar = new ProgressBar();
        private CancellationTokenSource cancellation;

        public MainForm()
        {
            Text = "MWD \u7535\u673a\u4e32\u53e3\u68c0\u6d4b";
            Icon = SystemIcons.Application;
            StartPosition = FormStartPosition.CenterScreen;
            MinimumSize = new Size(820, 560);
            ClientSize = new Size(940, 640);
            Font = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular, GraphicsUnit.Point);
            AutoScaleMode = AutoScaleMode.Dpi;

            BuildInterface();
            RefreshPorts();
        }

        private void BuildInterface()
        {
            var root = new TableLayoutPanel();
            root.Dock = DockStyle.Fill;
            root.Padding = new Padding(12);
            root.RowCount = 4;
            root.ColumnCount = 1;
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 156F));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 122F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 30F));
            Controls.Add(root);

            var settingsGroup = new GroupBox();
            settingsGroup.Text = "\u626b\u63cf\u8bbe\u7f6e";
            settingsGroup.Dock = DockStyle.Fill;
            root.Controls.Add(settingsGroup, 0, 0);

            var settingsLayout = new TableLayoutPanel();
            settingsLayout.Dock = DockStyle.Fill;
            settingsLayout.Padding = new Padding(8);
            settingsLayout.ColumnCount = 4;
            settingsLayout.RowCount = 4;
            settingsLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 340F));
            settingsLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 90F));
            settingsLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 180F));
            settingsLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            settingsGroup.Controls.Add(settingsLayout);

            var portLabel = new Label();
            portLabel.Text = "\u4e32\u53e3";
            portLabel.Dock = DockStyle.Top;
            portLabel.AutoSize = true;
            settingsLayout.Controls.Add(portLabel, 0, 0);

            portList.CheckOnClick = true;
            portList.Dock = DockStyle.Fill;
            settingsLayout.SetRowSpan(portList, 3);
            settingsLayout.Controls.Add(portList, 0, 1);

            refreshButton.Text = "\u5237\u65b0";
            refreshButton.AutoSize = true;
            refreshButton.Click += delegate { RefreshPorts(); };
            settingsLayout.Controls.Add(refreshButton, 1, 1);

            selectAllButton.Text = "\u5168\u9009";
            selectAllButton.AutoSize = true;
            selectAllButton.Click += delegate
            {
                for (int index = 0; index < portList.Items.Count; index++)
                {
                    portList.SetItemChecked(index, true);
                }
            };
            settingsLayout.Controls.Add(selectAllButton, 1, 2);

            AddField(settingsLayout, "\u6ce2\u7279\u7387", baudBox, 2, 0);
            baudBox.DropDownStyle = ComboBoxStyle.DropDownList;
            baudBox.Items.Add("\u81ea\u52a8\u5faa\u73af (\u5168\u90e8)");
            baudBox.Items.AddRange(new object[]
            {
                "115200", "500000", "1000000", "1500000", "2500000"
            });
            baudBox.SelectedIndex = 0;

            AddField(settingsLayout, "\u7535\u673a ID", idRangeBox, 2, 1);
            idRangeBox.DropDownStyle = ComboBoxStyle.DropDownList;
            idRangeBox.Items.AddRange(new object[]
            {
                "1-2 (\u5feb\u901f)",
                "1-32 (\u5b8c\u6574)"
            });
            idRangeBox.SelectedIndex = 0;

            AddField(settingsLayout, "\u7b49\u5f85 (ms)", timeoutBox, 2, 2);
            timeoutBox.Minimum = 10;
            timeoutBox.Maximum = 500;
            timeoutBox.Value = 40;
            timeoutBox.Increment = 10;

            var actionPanel = new FlowLayoutPanel();
            actionPanel.Dock = DockStyle.Fill;
            actionPanel.FlowDirection = FlowDirection.LeftToRight;
            actionPanel.WrapContents = false;
            actionPanel.Padding = new Padding(4, 22, 0, 0);
            settingsLayout.SetRowSpan(actionPanel, 3);
            settingsLayout.Controls.Add(actionPanel, 3, 0);

            scanButton.Text = "\u5f00\u59cb\u68c0\u6d4b";
            scanButton.AutoSize = true;
            scanButton.Padding = new Padding(10, 4, 10, 4);
            scanButton.Click += ScanClicked;
            actionPanel.Controls.Add(scanButton);

            cancelButton.Text = "\u53d6\u6d88";
            cancelButton.AutoSize = true;
            cancelButton.Padding = new Padding(10, 4, 10, 4);
            cancelButton.Enabled = false;
            cancelButton.Click += delegate
            {
                if (cancellation != null)
                {
                    cancellation.Cancel();
                }
            };
            actionPanel.Controls.Add(cancelButton);

            resultGrid.Dock = DockStyle.Fill;
            resultGrid.ReadOnly = true;
            resultGrid.AllowUserToAddRows = false;
            resultGrid.AllowUserToDeleteRows = false;
            resultGrid.AllowUserToResizeRows = false;
            resultGrid.RowHeadersVisible = false;
            resultGrid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
            resultGrid.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill;
            resultGrid.BackgroundColor = SystemColors.Window;
            resultGrid.BorderStyle = BorderStyle.Fixed3D;
            resultGrid.Columns.Add("Port", "\u4e32\u53e3");
            resultGrid.Columns.Add("BaudRate", "\u6ce2\u7279\u7387");
            resultGrid.Columns.Add("MotorId", "\u7535\u673a ID");
            resultGrid.Columns.Add("Temperature", "\u6e29\u5ea6 (C)");
            resultGrid.Columns.Add("Speed", "\u8f6c\u901f (dps)");
            resultGrid.Columns.Add("OutputAngle", "\u8f93\u51fa\u89d2\u5ea6 (deg)");
            resultGrid.Columns.Add("Control", "\u63a7\u5236\u91cf");
            root.Controls.Add(resultGrid, 0, 1);

            logBox.Dock = DockStyle.Fill;
            logBox.Multiline = true;
            logBox.ReadOnly = true;
            logBox.ScrollBars = ScrollBars.Vertical;
            logBox.BackColor = SystemColors.Window;
            logBox.Font = new Font("Consolas", 9F, FontStyle.Regular, GraphicsUnit.Point);
            root.Controls.Add(logBox, 0, 2);

            var statusPanel = new TableLayoutPanel();
            statusPanel.Dock = DockStyle.Fill;
            statusPanel.ColumnCount = 2;
            statusPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            statusPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 220F));
            statusLabel.Text = "\u5c31\u7eea - \u53ea\u8bfb\u72b6\u6001\u67e5\u8be2 0x9C";
            statusLabel.Dock = DockStyle.Fill;
            statusLabel.TextAlign = ContentAlignment.MiddleLeft;
            progressBar.Dock = DockStyle.Fill;
            progressBar.Style = ProgressBarStyle.Continuous;
            statusPanel.Controls.Add(statusLabel, 0, 0);
            statusPanel.Controls.Add(progressBar, 1, 0);
            root.Controls.Add(statusPanel, 0, 3);

            FormClosing += delegate
            {
                if (cancellation != null)
                {
                    cancellation.Cancel();
                }
            };
        }

        private static void AddField(
            TableLayoutPanel layout, string labelText, Control control, int column, int row)
        {
            var panel = new FlowLayoutPanel();
            panel.AutoSize = true;
            panel.WrapContents = false;
            panel.FlowDirection = FlowDirection.LeftToRight;

            var label = new Label();
            label.Text = labelText;
            label.Width = 76;
            label.TextAlign = ContentAlignment.MiddleLeft;
            label.Margin = new Padding(0, 5, 4, 0);
            control.Width = 92;
            panel.Controls.Add(label);
            panel.Controls.Add(control);
            layout.Controls.Add(panel, column, row);
        }

        private void RefreshPorts()
        {
            var previouslyChecked = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (object item in portList.CheckedItems)
            {
                var port = item as SerialPortItem;
                previouslyChecked.Add(port == null ? item.ToString() : port.PortName);
            }

            List<SerialPortItem> ports = DiscoverPorts();
            portList.Items.Clear();
            foreach (SerialPortItem port in ports)
            {
                bool isChecked = previouslyChecked.Count == 0 ||
                    previouslyChecked.Contains(port.PortName);
                portList.Items.Add(port, isChecked);
            }
            statusLabel.Text = ports.Count == 0
                ? "\u672a\u53d1\u73b0\u4e32\u53e3"
                : string.Format("\u5df2\u53d1\u73b0 {0} \u4e2a\u4e32\u53e3", ports.Count);
        }

        private static List<SerialPortItem> DiscoverPorts()
        {
            var byPort = new Dictionary<string, SerialPortItem>(
                StringComparer.OrdinalIgnoreCase);
            foreach (string portName in SerialPort.GetPortNames())
            {
                byPort[portName] = new SerialPortItem
                {
                    PortName = portName,
                    FriendlyName = portName,
                    PnpDeviceId = string.Empty
                };
            }

            try
            {
                using (var searcher = new ManagementObjectSearcher(
                    "SELECT DeviceID, Name, PNPDeviceID FROM Win32_SerialPort"))
                using (ManagementObjectCollection devices = searcher.Get())
                {
                    foreach (ManagementObject device in devices)
                    {
                        string portName = Convert.ToString(device["DeviceID"]);
                        SerialPortItem item;
                        if (string.IsNullOrWhiteSpace(portName) ||
                            !byPort.TryGetValue(portName, out item))
                        {
                            continue;
                        }
                        item.FriendlyName = Convert.ToString(device["Name"]);
                        item.PnpDeviceId = Convert.ToString(device["PNPDeviceID"]);
                    }
                }
            }
            catch
            {
                // SerialPort.GetPortNames remains a usable fallback if WMI is unavailable.
            }

            return byPort.Values
                .OrderBy(delegate(SerialPortItem item)
                {
                    int number;
                    return TryGetPortNumber(item.PortName, out number)
                        ? number
                        : int.MaxValue;
                })
                .ThenBy(delegate(SerialPortItem item) { return item.PortName; })
                .ToList();
        }

        private static int ComparePortNames(string left, string right)
        {
            int leftNumber;
            int rightNumber;
            if (TryGetPortNumber(left, out leftNumber) && TryGetPortNumber(right, out rightNumber))
            {
                return leftNumber.CompareTo(rightNumber);
            }
            return string.Compare(left, right, StringComparison.OrdinalIgnoreCase);
        }

        private static bool TryGetPortNumber(string portName, out int number)
        {
            number = 0;
            return portName.StartsWith("COM", StringComparison.OrdinalIgnoreCase) &&
                int.TryParse(portName.Substring(3), out number);
        }

        private async void ScanClicked(object sender, EventArgs eventArgs)
        {
            var ports = new List<string>();
            var selectedItems = new List<SerialPortItem>();
            foreach (object item in portList.CheckedItems)
            {
                var port = item as SerialPortItem;
                if (port == null)
                {
                    ports.Add(item.ToString());
                    continue;
                }
                selectedItems.Add(port);
                ports.Add(port.PortName);
            }
            if (ports.Count == 0)
            {
                MessageBox.Show(this, "\u8bf7\u81f3\u5c11\u52fe\u9009\u4e00\u4e2a\u4e32\u53e3\u3002",
                    Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            IList<int> baudRates = baudBox.SelectedIndex == 0
                ? (IList<int>)new List<int>
                {
                    115200, 500000, 1000000, 1500000, 2500000
                }
                : (IList<int>)new List<int>
                {
                    int.Parse(baudBox.SelectedItem.ToString())
                };
            IList<int> motorIds = idRangeBox.SelectedIndex == 0
                ? (IList<int>)new List<int> { 1, 2 }
                : Enumerable.Range(1, 32).ToList();
            int timeoutMs = Decimal.ToInt32(timeoutBox.Value);

            resultGrid.Rows.Clear();
            logBox.Clear();

            foreach (SerialPortItem port in selectedItems)
            {
                AppendLog(string.Format("PORT: {0}; PNP: {1}",
                    port, string.IsNullOrWhiteSpace(port.PnpDeviceId)
                        ? "unknown"
                        : port.PnpDeviceId));
            }
            List<SerialPortItem> canAdapters = selectedItems
                .Where(delegate(SerialPortItem item) { return item.IsKnownCanAdapter; })
                .ToList();
            if (canAdapters.Count > 0)
            {
                string names = string.Join(", ", canAdapters
                    .Select(delegate(SerialPortItem item) { return item.PortName; }));
                string warning = string.Format(
                    "{0} \u662f CANable2 USB-CAN\uff0c\u4e0d\u662f USB-RS485\u3002MWD RS485 \u4e8c\u8fdb\u5236\u547d\u4ee4\u4e0d\u80fd\u76f4\u63a5\u901a\u8fc7\u5b83\u53d1\u9001\u3002\u8bf7\u63a5\u5165 USB-RS485 \u8f6c\u6362\u5668\u540e\u5237\u65b0\u4e32\u53e3\u3002",
                    names);
                AppendLog("WARNING: " + warning);
                statusLabel.Text = "\u68c0\u6d4b\u5df2\u505c\u6b62 - \u9009\u4e2d\u7684\u662f USB-CAN\uff0c\u4e0d\u662f USB-RS485";
                MessageBox.Show(this, warning, Text,
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            progressBar.Minimum = 0;
            progressBar.Maximum = ports.Count * baudRates.Count * motorIds.Count;
            progressBar.Value = 0;
            scanButton.Enabled = false;
            refreshButton.Enabled = false;
            selectAllButton.Enabled = false;
            cancelButton.Enabled = true;
            cancellation = new CancellationTokenSource();
            statusLabel.Text = "\u6b63\u5728\u68c0\u6d4b...";

            try
            {
                CancellationToken token = cancellation.Token;
                List<MotorStatus> results = await Task.Run(delegate
                {
                    return MwdDetector.Scan(
                        ports, baudRates, motorIds, timeoutMs, token,
                        delegate(string message)
                        {
                            BeginInvoke((Action)delegate { AppendLog(message); });
                        },
                        delegate(int completed, int total, string current)
                        {
                            BeginInvoke((Action)delegate
                            {
                                int value = Math.Max(progressBar.Minimum,
                                    Math.Min(progressBar.Maximum, completed));
                                progressBar.Value = value;
                                if (!string.IsNullOrEmpty(current))
                                {
                                    statusLabel.Text = "\u6b63\u5728\u68c0\u6d4b " + current;
                                }
                            });
                        });
                }, token);

                foreach (MotorStatus status in results)
                {
                    resultGrid.Rows.Add(
                        status.Port,
                        status.BaudRate,
                        status.MotorId,
                        status.TemperatureC,
                        status.SpeedDps,
                        status.OutputAngleDeg,
                        status.ControlValue);
                }

                progressBar.Value = progressBar.Maximum;
                if (results.Count > 0)
                {
                    statusLabel.Text = string.Format(
                        "\u68c0\u6d4b\u5b8c\u6210 - \u627e\u5230 {0} \u4e2a MWD \u7535\u673a\u54cd\u5e94",
                        results.Count);
                }
                else
                {
                    statusLabel.Text = "\u68c0\u6d4b\u5b8c\u6210 - \u672a\u6536\u5230\u6709\u6548 MWD \u54cd\u5e94";
                    AppendLog("No valid response. Check RS485 mode, A/B polarity, motor power, baud rate and ID.");
                    AppendLog("Motors sharing the same ID may reply at the same time and collide.");
                }
            }
            catch (OperationCanceledException)
            {
                statusLabel.Text = "\u68c0\u6d4b\u5df2\u53d6\u6d88";
                AppendLog("Scan cancelled.");
            }
            catch (Exception exception)
            {
                statusLabel.Text = "\u68c0\u6d4b\u5931\u8d25";
                AppendLog(exception.ToString());
                MessageBox.Show(this, exception.Message, Text,
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                cancellation.Dispose();
                cancellation = null;
                scanButton.Enabled = true;
                refreshButton.Enabled = true;
                selectAllButton.Enabled = true;
                cancelButton.Enabled = false;
            }
        }

        private void AppendLog(string message)
        {
            logBox.AppendText(string.Format("[{0:HH:mm:ss.fff}] {1}{2}",
                DateTime.Now, message, Environment.NewLine));
        }
    }

    internal static class Program
    {
        [STAThread]
        private static int Main(string[] args)
        {
            try
            {
                MwdDetector.SelfTest();
                if (args.Any(delegate(string value)
                    { return string.Equals(value, "--self-test", StringComparison.OrdinalIgnoreCase); }))
                {
                    return 0;
                }

                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Application.Run(new MainForm());
                return 0;
            }
            catch (Exception exception)
            {
                MessageBox.Show(exception.ToString(), "MWD Motor Detector",
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }
    }
}
