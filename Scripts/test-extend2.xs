'Test Integer Extends
Module MathUp
  Public Function Multiply(Extends a As Integer, b As Integer) As Integer
    Return a * b
  End Function
End Module

var myint as Integer = 13
var output as Integer = myint.Multiply(2)
print(str(output))


' Test String Extends
Module Demo
  Public Function PrintToScreen(Extends a As String) as String
    Return a
  End Function
End Module

var myInput as String = "Hi"
print(myInput.PrintToScreen())


' Test Double Extends
Module TestDoubleExt
  Public Function Twice(Extends a As Double) As Double
    Return a * 2.0
  End Function
End Module

Var x As Double = 3.5
Print(str(x.Twice()))


' Test Boolean Extends
Module TestBoolExt
  Public Function Flip(Extends b As Boolean) As Boolean
    Return Not b
  End Function
End Module

Var b As Boolean = True
Print(str(b.Flip()))
