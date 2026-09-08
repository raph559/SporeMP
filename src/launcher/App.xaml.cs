using System.IO;
using System.Windows;
using System.Runtime.InteropServices;

namespace SporeMP.Launcher;

public partial class App : Application
{
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern int SetCurrentProcessExplicitAppUserModelID(string appID);
    protected override void OnStartup(StartupEventArgs e)
    {
        SetCurrentProcessExplicitAppUserModelID("SporeMP.Desktop");
        base.OnStartup(e);
        try
        {
            var client = LauncherClient.FromConfiguration(Path.Combine(AppContext.BaseDirectory, "launcher.runtime.json"));
            MainWindow = new MainWindow(client);
            MainWindow.Show();
        }
        catch (Exception error)
        {
            MessageBox.Show("SporeMP could not open its local tools.\n\n" + error.Message +
                "\n\nRun tools/build/build-launcher.ps1 from the source checkout to rebuild the launcher configuration.",
                "SporeMP launcher setup", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(2);
        }
    }
}
