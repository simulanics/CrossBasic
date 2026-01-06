'' IDE.xs

'' Start the CrossBasic Web Application Server
Declare Function ShellExecuteA Lib "Shell32.dll" Alias "ShellExecuteA"(hwnd As Ptr, lpOperation As String, lpFile As String, lpParameters As String, lpDirectory As String, nShowCmd As Integer) As Ptr

Const SW_HIDE As Integer = 0  ' hide the window
Const SW_SHOWNORMAL As Integer = 1

Dim result As Ptr = ShellExecuteA(0, "open", "server.exe", "", "", SW_HIDE)


'' Create the main window
Var win As New XWindow
win.Title = "CrossBasic IDE"
win.Resizable = True
win.Width   = 1150
win.Height  = 900
win.SetIcon("ico.png")
AddHandler(win.Closing, AddressOf(AppClosing))

Sub AppClosing()
    var cntShell as new Shell
    cntShell.Timeout = -1
    cntShell.Execute("taskkill /f /im server.exe")
	Quit()
End Sub

'' Create the WebView control to house the IDE
Var webview As New XWebView
webview.Parent = win.Handle
webview.Left   = 0
webview.Top    = 0
webview.Width  = win.Width - 16
webview.Height = win.Height - 40
webview.locktop = true
webview.lockbottom = true
webview.lockright = true
webview.lockleft = true

'' Show the IDE
webview.LoadURL("http://localhost:8080")

'' Show the window
win.Show()


'' Keep the app alive and process events
While True
	DoEvents(1)
Wend