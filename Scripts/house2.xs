'--------------------------------------------------------------------------------
'  XCanvas + XGraphics + Animated Scene Demo (House w/ Roof, Door, Windows)
'--------------------------------------------------------------------------------

' 1) Create our main window
Dim win As New XWindow
win.Title               = "XCanvas / XGraphics - Animated Scene"
win.Width               = 650
win.Height              = 500
win.Resize              = False
win.HasMaximizeButton   = False
win.HasMinimizeButton   = False

AddHandler(win.Closing, AddressOf(TerminateApplication))

' 6) Quit handler
Sub TerminateApplication()
  Quit()
End Sub

' 2) Create and position an XCanvas
Dim canvas As New XCanvas
canvas.Parent              = win.Handle
canvas.Graphics.Antialias  = True
canvas.Left                = 20
canvas.Top                 = 20
canvas.Width               = 600
canvas.Height              = 400

' 3) Scene state variables
Var sunX        As Integer = 0                 ' sun X
Var sunVelX     As Integer = 1                 ' sun horizontal speed

Var cloudX      As Integer = canvas.Width      ' cloud starts off right edge
Var cloudVelX   As Integer = -2                ' cloud drifts left

Var houseX      As Integer = 250               ' house position
Var houseY      As Integer = 220


Dim pic As New XPicture
pic.Load("./Scripts/forest.png")

Dim picSun as New XPicture
picSun.Load("./Scripts/sun.png")

' Helper: draw a window with cross-hatch (center cross)
Sub DrawWindow(g As XGraphics, x As Integer, y As Integer, w As Integer, h As Integer)
  ' frame
  g.PenSize = 3
  g.DrawingColor = &c000000
  g.DrawRect(x, y, w, h)

  ' glass
  g.DrawingColor = &cADD8E6  ' light blue
  g.FillRect(x + 2, y + 2, w - 4, h - 4)

  ' muntins / cross hatch
  g.PenSize = 2
  g.DrawingColor = &c000000
  g.DrawLine(x + w / 2, y, x + w / 2, y + h)     ' vertical
  g.DrawLine(x, y + h / 2, x + w, y + h / 2)     ' horizontal
End Sub

' Helper: draw a simple door
Sub DrawDoor(g As XGraphics, x As Integer, y As Integer, w As Integer, h As Integer)
  ' door fill
  g.DrawingColor = &c654321
  g.FillRect(x, y, w, h)

  ' door outline
  g.PenSize = 3
  g.DrawingColor = &c000000
  g.DrawRect(x, y, w, h)

  ' panels (simple inset rectangles)
  g.PenSize = 2
  g.DrawingColor = &c3A1F0F
  g.DrawRect(x + 8, y + 10, w - 16, (h / 2) - 14)
  g.DrawRect(x + 8, y + (h / 2) + 6, w - 16, (h / 2) - 16)

  ' knob
  g.DrawingColor = &cFFD700
  g.FillOval(x + w - 14, y + (h / 2), 8, 8)
End Sub

' 4) Paint handler
Sub CanvasPaint(g As XGraphics) As Boolean
  g.Antialias = True

  ' — Sky background
  g.DrawingColor = &c87CEEB      ' light-blue
  g.FillRect(0, 0, g.Width, g.Height)


  g.DrawPicture(pic, 0, 0, g.width, g.height)

  ' — Grass ground
  g.DrawingColor = &c2E8B57      ' sea green
  g.FillRect(0, 330, g.Width, g.Height - 330)

  ' — Sun
  'g.DrawingColor = &cFFFF00      ' yellow
  'g.FillOval(sunX, 30, 60, 60)
  g.DrawPicture(picSun, sunX, 30, 60, 60)

  ' — Cloud (two overlapping ellipses)
  g.DrawingColor = &cFFFFFF      ' white
  g.FillOval(cloudX,    50, 80, 40)
  g.FillOval(cloudX+40, 40, 80, 50)

  ' — House walls
  g.DrawingColor = &c8B4513      ' brown
  g.FillRect(houseX, houseY, 250, 150)

  ' — House wall outline
  g.PenSize = 3
  g.DrawingColor = &c000000
  g.DrawRect(houseX, houseY, 250, 150)

  ' — Roof (filled triangle)  (USES XPoints)
  g.DrawingColor = &cB22222      ' firebrick red
  Dim roof As New XPoints
  roof.Add(houseX,       houseY)
  roof.Add(houseX + 125, houseY - 100)
  roof.Add(houseX + 250, houseY)
  g.FillPolygon(roof)

  ' — Roof outline
  g.PenSize = 3
  g.DrawingColor = &c000000
  Dim roofEdge As New XPoints
  roofEdge.Add(houseX,       houseY)
  roofEdge.Add(houseX + 125, houseY - 100)
  roofEdge.Add(houseX + 250, houseY)
  g.DrawPolygon(roofEdge)

  ' — Door
  Dim doorW As Integer = 55
  Dim doorH As Integer = 90
  Dim doorX As Integer = houseX + (150 - doorW) \ 2
  Dim doorY As Integer = houseY + 150 - doorH
  DrawDoor(g, doorX, doorY, doorW, doorH)

  ' — Windows (with cross hatch)
  DrawWindow(g, houseX + 25,  houseY + 35, 55, 45)
  DrawWindow(g, houseX + 170, houseY + 35, 55, 45)

  Return True
End Sub

' 5) Timer to drive animation
Sub TimerFired()
  ' Move sun; wrap when off right edge
  sunX = sunX + sunVelX
  If (sunX > canvas.Width) Then 
    sunX = -60
  End If

  ' Move cloud; wrap when off left edge
  cloudX = cloudX + cloudVelX
  If (cloudX < -200) Then 
    cloudX = 600
  End If


  canvas.Invalidate()  ' trigger repaint
End Sub

' Show window and start
win.Show()

Var t As New XTimer
t.Period  = 16
t.RunMode = 2

' Wire up events
AddHandler(canvas.Paint, AddressOf(CanvasPaint))
AddHandler(t.Action,     AddressOf(TimerFired))

t.Enabled = True
canvas.Invalidate()

' Keep app alive
While True
  DoEvents(1)
Wend
