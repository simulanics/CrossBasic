Var CalculatorDemo As New XWindow
CalculatorDemo.Width = 257
CalculatorDemo.Height = 387
CalculatorDemo.BackgroundColor = &c1a1a1a
CalculatorDemo.ViewType = 0
CalculatorDemo.HasCloseButton = True
CalculatorDemo.HasMinimizeButton = True
CalculatorDemo.HasMaximizeButton = False
CalculatorDemo.HasFullScreenButton = False
CalculatorDemo.HasTitleBar = True
CalculatorDemo.Resizable = False

AddHandler(CalculatorDemo.Closing, AddressOf(CalculatorDemo_Closing))

Sub CalculatorDemo_Closing()
	Quit()
End Sub

Var operand1 As Double = 0

Var operand2 As Double = 0

Var currentOp As String = ""

Var isNewEntry As Boolean = False

Sub OnNumber(label As String)
	If isNewEntry Then
	    txt.Text = label
	    isNewEntry = False
	Else
	    txt.Text = txt.Text + label
	End If
End Sub

Sub OnOperator(op As String)
	operand1 = Val(txt.Text)
	currentOp = op
	isNewEntry = True
End Sub

Sub OnEqual()
	operand2 = Val(txt.Text)
	Var result As Double
	Select Case currentOp
	  Case "+"
	      result = operand1 + operand2
	  Case "-"
	      result = operand1 - operand2
	  Case "*"
	      result = operand1 * operand2
	  Case "/"
	    If operand2 <> 0 Then
	      result = operand1 / operand2
	    Else
	      result = 0
	    End If
	  Case Else
	      result = Val(txt.Text)
	End Select
	txt.Text = Str(result)
	isNewEntry = True
End Sub

Sub OnC()
	txt.Text = "0"
	operand1 = 0
	operand2 = 0
	currentOp = ""
	isNewEntry = True
End Sub

Sub OnCE()
	txt.Text = "0"
	isNewEntry = True
End Sub

Var txt As New XTextField
txt.Parent = CalculatorDemo.Handle
txt.Left = 10
txt.Top = 10
txt.Width = 230
txt.Height = 40
txt.Text = "0"
txt.Fontname = "Arial"
txt.Fontsize = 32
txt.Textcolor = &cffff00
txt.Enabled = True
txt.Visible = True
txt.Bold = True

AddHandler(txt.TextChanged, AddressOf(txt_TextChanged))

Sub txt_TextChanged(Data As String)
	
End Sub

Var btnC As New XButton
btnC.Parent = CalculatorDemo.Handle
btnC.Left = 10
btnC.Top = 60
btnC.Width = 50
btnC.Height = 50
btnC.Caption = "C"
btnC.Bold = False
btnC.Underline = False
btnC.Italic = False
btnC.Fontname = "Arial"
btnC.Fontsize = 12
btnC.Textcolor = &cffffff
btnC.Enabled = True
btnC.Visible = True

AddHandler(btnC.Pressed, AddressOf(btnC_Pressed))

Sub btnC_Pressed()
	OnC()
End Sub

Var btnCE As New XButton
btnCE.Parent = CalculatorDemo.Handle
btnCE.Left = 70
btnCE.Top = 60
btnCE.Width = 50
btnCE.Height = 50
btnCE.Caption = "CE"
btnCE.Bold = False
btnCE.Underline = False
btnCE.Italic = False
btnCE.Fontname = "Arial"
btnCE.Fontsize = 12
btnCE.Textcolor = &cffffff
btnCE.Enabled = True
btnCE.Visible = True

AddHandler(btnCE.Pressed, AddressOf(btnCE_Pressed))

Sub btnCE_Pressed()
	OnCE()
End Sub

Var btnDiv As New XButton
btnDiv.Parent = CalculatorDemo.Handle
btnDiv.Left = 130
btnDiv.Top = 60
btnDiv.Width = 50
btnDiv.Height = 50
btnDiv.Caption = "/"
btnDiv.Bold = False
btnDiv.Underline = False
btnDiv.Italic = False
btnDiv.Fontname = "Arial"
btnDiv.Fontsize = 12
btnDiv.Textcolor = &cffffff
btnDiv.Enabled = True
btnDiv.Visible = True

AddHandler(btnDiv.Pressed, AddressOf(btnDiv_Pressed))

Sub btnDiv_Pressed()
	OnOperator("/")
End Sub

Var btnMul As New XButton
btnMul.Parent = CalculatorDemo.Handle
btnMul.Left = 190
btnMul.Top = 60
btnMul.Width = 50
btnMul.Height = 50
btnMul.Caption = "*"
btnMul.Bold = False
btnMul.Underline = False
btnMul.Italic = False
btnMul.Fontname = "Arial"
btnMul.Fontsize = 12
btnMul.Textcolor = &cffffff
btnMul.Enabled = True
btnMul.Visible = True

AddHandler(btnMul.Pressed, AddressOf(btnMul_Pressed))

Sub btnMul_Pressed()
	OnOperator("*")
End Sub

Var btn7 As New XButton
btn7.Parent = CalculatorDemo.Handle
btn7.Left = 10
btn7.Top = 120
btn7.Width = 50
btn7.Height = 50
btn7.Caption = "7"
btn7.Bold = False
btn7.Underline = False
btn7.Italic = False
btn7.Fontname = "Arial"
btn7.Fontsize = 12
btn7.Textcolor = &cffffff
btn7.Enabled = True
btn7.Visible = True

AddHandler(btn7.Pressed, AddressOf(btn7_Pressed))

Sub btn7_Pressed()
	OnNumber("7")
End Sub

Var btn8 As New XButton
btn8.Parent = CalculatorDemo.Handle
btn8.Left = 70
btn8.Top = 120
btn8.Width = 50
btn8.Height = 50
btn8.Caption = "8"
btn8.Bold = False
btn8.Underline = False
btn8.Italic = False
btn8.Fontname = "Arial"
btn8.Fontsize = 12
btn8.Textcolor = &cffffff
btn8.Enabled = True
btn8.Visible = True

AddHandler(btn8.Pressed, AddressOf(btn8_Pressed))

Sub btn8_Pressed()
	OnNumber("8")
End Sub

Var btn9 As New XButton
btn9.Parent = CalculatorDemo.Handle
btn9.Left = 130
btn9.Top = 120
btn9.Width = 50
btn9.Height = 50
btn9.Caption = "9"
btn9.Bold = False
btn9.Underline = False
btn9.Italic = False
btn9.Fontname = "Arial"
btn9.Fontsize = 12
btn9.Textcolor = &cffffff
btn9.Enabled = True
btn9.Visible = True

AddHandler(btn9.Pressed, AddressOf(btn9_Pressed))

Sub btn9_Pressed()
	OnNumber("9")
End Sub

Var btnMinus As New XButton
btnMinus.Parent = CalculatorDemo.Handle
btnMinus.Left = 190
btnMinus.Top = 120
btnMinus.Width = 50
btnMinus.Height = 50
btnMinus.Caption = "-"
btnMinus.Bold = False
btnMinus.Underline = False
btnMinus.Italic = False
btnMinus.Fontname = "Arial"
btnMinus.Fontsize = 12
btnMinus.Textcolor = &cffffff
btnMinus.Enabled = True
btnMinus.Visible = True

AddHandler(btnMinus.Pressed, AddressOf(btnMinus_Pressed))

Sub btnMinus_Pressed()
	OnOperator("-")
End Sub

Var btn4 As New XButton
btn4.Parent = CalculatorDemo.Handle
btn4.Left = 10
btn4.Top = 180
btn4.Width = 50
btn4.Height = 50
btn4.Caption = "4"
btn4.Bold = False
btn4.Underline = False
btn4.Italic = False
btn4.Fontname = "Arial"
btn4.Fontsize = 12
btn4.Textcolor = &cffffff
btn4.Enabled = True
btn4.Visible = True

AddHandler(btn4.Pressed, AddressOf(btn4_Pressed))

Sub btn4_Pressed()
	OnNumber("4")
End Sub

Var btn5 As New XButton
btn5.Parent = CalculatorDemo.Handle
btn5.Left = 70
btn5.Top = 180
btn5.Width = 50
btn5.Height = 50
btn5.Caption = "5"
btn5.Bold = False
btn5.Underline = False
btn5.Italic = False
btn5.Fontname = "Arial"
btn5.Fontsize = 12
btn5.Textcolor = &cffffff
btn5.Enabled = True
btn5.Visible = True

AddHandler(btn5.Pressed, AddressOf(btn5_Pressed))

Sub btn5_Pressed()
	OnNumber("5")
End Sub

Var btn6 As New XButton
btn6.Parent = CalculatorDemo.Handle
btn6.Left = 130
btn6.Top = 180
btn6.Width = 50
btn6.Height = 50
btn6.Caption = "6"
btn6.Bold = False
btn6.Underline = False
btn6.Italic = False
btn6.Fontname = "Arial"
btn6.Fontsize = 12
btn6.Textcolor = &cffffff
btn6.Enabled = True
btn6.Visible = True

AddHandler(btn6.Pressed, AddressOf(btn6_Pressed))

Sub btn6_Pressed()
	OnNumber("6")
End Sub

Var btnPlus As New XButton
btnPlus.Parent = CalculatorDemo.Handle
btnPlus.Left = 190
btnPlus.Top = 180
btnPlus.Width = 50
btnPlus.Height = 50
btnPlus.Caption = "+"
btnPlus.Bold = False
btnPlus.Underline = False
btnPlus.Italic = False
btnPlus.Fontname = "Arial"
btnPlus.Fontsize = 12
btnPlus.Textcolor = &cffffff
btnPlus.Enabled = True
btnPlus.Visible = True

AddHandler(btnPlus.Pressed, AddressOf(btnPlus_Pressed))

Sub btnPlus_Pressed()
	OnOperator("+")
End Sub

Var btn1 As New XButton
btn1.Parent = CalculatorDemo.Handle
btn1.Left = 10
btn1.Top = 240
btn1.Width = 50
btn1.Height = 50
btn1.Caption = "1"
btn1.Bold = False
btn1.Underline = False
btn1.Italic = False
btn1.Fontname = "Arial"
btn1.Fontsize = 12
btn1.Textcolor = &cffffff
btn1.Enabled = True
btn1.Visible = True

AddHandler(btn1.Pressed, AddressOf(btn1_Pressed))

Sub btn1_Pressed()
	OnNumber("1")
End Sub

Var btn2 As New XButton
btn2.Parent = CalculatorDemo.Handle
btn2.Left = 70
btn2.Top = 240
btn2.Width = 50
btn2.Height = 50
btn2.Caption = "2"
btn2.Bold = False
btn2.Underline = False
btn2.Italic = False
btn2.Fontname = "Arial"
btn2.Fontsize = 12
btn2.Textcolor = &cffffff
btn2.Enabled = True
btn2.Visible = True

AddHandler(btn2.Pressed, AddressOf(btn2_Pressed))

Sub btn2_Pressed()
	OnNumber("2")
End Sub

Var btn3 As New XButton
btn3.Parent = CalculatorDemo.Handle
btn3.Left = 130
btn3.Top = 240
btn3.Width = 50
btn3.Height = 50
btn3.Caption = "3"
btn3.Bold = False
btn3.Underline = False
btn3.Italic = False
btn3.Fontname = "Arial"
btn3.Fontsize = 12
btn3.Textcolor = &cffffff
btn3.Enabled = True
btn3.Visible = True

AddHandler(btn3.Pressed, AddressOf(btn3_Pressed))

Sub btn3_Pressed()
	OnNumber("3")
End Sub

Var btnEqual As New XButton
btnEqual.Parent = CalculatorDemo.Handle
btnEqual.Left = 190
btnEqual.Top = 240
btnEqual.Width = 50
btnEqual.Height = 50
btnEqual.Caption = "="
btnEqual.Bold = False
btnEqual.Underline = False
btnEqual.Italic = False
btnEqual.Fontname = "Arial"
btnEqual.Fontsize = 12
btnEqual.Textcolor = &cffffff
btnEqual.Enabled = True
btnEqual.Visible = True

AddHandler(btnEqual.Pressed, AddressOf(btnEqual_Pressed))

Sub btnEqual_Pressed()
	OnEqual()
End Sub

Var btn0 As New XButton
btn0.Parent = CalculatorDemo.Handle
btn0.Left = 10
btn0.Top = 300
btn0.Width = 50
btn0.Height = 50
btn0.Caption = "0"
btn0.Bold = False
btn0.Underline = False
btn0.Italic = False
btn0.Fontname = "Arial"
btn0.Fontsize = 12
btn0.Textcolor = &cffffff
btn0.Enabled = True
btn0.Visible = True

AddHandler(btn0.Pressed, AddressOf(btn0_Pressed))

Sub btn0_Pressed()
	OnNumber("0")
End Sub

// Show the window //////
CalculatorDemo.Show()

While True
	DoEvents(1)
Wend