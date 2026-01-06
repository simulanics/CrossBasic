'--------------------------------------------------------------------------------
'  XCanvas + XGraphics + Animated Scene Demo (House w/ Roof, Door, Windows)
'  Updates:
'   - Multiple clouds (different shapes/sizes/speeds) using a DrawCloud helper
'   - Sun moves on an arc (parametric angle) instead of straight across
'   - FIX: Clouds wrap using real drawn width per shape/scale (no more disappearing)
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
' --- Sun arc motion ---
Var sunAngle    As Double  = -3.141592653589793 / 2.0
Var sunVelAng   As Double  = 0.008
Var sunCX       As Integer = 300
Var sunCY       As Integer = 320
Var sunR        As Integer = 260
Var sunW        As Integer = 60
Var sunH        As Integer = 60

' --- Clouds ---
Var cloud1X     As Integer = 650
Var cloud1Y     As Integer = 55
Var cloud1VelX  As Integer = -2
Var cloud1Scale As Double  = 1.00
Var cloud1Shape As Integer = 1

Var cloud2X     As Integer = 200
Var cloud2Y     As Integer = 85
Var cloud2VelX  As Integer = -1
Var cloud2Scale As Double  = 0.75
Var cloud2Shape As Integer = 2

Var cloud3X     As Integer = 950
Var cloud3Y     As Integer = 35
Var cloud3VelX  As Integer = -3
Var cloud3Scale As Double  = 0.60
Var cloud3Shape As Integer = 3

Var houseX      As Integer = 250
Var houseY      As Integer = 220

Dim pic As New XPicture
pic.Load("./Scripts/forest.png")

Dim picSun As New XPicture
picSun.Load("./Scripts/sun.png")

'--------------------------
' Helpers: house parts
'--------------------------
Sub DrawWindow(g As XGraphics, x As Integer, y As Integer, w As Integer, h As Integer)
  g.PenSize = 3
  g.DrawingColor = &c000000
  g.DrawRect(x, y, w, h)

  g.DrawingColor = &cADD8E6
  g.FillRect(x + 2, y + 2, w - 4, h - 4)

  g.PenSize = 2
  g.DrawingColor = &c000000
  g.DrawLine(x + w / 2, y, x + w / 2, y + h)
  g.DrawLine(x, y + h / 2, x + w, y + h / 2)
End Sub

Sub DrawDoor(g As XGraphics, x As Integer, y As Integer, w As Integer, h As Integer)
  g.DrawingColor = &c654321
  g.FillRect(x, y, w, h)

  g.PenSize = 3
  g.DrawingColor = &c000000
  g.DrawRect(x, y, w, h)

  g.PenSize = 2
  g.DrawingColor = &c3A1F0F
  g.DrawRect(x + 8, y + 10, w - 16, (h / 2) - 14)
  g.DrawRect(x + 8, y + (h / 2) + 6, w - 16, (h / 2) - 16)

  g.DrawingColor = &cFFD700
  g.FillOval(x + w - 14, y + (h / 2), 8, 8)
End Sub

'--------------------------
' Helpers: clouds
'--------------------------

' Return the approximate max X extent (width) of each cloud shape at scale=1.0.
' This MUST match the largest right-side lobe offset + lobe width in DrawCloud.
Function CloudWidth(shapeId As Integer, scale As Double) As Integer
  Dim baseW As Integer

  If shapeId = 1 Then
    ' Right-most lobe: offset 85 + width 55 = 140
    baseW = 140
  ElseIf shapeId = 2 Then
    ' Right-most lobe: offset 85 + width 80 = 165
    baseW = 165
  Else
    ' Right-most lobe: offset 95 + width 40 = 135
    baseW = 135
  End If

  Return baseW * scale
End Function

' Wrap a cloud that moves left so it respawns off the right edge.
Sub WrapCloud(ByRef x As Integer, velX As Integer, shapeId As Integer, scale As Double, canvasW As Integer, respawnPad As Integer)
  Dim w As Integer = CloudWidth(shapeId, scale)

  ' If entirely off the left side, move it off the right side with padding.
  If x < -w Then
    x = canvasW + respawnPad
  End If
End Sub

Sub DrawCloud(g As XGraphics, baseX As Integer, baseY As Integer, scale As Double, shapeId As Integer)
  g.DrawingColor = &cFFFFFF

  If shapeId = 1 Then
    g.FillOval(baseX +  0 * scale, baseY + 18 * scale, 55 * scale, 35 * scale)
    g.FillOval(baseX + 25 * scale, baseY +  5 * scale, 60 * scale, 45 * scale)
    g.FillOval(baseX + 60 * scale, baseY + 10 * scale, 70 * scale, 40 * scale)
    g.FillOval(baseX + 85 * scale, baseY + 22 * scale, 55 * scale, 33 * scale)

    g.PenSize = 2
    g.DrawingColor = &cE6E6E6
    g.DrawOval(baseX +  0 * scale, baseY + 18 * scale, 55 * scale, 35 * scale)
    g.DrawOval(baseX + 25 * scale, baseY +  5 * scale, 60 * scale, 45 * scale)
    g.DrawOval(baseX + 60 * scale, baseY + 10 * scale, 70 * scale, 40 * scale)
    g.DrawOval(baseX + 85 * scale, baseY + 22 * scale, 55 * scale, 33 * scale)

  ElseIf shapeId = 2 Then
    g.DrawingColor = &cFFFFFF
    g.FillOval(baseX +  0 * scale, baseY + 22 * scale, 70 * scale, 32 * scale)
    g.FillOval(baseX + 35 * scale, baseY + 10 * scale, 85 * scale, 45 * scale)
    g.FillOval(baseX + 85 * scale, baseY + 22 * scale, 80 * scale, 32 * scale)

    g.PenSize = 2
    g.DrawingColor = &cE6E6E6
    g.DrawOval(baseX +  0 * scale, baseY + 22 * scale, 70 * scale, 32 * scale)
    g.DrawOval(baseX + 35 * scale, baseY + 10 * scale, 85 * scale, 45 * scale)
    g.DrawOval(baseX + 85 * scale, baseY + 22 * scale, 80 * scale, 32 * scale)

  Else
    g.DrawingColor = &cFFFFFF
    g.FillOval(baseX +  0 * scale, baseY + 24 * scale, 40 * scale, 24 * scale)
    g.FillOval(baseX + 20 * scale, baseY + 14 * scale, 45 * scale, 32 * scale)
    g.FillOval(baseX + 45 * scale, baseY +  8 * scale, 50 * scale, 36 * scale)
    g.FillOval(baseX + 75 * scale, baseY + 14 * scale, 45 * scale, 32 * scale)
    g.FillOval(baseX + 95 * scale, baseY + 24 * scale, 40 * scale, 24 * scale)

    g.PenSize = 2
    g.DrawingColor = &cE6E6E6
    g.DrawOval(baseX +  0 * scale, baseY + 24 * scale, 40 * scale, 24 * scale)
    g.DrawOval(baseX + 20 * scale, baseY + 14 * scale, 45 * scale, 32 * scale)
    g.DrawOval(baseX + 45 * scale, baseY +  8 * scale, 50 * scale, 36 * scale)
    g.DrawOval(baseX + 75 * scale, baseY + 14 * scale, 45 * scale, 32 * scale)
    g.DrawOval(baseX + 95 * scale, baseY + 24 * scale, 40 * scale, 24 * scale)
  End If
End Sub

' 4) Paint handler
Sub CanvasPaint(g As XGraphics) As Boolean
  g.Antialias = True

  ' — Sky background
  g.DrawingColor = &c87CEEB
  g.FillRect(0, 0, g.Width, g.Height)

  ' Background picture
  g.DrawPicture(pic, 0, 0, g.Width, g.Height)

  ' --- Sun position on an arc ---
  Dim sx As Integer = sunCX + (sunR * Cos(sunAngle)) - (sunW / 2)
  Dim sy As Integer = sunCY - (sunR * Sin(sunAngle)) - (sunH / 2)
  g.DrawPicture(picSun, sx, sy, sunW, sunH)

  ' --- Clouds ---
  DrawCloud(g, cloud1X, cloud1Y, cloud1Scale, cloud1Shape)
  DrawCloud(g, cloud2X, cloud2Y, cloud2Scale, cloud2Shape)
  DrawCloud(g, cloud3X, cloud3Y, cloud3Scale, cloud3Shape)

  ' — Grass ground (draw AFTER sun so it looks like horizon)
  g.DrawingColor = &c2E8B57
  g.FillRect(0, 330, g.Width, g.Height - 330)

  ' — House walls
  g.DrawingColor = &c8B4513
  g.FillRect(houseX, houseY, 250, 150)

  g.PenSize = 3
  g.DrawingColor = &c000000
  g.DrawRect(houseX, houseY, 250, 150)

  ' — Roof
  g.DrawingColor = &cB22222
  Dim roof As New XPoints
  roof.Add(houseX,       houseY)
  roof.Add(houseX + 125, houseY - 100)
  roof.Add(houseX + 250, houseY)
  g.FillPolygon(roof)

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

  ' — Windows
  DrawWindow(g, houseX + 25,  houseY + 35, 55, 45)
  DrawWindow(g, houseX + 170, houseY + 35, 55, 45)

  Return True
End Sub

' 5) Timer to drive animation
Sub TimerFired()
  ' Sun along arc
  sunAngle = sunAngle + sunVelAng
  If sunAngle > 3.141592653589793 Then
    sunAngle = -3.141592653589793
  End If

  ' Move clouds
  cloud1X = cloud1X + cloud1VelX
  cloud2X = cloud2X + cloud2VelX
  cloud3X = cloud3X + cloud3VelX

  ' Wrap clouds using computed widths (THIS FIXES THE DISAPPEARING)
  WrapCloud(cloud1X, cloud1VelX, cloud1Shape, cloud1Scale, canvas.Width, 40)
  WrapCloud(cloud2X, cloud2VelX, cloud2Shape, cloud2Scale, canvas.Width, 120)
  WrapCloud(cloud3X, cloud3VelX, cloud3Shape, cloud3Scale, canvas.Width, 220)

  canvas.Invalidate()
End Sub

' Show window and start
win.Show()

Var t As New XTimer
t.Period  = 16
t.RunMode = 2

AddHandler(canvas.Paint, AddressOf(CanvasPaint))
AddHandler(t.Action,     AddressOf(TimerFired))

t.Enabled = True
canvas.Invalidate()

While True
  DoEvents(1)
Wend
