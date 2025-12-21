'Testing the Not operator
Dim flag As Boolean = true
'Not True = False / Not False = True
If NOT flag Then
  Print("Flag is false")
Else
  Print("Flag is true")
End If

'Using Not with Boolean values:
Var a As Boolean
Var b As Boolean
Var c As Boolean ' defaults to False

a = True

b = Not a ' b = False
print b
b = Not c ' b = True
print b


'Using Not with numeric values:
Var i As Integer = 255

Var result As Integer
result = Not i ' result = -256
print result