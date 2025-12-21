'#Application

Var NewView As New XWindow
NewView.Width = 375
NewView.Height = 400
NewView.BackgroundColor = &c1a1a1a
NewView.ViewType = 0
NewView.HasCloseButton = True
NewView.HasMinimizeButton = False
NewView.HasMaximizeButton = False
'NewView.HasFullScreenButton = False
NewView.HasTitleBar = True
NewView.Resizable = False

AddHandler(NewView.Closing, AddressOf(NewView_Closing))

Sub NewView_Closing()
	Quit()
End Sub

Var myButton As New XButton
myButton.Parent = NewView.Handle
myButton.Left = 50
myButton.Top = 50
myButton.Width = 100
myButton.Height = 42
myButton.Caption = "Wow!"
myButton.TextColor = &cffffff
myButton.Bold = False
myButton.Underline = False
myButton.Italic = False
myButton.FontName = "Arial"
myButton.FontSize = 12
myButton.Enabled = True
myButton.Visible = True

AddHandler(myButton.Pressed, AddressOf(myButton_Pressed))


Sub myButton_Pressed()

'Example Event Handler

Messagebox("Hello " + UserName + ", from CrossBasic!")
End Sub

var Username as String = "User"


// Show the window//////
NewView.Show()


While True
	DoEvents(1)
Wend

