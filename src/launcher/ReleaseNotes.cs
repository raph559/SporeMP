using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text.Json;

namespace SporeMP.Launcher;

public sealed record ReleaseChange(string Category, string Text);

public sealed record ReleaseNote(string Version, string Date, string Title, string Summary, ReleaseChange[] Changes)
{
    public string VersionLabel => "v" + Version;
    public bool IsCurrentVersion => Version == ReleaseNotes.CurrentVersion;
    public string DateLabel => DateTime.TryParseExact(Date, "yyyy-MM-dd", CultureInfo.InvariantCulture,
        DateTimeStyles.None, out var date) ? date.ToString("d MMM yyyy", CultureInfo.InvariantCulture) : Date;

    public bool Matches(string query)
    {
        var terms = query.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        var text = string.Join(" ", VersionLabel, Date, DateLabel, Title, Summary,
            string.Join(" ", Changes.Select(change => change.Category + " " + change.Text)));
        return terms.All(term => text.Contains(term, StringComparison.OrdinalIgnoreCase));
    }
}

public static class ReleaseNotes
{
    public static string CurrentVersion => typeof(ReleaseNotes).Assembly.GetName().Version?.ToString(3) ?? "0.1.2";

    public static IReadOnlyList<ReleaseNote> Load()
    {
        try
        {
            using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("SporeMP.ReleaseNotes.json");
            if (stream is not null)
            {
                var notes = JsonSerializer.Deserialize<ReleaseNote[]>(stream, new JsonSerializerOptions
                {
                    PropertyNameCaseInsensitive = true
                });
                if (notes is { Length: > 0 } && notes.All(note => note is not null &&
                    !string.IsNullOrWhiteSpace(note.Version) && !string.IsNullOrWhiteSpace(note.Title) &&
                    note.Changes is not null))
                    return notes;
            }
        }
        catch (Exception error) when (error is IOException or JsonException) { }
        // Reading optional release notes must never prevent the player from launching.
        return [new(CurrentVersion, "", "Release notes", "Release notes are unavailable for this build.", [])];
    }
}
