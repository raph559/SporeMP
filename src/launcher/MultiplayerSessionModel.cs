using System.ComponentModel;
using System.IO;
using System.Net;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace SporeMP.Launcher;

public sealed class ServerInvitation
{
    public string Host { get; }
    public int Port { get; }
    public string Endpoint => Host.Contains(':') ? $"[{Host}]:{Port}" : $"{Host}:{Port}";
    private ServerInvitation(string host, int port) { Host = host; Port = port; }
    public override string ToString() => Endpoint;

    public static ServerInvitation Parse(string value)
    {
        const string message = "Paste the complete invitation supplied by your server host.";
        if (value.Length is < 1 or > 2048) throw new ArgumentException(message);
        value = value.Trim();
        if (value.Any(c => c <= 32 || c > 126) || !Uri.TryCreate(value, UriKind.Absolute, out var uri) ||
            uri.Scheme != "sporemp" || uri.Authority != "join" || uri.AbsolutePath is not ("" or "/") || uri.Fragment.Length != 0)
            throw new ArgumentException(message);
        var fields = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (string pair in uri.Query.TrimStart('?').Split('&'))
        {
            string[] parts = pair.Split('=');
            if (parts.Length != 2 || !fields.TryAdd(Uri.UnescapeDataString(parts[0]), Uri.UnescapeDataString(parts[1])))
                throw new ArgumentException(message);
        }
        if (fields.Count != 4 || !new[] { "host", "port", "cert", "token" }.All(fields.ContainsKey))
            throw new ArgumentException(message);
        string host = fields["host"];
        if (host.Length is < 1 or > 253 || host.Any(c => c <= 32 || c > 126)) throw new ArgumentException(message);
        if (IPAddress.TryParse(host, out var address)) host = address.ToString();
        else if (!Regex.IsMatch(host, @"\A[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?\z") ||
            host.Split('.').Any(label => label.Length is < 1 or > 63 || label.StartsWith('-') || label.EndsWith('-')))
            throw new ArgumentException(message);
        if (!Regex.IsMatch(fields["port"], @"\A[0-9]{1,5}\z") || !int.TryParse(fields["port"], out int port) || port is < 1 or > 65535)
            throw new ArgumentException(message);
        foreach (var field in new[] { "cert", "token" })
            if (!Regex.IsMatch(fields[field], @"\A[A-Fa-f0-9]{64}\z") || fields[field] == new string('0', 64))
                throw new ArgumentException(message);
        return new(host.ToLowerInvariant(), port);
    }
}

public sealed class MultiplayerSessionModel(LauncherClient client) : INotifyPropertyChanged
{
    private string invitation = "";
    private string? joinedInvitation;
    private ServerInvitation? parsed;
    public event PropertyChangedEventHandler? PropertyChanged;
    public string Invitation
    {
        get => invitation;
        set
        {
            if (IsActive) return;
            invitation = value;
            try { parsed = ServerInvitation.Parse(value); Detail = "Invitation ready. Your place on this server follows this invitation."; }
            catch (ArgumentException) { parsed = null; Detail = value.Length == 0 ? "Ask your host for an invitation, then paste it here." : "This invitation is incomplete. Paste the full link from your host."; }
            Status = "Private multiplayer";
            Refresh();
        }
    }
    public bool IsActive { get; private set; }
    public bool CanJoin => !IsActive && parsed is not null;
    public bool CanEdit => !IsActive;
    public bool IsConnected { get; private set; }
    public string ServerLabel => parsed?.Endpoint ?? "Join a shared universe";
    public string ButtonLabel => IsConnected ? "Connected" : IsActive ? "Joining…" : invitation == joinedInvitation ? "Rejoin server" : "Join server";
    public string Status { get; private set; } = "Private multiplayer";
    public string Detail { get; private set; } = "Ask your host for an invitation, then paste it here.";
    public string TechnicalDetail { get; private set; } = "";
    public OperationReport? LastReport { get; private set; }
    private void Refresh() => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(null));

    public async Task JoinAsync(string gameRoot)
    {
        if (!CanJoin) return;
        string captured = invitation;
        IsActive = true; IsConnected = false; Status = "Checking your invitation…";
        TechnicalDetail = "";
        Refresh();
        try
        {
            var progress = new Progress<LauncherProgress>(value =>
            {
                if (!IsActive) return;
                if (value.Phase == "connected")
                {
                    IsConnected = true; joinedInvitation = captured;
                    Status = "Connected to " + ServerLabel;
                    Detail = "Your shared scene is ready in SPORE.";
                }
                else if (value.Phase == "disconnected")
                {
                    IsConnected = false; Status = "Connection lost";
                    Detail = "Waiting for the server. Your place is kept for reconnect.";
                }
                else { Status = value.Message; }
                Refresh();
            });
            var report = await client.RunAsync("native_join", gameRoot, progress: progress, invitation: captured);
            LastReport = report;
            if (report.Result.TryGetProperty("authenticated", out var authenticated) && authenticated.GetBoolean()) joinedInvitation = captured;
            if (report.Result.TryGetProperty("error", out var error)) throw new IOException(error.GetString());
            if (report.ExitCode != 0) throw new IOException("The multiplayer session could not finish. The connection result is recorded in Settings.");
            Status = "Session closed";
            Detail = "Rejoin with this invitation to return as the same player.";
            TechnicalDetail = "Session result: " + report.ReportPath;
        }
        catch (Exception error) when (error is IOException or JsonException or InvalidOperationException or KeyNotFoundException or Win32Exception or ArgumentException)
        {
            Status = "Couldn’t join this server";
            Detail = error is ArgumentException ? error.Message : "Check that the server is available and your invitation is current. Details are in Settings.";
            TechnicalDetail = error.Message;
        }
        finally { IsActive = false; IsConnected = false; Refresh(); }
    }
}
