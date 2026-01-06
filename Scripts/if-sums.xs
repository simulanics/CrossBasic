' Sum the first 10 numbers
Dim sum As Integer = 0

For i As Integer = 1 To 10
  sum += i
Next i

Print("Sum = " + sum.ToString)  ' Outputs "Sum = 55"


' Sum the first 10 numbers starting from highest to lowest indices *(backward)*
sum = 0

For i As Integer = 10 DownTo 1
  sum += i
Next i

Print("Sum = " + sum.ToString)  ' Outputs "Sum = 55"


' Sum the first 10 numbers skipping 2
sum = 0

For i As Integer = 1 To 10 Step 2
  sum += i
Next i

Print("Sum = " + sum.ToString)  ' Outputs "Sum = 25"