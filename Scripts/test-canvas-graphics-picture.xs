'--------------------------------------------------------------------------------
'  XCanvas + XGraphics + XPicture Demo
'--------------------------------------------------------------------------------

' Create our main window
Dim win As New XWindow
win.Title  = "XCanvas / XGraphics / XPicture Demo"
win.Width  = 650
win.Height = 500

'------------------------------------------------------------------------
'  1) Load an image from disk into an XPicture
'------------------------------------------------------------------------
Print("Loaded XPicture Dimensions")
Dim pic As New XPicture
pic.Load("./Scripts/ico.png")    ' replace with a valid path

Print(Str(pic.Width) + ", " + Str(pic.Height))
pic.Save("canvas_snapshot.png")

'------------------------------------------------------------------------
'  2) Create and position an XCanvas
'------------------------------------------------------------------------
Dim canvas As New XCanvas
canvas.Left   = 20
canvas.Top    = 20
canvas.Width  = 600
canvas.Height = 400
canvas.Parent = win.Handle

canvas.Graphics.Antialias = True

' Optional: assign the picture as the canvas backdrop (drawn automatically before Paint)
canvas.Backdrop = pic

'------------------------------------------------------------------------
'  2b) Prepare some polygon points (XPoints)
'------------------------------------------------------------------------
Dim poly As New XPoints
poly.Add(500, 300)
poly.Add(550, 350)
poly.Add(450, 350)

Dim poly2 As New XPoints
poly2.Add(0, 0)
poly2.Add(50, 0)
poly2.Add(25, 50)

'------------------------------------------------------------------------
'  3) Paint + Mouse handlers
'------------------------------------------------------------------------
Function CanvasPaint(g as XGraphics) As Boolean
  g.Antialias = True
  Print("PAINTING")

  g.Clear()

  ' If you set canvas.Backdrop = pic, the plugin already draws the backdrop.
  ' Keeping this is fine (just redundant), or remove it.
  g.DrawPicture(pic, 0, 0, canvas.Width, canvas.Height)

  g.PenSize = 3
  g.DrawingColor = &c0000FF
  g.DrawLine(0, 0, canvas.Width, canvas.Height)

  g.DrawingColor = &cFF0000
  g.DrawRect(50, 50, 150, 100)

  g.DrawingColor = &cFF0000
  g.FillRect(250, 200, 100, 150)

  g.DrawingColor = &cFFFF00
  g.DrawOval(400, 50, 100, 150)

  g.DrawingColor = &c00FF00
  g.FillOval(250, 50, 100, 100)

  g.PenSize = 5
  g.DrawingColor = &c00FFFF
  g.DrawPolygon(poly2)

  g.DrawingColor = &cFF00FF
  g.FillPolygon(poly)

  g.DrawingColor = &c000000
  g.FontName = "Arial"
  g.FontSize = 30
  g.DrawText("Hello, XCanvas!", 20, canvas.Height - 80)

  Return True
End Function

Sub MouseDown(x as Integer, y as Integer)
  Print("MouseDown at x=" + Str(x) + " y=" + Str(y))
End Sub

Sub MouseUp(x as Integer, y as Integer)
  Print("MouseUp at x=" + Str(x) + " y=" + Str(y))
End Sub

Sub MouseMove(x as Integer, y as Integer)
  Print("x=" + Str(x) + " y=" + Str(y))
End Sub

Sub DoubleClick(x as Integer, y as Integer)
  Print("DoubleClicked at x=" + Str(x) + " y=" + Str(y))
End Sub

Sub TerminateApplication()
  Quit()
End Sub

'------------------------------------------------------------------------
'  4) Hook events
'------------------------------------------------------------------------
AddHandler(canvas.Paint, AddressOf(CanvasPaint))
AddHandler(canvas.MouseDown, AddressOf(MouseDown))
AddHandler(canvas.MouseUp, AddressOf(MouseUp))
AddHandler(canvas.MouseMove, AddressOf(MouseMove))
AddHandler(canvas.DoubleClick, AddressOf(DoubleClick))
AddHandler(win.Closing, AddressOf(TerminateApplication))

' Show the window and start the event loop
win.Show()
canvas.Invalidate()

Print("XPicture Canvas.Backdrop Dimensions")
Var gg As XPicture = canvas.Backdrop
Print(Str(gg.Width) + ", " + Str(gg.Height))
gg.Save("canvas_image.png")

Print("XGraphics Canvas.Graphics Dimensions")
Var cg As XGraphics = canvas.Graphics
Print(Str(canvas.Width) + ", " + Str(canvas.Height))
Print(Str(cg.Width) + ", " + Str(cg.Height))
cg.SaveToFile("canvas-graphics.png")

While True
  DoEvents(1)
Wend
