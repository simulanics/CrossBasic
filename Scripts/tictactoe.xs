////tictactoe.xs////

// -----------------------------------------------------------------------------
// Demo: Tic Tac Toe in CrossBasic
// - Creates a 3x3 grid of buttons using XButton
// - Alternates between "X" and "O" on each click
// - Detects wins or draws and resets the board
// -----------------------------------------------------------------------------

// Main window setup
Var win As New XWindow
win.Title              = "Tic Tac Toe"
win.Width              = 320
win.Height             = 360
win.Resizable          = False
win.HasCloseButton     = True
win.HasMinimizeButton  = True
win.HasMaximizeButton  = False
win.HasFullScreenButton = False
win.HasTitleBar        = True
win.SetIcon("./scripts/ico.png")

AddHandler(win.Closing, AddressOf(OnQuit))

// Game state
Var currentPlayer As String = "X"

// Create 3x3 grid of buttons
Dim btns() As XButton
Dim btnSize As Integer = 90
Dim spacing As Integer = 5
Dim startX  As Integer = 10
Dim startY  As Integer = 10

var row as Integer = 0
var firstRun as Boolean = true
var loopcount as Integer = 0

// — Build the 3x3 Grid —
// Outer loop = rows (0 to 2), inner loop = cols (0 to 2)
For row As Integer = 0 To 2
  For col As Integer = 0 To 2
    // compute linear index if you need it:
    Dim idx As Integer = row * 3 + col

    // create button
    Var b As New XButton
    b.Parent   = win.Handle
    b.Left     = startX + col * (btnSize + spacing)
    b.Top      = startY + row * (btnSize + spacing)
    b.Width    = btnSize
    b.Height   = btnSize
    b.Caption  = ""
    b.FontName = "Arial"
    b.FontSize = 24
    b.Visible  = True

    // hook up your shared handler
    AddHandler(b.Pressed, AddressOf(ButtonPressed))

    // keep a reference
    btns.Add(b)
  Next col
Next row


// Show window and event loop

'Add auto-setters for window type offsets in IDE

win.Width = win.Width - 15
win.Height = win.Height - 32
win.Show()

While True
  DoEvents(10)
Wend

// ——— Event Handlers ———

Sub ButtonPressed(sender as XButton)
  ' find index
  Dim i As Integer = -1
  For jj As Integer = 0 To btns.LastIndex()
    If btns(jj).Handle = sender.Handle Then
      i = jj
      goto DoNext
    End If
  Next jj

  DoNext:

  ' invalid click (already marked)
  If btns(i).Caption <> "" Then 
    Return 0
  end if

  ' mark
  sender.Caption = currentPlayer

  ' win?
  If CheckWin(currentPlayer) Then
    print("player wins")
    win.MessageBox("Winner!", "Player " + currentPlayer + " wins!", 0)
    ResetBoard()
    Return 0
  End If

  ' draw?
  Dim emptyCount As Integer
  For j As Integer = 0 To 8
    If btns(j).Caption = "" Then 
    emptyCount = emptyCount + 1
    end if
  Next j
  If emptyCount = 0 Then
    print("draw 1")
    win.MessageBox("Draw!")
    ResetBoard()
    Return 0
  End If

  ' **now** switch player
  If currentPlayer = "X" Then
    currentPlayer = "O"
  Else
    currentPlayer = "X"
  End If
End Sub


// Check all winning lines for player p
Function CheckWin(p As String) As Boolean
  // Rows
  For r As Integer = 0 To 2
    If btns(r*3).caption = p And btns(r*3+1).caption = p And btns(r*3+2).caption = p Then
      Return True
    End If
  Next r

  // Columns
  For c As Integer = 0 To 2
    If btns(c).caption = p And btns(c+3).caption = p And btns(c+6).caption = p Then
      Return True
    End If
  Next c

  // Diagonals
  If btns(0).caption = p And btns(4).caption = p And btns(8).caption = p Then
    Return True
  End If
  If btns(2).caption = p And btns(4).caption = p And btns(6).caption = p Then
    Return True
  End If
  Return False

 
End Function


// Reset the board for a new game
Sub ResetBoard()
  For k As Integer = 0 To 8
     var b as xbutton = btns(k)
     b.Caption = ""
     'btns(k).caption = "" - This will not work because setters require objects as their class type intstance and not as a typedef of the class.
  Next k
  currentPlayer = "X"
End Sub

// Quit application
Sub OnQuit()
  Quit()
End Sub

////EOF////
