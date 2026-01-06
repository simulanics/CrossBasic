'--------------------------------------------------------------------------------
'  XCanvas Draggable + Resizable Rectangle Demo (White Background)
'  - Drag the rectangle by clicking and dragging inside it
'  - Resize using the 4 corner handles
'--------------------------------------------------------------------------------

' Create our main window
Dim win As New XWindow
win.Title  = "XCanvas Drag + Resize Demo"
win.Width  = 700
win.Height = 550

' Create and position an XCanvas
Dim canvas As New XCanvas
canvas.Left   = 20
canvas.Top    = 20
canvas.Width  = 640
canvas.Height = 460
canvas.Parent = win.Handle
canvas.Graphics.Antialias = True

'--------------------------------------------------------------------------------
' Rectangle state
'--------------------------------------------------------------------------------
Const HANDLE_SIZE As Integer = 12
Const MIN_W As Integer = 40
Const MIN_H As Integer = 40

Dim rectX As Integer = 200
Dim rectY As Integer = 140
Dim rectW As Integer = 220
Dim rectH As Integer = 140

Dim isDragging As Boolean = False
Dim dragOffsetX As Integer = 0
Dim dragOffsetY As Integer = 0

Dim isResizing As Boolean = False
Dim resizeCorner As Integer = -1   ' 0=TL, 1=TR, 2=BL, 3=BR

Dim startMouseX As Integer = 0
Dim startMouseY As Integer = 0
Dim startRectX As Integer = 0
Dim startRectY As Integer = 0
Dim startRectW As Integer = 0
Dim startRectH As Integer = 0

'--------------------------------------------------------------------------------
' Helpers
'--------------------------------------------------------------------------------
Function Clamp(v As Integer, mn As Integer, mx As Integer) As Integer
  If v < mn Then Return mn
  If v > mx Then Return mx
  Return v
End Function

Function PointInRect(px As Integer, py As Integer, rx As Integer, ry As Integer, rw As Integer, rh As Integer) As Boolean
  If px < rx Then Return False
  If py < ry Then Return False
  If px > rx + rw Then Return False
  If py > ry + rh Then Return False
  Return True
End Function

Function CornerHitTest(px As Integer, py As Integer) As Integer
  ' Returns corner index if mouse is on a handle square, else -1

  Dim hs As Integer = HANDLE_SIZE
  Dim half As Integer = hs / 2

  ' Corner centers
  Dim tlx As Integer = rectX
  Dim tly As Integer = rectY

  Dim trx As Integer = rectX + rectW
  Dim tryy As Integer = rectY

  Dim blx As Integer = rectX
  Dim bly As Integer = rectY + rectH

  Dim brx As Integer = rectX + rectW
  Dim bry As Integer = rectY + rectH

  ' Top-left
  If PointInRect(px, py, tlx - half, tly - half, hs, hs) Then Return 0
  ' Top-right
  If PointInRect(px, py, trx - half, tryy - half, hs, hs) Then Return 1
  ' Bottom-left
  If PointInRect(px, py, blx - half, bly - half, hs, hs) Then Return 2
  ' Bottom-right
  If PointInRect(px, py, brx - half, bry - half, hs, hs) Then Return 3

  Return -1
End Function

Sub NormalizeAndClampRect()
  ' Enforce minimum sizes and keep within canvas bounds.

  If rectW < MIN_W Then rectW = MIN_W
  If rectH < MIN_H Then rectH = MIN_H

  Dim maxX As Integer = canvas.Width - rectW
  Dim maxY As Integer = canvas.Height - rectH

  If maxX < 0 Then
    rectX = 0
    rectW = canvas.Width
  Else
    rectX = Clamp(rectX, 0, maxX)
  End If

  If maxY < 0 Then
    rectY = 0
    rectH = canvas.Height
  Else
    rectY = Clamp(rectY, 0, maxY)
  End If
End Sub

'--------------------------------------------------------------------------------
' Paint handler
'--------------------------------------------------------------------------------
Function CanvasPaint(g As XGraphics) As Boolean
  g.Antialias = True

  ' White background (safe: explicitly fill)
  g.DrawingColor = &cFFFFFF
  g.FillRect(0, 0, canvas.Width, canvas.Height)

  ' Draw main rectangle (light gray fill + black border)
  g.DrawingColor = &cE6E6E6
  g.FillRect(rectX, rectY, rectW, rectH)

  g.PenSize = 2
  g.DrawingColor = &c000000
  g.DrawRect(rectX, rectY, rectW, rectH)

  ' Draw corner handles
  Dim hs As Integer = HANDLE_SIZE
  Dim half As Integer = hs / 2

  Dim tlx As Integer = rectX + hs
  Dim tly As Integer = rectY + hs

  Dim trx As Integer = rectX + rectW
  Dim tryy As Integer = rectY

  Dim blx As Integer = rectX
  Dim bly As Integer = rectY + rectH

  Dim brx As Integer = rectX + rectW
  Dim bry As Integer = rectY + rectH

  ' Handle fill
  g.DrawingColor = &c444444
  g.FillRect(tlx - half, tly - half, hs, hs)
  g.FillRect(trx - half, tryy - half, hs, hs)
  g.FillRect(blx - half, bly - half, hs, hs)
  g.FillRect(brx - half, bry - half, hs, hs)

  ' Handle border
  g.PenSize = 1
  g.DrawingColor = &c000000
  g.DrawRect(tlx - half, tly - half, hs, hs)
  g.DrawRect(trx - half, tryy - half, hs, hs)
  g.DrawRect(blx - half, bly - half, hs, hs)
  g.DrawRect(brx - half, bry - half, hs, hs)

  Return True
End Function

'--------------------------------------------------------------------------------
' Mouse handlers
'--------------------------------------------------------------------------------
Sub MouseDown(x As Integer, y As Integer)
  ' First: check corner resize handles
  Dim c As Integer = CornerHitTest(x, y)
  If c <> -1 Then
    isResizing = True
    resizeCorner = c

    startMouseX = x
    startMouseY = y
    startRectX = rectX
    startRectY = rectY
    startRectW = rectW
    startRectH = rectH

    
  End If

  ' Else: check dragging inside the rectangle
  If PointInRect(x, y, rectX, rectY, rectW, rectH) Then
    isDragging = True
    dragOffsetX = x - rectX
    dragOffsetY = y - rectY
    
  End If
End Sub

Sub MouseUp(x As Integer, y As Integer)
  isDragging = False
  isResizing = False
  resizeCorner = -1
End Sub

Sub MouseMove(x As Integer, y As Integer)
  If isDragging Then
    rectX = x - dragOffsetX
    rectY = y - dragOffsetY
    NormalizeAndClampRect()
    canvas.Invalidate()
    
  End If

  If isResizing Then
    Dim dx As Integer = x - startMouseX
    Dim dy As Integer = y - startMouseY

    Dim newX As Integer = startRectX
    Dim newY As Integer = startRectY
    Dim newW As Integer = startRectW
    Dim newH As Integer = startRectH

    ' 0=TL, 1=TR, 2=BL, 3=BR
    If resizeCorner = 0 Then
      newX = startRectX + dx
      newY = startRectY + dy
      newW = startRectW - dx
      newH = startRectH - dy
    ElseIf resizeCorner = 1 Then
      newY = startRectY + dy
      newW = startRectW + dx
      newH = startRectH - dy
    ElseIf resizeCorner = 2 Then
      newX = startRectX + dx
      newW = startRectW - dx
      newH = startRectH + dy
    ElseIf resizeCorner = 3 Then
      newW = startRectW + dx
      newH = startRectH + dy
    End If

    ' Enforce minimum sizes (and compensate anchors for left/top corners)
    If newW < MIN_W Then
      If resizeCorner = 0 Or resizeCorner = 2 Then
        newX = (startRectX + startRectW) - MIN_W
      End If
      newW = MIN_W
    End If

    If newH < MIN_H Then
      If resizeCorner = 0 Or resizeCorner = 1 Then
        newY = (startRectY + startRectH) - MIN_H
      End If
      newH = MIN_H
    End If

    rectX = newX
    rectY = newY
    rectW = newW
    rectH = newH

    NormalizeAndClampRect()
    canvas.Invalidate()
    
  End If
End Sub

Sub DoubleClick(x As Integer, y As Integer)
  ' Optional convenience: reset rectangle on double-click anywhere
  rectX = 200
  rectY = 140
  rectW = 220
  rectH = 140
  NormalizeAndClampRect()
  canvas.Invalidate()
End Sub

Sub TerminateApplication()
  Quit()
End Sub

'--------------------------------------------------------------------------------
' Hook events
'--------------------------------------------------------------------------------
AddHandler(canvas.Paint,       AddressOf(CanvasPaint))
AddHandler(canvas.MouseDown,   AddressOf(MouseDown))
AddHandler(canvas.MouseUp,     AddressOf(MouseUp))
AddHandler(canvas.MouseMove,   AddressOf(MouseMove))
AddHandler(canvas.DoubleClick, AddressOf(DoubleClick))
AddHandler(win.Closing,        AddressOf(TerminateApplication))

' Show the window and start the event loop
win.Show()
canvas.Invalidate()

While True
  DoEvents(1)
Wend
