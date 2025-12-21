Module MathUp
  Public Function Multiply(Extends a As Integer, b As Integer) As Integer
    Return a * b
  End Function
End Module

var myint as Integer = 13
var output as Integer = myint.Multiply(2) 'output should now equal 26
print(str(output))


Module Demo
  Public Function PrintToScreen(Extends a As String) as String
    Return a
  End Function
End Module

var myInput as String = "Hi"

print(myInput.PrintToScreen()) 'Print "Hi"