' ——— MainWindow.cls ———
Class MainWindow Inherits XWindow

  Sub Open()
    ' 1) Create and parent the XamlContainer
    Var xc As New XamlContainer
    xc.Left   = 20
    xc.Top    = 20
    xc.Width  = 400
    xc.Height = 300
    xc.Parent = Me.Handle

    ' 2) Load some XAML with a named Button
    Var xamlMarkup As String = _
      "<Grid xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'>" + _
        "<Button x:Name='MyButton' Content='Press Me' Width='100' Height='30' />" + _
      "</Grid>"
    xc.Xaml = xamlMarkup

    ' 3) Dynamically hook its Click
    xc.AddXamlEvent("MyButton", "Click", AddressOf OnMyBtnClick)
  End Sub

  Sub OnMyBtnClick(data As String)
    MessageBox("You clicked MyButton!")
  End Sub

End Class


' ——— App/Main.bas ———
Sub Main()
  Var w As New MainWindow("XAML Demo", 600, 450)
  w.Show
End Sub
