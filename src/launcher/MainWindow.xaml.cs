using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Text.Json;
using System.Windows;
using Microsoft.Win32;

namespace SporeMP.Launcher;

public sealed record InstallationChoice(string Root, string Store);

public partial class MainWindow : Window, INotifyPropertyChanged
{
    private readonly LauncherClient client;
    private readonly CancellationTokenSource lifetime = new();
    private bool busy = true, settingsOpen, updatesOpen;
    private bool nativeAvailable, nativeActive, closeAfterGame;
    private string state = "preparing";
    private string? lastReport;
    public event PropertyChangedEventHandler? PropertyChanged;
    public ObservableCollection<InstallationChoice> Installations { get; } = [];
    public IReadOnlyList<ReleaseNote> UpdateHistory { get; } = ReleaseNotes.Load();
    public ReleaseNote LatestUpdate => UpdateHistory[0];
    public string BuildLabel => "SPORE MULTIPLAYER  /  " + ReleaseNotes.CurrentVersion;
    public string VersionLabel => "v" + ReleaseNotes.CurrentVersion;
    public string GameRoot { get; private set; } = "Detecting automatically…";
    public string GameName { get; private set; } = "Looking for SPORE";
    public bool CanRun => !busy;
    public bool CanExport => !busy && lastReport is not null;
    public bool CanPrimary => !busy && (nativeAvailable && state == "development_build" || state is "game_not_found" or "choose_installation" or "error" or "game_running" or "unsupported_installation");
    public string PrimaryLabel => nativeActive ? "SPORE is running…" : busy && state == "preparing" ? "Getting ready…" : state switch { "game_not_found" => "Locate SPORE", "choose_installation" => "Choose installation", "game_running" => "Check again", "error" => "Try again", "unsupported_installation" => "Review installation", _ => nativeAvailable ? "Play SPORE" : "Unavailable" };
    public string PrimaryHint => nativeActive ? "Enjoy your game" : busy && state == "preparing" ? "Setup runs automatically" : "Multiplayer is still in development";
    public string DetailsLabel => state == "development_build" && !nativeAvailable ? "Why can’t I play?  ↗" : "Settings & setup  ↗";
    public string StatusBrush => state == "development_build" ? "#96D8C8" : state == "error" || state == "unsupported_installation" ? "#EEB193" : "#F1CB8A";
    public Visibility BusyVisibility => busy && state == "preparing" ? Visibility.Visible : Visibility.Collapsed;
    public Visibility SettingsVisibility => settingsOpen ? Visibility.Visible : Visibility.Collapsed;
    public Visibility UpdatesVisibility => updatesOpen ? Visibility.Visible : Visibility.Collapsed;
    public Visibility ChoicesVisibility => Installations.Count > 1 ? Visibility.Visible : Visibility.Collapsed;
    public string StatusTitle { get; private set; } = "Finding SPORE…";
    public string StatusDetail { get; private set; } = "We’ll take care of the setup.";
    public string FilesDetail { get; private set; } = "Game files · waiting for detection";
    public string SavesDetail { get; private set; } = "Uses your existing SPORE saves";
    public string WorkspaceDetail { get; private set; } = "Runs under your normal Windows account";
    public string BackupPath { get; private set; } = "";
    public string TechnicalDetail { get; private set; } = "";
    public string ExportDetail { get; private set; } = "";

    public MainWindow(LauncherClient client)
    {
        this.client = client;
        InitializeComponent();
        DataContext = this;
    }

    private void Refresh() => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(null));
    private async void OnLoaded(object sender, RoutedEventArgs e) { busy = false; await Prepare(); }

    private async Task Prepare(string? selectedRoot = null)
    {
        if (busy) return;
        busy = true;
        state = "preparing";
        StatusTitle = "Finding SPORE…";
        StatusDetail = "We’ll take care of the setup.";
        TechnicalDetail = "";
        FilesDetail = "Game files · checking";
        SavesDetail = "Uses your existing SPORE saves";
        WorkspaceDetail = "Runs under your normal Windows account";
        BackupPath = "";
        Refresh();
        try
        {
            var progress = new Progress<LauncherProgress>(value =>
            {
                if (!busy || lifetime.IsCancellationRequested) return;
                StatusTitle = value.Message;
                StatusDetail = value.Phase == "verify" ? "SPORE found. Checking your installation." : "Looking for your installed game.";
                Refresh();
            });
            var report = await client.RunAsync("prepare", selectedRoot, progress: progress, cancellationToken: lifetime.Token);
            lastReport = report.ReportPath;
            var result = report.Result;
            if (result.TryGetProperty("error", out var error)) throw new IOException(error.GetString());
            Installations.Clear();
            foreach (var installation in result.GetProperty("installations").EnumerateArray())
                Installations.Add(new(installation.GetProperty("root").GetString()!, installation.GetProperty("store").GetString()!));
            if (result.TryGetProperty("installation", out var selected))
            {
                GameRoot = selected.GetProperty("root").GetString()!;
                GameName = "SPORE · " + selected.GetProperty("store").GetString();
            }
            else { GameRoot = "No installation selected"; GameName = "SPORE"; }
            state = result.GetProperty("state").GetString()!;
            StatusTitle = state switch { "development_build" => "Ready to play.", "game_not_found" => "Let’s find your game", "choose_installation" => "Choose your SPORE installation", "game_running" => "SPORE is already running", _ => "This installation needs attention" };
            StatusDetail = result.GetProperty("message").GetString()!;
            if (result.TryGetProperty("compatibility", out var compatibility))
            {
                bool matches = compatibility.GetProperty("candidate_match").GetBoolean();
                FilesDetail = matches ? "Game files verified" : "This installation is incompatible with the current build";
                if (!matches) TechnicalDetail = string.Join(", ", compatibility.GetProperty("issues").EnumerateArray().Select(x => x.GetString()));
            }
            nativeAvailable = result.TryGetProperty("native", out var native) && native.GetProperty("available").GetBoolean();
            if (nativeAvailable)
            {
                TechnicalDetail = "Game and launcher files are checked automatically before starting SPORE.";
                StatusTitle = "Ready to play.";
                StatusDetail = GameName;
            }
            if (state == "choose_installation") settingsOpen = true;
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested) { }
        catch (Exception error) when (error is IOException or JsonException or InvalidOperationException or KeyNotFoundException or Win32Exception)
        {
            state = "error";
            StatusTitle = "Setup couldn’t finish";
            StatusDetail = "Open Settings for details, then try again.";
            TechnicalDetail = error.Message;
        }
        finally { busy = false; Refresh(); }
    }

    private async void PrimaryClick(object sender, RoutedEventArgs e)
    {
        if (state == "development_build" && nativeAvailable) await RunNative();
        else if (state == "game_not_found") BrowseClick(sender, e);
        else if (state is "choose_installation" or "unsupported_installation") { settingsOpen = true; Refresh(); }
        else if (state is "error" or "game_running") await Prepare();
    }
    private async Task RunNative()
    {
        if (busy) return;
        busy = true; nativeActive = true;
        StatusTitle = "Starting SPORE…";
        StatusDetail = GameName;
        Refresh();
        try
        {
            var progress = new Progress<LauncherProgress>(value => { StatusTitle = value.Message; Refresh(); });
            var report = await client.RunAsync("native_launch", GameRoot, progress: progress);
            lastReport = report.ReportPath;
            if (report.Result.TryGetProperty("error", out var error)) throw new IOException(error.GetString());
            StatusTitle = "Ready to play.";
            StatusDetail = GameName;
            TechnicalDetail = "Last session closed normally. Diagnostics: " + report.Result.GetProperty("evidence_directory").GetString();
        }
        catch (Exception error) when (error is IOException or JsonException or InvalidOperationException or KeyNotFoundException or Win32Exception)
        {
            StatusTitle = "SPORE couldn’t finish starting.";
            StatusDetail = "Open Settings for the recorded result.";
            TechnicalDetail = error.Message;
        }
        finally { nativeActive = false; busy = false; Refresh(); if (closeAfterGame) Close(); }
    }
    private async void BrowseClick(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog { Title = "Locate your SPORE installation" };
        if (Directory.Exists(GameRoot)) dialog.InitialDirectory = GameRoot;
        if (dialog.ShowDialog(this) == true) await Prepare(dialog.FolderName);
    }
    private async void ChooseClick(object sender, RoutedEventArgs e) { if (InstallationChoices.SelectedItem is InstallationChoice selected) await Prepare(selected.Root); }
    private async void RetryClick(object sender, RoutedEventArgs e) => await Prepare();
    private void SettingsClick(object sender, RoutedEventArgs e) { updatesOpen = false; settingsOpen = true; Refresh(); }
    private void SettingsCloseClick(object sender, RoutedEventArgs e) { settingsOpen = false; Refresh(); }
    private void OverlayClick(object sender, System.Windows.Input.MouseButtonEventArgs e) { settingsOpen = false; Refresh(); }
    private void UpdatesClick(object sender, RoutedEventArgs e) { settingsOpen = false; updatesOpen = true; Refresh(); }
    private void UpdatesCloseClick(object sender, RoutedEventArgs e) { updatesOpen = false; Refresh(); }
    private void UpdatesOverlayClick(object sender, System.Windows.Input.MouseButtonEventArgs e) { updatesOpen = false; Refresh(); }
    private void PlayTabClick(object sender, RoutedEventArgs e) { updatesOpen = false; settingsOpen = false; Refresh(); }
    private void WindowKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
    {
        if (e.Key == System.Windows.Input.Key.Escape && (updatesOpen || settingsOpen))
        {
            updatesOpen = false; settingsOpen = false; Refresh(); e.Handled = true;
        }
    }
    private void CloseClick(object sender, RoutedEventArgs e) => Close();
    private void MinimizeClick(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    private void MaximizeClick(object sender, RoutedEventArgs e) => WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
    private void OnClosing(object? sender, CancelEventArgs e)
    {
        if (nativeActive)
        {
            e.Cancel = true;
            closeAfterGame = true;
            ShowInTaskbar = false;
            Hide();
            return;
        }
        lifetime.Cancel();
    }

    private async void ExportClick(object sender, RoutedEventArgs e)
    {
        if (!CanExport) return;
        busy = true; Refresh();
        try
        {
            var report = await client.RunAsync("export", reportPath: lastReport, cancellationToken: lifetime.Token);
            if (report.Result.TryGetProperty("error", out var error)) throw new IOException(error.GetString());
            ExportDetail = "Saved locally: " + report.Result.GetProperty("export_path").GetString();
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested) { }
        catch (Exception error) when (error is IOException or JsonException or InvalidOperationException or KeyNotFoundException or Win32Exception) { ExportDetail = error.Message; }
        finally { busy = false; Refresh(); }
    }
}
