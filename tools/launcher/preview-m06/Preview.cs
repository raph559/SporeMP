using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using System.Xml.Linq;
using SporeMP.Launcher;

internal static class Preview
{
    [STAThread]
    private static void Main(string[] args)
    {
        string repo = Path.GetFullPath(args[0]), output = Path.GetFullPath(args[1]);
        if (Directory.Exists(output)) throw new IOException("Use a fresh preview directory.");
        Directory.CreateDirectory(output);
        string fixture = Path.Combine(output, "fixture");
        Directory.CreateDirectory(Path.Combine(fixture, "tools/launcher"));
        var configuration = LauncherClient.FromConfiguration(Path.Combine(repo, "build/launcher/Release/launcher.runtime.json"));
        File.WriteAllText(Path.Combine(fixture, "tools/launcher/launcher_service.py"), """
import json,pathlib,sys,time
assert sys.argv[1]=='native_join', 'This fixture refuses every other operation.'
invite=sys.stdin.read(2049)
assert '&token=' in invite
print(json.dumps({'kind':'sporemp-launcher-progress','phase':'connected','message':'Fixture connected'}),flush=True)
deadline=time.monotonic()+15
while not pathlib.Path('release-session').exists():
    if time.monotonic()>deadline: raise RuntimeError('Preview fixture deadline')
    time.sleep(.02)
print(json.dumps({'schema_version':1,'kind':'sporemp-launcher-operation','native_tests':'NOT_RUN','operation':'native_join','exit_code':0,'report_path':'fixture.json','result':{'authenticated':True,'connected_baseline_observed':True}}),flush=True)
""");
        var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
        XNamespace presentation = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
        var appXaml = XDocument.Load(Path.Combine(repo, "src/launcher/App.xaml"));
        app.Resources = (ResourceDictionary)XamlReader.Parse(new XElement(presentation + "ResourceDictionary",
            new XAttribute("xmlns", presentation.NamespaceName),
            new XAttribute(XNamespace.Xmlns + "x", "http://schemas.microsoft.com/winfx/2006/xaml"),
            appXaml.Root!.Element(presentation + "Application.Resources")!.Elements()).ToString());
        SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
        var errors = new StringBuilder();
        PresentationTraceSources.DataBindingSource.Listeners.Add(new TextWriterTraceListener(new StringWriter(errors)));
        Dispatcher.CurrentDispatcher.InvokeAsync(async () =>
        {
            int code = 1;
            try
            {
                var window = new MainWindow(new LauncherClient(fixture, configuration.PythonExecutable));
                void SetField(string name, object value) => typeof(MainWindow).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(window, value);
                SetField("busy", false); SetField("nativeAvailable", true); SetField("joinAvailable", true); SetField("state", "development_build");
                typeof(MainWindow).GetProperty("GameRoot")!.SetValue(window, "C:/Fixture/SPORE");
                typeof(MainWindow).GetProperty("GameName")!.SetValue(window, "SPORE · GOG (fixture)");
                typeof(MainWindow).GetProperty("StatusTitle")!.SetValue(window, "Ready to play.");
                typeof(MainWindow).GetProperty("StatusDetail")!.SetValue(window, "SPORE · GOG (fixture)");
                var password = (PasswordBox)window.FindName("InvitationBox");
                password.Password = "sporemp://join?host=127.0.0.1&port=27060&cert=" + new string('a',64) + "&token=" + new string('b',64);
                var join = (Button)window.FindName("JoinButton");
                var play = (Button)window.FindName("PrimaryButton");
                var root = (FrameworkElement)window.Content;
                ((Border)root).Background = window.Background;
                var home = (ScrollViewer)window.FindName("HomePage");
                async Task Settle() => await Dispatcher.CurrentDispatcher.InvokeAsync(() => { }, DispatcherPriority.ContextIdle);
                void Render(string name, int width, int height, double scroll = 0)
                {
                    root.Measure(new Size(width,height)); root.Arrange(new Rect(0,0,width,height)); root.UpdateLayout();
                    home.ScrollToVerticalOffset(scroll); root.UpdateLayout();
                    var bitmap = new RenderTargetBitmap(width,height,96,96,PixelFormats.Pbgra32); bitmap.Render(root);
                    var png = new PngBitmapEncoder(); png.Frames.Add(BitmapFrame.Create(bitmap));
                    using var stream = new FileStream(Path.Combine(output,name), FileMode.CreateNew); png.Save(stream);
                }
                await Settle();
                if (!join.IsEnabled || !play.IsEnabled) throw new Exception("Ready Join and Play binding failed.");
                Render("home-1280.png",1280,820);
                Render("join-1060.png",1060,700,360);
                join.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                for (int i=0;i<150 && !window.Multiplayer.IsConnected;i++) await Task.Delay(20);
                await Settle();
                if (!window.Multiplayer.IsConnected || join.IsEnabled || play.IsEnabled || password.IsEnabled)
                    throw new Exception("Actual Join button did not establish expected fixture controls.");
                Render("connected-1060.png",1060,700,360);
                File.WriteAllText(Path.Combine(fixture,"release-session"),"complete fixture");
                for (int i=0;i<150 && window.Multiplayer.IsActive;i++) await Task.Delay(20);
                await Settle();
                if (!join.IsEnabled || window.Multiplayer.ButtonLabel != "Rejoin server") throw new Exception("Rejoin binding failed.");
                Render("rejoin-1060.png",1060,700,360);
                File.Delete(Path.Combine(fixture,"release-session"));
                join.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                for (int i=0;i<150 && !window.Multiplayer.IsConnected;i++) await Task.Delay(20);
                bool closed = false;
                window.Closed += (_,_) => closed = true;
                window.Close();
                if (closed || !window.Multiplayer.IsActive) throw new Exception("Closing abandoned the active fixture session.");
                File.WriteAllText(Path.Combine(fixture,"release-session"),"finish after frontend hides");
                for (int i=0;i<150 && !closed;i++) await Task.Delay(20);
                if (!closed || errors.Length != 0) throw new Exception("Close completion/bindings failed: " + errors);
                File.WriteAllText(Path.Combine(output,"ui-checks.json"),JsonSerializer.Serialize(new {
                    evidence_class="HOST_OFFSCREEN_WPF_WITH_FIXTURE_SERVICE",native_game="NOT_RUN",desktop_input=false,
                    actual_join_button=true,credential_masked=true,connected_controls=true,rejoin_same_invitation=true,
                    close_waits_for_session=true,binding_errors=errors.ToString(),rendered_sizes=new[]{"1280x820","1060x700"}
                },new JsonSerializerOptions{WriteIndented=true}));
                Console.WriteLine("Actual offscreen WPF Join/Rejoin/close lifecycle passed; native game NOT RUN.");
                code = 0;
            }
            catch(Exception error) { Console.Error.WriteLine(error); }
            finally { Environment.Exit(code); }
        });
        Dispatcher.Run();
    }
}
