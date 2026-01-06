// XamlContainerDemo.cb
// A simple CrossBasic demo using the XamlContainer plugin:
// – Shows a window with a XAML Button loaded at runtime
// – Hooks the Button’s Click via AddXamlEvent

// Create the main window
Var win As New XWindow
win.Title            = "XAML Container Demo"
win.HasMaximizeButton = False
win.Resizable        = False
win.Width            = 600
win.Height           = 400
AddHandler(win.Closing, AddressOf(AppClosing) )

Sub AppClosing()
  Quit()
End Sub


// Create the XAML container
Var xc As New XamlContainer
'xc.Parent = win.Handle
xc.Left   = 20
xc.Top    = 60
xc.Width  = win.Width - 40
xc.Height = win.Height - 100

// Load XAML with a named Button in the center
'Var xamlMarkup As String = "<Grid xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' HorizontalAlignment='Stretch' VerticalAlignment='Stretch'><Button x:Name='MyButton' Content='Press Me' Width='120' Height='40' HorizontalAlignment='Center' VerticalAlignment='Center'/></Grid>"
'xc.Xaml = xamlMarkup

// Hook its Click dynamically
'xc.AddXamlEvent("MyButton", "Click", AddressOf(OnMyBtnClick))

Sub OnMyBtnClick(data As String)
  MessageBox("You clicked the XAML button!")
End Sub

// Show the window and run the event loop
win.Show()

While True
  DoEvents(1)
Wend
