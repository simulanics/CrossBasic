Dim pic As New XPicture(320,240,32)
pic.Load("./Scripts/ico.png")

Var win As New XWindow
win.Width  = 680
win.Height = 550

AddHandler(win.Closing, AddressOf(AppClosing))

Sub AppClosing()
  Quit()
End Sub

Var canvas As New XCanvas
canvas.Left   = 10
canvas.Top    = 10
canvas.Width  = 640
canvas.Height = 480
canvas.Parent = win.Handle         // creates the HWND and back-buffer

win.Show()

print str(pic.width)
print str(pic.height)

AddHandler(canvas.Paint, AddressOf(PaintHandler))

Function PaintHandler(g As XGraphics) as Boolean
  g.clear()
  g.DrawingColor = &cFF0000
  g.PenSize = 3
  g.DrawLine(0, 0, g.Width, g.Height)
  g.DrawPicture(pic, 0, 0, g.width, g.height)
  return true
End Function

canvas.Invalidate()

While True
    DoEvents(10)
Wend
