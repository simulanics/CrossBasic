// XWebViewDemo.cb
// A simple CrossBasic demo using the XWebView plugin:
// - Shows a window with Back, Forward, Reload, URL field and Go button
// - Embeds a WebView filling most of the window

// Create the main window
Var win As New XWindow
win.Title = "XWebView Demo"
'win.HasMaximizeButton = False
'win.Resizable = False
win.Width   = 1200
win.Height  = 1000
AddHandler(win.Closing, AddressOf(AppClosing))

Sub AppClosing()
	Quit()
End Sub

// Create the WebView control
Var webview As New XWebView
webview.Parent = win.Handle
webview.Left   = 0
webview.Top    = 60
webview.Width  = win.Width - 16
webview.Height = win.Height - 100
webview.locktop = true
webview.lockbottom = true
webview.lockright = true
webview.lockleft = true


// Back button
Var btnBack As New XButton
btnBack.Parent  = win.Handle
btnBack.FontName = "Arial"
btnBack.FontSize = 16
btnBack.Caption = "<"
btnBack.Left    = 10
btnBack.Top     = 10
btnBack.Width   = 32
btnBack.Height  = 32
AddHandler(btnBack.Pressed, AddressOf(GoBack))

Sub GoBack()
	Print("GoBack Pressed")
	webview.GoBack()
End Sub

// Forward button
Var btnForward As New XButton
btnForward.Parent  = win.Handle
btnForward.FontName = "Arial"
btnForward.FontSize = 16
btnForward.Caption = ">"
btnForward.Left    = 50
btnForward.Top     = 10
btnForward.Width   = 32
btnForward.Height  = 32

AddHandler(btnForward.Pressed, AddressOf(GoForward))

Sub GoForward()
	Print("GoForward Pressed")
	webview.GoForward()
End Sub

// Reload button
Var btnReload As New XButton
btnReload.Parent  = win.Handle
btnReload.FontName = "Arial"
btnReload.FontSize = 16
btnReload.Caption = "@"
btnReload.Left    = 90
btnReload.Top     = 10
btnReload.Width   = 32
btnReload.Height  = 32
AddHandler(btnReload.Pressed, AddressOf(RefreshWebView))

Sub RefreshWebView()
	Print("Refresh Pressed")
	webview.Refresh()
End Sub

// URL text field
Var txtURL As New XTextField
txtURL.Parent = win.Handle
txtURL.FontName = "Arial"
txtURL.FontSize = 20
txtURL.Left   = 130
txtURL.Top    = 10
txtURL.Width  = win.Width - 260
txtURL.Height = 32
txtURL.lockright = true
txtURL.lockleft = true
txtURL.Text   = "https://ide.crossbasic.com"
txtURL.TextColor = &cFFFFFF


// Go button
Var btnGo As New XButton
btnGo.Parent  = win.Handle
btnGo.FontName = "Arial"
btnGo.FontSize = 16
btnGo.Caption = "Go"
btnGo.Left    = win.Width - 120
btnGo.Top     = 10
btnGo.Width   = 32
btnGo.Height  = 32
btnGo.lockright = true
AddHandler(btnGo.Pressed, AddressOf(LoadPage))
  
Sub LoadPage()
	Print("Load Page Pressed")
	webview.LoadURL(txtURL.Text)
End Sub

// JS button
Var btnJS As New XButton
btnJS.Parent  = win.Handle
btnJS.FontName = "Arial"
btnJS.FontSize = 16
btnJS.Caption = "JS"
btnJS.Left    = win.Width - 60
btnJS.Top     = 10
btnJS.Width   = 32
btnJS.Height  = 32
btnJS.lockright = true
AddHandler(btnJS.Pressed, AddressOf(RunJS))

Var jsFunction As String = "function calculateExpression(expression){try{if(!/^[0-9+\-*/().\s^%]+$/.test(expression)){throw new Error('Invalid characters in expression.');}expression=expression.replace(/\^/g,'**');const result=new Function(`return (${expression});`)();if(isNaN(result)||!isFinite(result)){throw new Error('Invalid result.');}return result;}catch(error){return `Error: ${error.message}`;}}"


Sub RunJS()
	
	var content as String = webview.ExecuteJavascriptSync(jsFunction + " return calculateExpression('3 + 4 * 2');")
	print("ExecuteJavaScriptSync Returned: " + content)

	'Following line runs as expected.
	webview.ExecuteJavascript("alert('3 + 4 *2 = ' + '" + content + "');")

End Sub

// Show the window modally
win.Show()

webview.LoadURL(txtURL.Text)

While True
	DoEvents(1)
Wend