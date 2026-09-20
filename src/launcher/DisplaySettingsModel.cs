using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Text.Json;

namespace SporeMP.Launcher;

public sealed record DisplayPreferences(string Mode, string Resolution);
public sealed record DisplayChoice(string Value, string Label);

public sealed class DisplaySettingsModel(LauncherClient client) : INotifyPropertyChanged
{
    private DisplayChoice? selectedMode, selectedResolution;
    private DisplayPreferences? saved;
    private bool loaded, busy;
    public event PropertyChangedEventHandler? PropertyChanged;
    public IReadOnlyList<DisplayChoice> Modes { get; } = [
        new("game", "Use game settings"), new("fullscreen", "Fullscreen"), new("windowed", "Windowed")];
    public ObservableCollection<DisplayChoice> Resolutions { get; } = [];
    public DisplayChoice? SelectedMode
    {
        get => selectedMode;
        set { if (selectedMode == value) return; selectedMode = value; SelectionChanged(); }
    }
    public DisplayChoice? SelectedResolution
    {
        get => selectedResolution;
        set { if (selectedResolution == value) return; selectedResolution = value; SelectionChanged(); }
    }
    private DisplayPreferences? Selection => selectedMode is null || selectedResolution is null ? null :
        new(selectedMode.Value, selectedMode.Value == "game" ? "desktop" : selectedResolution.Value);
    public bool CanEdit => loaded && !busy;
    public bool CanChooseResolution => CanEdit && selectedMode?.Value != "game";
    public bool CanReload => !busy;
    public bool CanSave => CanEdit && Selection is not null && Selection != saved;
    public string SaveLabel => busy ? "Please wait…" : "Save display settings";
    public string Message { get; private set; } = "Reading your display…";
    public string Summary { get; private set; } = "Display settings";

    private void Refresh() => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(null));
    private void SelectionChanged()
    {
        if (loaded && !busy)
            Message = Selection != saved ? "Unsaved changes. Save to use these settings next time you play." :
                "Applies the next time you launch SPORE.";
        Refresh();
    }

    public async Task LoadAsync(CancellationToken cancellationToken = default)
    {
        if (busy) return;
        busy = true; Message = "Reading your display…"; Refresh();
        try { Apply(await client.RunAsync("display_settings", cancellationToken: cancellationToken), false); }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested) { }
        catch (Exception error) when (error is IOException or JsonException or InvalidOperationException or KeyNotFoundException or Win32Exception)
        { Message = error.Message; }
        finally { busy = false; Refresh(); }
    }

    public async Task SaveAsync(CancellationToken cancellationToken = default)
    {
        if (!CanSave || Selection is not DisplayPreferences choices) return;
        busy = true; Message = "Saving your display settings…"; Refresh();
        try { Apply(await client.RunAsync("save_display", cancellationToken: cancellationToken, display: choices), true); }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested) { }
        catch (Exception error) when (error is IOException or JsonException or InvalidOperationException or KeyNotFoundException or Win32Exception)
        { Message = error.Message; }
        finally { busy = false; Refresh(); }
    }

    private void Apply(OperationReport report, bool justSaved)
    {
        if (report.Result.TryGetProperty("error", out var error)) throw new IOException(error.GetString());
        if (report.ExitCode != 0) throw new IOException("Display settings could not be updated. Try again.");
        var result = report.Result;
        var values = result.GetProperty("display");
        var choices = new DisplayPreferences(values.GetProperty("mode").GetString()!, values.GetProperty("resolution").GetString()!);
        selectedMode = Modes.First(x => x.Value == choices.Mode);
        Resolutions.Clear();
        Resolutions.Add(new("desktop", "Desktop · " + Format(result.GetProperty("desktop").GetString()!) + " (recommended)"));
        foreach (var resolution in result.GetProperty("resolutions").EnumerateArray())
            Resolutions.Add(new(resolution.GetString()!, Format(resolution.GetString()!)));
        if (!Resolutions.Any(x => x.Value == choices.Resolution))
            Resolutions.Add(new(choices.Resolution, Format(choices.Resolution) + " (unavailable)"));
        selectedResolution = Resolutions.First(x => x.Value == choices.Resolution);
        string warning = result.GetProperty("warning").GetString()!;
        saved = warning.Length == 0 ? choices : null;
        Summary = choices.Mode == "game" ? "Use game settings" : selectedMode.Label + " · " +
            (choices.Resolution == "desktop" ? "Desktop" : Format(choices.Resolution));
        Message = warning.Length > 0 ? warning : justSaved ? "Saved. Applies next time you launch SPORE." :
            "Choose a mode and resolution. Your choice is remembered for every launch.";
        loaded = true;
        Refresh();
    }

    private static string Format(string value) => value.Replace("x", " × ");
}
