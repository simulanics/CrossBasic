// -----------------------------------------------------------------------------
// CrossBasic Demonstration Script
// This comprehensive demo showcases various CrossBasic/XojoScript features:
// - Variable declarations and control structures
// - Built-in functions (math, string, array, flow control)
// - Class and module usage
// - Plugin interaction and UTF-8 support
// - SQLite access, shell execution, cryptography
// - JSON manipulation, LLM usage, and markdown conversion
// -----------------------------------------------------------------------------

// file: test.txt
// comment test

// Store starting ticks for timing runtime
Dim StartTime As Double
StartTime = ticks

// Print tick time
print(str(StartTime))
print(StartTime.toString)
print(starttime.toString)

// Factorial calculation (uses plugin if available)
Dim f As Integer = factorial(6)
print("Factorial of 6 is " + str(f))

// Integer array tests
Dim x() As Integer
x.add(2)
x.add(25)
x.add(336)
print(str(x.indexof(336)))
print(str(x.indexof(214)))

// Simple For loop from 0 to 100
For i As Integer = 0 To 100
    print("Loop: " + str(i))
Next

// Simple print
print("Hello World!")

// Concatenate two strings from an array
Dim strOutput() As String
strOutput.add("hello ")
strOutput.add("World")
print(strOutput(0) + strOutput(1))

// Sum two integers from array
Dim intOut() As Integer
intOut.add(2)
intOut.add(100)
print(str(intOut(0) + intOut(1)))

// While loop with nested For loop
Dim intCount As Integer
Dim c2 As Integer
While intCount < 10
    print(">>>> " + intCount.toString + " <<<<")
    intCount = intCount + 1
    For c2 = 1 To 10 Step 2
        print("## " + str(c2) + " ##")
    Next
Wend

// Array initialization via Array() function
Dim inp() As String
inp = Array("Hello", " ", "WORLD")
print(inp(0) + inp(1) + inp(2))

// String to numeric conversion and formatting
print(val("236"))
var tv as integer = 236
print(tv.tostring)

// Define and use a class
Class TestClass
  Var i As Integer
  Var c As Color
  Var fname As String
  Var lname As String

  Sub Constructor(firstname As String, Optional lastname As String = "Combatti")
    fname = firstname
    self.lname = lastname
    print("Name: " + fname + " " + lastname)
  End Sub

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

// Instantiate and use TestClass
Dim s As New TestClass("Matthew")
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

// Multi-line string concatenation
print("Hello, " + _
"this is a line concatenation " + _
"test! -" + _
s.fname + " " + s.lname)

// Variant array test
var cc as Color = &c0000FF
var tt() as Variant
tt.add("hello")
tt.add(cc)
tt.add(2216)
print(str(tt.count()))
for tx as integer = 0 to tt.lastindex()
   print(tt(tx))
next

// Boolean and condition test
var bb as Boolean = false
if 3 - 3 = 0 then
  bb = true
end if
print(str(bb))

// Floating-point string conversion
print(str(val("216.14")))

// Fibonacci sequence generator
Dim n As Integer = 20
Print("Fibonacci Series up to " + Str(n) + " terms:")
Dim fibSeries() As Integer 
Dim fib As Integer
For i As Integer = 0 To n - 1 
	fib = Fibonacci(i)
	fibSeries.Add(fib) 
	Print("Fibonacci(" + Str(i) + ") = " + Str(fib)) 
Next

// Golden ratio approximation
Dim goldenRatio As Double 
If n > 1 Then 
	goldenRatio = fibSeries(n-1) / fibSeries(n-2)
	Print("Golden Ratio approximation: " + Str(goldenRatio)) 
End If

Print("Done")

// Determine digit count in a number
Var theNumber As Integer = 33
Var digits As Integer
If theNumber < 10 Then
  digits = 1
ElseIf theNumber < 100 Then
  digits = 2
ElseIf theNumber < 1000 Then
  digits = 3
Else
  digits = 4
End If

// Math function demonstrations
Var x As Double
Var y As Integer
Var r As New Random
Var Pi as Double = 3.14159
x = Abs(-23.9)
Print(Str(x))
x = Acos(1)
Print(Str(x))
y = Asc("A")
Print(Str(y))
x = Asin(0.5)
Print(Str(x))
x = Atan(1)
Print(Str(x))
x = Atan2(3, 4)
Print(Str(x))
x = Ceiling(2.3)
Print(Str(x))
x = Cos(Pi/3)
Print(Str(x))
x = Exp(1)
Print(Str(x))
print(str( 4^2 ))
x = Floor(2.9)
Print(Str(x))
x = Log(2.7183)
Print(Str(x))
x = Max(10, 20)
Print(Str(x))
x = Min(10, 20)
Print(Str(x))
y = 10 Mod 3
Print(Str(y))
Var octValue As String = Oct(10)
Print(octValue)
x = Pow(2, 3)
Print(Str(x))
x = r.InRange(1, 100)
Print(Str(x))
x = Rnd()
Print(Str(x))
x = Round(2.5)
Print(Str(x))
y = Sign(-10)
Print(Str(y))
x = Sin(Pi/2)
Print(Str(x))
x = Sqrt(9)
Print(Str(x))
x = Tan(Pi/4)
Print(Str(x))
x = 2 ^ 3
Print(Str(x))

// Module test
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

print(str(getRate()))
Print(str(MyMod.getRate()))
print(str(PIN))
Print(secret + " using Module globally accessible method name")
Print(MyMod.secret + " using Module namespace.methodname")

// Select Case control structure
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

// Enum usage test
Enum foo
  enumvalue1 = 432
  enumvalue2 = 519
  enumvalue3 = 809
end
Var x as integer = foo.enumvalue2
print(str(x))

// Plugin function calls
print(sayhello("matt"))
var xtv as variant = addtwonumbers(5,4)
var ztv as Double = 1.1735 + 3.14159
print(str(xtv))
print(str(ztv))

// UTF-8 test
print("UTF-8 Support")
print("😊")

// Timing and HTML-to-Markdown conversion
Dim EndTime As Double = ticks
print("Ticks: " + ticks.toString)
EndTime = ticks / 60
print("Run Time: " + str(EndTime) + " seconds")
print("Run Time: " + str(microseconds / 1000000) + " seconds")

// HTML-to-Markdown conversion
var md as String = "<h1>Hello</h1>:" + chr(13) + "<ul><li>HTML to Markdown - yay!</li>" + chr(13) + _
"<li>Testing one </li>" + chr(13) + _
"<li>Testing two </li>" + chr(13) + _
"<li>Testing three</li></ul>"
print( HTMLtoMarkdown(md) )

// Load URL and convert to Markdown
var url as string = "https://www.example.com"
Print("Loading HTML from " + URL + " for translation to Markdown")
var y as string
y = URLtoMarkdown(url)
print(y)