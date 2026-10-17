// CoopTunnel carries CoopAndreas' UDP traffic (ENet, port 6767) over Steam P2P, using AppID 480 (Spacewar),
// so players behind CGNAT can play without port forwarding. Steam picks a direct route or a Valve relay.
//
//   CoopTunnel host [--server-port 6767] [--allow-all]   on the PC that runs server.exe
//   CoopTunnel join [steamid64] [--port 6767]           on a friend's PC; the game connects to 127.0.0.1:<port>
//   CoopTunnel test [steamid64]                         ping test against a running host, no game needed
//
// Every Steam message starts with one byte: 0 = game datagram, 1 = ping, 2 = pong, 3 = hello (joiner's SteamID64).
// Facepunch.Steamworks 2.3.3 can't read the remote identity of a connection with the current Steam client
// (it comes back as 'ip:::'), so the host checks the friendship from the hello message instead.

using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using Steamworks;
using Steamworks.Data;

static class Wire
{
    public const byte Game = 0, Ping = 1, Pong = 2, Hello = 3;

    // ENet does its own reliability and ordering, so game datagrams go out unreliable and unbuffered
    public const SendType Fast = SendType.Unreliable | SendType.NoNagle;

    public static byte[] Copy(IntPtr data, int size)
    {
        var bytes = new byte[size];
        Marshal.Copy(data, bytes, 0, size);
        return bytes;
    }

    public static string Name(SteamId id) => $"{new Friend(id).Name} ({id.Value})";

    // without this a "port unreachable" (server.exe not running yet) makes the next Receive throw
    public static void IgnoreConnReset(UdpClient udp)
    {
        const int SIO_UDP_CONNRESET = -1744830452;
        udp.Client.IOControl(SIO_UDP_CONNRESET, [0, 0, 0, 0], null);
    }
}

sealed class HostSocket : SocketManager
{
    sealed class Peer(Connection connection, UdpClient udp)
    {
        public readonly Connection Connection = connection;
        public readonly UdpClient Udp = udp;
        public readonly Stopwatch Age = Stopwatch.StartNew();
        public string Name = "unverified connection";
        public bool Verified;
        public long BytesIn, BytesOut;
    }

    public int ServerPort = 6767;
    public bool AllowAll;
    readonly Dictionary<uint, Peer> _peers = [];
    readonly byte[] _buffer = new byte[65536];

    public override void OnConnecting(Connection connection, ConnectionInfo info)
    {
        // the friend check happens on the hello message, see OnMessage
        Console.WriteLine("Incoming connection, accepting...");
        base.OnConnecting(connection, info);
    }

    public override void OnConnected(Connection connection, ConnectionInfo info)
    {
        base.OnConnected(connection, info);
        // one local socket per friend, so server.exe sees each of them as a separate ENet peer
        var udp = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        Wire.IgnoreConnReset(udp);
        udp.Connect(IPAddress.Loopback, ServerPort);
        _peers[connection.Id] = new Peer(connection, udp);
    }

    public override void OnDisconnected(Connection connection, ConnectionInfo info)
    {
        base.OnDisconnected(connection, info);
        if (_peers.Remove(connection.Id, out var peer))
        {
            peer.Udp.Dispose();
            Console.WriteLine($"Left: {peer.Name} ({info.EndReason})");
        }
    }

    // a connection that hasn't said hello within 10 s is closed
    public void ExpireUnverified()
    {
        foreach (var peer in _peers.Values.Where(p => !p.Verified && p.Age.Elapsed.TotalSeconds > 10).ToList())
        {
            Console.WriteLine("No hello within 10 s, closing the connection.");
            peer.Connection.Close();
        }
    }

    void Verify(Peer peer, byte[] hello)
    {
        var id = new SteamId { Value = BitConverter.ToUInt64(hello, 1) };
        if (!AllowAll && !new Friend(id).IsFriend)
        {
            Console.WriteLine($"Refused (not your Steam friend): {Wire.Name(id)}");
            peer.Connection.Close();
            return;
        }
        peer.Verified = true;
        peer.Name = Wire.Name(id);
        Console.WriteLine($"Connected: {peer.Name} -> 127.0.0.1:{ServerPort}");
    }

    public override void OnMessage(Connection connection, NetIdentity identity, IntPtr data, int size, long messageNum, long recvTime, int channel)
    {
        if (size < 1 || !_peers.TryGetValue(connection.Id, out var peer))
        {
            return;
        }
        var bytes = Wire.Copy(data, size);
        switch (bytes[0])
        {
            case Wire.Hello when size >= 9 && !peer.Verified:
                Verify(peer, bytes);
                break;
            case Wire.Game when peer.Verified:
                peer.Udp.Client.Send(bytes, 1, size - 1, SocketFlags.None);
                peer.BytesIn += size - 1;
                break;
            case Wire.Ping:
                bytes[0] = Wire.Pong;
                connection.SendMessage(bytes, 0, size, Wire.Fast);
                break;
        }
    }

    // server.exe -> friends
    public void PumpUdp()
    {
        foreach (var peer in _peers.Values.Where(p => p.Verified))
        {
            while (peer.Udp.Available > 0)
            {
                int n;
                try
                {
                    n = peer.Udp.Client.Receive(_buffer, 1, _buffer.Length - 1, SocketFlags.None);
                }
                catch (SocketException)
                {
                    break;
                }
                _buffer[0] = Wire.Game;
                peer.Connection.SendMessage(_buffer, 0, n + 1, Wire.Fast);
                peer.BytesOut += n;
            }
        }
    }

    public void PrintStatus()
    {
        if (_peers.Count == 0)
        {
            Console.WriteLine("Waiting: no friend connected yet.");
        }
        foreach (var peer in _peers.Values)
        {
            Console.WriteLine($"  {peer.Name}: {peer.BytesIn / 1024} KB to the server, {peer.BytesOut / 1024} KB from it");
        }
    }
}

sealed class HostLink : ConnectionManager
{
    public Action<byte[], int, int>? OnGameData;
    public Action<double>? OnRtt;
    public SteamId Host;
    public bool Closed;

    public override void OnConnected(ConnectionInfo info)
    {
        base.OnConnected(info);
        // tell the host who we are; it can't read our identity from the connection itself
        var hello = new byte[9];
        hello[0] = Wire.Hello;
        BitConverter.TryWriteBytes(hello.AsSpan(1), SteamClient.SteamId.Value);
        Connection.SendMessage(hello, SendType.Reliable);
        Console.WriteLine($"Connected to the host: {Wire.Name(Host)}");
    }

    public override void OnDisconnected(ConnectionInfo info)
    {
        base.OnDisconnected(info);
        Closed = true;
        Console.WriteLine($"Connection closed ({info.EndReason}).");
    }

    public override void OnMessage(IntPtr data, int size, long messageNum, long recvTime, int channel)
    {
        if (size < 1)
        {
            return;
        }
        var bytes = Wire.Copy(data, size);
        if (bytes[0] == Wire.Game)
        {
            OnGameData?.Invoke(bytes, 1, size - 1);
        }
        else if (bytes[0] == Wire.Pong && size >= 9)
        {
            long sent = BitConverter.ToInt64(bytes, 1);
            OnRtt?.Invoke(Stopwatch.GetElapsedTime(sent).TotalMilliseconds);
        }
    }

    public void SendPing()
    {
        var ping = new byte[9];
        ping[0] = Wire.Ping;
        BitConverter.TryWriteBytes(ping.AsSpan(1), Stopwatch.GetTimestamp());
        Connection.SendMessage(ping, Wire.Fast);
    }
}

static class Program
{
    const uint SpacewarAppId = 480;
    const int DefaultPort = 6767;
    static volatile bool s_quit;
    static Stopwatch? s_quitAfter;
    static int s_quitAfterSeconds;

    // without it Thread.Sleep(1) in the pump loops can last 15.6 ms, which adds straight to the game's ping
    [DllImport("winmm.dll")]
    static extern uint timeBeginPeriod(uint period);

    static int Main(string[] args)
    {
        Console.OutputEncoding = System.Text.Encoding.UTF8;  // Steam names in any language on any console code page
        timeBeginPeriod(1);
        if (args.Length == 2 && args[0] == "whois" && ulong.TryParse(args[1], out ulong whoisId))
        {
            // diagnostics: how this account sees another Steam account
            SteamClient.Init(SpacewarAppId, asyncCallbacks: false);
            var friend = new Friend(new SteamId { Value = whoisId });
            Console.WriteLine($"{friend.Name}: relationship={friend.Relationship}, IsFriend={friend.IsFriend}, " +
                              $"inSpacewar={friend.IsPlayingThisGame}, inFriendList={SteamFriends.GetFriends().Any(f => f.Id.Value == whoisId)}");
            SteamClient.Shutdown();
            return 0;
        }
        if (args.Length == 0 || args[0] is not ("host" or "join" or "test"))
        {
            Console.WriteLine("Usage:\n" +
                              "  CoopTunnel host [--server-port 6767] [--allow-all]   on the PC that runs server.exe\n" +
                              "  CoopTunnel join [steamid64] [--port 6767]           on a friend's PC; the game connects to 127.0.0.1\n" +
                              "  CoopTunnel test [steamid64]                         20 pings to a running host, no game needed");
            return 1;
        }
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; s_quit = true; };
        s_quitAfterSeconds = Option(args, "--quit-after", 0);  // for unattended checks
        s_quitAfter = Stopwatch.StartNew();

        try
        {
            SteamClient.Init(SpacewarAppId, asyncCallbacks: false);
        }
        catch (Exception e)
        {
            Console.WriteLine($"Could not start Steam: {e.Message}\nSteam must be running and signed in.");
            return 2;
        }

        try
        {
            Console.WriteLine($"Steam account: {SteamClient.Name} ({SteamClient.SteamId.Value}), AppID {SpacewarAppId} (Spacewar)");
            if (!WaitForRelayNetwork())
            {
                return 3;
            }
            switch (args[0])
            {
                case "host":
                    return RunHost(Option(args, "--server-port", DefaultPort), args.Contains("--allow-all"));
                case "join":
                    return FindHost(args) is SteamId joinHost ? RunJoin(joinHost, Option(args, "--port", DefaultPort)) : 4;
                default:
                    return FindHost(args) is SteamId testHost ? RunTest(testHost) : 4;
            }
        }
        finally
        {
            SteamClient.Shutdown();
        }
    }

    static bool ShouldQuit() => s_quit || (s_quitAfterSeconds > 0 && s_quitAfter!.Elapsed.TotalSeconds >= s_quitAfterSeconds);

    static int Option(string[] args, string name, int fallback)
    {
        int i = Array.IndexOf(args, name);
        return i >= 0 && i + 1 < args.Length && int.TryParse(args[i + 1], out int value) ? value : fallback;
    }

    static bool WaitForRelayNetwork()
    {
        SteamNetworkingUtils.InitRelayNetworkAccess();
        var last = (SteamNetworkingAvailability)(-1);
        var timer = Stopwatch.StartNew();
        while (!ShouldQuit() && timer.Elapsed.TotalSeconds < 30)
        {
            SteamClient.RunCallbacks();
            var status = SteamNetworkingUtils.Status;
            if (status != last)
            {
                Console.WriteLine($"Steam relay network: {status}");
                last = status;
            }
            if (status == SteamNetworkingAvailability.Current)
            {
                return true;
            }
            Thread.Sleep(100);
        }
        Console.WriteLine("Could not reach the Steam relay network within 30 s.");
        return false;
    }

    static SteamId? FindHost(string[] args)
    {
        if (args.Length > 1 && ulong.TryParse(args[1], out ulong explicitId))
        {
            return new SteamId { Value = explicitId };
        }
        Console.WriteLine("Looking for a Steam friend who runs the tunnel (shown as playing Spacewar)...");
        var timer = Stopwatch.StartNew();
        while (!ShouldQuit() && timer.Elapsed.TotalSeconds < 60)
        {
            SteamClient.RunCallbacks();
            var hosts = SteamFriends.GetFriends().Where(f => f.IsPlayingThisGame).ToList();
            if (hosts.Count == 1)
            {
                Console.WriteLine($"Host found: {hosts[0].Name} ({hosts[0].Id.Value})");
                return hosts[0].Id;
            }
            if (hosts.Count > 1)
            {
                for (int i = 0; i < hosts.Count; i++)
                {
                    Console.WriteLine($"  {i + 1}) {hosts[i].Name} ({hosts[i].Id.Value})");
                }
                Console.Write("Connect to which one? Number: ");
                if (int.TryParse(Console.ReadLine(), out int pick) && pick >= 1 && pick <= hosts.Count)
                {
                    return hosts[pick - 1].Id;
                }
                return null;
            }
            Thread.Sleep(1000);
        }
        Console.WriteLine("No friend found playing Spacewar. The host must run 'CoopTunnel host' (1-Host.bat),\n" +
                          "or give the host's SteamID64: CoopTunnel join 7656119xxxxxxxxxx");
        return null;
    }

    static HostLink? Connect(SteamId host)
    {
        HostLink link;
        try
        {
            link = SteamNetworkingSockets.ConnectRelay<HostLink>(host, 0);
            link.Host = host;
        }
        catch (ArgumentException)
        {
            // Steam refused to even start the connection: a wrong SteamID, or our own
            Console.WriteLine($"Steam refused to start this connection (wrong SteamID, or your own account): {host.Value}");
            return null;
        }
        var timer = Stopwatch.StartNew();
        while (!ShouldQuit() && !link.Connected && !link.Closed && timer.Elapsed.TotalSeconds < 30)
        {
            SteamClient.RunCallbacks();
            link.Receive(256);
            Thread.Sleep(5);
        }
        if (link.Connected)
        {
            return link;
        }
        Console.WriteLine("Could not connect to the host. Is the host's tunnel running, and are you Steam friends?");
        link.Close();
        return null;
    }

    static int RunHost(int serverPort, bool allowAll)
    {
        var socket = SteamNetworkingSockets.CreateRelaySocket<HostSocket>(0);
        socket.ServerPort = serverPort;
        socket.AllowAll = allowAll;
        Console.WriteLine($"Host ready. SteamID64: {SteamClient.SteamId.Value}");
        Console.WriteLine($"Friends run 'CoopTunnel join' (3-Join.bat); their traffic goes to 127.0.0.1:{serverPort} (server.exe).");
        Console.WriteLine("Press Ctrl+C to stop.");

        var status = Stopwatch.StartNew();
        while (!ShouldQuit())
        {
            SteamClient.RunCallbacks();
            socket.Receive(256);
            socket.PumpUdp();
            socket.ExpireUnverified();
            if (status.Elapsed.TotalSeconds >= 15)
            {
                socket.PrintStatus();
                status.Restart();
            }
            Thread.Sleep(1);
        }
        socket.Close();
        return 0;
    }

    static int RunJoin(SteamId host, int localPort)
    {
        UdpClient udp;
        try
        {
            udp = new UdpClient(new IPEndPoint(IPAddress.Loopback, localPort));
        }
        catch (SocketException)
        {
            Console.WriteLine($"127.0.0.1:{localPort} is in use. Close server.exe if it runs on this PC, or pick another port with --port.");
            return 5;
        }
        Wire.IgnoreConnReset(udp);
        IPEndPoint? game = null;
        double lastRtt = 0;
        var buffer = new byte[65536];

        while (!ShouldQuit())
        {
            var link = Connect(host);
            if (link == null)
            {
                Thread.Sleep(3000);
                continue;
            }
            link.OnGameData = (data, offset, length) =>
            {
                if (game != null)
                {
                    udp.Client.SendTo(data, offset, length, SocketFlags.None, game);
                }
            };
            link.OnRtt = rtt => lastRtt = rtt;
            Console.WriteLine($"Ready. In the game's connect screen use IP 127.0.0.1, port {localPort} (your nickname must differ from the host's).");

            var ping = Stopwatch.StartNew();
            var status = Stopwatch.StartNew();
            while (!ShouldQuit() && !link.Closed)
            {
                SteamClient.RunCallbacks();
                link.Receive(256);
                while (udp.Available > 0)
                {
                    EndPoint from = new IPEndPoint(IPAddress.Any, 0);
                    int n;
                    try
                    {
                        n = udp.Client.ReceiveFrom(buffer, 1, buffer.Length - 1, SocketFlags.None, ref from);
                    }
                    catch (SocketException)
                    {
                        break;
                    }
                    game = (IPEndPoint)from;
                    buffer[0] = Wire.Game;
                    link.Connection.SendMessage(buffer, 0, n + 1, Wire.Fast);
                }
                if (ping.Elapsed.TotalSeconds >= 2)
                {
                    link.SendPing();
                    ping.Restart();
                }
                if (status.Elapsed.TotalSeconds >= 15)
                {
                    Console.WriteLine($"Connected, ping {lastRtt:F0} ms{(game == null ? ", the game hasn't connected yet" : "")}");
                    status.Restart();
                }
                Thread.Sleep(1);
            }
            link.Close();
            if (!ShouldQuit())
            {
                Console.WriteLine("Reconnecting...");
                Thread.Sleep(3000);
            }
        }
        udp.Dispose();
        return 0;
    }

    static int RunTest(SteamId host)
    {
        var link = Connect(host);
        if (link == null)
        {
            return 6;
        }
        var rtts = new List<double>();
        link.OnRtt = rtts.Add;
        const int count = 20;
        for (int i = 0; i < count && !ShouldQuit() && !link.Closed; i++)
        {
            link.SendPing();
            var wait = Stopwatch.StartNew();
            while (wait.ElapsedMilliseconds < 250)
            {
                SteamClient.RunCallbacks();
                link.Receive(256);
                Thread.Sleep(2);
            }
        }
        Console.WriteLine(rtts.Count == 0
            ? "No replies."
            : $"Ping: {rtts.Count}/{count} replies, min {rtts.Min():F0} ms, average {rtts.Average():F0} ms, max {rtts.Max():F0} ms");
        Console.WriteLine("---- Steam connection details (route: direct or relay) ----");
        Console.WriteLine(link.Connection.DetailedStatus());
        link.Close();
        return rtts.Count > 0 ? 0 : 7;
    }
}
