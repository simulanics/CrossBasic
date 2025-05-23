// file: test.txt
// comment test

Dim StartTime As Double
StartTime = ticks

print(str(StartTime))
print(StartTime.toString)
print(starttime.toString)


//Implementation not using plugin. Uncomment to override plugin if present.
'Function factorial(n As Integer) As Integer
'    If n <= 1 Then
'        Return 1
'    Else
'        Return n * factorial(n - 1)
'    End If
'End Function

Dim f As Integer = factorial(6)
print("Factorial of 6 is " + str(f))


Dim x() As Integer
x.add(2)
x.add(25)
x.add(336)
print(str(x.indexof(336)))
print(str(x.indexof(214)))

For i As Integer = 0 To 100
    print("Loop: " + str(i))
Next

print("Hello World!")

Dim strOutput() As String
strOutput.add(  "hello "      )
strOutput.add("World")
print(strOutput(0) + strOutput(1))

' Change intOut to be an array of Integer so we can add numbers.
Dim intOut() As Integer
intOut.add(2)
intOut.add(100)
print(str(intOut(0) + intOut(1)))


Dim intCount As Integer
Dim c2 As Integer

While intCount < 10
    print(">>>> " + intCount.toString + " <<<<")
    intCount = intCount + 1
    For c2 = 1 To 10 Step 2
        print("## " + str(c2) + " ##")
    Next
Wend

Dim inp() As String
inp = Array("Hello", " ", "WORLD")
print(inp(0) + inp(1) + inp(2))


print(val("236"))
var tv as integer = 236
print(tv.tostring)


' Sample Class definition
Class TestClass

  ' Property declarations
  Var i As Integer
  Var c As Color
  Var fname As String
  Var lname As String
  //var arrList() as string

  Sub Constructor(firstname As String, Optional lastname As String = "Combatti")
    fname = firstname
    self.lname = lastname //self test
    print("Name: " + fname + " " + lastname)
   // arrlist.add("test 2")
  End Sub
  
  ' Method declarations
  Sub myMethod(x As Integer, y As Integer)
    print(x.toString + ", " + y.toString)
  End Sub
  
  Function myFunc() As String
    Return "hello world from TestClass.myFunc()"
  End Function

  Function myFunc2() As Integer
    Return 37
  End Function

End Class

Dim s As New TestClass //("Matthew") ' Optional lastname parameter not provided.
s.constructor("Matthew")
s.i = 10
s.c = &cFF00FF
s.myMethod(20, 15)
print(s.myFunc())
print(str(s.i))
print(s.i.tostring)
print(str(s.c))
print(str(s.myFunc2() + 3))
print(s.fname)
print(s.lname)



print("Hello, " + _
"this is a line concatenation " + _
"test! -" + _
s.fname + " " + s.lname)



var cc as Color
cc = &c0000FF

var tt() as Variant
tt.add("hello")
tt.add(cc)
tt.add(2216)
print(str(tt.count()))
for tx as integer = 0 to tt.lastindex()
   print(tt(tx))
next

// Boolean test
var bb as Boolean = false
if 3 - 3 = 0 then
  bb = true
end if
print(str(bb))


print(str(val("216.14")))


' Fibonacci Series Demo in XojoScript
//Implementation not using plugin. Uncomment to override plugin if present.
'Function Fibonacci(n2 As Integer) As Integer
'	If n2 <= 0 Then 
'		Return 0 
'	ElseIf n2 = 1 Then 
'		Return 1 
'	Else
'		Return Fibonacci(n2 - 1) + Fibonacci(n2 - 2) 
'	End If
'End Function

Dim n As Integer = 20  ' Change this value to generate more numbers 
Print("Fibonacci Series up to " + Str(n) + " terms:")

Dim fibSeries() As Integer 
Dim fib As Integer
For i As Integer = 0 To n - 1 
	fib = Fibonacci(i)
	fibSeries.Add(fib) 
	Print("Fibonacci(" + Str(i) + ") = " + Str(fib)) 
Next


' Golden Ratio Demo in XojoScript

' Calculate Golden Ratio 
Dim goldenRatio As Double 
If n > 1 Then 
	goldenRatio = fibSeries(n-1) / fibSeries(n-2) Print("Golden Ratio approximation: " + Str(goldenRatio)) 
End If



Print("Done")

Var theNumber As Integer
Var digits As Integer
theNumber = 33
If theNumber < 10 Then
  digits = 1
ElseIf theNumber < 100 Then
  digits = 2
ElseIf theNumber < 1000 Then
  digits = 3
Else
  digits = 4
End If



Var x As Double
Var y As Integer
Var r As New Random
Var Pi as Double = 3.14159

' Abs() - Absolute value
x = Abs(-23.9)
Print(Str(x)) 
Print("Expected Result: 23.9")

' Acos() - Arc cosine
x = Acos(1)
Print(Str(x)) 
Print("Expected Result: 0")

' Asc() - ASCII value of character
y = Asc("A")
Print(Str(y)) 
Print("Expected Result: 65")

' Asin() - Arc sine
x = Asin(0.5)
Print(Str(x)) 
Print("Expected Result: 0.523598")

' Atan() - Arc tangent
x = Atan(1)
Print(Str(x)) 
Print("Expected Result: 0.785398")

' Atan2() - Arc tangent using y and x
x = Atan2(3, 4)
Print(Str(x)) 
Print("Expected Result: 0.6435")

' Ceiling() - Round up
x = Ceiling(2.3)
Print(Str(x)) 
Print("Expected Result: 3")

' Cos() - Cosine of angle
x = Cos(Pi/3)
Print(Str(x)) 
Print("Expected Result: 0.5")

' Exp() - Exponential function (e^x)
x = Exp(1)
Print(Str(x)) 
Print("Expected Result: 2.7183")

print(str( 4^2 ))
Print("Expected Result: 16")

' Floor() - Round down
x = Floor(2.9)
Print(Str(x)) 
Print("Expected Result: 2")

' Log() - Natural logarithm
x = Log(2.7183)
Print(Str(x)) 
Print("Expected Result: 1")

' Max() - Maximum of two values
x = Max(10, 20)
Print(Str(x)) 
Print("Expected Result: 20")

' Min() - Minimum of two values
x = Min(10, 20)
Print(Str(x)) 
Print("Expected Result: 10")

' Mod - Remainder of division
y = 10 Mod 3
Print(Str(y)) 
Print("Expected Result: 1")

' Oct() - Convert to octal string
Var octValue As String = Oct(10)
Print(octValue) 
Print("Expected Result: 12")

' Pow() - Exponentiation
x = Pow(2, 3)
Print(Str(x)) 
Print("Expected Result: 8")

' Random - Generate a random number
x = r.InRange(1, 100)
Print(Str(x)) 
Print("Expected Result: Random number between 1 and 100")

' Rnd() - Generate a random number between 0 and 1
x = Rnd()
Print(Str(x)) 
Print("Expected Result: Random value between 0 and 1")


' Round() - Rounding to nearest integer
x = Round(2.5)
Print(Str(x)) 
Print("Expected Result: 3")

' Sign() - Get the sign of a number
y = Sign(-10)
Print(Str(y)) 
Print("Expected Result: -1")

' Sin() - Sine function
x = Sin(Pi/2)
Print(Str(x)) 
Print("Expected Result: 1")

' Sqrt() - Square root
x = Sqrt(9)
Print(Str(x)) 
Print("Expected Result: 3")

' Tan() - Tangent function
x = Tan(Pi/4)
Print(Str(x)) 
Print("Expected Result Approximately: 1")

' ^ - Exponentiation operator
x = 2 ^ 3
Print(Str(x)) 
Print("Expected Result: 8")

Module MyMod
  Const pIN as Double = 1.683
  Const Pi as Double = 3.14
  Const GRate as Double = 0.22

  Var secret As String = "MySecret123"

  Public Function getRate() as Double
    Print(secret + " Inside Module getRate() method")
    Return GRate
  End Function

End Module

print(str(getRate())) 'Using global name
Print(str(MyMod.getRate())) 'Using namespace name
print(str(PIN))
Print(secret + " using Module globally accessible method name")
Print(MyMod.secret + " using Module namespace.methodname")

var df as integer = 3

Print("Select Case Test")
select case df
  case 1
    Print(str(1))
  case 2
    Print(Str(2))
  case 3
    Print(str(3))
End select

select case bb
  case true
    Print("this was a " + str(true))
  case false
    Print("this was false")
  case else
    print("this was undecided")
End select

Enum foo    
  enumvalue1 = 432    
  enumvalue2 = 519    
  enumvalue3 = 809    
end    
    
Var x as integer = foo.enumvalue2    
print(str(x))

'plugin test
print(sayhello("matt"))
var xtv as integer = addtwonumbers(1.1735, 3.14159)
print(str(xtv))

print("UTF-8 Support")
print("😊")

'Declare Sub MyAPI Lib "mylib.dll" (x as string, y as integer) 
'Declare Function MyFunc Lib "mylib.dll" () As Integer
'MyAPI("test", 4)
'print(str(MyFunc()))

Dim EndTime As Double = ticks
print("Ticks: " + ticks.toString)
EndTime = ticks / 60
print("Run Time: " + str(EndTime) + " seconds")
print("Run Time: " + str(microseconds / 1000000) + " seconds")


   var md as String = "<h1>Hello</h1>:" + chr(13) + "<ul><li>HTML to Markdown - yay!</li>" + chr(13) + _
      "<li>Testing one </li>" + chr(13) +_
      "<li>Testing two </li>" + chr(13) +_
      "<li>Testing three</li></ul>"
   
   print( HTMLtoMarkdown(md) )

var url as string = "https://www.example.com"
Print("Loading HTML from " + URL + " for translation to Markdown")
var y as string
y = URLtoMarkdown(url)
print(y)